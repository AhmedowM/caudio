#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <windows.h>

static double phase = 0.0;

static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    (void)pDevice;
    float* output = (float*)pOutput;
    ma_uint32 totalSamples = frameCount * 2;

    const double freq = 440.0;
    const double sampleRate = 48000.0;
    const double phaseInc = 2.0 * 3.141592653589793 * freq / sampleRate;

    for (ma_uint32 i = 0; i < totalSamples; i += 2) {
        float sample = (float)(sin(phase) * 0.3f);
        output[i] = sample;     // Left channel
        output[i + 1] = sample; // Right channel
        phase += phaseInc;
        if (phase >= 2.0 * 3.141592653589793) phase -= 2.0 * 3.141592653589793;
    }
}

int main() {
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate = 48000;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = NULL;

    ma_device device;
    ma_result res = ma_device_init(NULL, &deviceConfig, &device);
    if (res != MA_SUCCESS) {
        printf("Failed to init device: %d\n", res);
        return 1;
    }

    printf("Device started: %d ch, %d Hz\n", device.playback.channels, device.sampleRate);

    res = ma_device_start(&device);
    if (res != MA_SUCCESS) {
        printf("Failed to start device: %d\n", res);
        ma_device_uninit(&device);
        return 1;
    }

    printf("Playing 440Hz sine wave for 3 seconds...\n");
    Sleep(3000);

    ma_device_stop(&device);
    ma_device_uninit(&device);
    printf("Done\n");
    return 0;
}