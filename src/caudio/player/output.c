#include <caudio/player/output.h>

#include <caudio/runtime/error.h>
#include <caudio/runtime/log.h>

#include <cstdx/allocator.h>
#include <cstdx/string_view.h>

#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <miniaudio.h>

/* --------------------------------------------------------------- */
/* Audio output implementation                                     */
/* --------------------------------------------------------------- */

/* Convert cstdx cx_status to caudio ca_status. */
static ca_status cx_to_ca(cx_status status) {
    switch (status) {
    case CX_OK:
        return CA_OK;
    case CX_OUT_OF_MEMORY:
        return CA_OUT_OF_MEMORY;
    case CX_INVALID_ARGUMENT:
        return CA_INVALID_ARGUMENT;
    case CX_OVERFLOW:
        return CA_OVERFLOW;
    case CX_INVALID_STATE:
        return CA_INVALID_ARGUMENT;
    case CX_NOT_FOUND:
        return CA_NOT_FOUND;
    case CX_UNSUPPORTED:
        return CA_NOT_SUPPORTED;
    case CX_IO:
        return CA_IO_ERROR;
    case CX_INTERNAL:
        return CA_IO_ERROR;
    default:
        return CA_INVALID_ARGUMENT;
    }
}

struct ca_audio_output {
    ca_audio_output_config cfg;
    ma_device device;
    _Atomic float volume;
    _Atomic bool running;
    _Atomic bool initialized;
};

/* Audio-thread entry point (miniaudio trampoline).
 * Reads from the ring lock-free, applies volume, zero-fills underrun.
 * No allocations, no blocking. */
static void output_data_callback(ma_device* p_device, void* p_output,
                                 const void* p_input, ma_uint32 frame_count) {
    (void)p_input;
    struct ca_audio_output* self = (struct ca_audio_output*)p_device->pUserData;
    if (!self)
        return;

    float* output = (float*)p_output;
    uint32_t channels = p_device->playback.channels;
    size_t total_samples = (size_t)frame_count * (size_t)channels;
    float vol = atomic_load_explicit(&self->volume, memory_order_relaxed);

    ca_ring* ring = self->cfg.ring;
    size_t generated_frames = 0;
    if (ring) {
        /* ca_ring_read takes frames; out.size() / channels == max frames. */
        generated_frames = ca_ring_read(ring, output, total_samples / (size_t)channels);
    }
    size_t generated_samples = generated_frames * (size_t)channels;

    if (generated_samples < total_samples) {
        memset(output + generated_samples, 0,
               (total_samples - generated_samples) * sizeof(float));
    }

    if (vol != 1.0f) {
        for (size_t i = 0; i < total_samples; ++i)
            output[i] *= vol;
    }
}

void ca_audio_output_config_init(ca_audio_output_config* cfg) {
    if (!cfg)
        return;
    cfg->sample_rate = 48000;
    cfg->channels = 2;
    cfg->ring = NULL;
    cfg->volume = 1.0f;
    cfg->logger = NULL;
}

ca_status ca_audio_output_create(ca_audio_output** out,
                                 const ca_audio_output_config* cfg,
                                 ca_error* err) {
    if (err)
        ca_error_init(err);
    if (!out || !cfg) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return CA_INVALID_ARGUMENT;
    }

    struct ca_audio_output* self = (struct ca_audio_output*)cx_allocator_alloc(
        cx_allocator_default(), sizeof(struct ca_audio_output),
        alignof(struct ca_audio_output));
    if (!self) {
        if (err)
            ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }

    memset(self, 0, sizeof(*self));
    atomic_init(&self->volume, 1.0f);
    atomic_init(&self->running, false);
    atomic_init(&self->initialized, false);

    if (!ca_audio_output_init(self, cfg, err)) {
        ca_status st = err ? err->code : CA_IO_ERROR;
        ca_audio_output_destroy(self);
        *out = NULL;
        return st;
    }

    if (cfg->logger)
        ca_logger_log(cfg->logger, CA_LOG_DEBUG,
                      "audio output opened rate=%u channels=%u",
                      cfg->sample_rate, cfg->channels);

    *out = (ca_audio_output*)self;
    return CA_OK;
}

void ca_audio_output_set_volume(ca_audio_output* self, float vol) {
    if (!self)
        return;
    float v = vol;
    if (!isfinite(v) || v < 0.0f)
        v = 0.0f;
    else if (v > 1.0f)
        v = 1.0f;
    atomic_store_explicit(&self->volume, v, memory_order_relaxed);
}

float ca_audio_output_volume(const ca_audio_output* self) {
    if (!self)
        return 0.0f;
    return atomic_load_explicit(&self->volume, memory_order_relaxed);
}

void ca_audio_output_start(ca_audio_output* self) {
    if (!self)
        return;
    if (!atomic_load_explicit(&self->initialized, memory_order_acquire))
        return;
    bool expected = false;
    if (atomic_compare_exchange_strong_explicit(&self->running, &expected, true,
                                                memory_order_acq_rel,
                                                memory_order_acquire)) {
        ma_device_start(&self->device);
    }
}

void ca_audio_output_stop(ca_audio_output* self) {
    if (!self)
        return;
    bool expected = true;
    if (atomic_compare_exchange_strong_explicit(&self->running, &expected, false,
                                                memory_order_acq_rel,
                                                memory_order_acquire)) {
        ma_device_stop(&self->device);
    }
}

void ca_audio_output_shutdown(ca_audio_output* self) {
    if (!self)
        return;
    ca_audio_output_stop(self);
    if (atomic_exchange_explicit(&self->initialized, false, memory_order_acq_rel)) {
        ma_device_uninit(&self->device);
    }
}

bool ca_audio_output_init(ca_audio_output* self,
                          const ca_audio_output_config* cfg,
                          ca_error* err) {
    if (!self || !cfg) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return false;
    }
    if (cfg->channels == 0 || cfg->channels > 32 || cfg->sample_rate == 0 ||
        !cfg->ring) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT,
                         "invalid config (channels, sample rate or ring)");
        return false;
    }

    self->cfg = *cfg;
    float v = cfg->volume;
    if (!isfinite(v) || v < 0.0f)
        v = 0.0f;
    else if (v > 1.0f)
        v = 1.0f;
    atomic_store_explicit(&self->volume, v, memory_order_relaxed);

    ma_device_config device_config = ma_device_config_init(ma_device_type_playback);
    device_config.playback.format = ma_format_f32;
    device_config.playback.channels = cfg->channels;
    device_config.sampleRate = cfg->sample_rate;
    device_config.dataCallback = output_data_callback;
    device_config.pUserData = self;

    ma_result res = ma_device_init(NULL, &device_config, &self->device);
    if (res != MA_SUCCESS) {
        if (err)
            ca_error_set(err, CA_IO_ERROR, "miniaudio device init failed (%d)", (int)res);
        return false;
    }
    atomic_store_explicit(&self->initialized, true, memory_order_release);

    /* The device starts stopped; the caller starts it after preroll. */
    atomic_store_explicit(&self->running, false, memory_order_release);

    return true;
}

void ca_audio_output_destroy(ca_audio_output* self) {
    if (!self)
        return;
    ca_audio_output_shutdown(self);
    cx_allocator_free(cx_allocator_default(), self, sizeof(struct ca_audio_output),
                      alignof(struct ca_audio_output));
}

ca_status ca_audio_output_enumerate_devices(const cx_allocator* alloc,
                                            ca_device_list** out, ca_error* err) {
    if (err)
        ca_error_init(err);
    if (!out) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is null");
        return CA_INVALID_ARGUMENT;
    }
    const cx_allocator* resolved = alloc ? alloc : cx_allocator_default();

    ma_context context;
    ma_context_config ctx_config = ma_context_config_init();
    ma_result res = ma_context_init(NULL, 0, &ctx_config, &context);
    if (res != MA_SUCCESS) {
        /* Return an empty, valid list rather than failing. */
        ca_device_list* list = (ca_device_list*)cx_allocator_alloc(
            resolved, sizeof(ca_device_list), alignof(ca_device_list));
        if (!list) {
            if (err)
                ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
            return CA_OUT_OF_MEMORY;
        }
        ca_status st = ca_device_list_init(list, resolved);
        if (st != CA_OK) {
            cx_allocator_free(resolved, list, sizeof(ca_device_list),
                              alignof(ca_device_list));
            if (err)
                ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
            return CA_OUT_OF_MEMORY;
        }
        *out = list;
        return CA_OK;
    }

    ca_device_list* list = (ca_device_list*)cx_allocator_alloc(
        resolved, sizeof(ca_device_list), alignof(ca_device_list));
    if (!list) {
        ma_context_uninit(&context);
        if (err)
            ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }
    ca_status st = ca_device_list_init(list, resolved);
    if (st != CA_OK) {
        cx_allocator_free(resolved, list, sizeof(ca_device_list),
                          alignof(ca_device_list));
        ma_context_uninit(&context);
        if (err)
            ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }

    ma_device_info* playback_infos = NULL;
    ma_uint32 playback_count = 0;
    ma_device_info* capture_infos = NULL;
    ma_uint32 capture_count = 0;

    res = ma_context_get_devices(&context, &playback_infos, &playback_count,
                                 &capture_infos, &capture_count);
    if (res == MA_SUCCESS && playback_infos && playback_count > 0) {
        for (ma_uint32 i = 0; i < playback_count; ++i) {
            const ma_device_info* info = &playback_infos[i];

            ca_device_info dev;
            st = ca_device_info_init(&dev, resolved);
            if (st != CA_OK)
                continue;

            char id_buf[32];
            snprintf(id_buf, sizeof(id_buf), "%u", i);
            cx_string_view id_view = cx_string_view_from_cstr(id_buf);
            cx_string_view name_view = cx_string_view_from_cstr(info->name);
            st = cx_to_ca(cx_string_assign_view(dev.id, id_view));
            if (st == CA_OK)
                st = cx_to_ca(cx_string_assign_view(dev.name, name_view));
            if (st != CA_OK) {
                ca_device_info_destroy(&dev);
                continue;
            }
            dev.is_default = info->isDefault ? true : false;

            cx_status cvs = cx_vector_push_copy(list->devices, &dev, sizeof(dev));
            ca_device_info_destroy(&dev);
            if (cvs != CX_OK)
                break;
        }
    }

    ma_context_uninit(&context);
    *out = list;
    return CA_OK;
}
