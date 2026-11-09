#ifndef CAUDIO_MEDIA_FFT_H
#define CAUDIO_MEDIA_FFT_H

/**
 * @file caudio/media/fft.h
 * @brief Real and complex FFT with windowing helpers.
 * @ingroup caudio_media
 * @details Power-of-two length FFT implementation using a radix-2
 * Cooley-Tukey decimation-in-time algorithm. Mirrors the
 * kanariv::media::fft interface in C.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Complex float value.
 */
typedef struct ca_complex_f {
    float re;
    float im;
} ca_complex_f;

/**
 * @brief Opaque FFT plan handle.
 */
typedef struct ca_fft_plan ca_fft_plan;

/**
 * @brief Create a real-input FFT plan for length n.
 *
 * @param[out] out Receives the new plan handle on success.
 * @param n Transform length; must be a power of two and >= 2.
 * @param err Optional error record.
 * @return CA_OK on success, CA_INVALID_ARGUMENT for invalid length,
 *         CA_OUT_OF_MEMORY on allocation failure.
 */
[[nodiscard]] CAUDIO_API ca_status ca_fft_plan_create_r2c(ca_fft_plan** out,
                                               size_t n,
                                               ca_error* err);

/**
 * @brief Create a complex-input FFT plan for length n.
 *
 * @param[out] out Receives the new plan handle on success.
 * @param n Transform length; must be a power of two and >= 2.
 * @param err Optional error record.
 * @return CA_OK on success, CA_INVALID_ARGUMENT for invalid length,
 *         CA_OUT_OF_MEMORY on allocation failure.
 */
[[nodiscard]] CAUDIO_API ca_status ca_fft_plan_create_c2c(ca_fft_plan** out,
                                               size_t n,
                                               ca_error* err);

/**
 * @brief Execute a real-input FFT.
 *
 * @param plan Plan handle.
 * @param input n real samples.
 * @param output n/2 + 1 complex bins (DC through Nyquist).
 * @param err Optional error record.
 * @return CA_OK on success, CA_INVALID_ARGUMENT on size mismatch.
 */
[[nodiscard]] CAUDIO_API ca_status ca_fft_execute_r2c(const ca_fft_plan* plan,
                                           const float* input,
                                           ca_complex_f* output,
                                           ca_error* err);

/**
 * @brief Execute a complex-input FFT (forward or inverse).
 *
 * @param plan Plan handle.
 * @param input n complex samples.
 * @param output n complex bins.
 * @param inverse When true, computes the inverse FFT (scales by 1/n).
 * @param err Optional error record.
 * @return CA_OK on success, CA_INVALID_ARGUMENT on size mismatch.
 */
[[nodiscard]] CAUDIO_API ca_status ca_fft_execute_c2c(const ca_fft_plan* plan,
                                           const ca_complex_f* input,
                                           ca_complex_f* output,
                                           bool inverse,
                                           ca_error* err);

/**
 * @brief Transform length (power of two).
 */
[[nodiscard]] CAUDIO_API size_t ca_fft_plan_length(const ca_fft_plan* plan);

/**
 * @brief Number of real-output bins for an R2C plan (n/2 + 1).
 */
[[nodiscard]] CAUDIO_API size_t ca_fft_plan_output_bins(const ca_fft_plan* plan);

/**
 * @brief Destroy an FFT plan.
 *
 * Passing nullptr is a no-op.
 */
CAUDIO_API void ca_fft_plan_destroy(ca_fft_plan* plan);

/* ---------------------------------------------------------- */
/* Window functions                                           */
/* ---------------------------------------------------------- */

/**
 * @brief Hann window of length n.
 *
 * @param n Window length; must be >= 1.
 * @param[out] out Receives n window samples.
 * @return CA_OK on success, CA_INVALID_ARGUMENT for n == 0.
 */
[[nodiscard]] CAUDIO_API ca_status ca_fft_window_hann(float* out, size_t n);

/**
 * @brief Hamming window of length n.
 *
 * @param n Window length; must be >= 1.
 * @param[out] out Receives n window samples.
 * @return CA_OK on success, CA_INVALID_ARGUMENT for n == 0.
 */
[[nodiscard]] CAUDIO_API ca_status ca_fft_window_hamming(float* out, size_t n);

/**
 * @brief Apply a window in place to samples.
 *
 * @param samples Audio block to window (modified in place).
 * @param n Number of samples.
 * @param window Window coefficients; must equal n samples.
 * @return CA_OK on success, CA_INVALID_ARGUMENT on length mismatch.
 */
[[nodiscard]] CAUDIO_API ca_status ca_fft_window_apply(float* samples,
                                            size_t n,
                                            const float* window);

/* ---------------------------------------------------------- */
/* One-shot convenience functions                             */
/* ---------------------------------------------------------- */

/**
 * @brief One-shot real FFT: plan + execute in a single call.
 *
 * @param samples n real samples (n must be a power of two).
 * @param n Number of samples.
 * @param[out] bins Receives n/2 + 1 complex bins (allocated).
 * @param[out] bins_count Receives the number of bins.
 * @param err Optional error record.
 * @return CA_OK on success, ca_status error on failure.
 * @details The caller owns the bins array and must free it.
 */
[[nodiscard]] CAUDIO_API ca_status ca_fft_real(const float* samples,
                                    size_t n,
                                    ca_complex_f** bins,
                                    size_t* bins_count,
                                    ca_error* err);

/**
 * @brief One-shot inverse real FFT: plan + execute in a single call.
 *
 * @param bins n/2 + 1 complex bins (DC through Nyquist).
 * @param bins_count Number of bins.
 * @param[out] samples Receives n real samples (allocated).
 * @param[out] samples_count Receives the number of samples.
 * @param err Optional error record.
 * @return CA_OK on success, ca_status error on failure.
 * @details The caller owns the samples array and must free it.
 */
[[nodiscard]] CAUDIO_API ca_status ca_fft_ifft_real(const ca_complex_f* bins,
                                         size_t bins_count,
                                         float** samples,
                                         size_t* samples_count,
                                         ca_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_MEDIA_FFT_H */
