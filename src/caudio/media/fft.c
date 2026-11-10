#include <caudio/media/fft.h>

#include <caudio/runtime/error.h>

#include <cstdx/allocator.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

static constexpr double CA_M_PI = 3.14159265358979323846;

/* ---------------------------------------------------------- */
/* FFT plan implementation                                    */
/* ---------------------------------------------------------- */

struct ca_fft_plan {
    size_t length;
    size_t output_bins;
    bool r2c;
    const cx_allocator* alloc;
};

static bool is_power_of_two(size_t n) {
    return n != 0 && (n & (n - 1)) == 0;
}

static int log2_of_power_of_two(size_t n) {
    int bits = 0;
    while (n > 1) { n >>= 1; ++bits; }
    return bits;
}

static size_t bit_reverse(size_t idx, int bits) {
    size_t rev = 0;
    for (int i = 0; i < bits; ++i) {
        rev = (rev << 1) | (idx & 1u);
        idx >>= 1;
    }
    return rev;
}

/* Iterative radix-2 decimation-in-time Cooley-Tukey. */
static void fft_radix2(ca_complex_f* z, size_t n, bool inverse) {
    int bits = log2_of_power_of_two(n);

    /* Bit-reversal permutation. */
    for (size_t i = 0; i < n; ++i) {
        size_t j = bit_reverse(i, bits);
        if (i < j) {
            ca_complex_f tmp = z[i];
            z[i] = z[j];
            z[j] = tmp;
        }
    }

    /* Butterflies. */
    for (size_t len = 2; len <= n; len <<= 1) {
        float sign = inverse ? 1.0f : -1.0f;
        float ang = sign * 2.0f * (float)CA_M_PI / (float)len;
        ca_complex_f wlen = { cosf(ang), sinf(ang) };
        for (size_t i = 0; i < n; i += len) {
            ca_complex_f w = { 1.0f, 0.0f };
            size_t half = len / 2;
            for (size_t j = 0; j < half; ++j) {
                ca_complex_f u = z[i + j];
                ca_complex_f v = {
                    z[i + j + half].re * w.re - z[i + j + half].im * w.im,
                    z[i + j + half].re * w.im + z[i + j + half].im * w.re
                };
                z[i + j].re = u.re + v.re;
                z[i + j].im = u.im + v.im;
                z[i + j + half].re = u.re - v.re;
                z[i + j + half].im = u.im - v.im;
                /* w *= wlen */
                ca_complex_f tmp = {
                    w.re * wlen.re - w.im * wlen.im,
                    w.re * wlen.im + w.im * wlen.re
                };
                w = tmp;
            }
        }
    }
}

ca_status ca_fft_plan_create_r2c(ca_fft_plan** out, size_t n, ca_error* err) {
    if (err) ca_error_init(err);
    if (!out) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "out is NULL");
        return CA_INVALID_ARGUMENT;
    }
    if (!is_power_of_two(n) || n < 2) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "length must be power of two >= 2");
        return CA_INVALID_ARGUMENT;
    }

    const cx_allocator* alloc = cx_allocator_default();
    ca_fft_plan* plan = (ca_fft_plan*)cx_allocator_alloc(alloc, sizeof(ca_fft_plan), alignof(ca_fft_plan));
    if (!plan) {
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }

    plan->length = n;
    plan->output_bins = n / 2 + 1;
    plan->r2c = true;
    plan->alloc = alloc;

    *out = plan;
    return CA_OK;
}

ca_status ca_fft_plan_create_c2c(ca_fft_plan** out, size_t n, ca_error* err) {
    if (err) ca_error_init(err);
    if (!out) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "out is NULL");
        return CA_INVALID_ARGUMENT;
    }
    if (!is_power_of_two(n) || n < 2) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "length must be power of two >= 2");
        return CA_INVALID_ARGUMENT;
    }

    const cx_allocator* alloc = cx_allocator_default();
    ca_fft_plan* plan = (ca_fft_plan*)cx_allocator_alloc(alloc, sizeof(ca_fft_plan), alignof(ca_fft_plan));
    if (!plan) {
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }

    plan->length = n;
    plan->output_bins = n;
    plan->r2c = false;
    plan->alloc = alloc;

    *out = plan;
    return CA_OK;
}

ca_status ca_fft_execute_r2c(const ca_fft_plan* plan,
                             const float* input,
                             ca_complex_f* output,
                             ca_error* err) {
    if (err) ca_error_init(err);
    if (!plan || !input || !output) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return CA_INVALID_ARGUMENT;
    }

    /* Pack real input into full complex array, run FFT, keep first n/2+1 bins. */
    ca_complex_f* z = (ca_complex_f*)malloc(plan->length * sizeof(ca_complex_f));
    if (!z) {
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < plan->length; ++i) {
        z[i].re = input[i];
        z[i].im = 0.0f;
    }

    fft_radix2(z, plan->length, false);

    for (size_t i = 0; i < plan->output_bins; ++i) {
        output[i] = z[i];
    }
    free(z);
    return CA_OK;
}

ca_status ca_fft_execute_c2c(const ca_fft_plan* plan,
                             const ca_complex_f* input,
                             ca_complex_f* output,
                             bool inverse,
                             ca_error* err) {
    if (err) ca_error_init(err);
    if (!plan || !input || !output) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return CA_INVALID_ARGUMENT;
    }

    ca_complex_f* z = (ca_complex_f*)malloc(plan->length * sizeof(ca_complex_f));
    if (!z) {
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }
    memcpy(z, input, plan->length * sizeof(ca_complex_f));

    fft_radix2(z, plan->length, inverse);

    if (inverse) {
        float scale = 1.0f / (float)plan->length;
        for (size_t i = 0; i < plan->length; ++i) {
            z[i].re *= scale;
            z[i].im *= scale;
        }
    }

    memcpy(output, z, plan->length * sizeof(ca_complex_f));
    free(z);
    return CA_OK;
}

size_t ca_fft_plan_length(const ca_fft_plan* plan) {
    return plan ? plan->length : 0;
}

size_t ca_fft_plan_output_bins(const ca_fft_plan* plan) {
    return plan ? plan->output_bins : 0;
}

void ca_fft_plan_destroy(ca_fft_plan* plan) {
    if (!plan) return;
    const cx_allocator* alloc = plan->alloc;
    cx_allocator_free(alloc, plan, sizeof(ca_fft_plan), alignof(ca_fft_plan));
}

/* ---------------------------------------------------------- */
/* Window functions                                           */
/* ---------------------------------------------------------- */

ca_status ca_fft_window_hann(float* out, size_t n) {
    if (!out || n == 0) return CA_INVALID_ARGUMENT;
    if (n == 1) {
        out[0] = 1.0f;
        return CA_OK;
    }
    float denom = (float)(n - 1);
    for (size_t i = 0; i < n; ++i) {
        out[i] = 0.5f * (1.0f - cosf(2.0f * (float)CA_M_PI * (float)i / denom));
    }
    return CA_OK;
}

ca_status ca_fft_window_hamming(float* out, size_t n) {
    if (!out || n == 0) return CA_INVALID_ARGUMENT;
    if (n == 1) {
        out[0] = 1.0f;
        return CA_OK;
    }
    float denom = (float)(n - 1);
    for (size_t i = 0; i < n; ++i) {
        out[i] = 0.54f - 0.46f * cosf(2.0f * (float)CA_M_PI * (float)i / denom);
    }
    return CA_OK;
}

ca_status ca_fft_window_apply(float* samples, size_t n, const float* window) {
    if (!samples || !window || n == 0) return CA_INVALID_ARGUMENT;
    for (size_t i = 0; i < n; ++i) {
        samples[i] *= window[i];
    }
    return CA_OK;
}

/* ---------------------------------------------------------- */
/* One-shot convenience functions                             */
/* ---------------------------------------------------------- */

ca_status ca_fft_real(const float* samples,
                      size_t n,
                      ca_complex_f** bins,
                      size_t* bins_count,
                      ca_error* err) {
    if (err) ca_error_init(err);
    if (!samples || !bins || !bins_count || n == 0) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return CA_INVALID_ARGUMENT;
    }

    ca_fft_plan* plan = nullptr;
    ca_status st = ca_fft_plan_create_r2c(&plan, n, err);
    if (st != CA_OK) return st;

    ca_complex_f* out = (ca_complex_f*)malloc(plan->output_bins * sizeof(ca_complex_f));
    if (!out) {
        ca_fft_plan_destroy(plan);
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }

    st = ca_fft_execute_r2c(plan, samples, out, err);
    size_t out_bins = plan->output_bins;
    ca_fft_plan_destroy(plan);
    if (st != CA_OK) {
        free(out);
        return st;
    }

    *bins = out;
    *bins_count = out_bins;
    return CA_OK;
}

ca_status ca_fft_ifft_real(const ca_complex_f* bins,
                           size_t bins_count,
                           float** samples,
                           size_t* samples_count,
                           ca_error* err) {
    if (err) ca_error_init(err);
    if (!bins || !samples || !samples_count || bins_count == 0) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return CA_INVALID_ARGUMENT;
    }

    size_t half = bins_count - 1;
    size_t n = 2 * half;
    if (!is_power_of_two(n)) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "bin count must be n/2+1 for power-of-two n");
        return CA_INVALID_ARGUMENT;
    }

    ca_fft_plan* plan = nullptr;
    ca_status st = ca_fft_plan_create_c2c(&plan, n, err);
    if (st != CA_OK) return st;

    /* Reconstruct conjugate-symmetric spectrum. */
    ca_complex_f* spectrum = (ca_complex_f*)malloc(n * sizeof(ca_complex_f));
    if (!spectrum) {
        ca_fft_plan_destroy(plan);
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i <= half; ++i) spectrum[i] = bins[i];
    for (size_t i = half + 1; i < n; ++i) {
        spectrum[i].re = bins[n - i].re;
        spectrum[i].im = -bins[n - i].im;
    }

    ca_complex_f* out_c = (ca_complex_f*)malloc(n * sizeof(ca_complex_f));
    if (!out_c) {
        free(spectrum);
        ca_fft_plan_destroy(plan);
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }

    st = ca_fft_execute_c2c(plan, spectrum, out_c, true, err);
    ca_fft_plan_destroy(plan);
    free(spectrum);
    if (st != CA_OK) {
        free(out_c);
        return st;
    }

    float* out = (float*)malloc(n * sizeof(float));
    if (!out) {
        free(out_c);
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }
    for (size_t i = 0; i < n; ++i) out[i] = out_c[i].re;
    free(out_c);

    *samples = out;
    *samples_count = n;
    return CA_OK;
}
