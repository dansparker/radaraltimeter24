/**
 * @file rdsp_fft.h
 * @brief Step 3 - real-valued FFT (radix-2, in place, no allocation).
 *
 * A real sequence of length N is transformed as a complex FFT of length N/2
 * followed by a split step. Output format (same as CMSIS arm_rfft_fast_f32):
 *   buf[0] = Re X[0]   (DC)
 *   buf[1] = Re X[N/2] (Nyquist)
 *   buf[2k], buf[2k+1] = Re X[k], Im X[k]   for k = 1 .. N/2-1
 *
 * The implementation is portable, so the exact same code runs on the target
 * and in the host unit tests. On a Cortex-M4F @168 MHz an N=2048 transform
 * takes roughly 1 ms.
 */
#ifndef RDSP_FFT_H
#define RDSP_FFT_H

#include "rdsp_common.h"

typedef struct {
    uint32_t n;          /**< real FFT length (power of two, >= 8) */
    const float *tw;     /**< twiddles W_N^k, k = 0..N/2-1, interleaved re/im */
} rdsp_rfft_t;

/**
 * Initialise an FFT instance.
 * @param tw_buf caller storage of n floats (kept by the instance)
 * @return 0 on success, -1 if n is not a power of two >= 8
 */
int rdsp_rfft_init(rdsp_rfft_t *f, uint32_t n, float *tw_buf);

/** In-place forward real FFT of buf[0..n-1] (output format see above). */
void rdsp_rfft(const rdsp_rfft_t *f, float *buf);

/**
 * |X[k]|^2 for k = 0 .. n/2-1 from the packed spectrum.
 * p may alias X only if p == X (it is computed front to back safely).
 */
void rdsp_rfft_power(const float *X, float *p, uint32_t n);

#endif /* RDSP_FFT_H */
