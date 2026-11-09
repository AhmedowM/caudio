#include <caudio/media/fft.h>

#include <caudio/runtime/error.h>

#include <cstdx/allocator.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ca_test.h"

static void test_fft_r2c(void) {
    ca_error err;
    ca_error_init(&err);
    const cx_allocator* alloc = cx_allocator_default();

    ca_fft_plan *plan = nullptr;
    CA_CHECK_EQ(CA_OK, ca_fft_plan_create_r2c(&plan, 8, &err));
    CA_CHECK(plan != nullptr);
    CA_CHECK_EQ(8, ca_fft_plan_length(plan));
    CA_CHECK_EQ(5, ca_fft_plan_output_bins(plan));

    float in[8] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    ca_complex_f out[5];
    CA_CHECK_EQ(CA_OK, ca_fft_execute_r2c(plan, in, out, &err));
    /* DC bin = sum of all inputs = 1.0 */
    CA_CHECK(fabs(out[0].re - 1.0f) < 0.001f);
    CA_CHECK(fabs(out[0].im) < 0.001f);

    ca_fft_plan_destroy(plan);
}

static void test_fft_c2c(void) {
    ca_error err;
    ca_error_init(&err);

    ca_fft_plan *plan = nullptr;
    CA_CHECK_EQ(CA_OK, ca_fft_plan_create_c2c(&plan, 4, &err));
    CA_CHECK(plan != nullptr);
    CA_CHECK_EQ(4, ca_fft_plan_length(plan));
    CA_CHECK_EQ(4, ca_fft_plan_output_bins(plan));

    ca_complex_f in[4] = {{1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}};
    ca_complex_f out[4];
    CA_CHECK_EQ(CA_OK, ca_fft_execute_c2c(plan, in, out, false, &err));
    /* DC bin = sum = 1.0 */
    CA_CHECK(fabs(out[0].re - 1.0f) < 0.001f);
    CA_CHECK(fabs(out[0].im) < 0.001f);

    /* Inverse should give back the original */
    ca_complex_f out2[4];
    CA_CHECK_EQ(CA_OK, ca_fft_execute_c2c(plan, out, out2, true, &err));
    CA_CHECK(fabs(out2[0].re - 1.0f) < 0.001f);
    CA_CHECK(fabs(out2[1].re) < 0.001f);

    ca_fft_plan_destroy(plan);
}

static void test_fft_invalid(void) {
    ca_error err;
    ca_error_init(&err);
    ca_fft_plan *plan = nullptr;

    /* Invalid length */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_fft_plan_create_r2c(&plan, 3, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_fft_plan_create_r2c(&plan, 1, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_fft_plan_create_c2c(&plan, 0, &err));
    CA_CHECK(plan == nullptr);
}

static void test_fft_one_shot(void) {
    ca_error err;
    ca_error_init(&err);

    float in[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    ca_complex_f *bins = nullptr;
    size_t bins_count = 0;
    CA_CHECK_EQ(CA_OK, ca_fft_real(in, 4, &bins, &bins_count, &err));
    CA_CHECK(bins != nullptr);
    CA_CHECK_EQ(3, bins_count);
    CA_CHECK(fabs(bins[0].re - 1.0f) < 0.001f);
    free(bins);

    /* IFFT */
    ca_complex_f bins2[3] = {{1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}};
    float *samples = nullptr;
    size_t samples_count = 0;
    CA_CHECK_EQ(CA_OK, ca_fft_ifft_real(bins2, 3, &samples, &samples_count, &err));
    CA_CHECK(samples != nullptr);
    CA_CHECK_EQ(4, samples_count);
    CA_CHECK(fabs(samples[0] - 1.0f) < 0.001f);
    CA_CHECK(fabs(samples[1]) < 0.001f);
    free(samples);
}

static void test_fft_windows(void) {
    float hann[4];
    CA_CHECK_EQ(CA_OK, ca_fft_window_hann(hann, 4));
    CA_CHECK(fabs(hann[0] - 0.0f) < 0.001f);
    CA_CHECK(fabs(hann[3] - 0.0f) < 0.001f);
    CA_CHECK(hann[1] > 0.0f && hann[1] < 1.0f);
    CA_CHECK(hann[2] > 0.0f && hann[2] < 1.0f);

    float hamming[4];
    CA_CHECK_EQ(CA_OK, ca_fft_window_hamming(hamming, 4));
    CA_CHECK(fabs(hamming[0] - 0.08f) < 0.01f);
    CA_CHECK(fabs(hamming[3] - 0.08f) < 0.01f);

    /* Apply in place */
    float data[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    CA_CHECK_EQ(CA_OK, ca_fft_window_apply(data, 4, hann));
    CA_CHECK(fabs(data[0]) < 0.001f);
    CA_CHECK(fabs(data[3]) < 0.001f);

    /* Invalid length */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_fft_window_hann(nullptr, 0));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_fft_window_hamming(nullptr, 0));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_fft_window_apply(nullptr, 4, hann));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_fft_window_apply(data, 4, nullptr));
}

static void test_fft_errors(void) {
    ca_error err;
    ca_error_init(&err);

    /* NULL args */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_fft_plan_create_r2c(nullptr, 8, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_fft_plan_create_c2c(nullptr, 8, &err));

    ca_fft_plan *plan = nullptr;
    CA_CHECK_EQ(CA_OK, ca_fft_plan_create_r2c(&plan, 4, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_fft_execute_r2c(nullptr, nullptr, nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_fft_execute_r2c(plan, nullptr, nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_fft_execute_c2c(nullptr, nullptr, nullptr, false, &err));

    ca_fft_plan_destroy(plan);
    ca_fft_plan_destroy(nullptr); /* no-op */
}

int main(void) {
    test_fft_r2c();
    test_fft_c2c();
    test_fft_invalid();
    test_fft_one_shot();
    test_fft_windows();
    test_fft_errors();
    return ca_test_finish("fft");
}
