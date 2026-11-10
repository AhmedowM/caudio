#include <caudio/player/output.h>

#include <caudio/runtime/error.h>

#include <cstdx/allocator.h>

#include <stdio.h>

#include "ca_test.h"

/* Mirror kanariv tests/test_output.cpp: exercise the output config and the
 * input validation that runs before any miniaudio device is touched. These
 * paths are headless-safe (no audio device required). */

static void test_output_config_defaults(void) {
    ca_audio_output_config cfg;
    ca_audio_output_config_init(&cfg);
    CA_CHECK_EQ(48000u, cfg.sample_rate);
    CA_CHECK_EQ(2u, cfg.channels);
    CA_CHECK(cfg.ring == nullptr);
    CA_CHECK_EQ(1.0f, cfg.volume);
    CA_CHECK(cfg.logger == nullptr);
}

static void test_output_create_validation(void) {
    ca_error err;
    ca_audio_output *out = nullptr;
    ca_audio_output_config cfg;

    /* NULL out handle */
    ca_audio_output_config_init(&cfg);
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_audio_output_create(nullptr, &cfg, &err));

    /* NULL config */
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_audio_output_create(&out, nullptr, &err));
    CA_CHECK(out == nullptr);

    /* NULL ring (validation happens before device init) */
    ca_audio_output_config_init(&cfg);
    cfg.ring = nullptr;
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_audio_output_create(&out, &cfg, &err));
    CA_CHECK(out == nullptr);

    /* channels == 0 */
    ca_audio_output_config_init(&cfg);
    cfg.channels = 0;
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_audio_output_create(&out, &cfg, &err));

    /* channels > 32 */
    ca_audio_output_config_init(&cfg);
    cfg.channels = 33;
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_audio_output_create(&out, &cfg, &err));

    /* sample_rate == 0 */
    ca_audio_output_config_init(&cfg);
    cfg.sample_rate = 0;
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_audio_output_create(&out, &cfg, &err));
}

int main(void) {
    test_output_config_defaults();
    test_output_create_validation();
    return ca_test_finish("output");
}
