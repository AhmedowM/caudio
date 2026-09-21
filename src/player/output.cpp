#include "caudio/player/output.hpp"

namespace caudio::player {

DeviceList enumerateDevices() {
    DeviceList list;
    ma_context context;
    ma_context_config ctxConfig = ma_context_config_init();
    ma_result res = ma_context_init(nullptr, 0, &ctxConfig, &context);
    if (res != MA_SUCCESS) {
        return list;
    }

    ma_device_info* pPlaybackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    ma_device_info* pCaptureInfos = nullptr;
    ma_uint32 captureCount = 0;

    res = ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, &pCaptureInfos,
                                 &captureCount);
    if (res == MA_SUCCESS && pPlaybackInfos && playbackCount > 0) {
        list.devices.reserve(playbackCount);
        for (ma_uint32 i = 0; i < playbackCount; ++i) {
            const auto& info = pPlaybackInfos[i];
            DeviceInfo di;
            di.name = info.name;
            di.isDefault = info.isDefault;
            // Use simple index-based ID for user-friendly CLI
            di.id = std::to_string(i);
            list.devices.push_back(std::move(di));
        }
    }

    ma_context_uninit(&context);
    return list;
}

AudioOutput::~AudioOutput() {
    shutdown();
}

AudioOutput::Expected AudioOutput::create(const Config& cfg) {
    auto out = std::make_unique<AudioOutput>();
    if (!out->init(cfg)) {
        return std::unexpected(caudio::utils::Error{
            caudio::utils::StatusCode::Device, std::string_view("miniaudio device init failed")});
    }
    return out;
}

float AudioOutput::clampVolume(float v) noexcept {
    if (!std::isfinite(v))
        return 0.0f;
    return std::clamp(v, 0.0f, 1.0f);
}

void AudioOutput::setVolume(float vol) {
    volume_.store(clampVolume(vol), std::memory_order_relaxed);
}

float AudioOutput::volume() const noexcept {
    return volume_.load(std::memory_order_relaxed);
}

void AudioOutput::testFill(std::span<float> buf) const noexcept {
    std::ranges::fill(buf, 0.0f);
}

void AudioOutput::fillFromRing(std::span<float> out, caudio::utils::SpscRing<float>* ring,
                               uint32_t channels, float vol) noexcept {
    if (out.empty())
        return;
    std::size_t totalSamples = out.size();
    std::size_t generatedFrames = 0;
    if (ring) {
        generatedFrames = ring->read(std::span<float>(out.data(), totalSamples));
    }
    std::size_t generatedSamples = generatedFrames * channels;
    if (generatedSamples < totalSamples) {
        std::ranges::fill(out.subspan(generatedSamples), 0.0f);
    }
    if (vol != 1.0f) {
        for (std::size_t i = 0; i < totalSamples; ++i)
            out[i] *= vol;
    }
}

void AudioOutput::fillForTest(std::span<float> out) noexcept {
    uint32_t channels = cfg_.channels ? cfg_.channels : 1;
    float vol = volume_.load(std::memory_order_relaxed);
    fillFromRing(out, cfg_.ring, channels, vol);
}

bool AudioOutput::isPlaying() const noexcept {
    return running_.load(std::memory_order_acquire);
}

void AudioOutput::start() {
    if (!initialized_.load(std::memory_order_acquire))
        return;
    bool expected = false;
    if (running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        ma_device_start(&device_);
    }
}

void AudioOutput::stop() {
    bool expected = true;
    if (running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        ma_device_stop(&device_);
    }
}

void AudioOutput::shutdown() noexcept {
    stop();
    if (initialized_.exchange(false, std::memory_order_acq_rel)) {
        ma_device_uninit(&device_);
    }
}

bool AudioOutput::init(const Config& cfg) {
    if (cfg.channels == 0 || cfg.channels > 32 || cfg.sampleRate == 0)
        return false;
    cfg_ = cfg;
    float v = clampVolume(cfg.volume);
    volume_.store(v, std::memory_order_relaxed);

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = cfg.channels;
    deviceConfig.sampleRate = cfg.sampleRate;
    deviceConfig.dataCallback = &AudioOutput::dataCallback;
    deviceConfig.pUserData = this;

    ma_result res = ma_device_init(nullptr, &deviceConfig, &device_);
    if (res != MA_SUCCESS) {
        return false;
    }
    initialized_.store(true, std::memory_order_release);

    // @pre caller must start() after preroll, cap/2 frames
    running_.store(false, std::memory_order_release);

    return true;
}

void AudioOutput::dataCallback(ma_device* pDevice, void* pOutput, const void* pInput,
                               ma_uint32 frameCount) {
    (void)pInput;
    auto* self = static_cast<AudioOutput*>(pDevice->pUserData);
    if (!self)
        return;

    float* output = static_cast<float*>(pOutput);
    ma_uint32 channels = pDevice->playback.channels;
    ma_uint32 totalSamples = frameCount * channels;
    float vol = self->volume_.load(std::memory_order_relaxed);
    // lock-free, no allocation
    fillFromRing(std::span<float>(output, totalSamples), self->cfg_.ring, channels, vol);
}

} // namespace caudio::player
