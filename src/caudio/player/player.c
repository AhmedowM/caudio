#include <caudio/player/player.h>

#include <caudio/media/decoder.h>
#include <caudio/media/reader.h>
#include <caudio/media/source.h>
#include <caudio/runtime/error.h>
#include <caudio/runtime/log.h>
#include <caudio/runtime/thread.h>
#include <caudio/runtime/mutex.h>
#include <caudio/runtime/cond.h>
#include <caudio/runtime/time.h>
#include <caudio/utils/ring.h>

#include <cstdx/allocator.h>

#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

/* --------------------------------------------------------------- */
/* Constants                                                       */
/* --------------------------------------------------------------- */

static constexpr uint32_t CA_PLAYER_DEFAULT_SAMPLE_RATE = 48000U;
static constexpr uint32_t CA_PLAYER_DEFAULT_CHANNELS = 2U;
/* Ring capacity in frames. Matches the C++ player (8192 frames). */
static constexpr size_t CA_PLAYER_RING_CAPACITY_FRAMES = 8192U;
/* Preroll target: half the ring capacity, in frames. */
static constexpr size_t CA_PLAYER_PREROLL_FRAMES_DIVISOR = 2U;
/* Max frames per decode chunk; bounded by 2048/channels too. */
static constexpr size_t CA_PLAYER_MAX_CHUNK_FRAMES = 1024U;
static constexpr size_t CA_PLAYER_MAX_CHUNK_BY_CHANNELS = 2048U;

/* --------------------------------------------------------------- */
/* Player implementation                                           */
/* --------------------------------------------------------------- */

struct ca_player {
    ca_player_options options;

    /* Playback components (owned). */
    ca_reader* reader;
    ca_decoder* decoder;
    ca_ring* ring;
    ca_audio_output* output;

    /* Threading. */
    ca_thread_t decode_thread;
    ca_mutex_t open_mutex;
    ca_cond_t cv;

    /* Atomic state. */
    _Atomic ca_player_state_t state;
    _Atomic uint64_t pos_base;      /* frames at last seek/start. */
    _Atomic int64_t pos_start_ms;   /* monotonic ms when play started. */
    _Atomic bool is_playing;
    _Atomic bool open_gate;         /* lets the decode loop run. */
    _Atomic bool decode_busy;       /* decode loop should fill the ring. */
    _Atomic bool destroy_requested; /* set by destroy() to exit the loop. */
    _Atomic float volume;

    char last_error[256];
    ca_logger* logger;
};

/* --------------------------------------------------------------- */
/* Helpers                                                         */
/* --------------------------------------------------------------- */

static int64_t now_ms(void) {
    ca_time_us_t us = 0;
    ca_error err;
    ca_error_init(&err);
    if (ca_time_now(&us, &err) != CA_OK)
        return 0;
    return (int64_t)(us / 1000ULL);
}

static void set_last_error(ca_player* self, const char* msg) {
    if (!self || !msg)
        return;
    size_t n = strlen(msg);
    if (n >= sizeof(self->last_error))
        n = sizeof(self->last_error) - 1;
    memcpy(self->last_error, msg, n);
    self->last_error[n] = '\0';
}

static void clear_last_error(ca_player* self) {
    if (!self)
        return;
    self->last_error[0] = '\0';
}

/* Forward declarations (static helpers defined later in this file). */
static ca_status ca_player_stop_locked(ca_player* self, ca_error* err);
static void ca_player_preroll(ca_player* self);

/* --------------------------------------------------------------- */
/* Decode loop                                                     */
/* --------------------------------------------------------------- */

static void* decode_thread_fn(void* arg) {
    struct ca_player* self = (struct ca_player*)arg;

    for (;;) {
        /* Wait until we should decode, or until destruction is requested.
         * The predicate is evaluated while holding open_mutex, which is also
         * held by whoever changes decode_busy/destroy_requested and
         * broadcasts, so there is no lost wakeup (e.g. destroy() racing the
         * thread's first wait). */
        ca_error err;
        ca_error_init(&err);
        ca_mutex_lock(self->open_mutex, &err);
        while (!atomic_load_explicit(&self->destroy_requested, memory_order_acquire) &&
               !atomic_load_explicit(&self->decode_busy, memory_order_acquire)) {
            ca_cond_wait(self->cv, self->open_mutex, &err);
        }
        ca_mutex_unlock(self->open_mutex, &err);

        if (atomic_load_explicit(&self->destroy_requested, memory_order_acquire))
            return NULL;

        if (!atomic_load_explicit(&self->is_playing, memory_order_acquire)) {
            atomic_store_explicit(&self->decode_busy, false, memory_order_release);
            continue;
        }

        if (!self->decoder || !self->ring || !self->output) {
            atomic_store_explicit(&self->decode_busy, false, memory_order_release);
            continue;
        }

        /* Clamp chunk size to free space in the ring. */
        size_t avail = ca_ring_available_write(self->ring);
        if (avail == 0) {
            ca_error_init(&err);
            ca_time_sleep_us(5000ULL, &err); /* 5 ms */
            continue;
        }

        uint32_t ch = ca_decoder_channels(self->decoder);
        if (ch == 0)
            ch = self->options.channels ? self->options.channels : 2U;

        size_t max_frames = avail / ch;
        if (max_frames > CA_PLAYER_MAX_CHUNK_FRAMES)
            max_frames = CA_PLAYER_MAX_CHUNK_FRAMES;
        size_t max_by_ch = CA_PLAYER_MAX_CHUNK_BY_CHANNELS / (size_t)ch;
        if (max_by_ch == 0)
            max_by_ch = 1;
        if (max_frames > max_by_ch)
            max_frames = max_by_ch;
        if (max_frames == 0)
            max_frames = 1;

        /* Allocate scratch per chunk (small; freed each iteration). */
        const cx_allocator* alloc = cx_allocator_default();
        float* scratch = (float*)cx_allocator_alloc(alloc, max_frames * ch * sizeof(float),
                                                    alignof(float));
        if (!scratch) {
            ca_error_init(&err);
            ca_time_sleep_us(5000ULL, &err);
            continue;
        }

        size_t frames = ca_decoder_decode(self->decoder, scratch, max_frames * ch);

        if (frames == 0) {
            /* EOF reached. */
            cx_allocator_free(alloc, scratch, max_frames * ch * sizeof(float), alignof(float));
            atomic_store_explicit(&self->is_playing, false, memory_order_release);
            ca_audio_output_stop(self->output);
            atomic_store_explicit(&self->state, CA_PLAYER_STATE_STOPPED,
                                  memory_order_release);
            atomic_store_explicit(&self->decode_busy, false, memory_order_release);
            continue;
        }

        size_t samples = frames * ch;
        size_t written = ca_ring_write(self->ring, scratch, samples);
        cx_allocator_free(alloc, scratch, max_frames * ch * sizeof(float), alignof(float));
        (void)written; /* ring full => retry next iteration */
    }

    return NULL;
}

/* --------------------------------------------------------------- */
/* Public API                                                      */
/* --------------------------------------------------------------- */

void ca_player_options_init(ca_player_options* opts) {
    if (!opts)
        return;
    opts->sample_rate = CA_PLAYER_DEFAULT_SAMPLE_RATE;
    opts->channels = CA_PLAYER_DEFAULT_CHANNELS;
    opts->logger = NULL;
}

ca_status ca_player_create(ca_player** out, const ca_player_options* opts, ca_error* err) {
    if (err)
        ca_error_init(err);
    if (!out) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is null");
        return CA_INVALID_ARGUMENT;
    }

    ca_player_options resolved;
    ca_player_options_init(&resolved);
    if (opts)
        resolved = *opts;
    if (resolved.sample_rate == 0)
        resolved.sample_rate = CA_PLAYER_DEFAULT_SAMPLE_RATE;
    if (resolved.channels == 0)
        resolved.channels = CA_PLAYER_DEFAULT_CHANNELS;

    struct ca_player* self = (struct ca_player*)cx_allocator_alloc(
        cx_allocator_default(), sizeof(struct ca_player), alignof(struct ca_player));
    if (!self) {
        if (err)
            ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }

    memset(self, 0, sizeof(*self));
    self->options = resolved;
    self->logger = resolved.logger;

    atomic_init(&self->state, CA_PLAYER_STATE_STOPPED);
    atomic_init(&self->pos_base, 0ULL);
    atomic_init(&self->pos_start_ms, 0LL);
    atomic_init(&self->is_playing, false);
    atomic_init(&self->open_gate, false);
    atomic_init(&self->decode_busy, false);
    atomic_init(&self->destroy_requested, false);
    atomic_init(&self->volume, 1.0f);

    ca_status st = ca_mutex_create(&self->open_mutex, err);
    if (st != CA_OK) {
        cx_allocator_free(cx_allocator_default(), self, sizeof(*self), alignof(*self));
        *out = NULL;
        return st;
    }
    st = ca_cond_create(&self->cv, err);
    if (st != CA_OK) {
        ca_mutex_destroy(self->open_mutex);
        cx_allocator_free(cx_allocator_default(), self, sizeof(*self), alignof(*self));
        *out = NULL;
        return st;
    }

    /* Start the decode thread (blocked until open_gate + decode_busy). */
    st = ca_thread_create(&self->decode_thread, decode_thread_fn, self, err);
    if (st != CA_OK) {
        ca_cond_destroy(self->cv);
        ca_mutex_destroy(self->open_mutex);
        cx_allocator_free(cx_allocator_default(), self, sizeof(*self), alignof(*self));
        *out = NULL;
        return st;
    }

    *out = (ca_player*)self;
    return CA_OK;
}

void ca_player_destroy(ca_player* self) {
    if (!self)
        return;

    /* Stop playback and close the open gate so the decode loop exits. */
    ca_error err;
    ca_error_init(&err);
    atomic_store_explicit(&self->destroy_requested, true, memory_order_release);
    ca_mutex_lock(self->open_mutex, &err);
    atomic_store_explicit(&self->open_gate, false, memory_order_release);
    ca_cond_broadcast(self->cv, &err);
    ca_mutex_unlock(self->open_mutex, &err);

    ca_thread_join(self->decode_thread, NULL, &err);

    if (self->output)
        ca_audio_output_destroy(self->output);
    if (self->ring)
        ca_ring_destroy(self->ring);
    if (self->decoder)
        ca_decoder_destroy(self->decoder);
    if (self->reader)
        ca_reader_destroy(self->reader);

    ca_cond_destroy(self->cv);
    ca_mutex_destroy(self->open_mutex);

    cx_allocator_free(cx_allocator_default(), self, sizeof(*self), alignof(*self));
}

ca_status ca_player_open(ca_player* self, const char* path, ca_error* err) {
    if (err)
        ca_error_init(err);
    if (!self || !path || path[0] == '\0') {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return CA_INVALID_ARGUMENT;
    }

    ca_reader* reader = NULL;
    ca_status st = ca_reader_file(&reader, path, err);
    if (st != CA_OK) {
        set_last_error(self, err ? err->message : "open failed");
        atomic_store_explicit(&self->state, CA_PLAYER_STATE_ERROR, memory_order_release);
        return st;
    }

    st = ca_player_open_reader(self, reader, err);
    if (st != CA_OK) {
        ca_reader_destroy(reader);
        set_last_error(self, err ? err->message : "open reader failed");
        atomic_store_explicit(&self->state, CA_PLAYER_STATE_ERROR, memory_order_release);
    }
    return st;
}

ca_status ca_player_open_reader(ca_player* self, ca_reader* reader, ca_error* err) {
    if (err)
        ca_error_init(err);
    if (!self || !reader) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return CA_INVALID_ARGUMENT;
    }

    ca_error open_err;
    ca_error_init(&open_err);
    ca_mutex_lock(self->open_mutex, &open_err);

    /* Stop any existing playback. */
    ca_player_state_t cur = atomic_load_explicit(&self->state, memory_order_acquire);
    if (cur != CA_PLAYER_STATE_STOPPED) {
        (void)ca_player_stop_locked(self, &open_err);
    }

    /* Open the decoder against the reader. */
    ca_decoder* decoder = NULL;
    ca_status st = ca_decoder_open(&decoder, reader, self->logger, &open_err);
    if (st != CA_OK) {
        set_last_error(self, open_err.message);
        atomic_store_explicit(&self->state, CA_PLAYER_STATE_ERROR, memory_order_release);
        ca_mutex_unlock(self->open_mutex, &open_err);
        return st;
    }

    self->decoder = decoder;
    self->reader = reader;

    uint32_t sr = ca_decoder_sample_rate(self->decoder);
    uint32_t ch = ca_decoder_channels(self->decoder);
    if (sr == 0)
        sr = self->options.sample_rate;
    if (ch == 0)
        ch = self->options.channels;

    /* Ring buffer: capacity frames x channels. */
    ca_ring* ring = NULL;
    st = ca_ring_create(&ring, CA_PLAYER_RING_CAPACITY_FRAMES, ch,
                        cx_allocator_default());
    if (st != CA_OK) {
        set_last_error(self, open_err.message);
        atomic_store_explicit(&self->state, CA_PLAYER_STATE_ERROR, memory_order_release);
        goto cleanup_decoder_reader;
    }
    self->ring = ring;

    /* Audio output. */
    ca_audio_output_config cfg;
    ca_audio_output_config_init(&cfg);
    cfg.sample_rate = sr;
    cfg.channels = ch;
    cfg.ring = ring;
    cfg.volume = atomic_load_explicit(&self->volume, memory_order_relaxed);
    cfg.logger = self->logger;

    ca_audio_output* output = NULL;
    st = ca_audio_output_create(&output, &cfg, &open_err);
    if (st != CA_OK) {
        set_last_error(self, open_err.message);
        atomic_store_explicit(&self->state, CA_PLAYER_STATE_ERROR, memory_order_release);
        goto cleanup_ring_decoder_reader;
    }
    self->output = output;

    /* Preroll: fill ring to half capacity before returning. */
    ca_player_preroll(self);

    atomic_store_explicit(&self->pos_base, 0ULL, memory_order_relaxed);
    atomic_store_explicit(&self->pos_start_ms, 0LL, memory_order_relaxed);
    atomic_store_explicit(&self->is_playing, false, memory_order_relaxed);
    atomic_store_explicit(&self->decode_busy, false, memory_order_relaxed);
    atomic_store_explicit(&self->open_gate, true, memory_order_release);
    clear_last_error(self);
    atomic_store_explicit(&self->state, CA_PLAYER_STATE_STOPPED, memory_order_release);

    ca_mutex_unlock(self->open_mutex, &open_err);
    return CA_OK;

cleanup_ring_decoder_reader:
    if (self->ring) {
        ca_ring_destroy(self->ring);
        self->ring = NULL;
    }
cleanup_decoder_reader:
    if (self->decoder) {
        ca_decoder_destroy(self->decoder);
        self->decoder = NULL;
    }
    if (self->reader) {
        ca_reader_destroy(self->reader);
        self->reader = NULL;
    }
    ca_mutex_unlock(self->open_mutex, &open_err);
    return st;
}

ca_status ca_player_play(ca_player* self, ca_error* err) {
    if (err)
        ca_error_init(err);
    if (!self) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "self is null");
        return CA_INVALID_ARGUMENT;
    }
    if (!self->decoder || !self->output || !self->ring) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "not opened");
        return CA_INVALID_ARGUMENT;
    }

    ca_player_state_t expected = CA_PLAYER_STATE_STOPPED;
    if (!atomic_compare_exchange_strong_explicit(&self->state, &expected,
                                                 CA_PLAYER_STATE_PLAYING,
                                                 memory_order_acq_rel,
                                                 memory_order_acquire)) {
        expected = CA_PLAYER_STATE_PAUSED;
        if (!atomic_compare_exchange_strong_explicit(&self->state, &expected,
                                                     CA_PLAYER_STATE_PLAYING,
                                                     memory_order_acq_rel,
                                                     memory_order_acquire)) {
            if (err)
                ca_error_set(err, CA_INVALID_ARGUMENT, "already playing");
            return CA_INVALID_ARGUMENT;
        }
    }

    atomic_store_explicit(&self->is_playing, true, memory_order_release);
    atomic_store_explicit(&self->pos_start_ms, now_ms(), memory_order_release);

    ca_audio_output_start(self->output);

    ca_error wake_err;
    ca_error_init(&wake_err);
    ca_mutex_lock(self->open_mutex, &wake_err);
    atomic_store_explicit(&self->decode_busy, true, memory_order_release);
    ca_cond_broadcast(self->cv, &wake_err);
    ca_mutex_unlock(self->open_mutex, &wake_err);

    return CA_OK;
}

ca_status ca_player_pause(ca_player* self, ca_error* err) {
    if (err)
        ca_error_init(err);
    if (!self) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "self is null");
        return CA_INVALID_ARGUMENT;
    }

    ca_player_state_t expected = CA_PLAYER_STATE_PLAYING;
    if (!atomic_compare_exchange_strong_explicit(&self->state, &expected,
                                                 CA_PLAYER_STATE_PAUSED,
                                                 memory_order_acq_rel,
                                                 memory_order_acquire)) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "not playing");
        return CA_INVALID_ARGUMENT;
    }

    atomic_store_explicit(&self->is_playing, false, memory_order_release);
    ca_audio_output_stop(self->output);

    /* Update pos_base with elapsed time. */
    int64_t now = now_ms();
    int64_t start = atomic_load_explicit(&self->pos_start_ms, memory_order_acquire);
    if (start > 0 && now > start) {
        int64_t elapsed_ms = now - start;
        int64_t elapsed_frames = (int64_t)((double)elapsed_ms * (double)self->options.sample_rate /
                                           1000.0);
        uint64_t base = atomic_load_explicit(&self->pos_base, memory_order_relaxed);
        atomic_store_explicit(&self->pos_base,
                              (uint64_t)((int64_t)base + elapsed_frames), memory_order_relaxed);
    }
    atomic_store_explicit(&self->pos_start_ms, 0LL, memory_order_relaxed);

    return CA_OK;
}

ca_status ca_player_resume(ca_player* self, ca_error* err) {
    if (err)
        ca_error_init(err);
    if (!self) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "self is null");
        return CA_INVALID_ARGUMENT;
    }

    ca_player_state_t expected = CA_PLAYER_STATE_PAUSED;
    if (!atomic_compare_exchange_strong_explicit(&self->state, &expected,
                                                 CA_PLAYER_STATE_PLAYING,
                                                 memory_order_acq_rel,
                                                 memory_order_acquire)) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "not paused");
        return CA_INVALID_ARGUMENT;
    }

    atomic_store_explicit(&self->is_playing, true, memory_order_release);
    atomic_store_explicit(&self->pos_start_ms, now_ms(), memory_order_release);
    ca_audio_output_start(self->output);

    ca_error wake_err;
    ca_error_init(&wake_err);
    ca_mutex_lock(self->open_mutex, &wake_err);
    atomic_store_explicit(&self->decode_busy, true, memory_order_release);
    ca_cond_broadcast(self->cv, &wake_err);
    ca_mutex_unlock(self->open_mutex, &wake_err);

    return CA_OK;
}

ca_status ca_player_stop(ca_player* self, ca_error* err) {
    if (err)
        ca_error_init(err);
    if (!self) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "self is null");
        return CA_INVALID_ARGUMENT;
    }

    ca_error open_err;
    ca_error_init(&open_err);
    ca_mutex_lock(self->open_mutex, &open_err);
    ca_status st = ca_player_stop_locked(self, &open_err);
    ca_mutex_unlock(self->open_mutex, &open_err);
    return st;
}

/* Caller must hold open_mutex. */
static ca_status ca_player_stop_locked(ca_player* self, ca_error* err) {
    (void)err;
    ca_player_state_t cur = atomic_load_explicit(&self->state, memory_order_acquire);
    if (cur == CA_PLAYER_STATE_STOPPED && !self->decoder) {
        return CA_OK;
    }

    atomic_store_explicit(&self->is_playing, false, memory_order_release);
    if (self->output)
        ca_audio_output_stop(self->output);
    if (self->ring)
        ca_ring_reset(self->ring);
    atomic_store_explicit(&self->pos_base, 0ULL, memory_order_relaxed);
    atomic_store_explicit(&self->pos_start_ms, 0LL, memory_order_relaxed);
    atomic_store_explicit(&self->state, CA_PLAYER_STATE_STOPPED, memory_order_release);
    atomic_store_explicit(&self->open_gate, false, memory_order_release);

    ca_error wake_err;
    ca_error_init(&wake_err);
    ca_cond_broadcast(self->cv, &wake_err);

    return CA_OK;
}

ca_status ca_player_seek(ca_player* self, double seconds, ca_error* err) {
    if (err)
        ca_error_init(err);
    if (!self) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "self is null");
        return CA_INVALID_ARGUMENT;
    }
    if (!self->decoder || !self->ring) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "not opened");
        return CA_INVALID_ARGUMENT;
    }
    if (seconds < 0.0 || isnan(seconds) || isinf(seconds)) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "bad seconds");
        return CA_INVALID_ARGUMENT;
    }

    ca_error seek_err;
    ca_error_init(&seek_err);
    ca_status st = ca_decoder_seek(self->decoder, seconds, &seek_err);
    if (st != CA_OK) {
        set_last_error(self, seek_err.message);
        atomic_store_explicit(&self->state, CA_PLAYER_STATE_ERROR, memory_order_release);
        if (err)
            *err = seek_err;
        return st;
    }

    uint64_t new_pos_base = (uint64_t)(seconds * (double)self->options.sample_rate);
    atomic_store_explicit(&self->pos_base, new_pos_base, memory_order_relaxed);
    ca_ring_reset(self->ring);

    ca_player_state_t s = atomic_load_explicit(&self->state, memory_order_acquire);
    if (s == CA_PLAYER_STATE_PLAYING) {
        atomic_store_explicit(&self->pos_start_ms, now_ms(), memory_order_release);
        ca_error wake_err;
        ca_error_init(&wake_err);
        ca_mutex_lock(self->open_mutex, &wake_err);
        atomic_store_explicit(&self->decode_busy, true, memory_order_release);
        ca_cond_broadcast(self->cv, &wake_err);
        ca_mutex_unlock(self->open_mutex, &wake_err);
    } else if (s == CA_PLAYER_STATE_PAUSED) {
        atomic_store_explicit(&self->pos_start_ms, 0LL, memory_order_relaxed);
    }

    return CA_OK;
}

ca_status ca_player_set_volume(ca_player* self, float volume, ca_error* err) {
    if (err)
        ca_error_init(err);
    if (!self) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "self is null");
        return CA_INVALID_ARGUMENT;
    }
    if (isnan(volume) || isinf(volume)) {
        if (err)
            ca_error_set(err, CA_INVALID_ARGUMENT, "bad volume");
        return CA_INVALID_ARGUMENT;
    }

    float vol = volume;
    if (vol < 0.0f)
        vol = 0.0f;
    else if (vol > 1.0f)
        vol = 1.0f;
    atomic_store_explicit(&self->volume, vol, memory_order_relaxed);
    if (self->output)
        ca_audio_output_set_volume(self->output, vol);
    return CA_OK;
}

ca_player_state_t ca_player_state(const ca_player* self) {
    if (!self)
        return CA_PLAYER_STATE_STOPPED;
    return atomic_load_explicit(&self->state, memory_order_acquire);
}

double ca_player_position(const ca_player* self) {
    if (!self)
        return 0.0;
    ca_player_state_t s = atomic_load_explicit(&self->state, memory_order_acquire);
    if (s == CA_PLAYER_STATE_STOPPED || s == CA_PLAYER_STATE_ERROR)
        return 0.0;

    uint64_t base = atomic_load_explicit(&self->pos_base, memory_order_relaxed);
    int64_t start = atomic_load_explicit(&self->pos_start_ms, memory_order_acquire);

    if (s == CA_PLAYER_STATE_PLAYING && start > 0) {
        int64_t now = now_ms();
        if (now > start) {
            double elapsed_sec = (double)(now - start) / 1000.0;
            uint64_t elapsed_frames = (uint64_t)(elapsed_sec * (double)self->options.sample_rate);
            return (double)(base + elapsed_frames) / (double)self->options.sample_rate;
        }
    }
    return (double)base / (double)self->options.sample_rate;
}

const char* ca_player_last_error(const ca_player* self) {
    if (!self)
        return "";
    return self->last_error;
}

/* --------------------------------------------------------------- */
/* Preroll                                                         */
/* --------------------------------------------------------------- */

static void ca_player_preroll(ca_player* self) {
    if (!self->decoder || !self->ring)
        return;

    size_t ring_cap = ca_ring_capacity(self->ring);
    size_t preroll_frames = ring_cap / CA_PLAYER_PREROLL_FRAMES_DIVISOR;
    uint32_t ch = ca_decoder_channels(self->decoder);
    if (ch == 0)
        ch = self->options.channels ? self->options.channels : 2U;

    size_t chunk_frames = CA_PLAYER_MAX_CHUNK_FRAMES;
    size_t max_chunk = CA_PLAYER_MAX_CHUNK_BY_CHANNELS / (size_t)ch;
    if (max_chunk == 0)
        max_chunk = 1;
    if (chunk_frames > max_chunk)
        chunk_frames = max_chunk;

    const cx_allocator* alloc = cx_allocator_default();
    float* buffer = (float*)cx_allocator_alloc(alloc, chunk_frames * ch * sizeof(float),
                                               alignof(float));
    if (!buffer)
        return;

    size_t filled = 0;
    while (filled < preroll_frames &&
           ca_ring_available_write(self->ring) >= chunk_frames) {
        size_t frames = ca_decoder_decode(self->decoder, buffer, chunk_frames * ch);
        if (frames == 0)
            break;
        size_t written = ca_ring_write(self->ring, buffer, frames * ch);
        filled += written;
        if (written < frames)
            break;
    }

    cx_allocator_free(alloc, buffer, chunk_frames * ch * sizeof(float), alignof(float));
}
