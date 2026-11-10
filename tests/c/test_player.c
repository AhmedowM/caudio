#include <caudio/player/player.h>

#include <caudio/runtime/error.h>

#include <cstdx/allocator.h>

#include <math.h>
#include <stdio.h>

#include "ca_test.h"

/* Mirror kanariv tests/test_player_integration.cpp: exercise the player state
 * machine and error paths using only mocked/minimal inputs (no real audio
 * file, no audio device). All paths are headless-safe. */

static void test_player_create_destroy(void) {
    ca_error err;
    ca_player *p = nullptr;
    ca_player_options opts;
    ca_player_options_init(&opts);

    ca_error_init(&err);
    CA_CHECK_EQ(CA_OK, ca_player_create(&p, &opts, &err));
    CA_CHECK(p != nullptr);
    CA_CHECK_EQ(CA_PLAYER_STATE_STOPPED, ca_player_state(p));
    CA_CHECK_EQ(0, (long long)ca_player_position(p));
    CA_CHECK_EQ('\0', ca_player_last_error(p)[0]);

    ca_player_destroy(p);
    /* destroy is NULL-safe; calling on a live pointer twice is caller UB. */
    ca_player_destroy(NULL);
}

static void test_player_create_validation(void) {
    ca_error err;
    ca_player *p = nullptr;

    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_player_create(nullptr, nullptr, &err));
    CA_CHECK(p == nullptr);
}

static void test_player_open_validation(void) {
    ca_error err;
    ca_player *p = nullptr;
    ca_player_options opts;
    ca_player_options_init(&opts);
    ca_error_init(&err);
    CA_CHECK_EQ(CA_OK, ca_player_create(&p, &opts, &err));

    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_player_open(nullptr, "x.wav", &err));
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_player_open(p, nullptr, &err));
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_player_open(p, "", &err));

    /* nonexistent path -> NOT_FOUND, state -> Error */
    ca_error_init(&err);
    CA_CHECK_EQ(CA_NOT_FOUND,
                ca_player_open(p, "caudio_nonexistent_player_xyz.wav", &err));
    CA_CHECK_EQ(CA_PLAYER_STATE_ERROR, ca_player_state(p));

    ca_player_destroy(p);
}

static void test_player_state_machine_errors(void) {
    ca_error err;
    ca_player *p = nullptr;
    ca_player_options opts;
    ca_player_options_init(&opts);
    ca_error_init(&err);
    CA_CHECK_EQ(CA_OK, ca_player_create(&p, &opts, &err));

    /* not opened: every mutating call is a state error */
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_player_play(p, &err));
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_player_pause(p, &err));
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_player_resume(p, &err));
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_player_seek(p, 1.0, &err));

    /* seek rejects invalid seconds */
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_player_seek(p, -1.0, &err));
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_player_seek(p, NAN, &err));
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_player_seek(p, INFINITY, &err));

    /* set_volume rejects NaN, clamps valid values (no output to apply to) */
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_player_set_volume(p, NAN, &err));
    ca_error_init(&err);
    CA_CHECK_EQ(CA_OK, ca_player_set_volume(p, 2.0f, &err));
    ca_error_init(&err);
    CA_CHECK_EQ(CA_OK, ca_player_set_volume(p, -1.0f, &err));

    ca_player_destroy(p);
}

static void test_player_open_reader_validation(void) {
    ca_error err;
    ca_player *p = nullptr;
    ca_player_options opts;
    ca_player_options_init(&opts);
    ca_error_init(&err);
    CA_CHECK_EQ(CA_OK, ca_player_create(&p, &opts, &err));

    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_player_open_reader(p, nullptr, &err));
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_player_open_reader(nullptr, nullptr, &err));

    ca_player_destroy(p);
}

int main(void) {
    test_player_create_destroy();
    test_player_create_validation();
    test_player_open_validation();
    test_player_state_machine_errors();
    test_player_open_reader_validation();
    return ca_test_finish("player");
}
