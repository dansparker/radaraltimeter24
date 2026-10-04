/**
 * @file rdsp_fft.c
 * @brief Step 3 - real-valued FFT (see rdsp_fft.h).
 *
 * Algorithm:
 *  1. z[k] = x[2k] + j x[2k+1], k = 0..M-1, M = N/2 (this is just the real
 *     buffer reinterpreted as complex - no copy needed)
 *  2. Z = CFFT_M(z) (iterative radix-2 DIT, bit reversal first)
 *  3. X[k] = Fe[k] + W_N^k Fo[k] with
 *        Fe[k] = (Z[k] + conj Z[M-k]) / 2
 *        Fo[k] = (Z[k] - conj Z[M-k]) / 2j
 *     and X[M-k] = conj(Fe[k] - W_N^k Fo[k]) so pairs are done in place.
 */
#include "rdsp/rdsp_fft.h"
#include <math.h>

int rdsp_rfft_init(rdsp_rfft_t *f, uint32_t n, float *tw_buf)
{
    if ((f == NULL) || (tw_buf == NULL) || (n < 8u) || ((n & (n - 1u)) != 0u)) {
        return -1;
    }
    for (uint32_t k = 0; k < n / 2u; k++) {
        const double a = -2.0 * 3.14159265358979323846 * (double)k / (double)n;
        tw_buf[2u * k] = (float)cos(a);
        tw_buf[2u * k + 1u] = (float)sin(a);
    }
    f->n = n;
    f->tw = tw_buf;
    return 0;
}

/* complex in-place FFT of m points; W_m^j = tw[2*j*stride] */
static void cfft(float *x, uint32_t m, const float *tw, uint32_t stride)
{
    /* bit reversal permutation */
    for (uint32_t i = 1u, j = 0u; i < m; i++) {
        uint32_t bit = m >> 1;
        for (; (j & bit) != 0u; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            float t = x[2u * i]; x[2u * i] = x[2u * j]; x[2u * j] = t;
            t = x[2u * i + 1u]; x[2u * i + 1u] = x[2u * j + 1u]; x[2u * j + 1u] = t;
        }
    }
    /* butterflies */
    for (uint32_t len = 2u; len <= m; len <<= 1) {
        const uint32_t half = len >> 1;
        const uint32_t step = (m / len) * stride;   /* W_len^j = W_m^(j*m/len) */
        for (uint32_t j = 0; j < half; j++) {
            const float wr = tw[2u * j * step];
            const float wi = tw[2u * j * step + 1u];
            for (uint32_t i = j; i < m; i += len) {
                float *a = &x[2u * i];
                float *b = &x[2u * (i + half)];
                const float tr = b[0] * wr - b[1] * wi;
                const float ti = b[0] * wi + b[1] * wr;
                b[0] = a[0] - tr;
                b[1] = a[1] - ti;
                a[0] += tr;
                a[1] += ti;
            }
        }
    }
}

void rdsp_rfft(const rdsp_rfft_t *f, float *buf)
{
    const uint32_t m = f->n / 2u;
    const float *tw = f->tw;

    /* W_m^j = W_N^(2j) -> stride 2 in the W_N table */
    cfft(buf, m, tw, 2u);

    /* k = 0 and k = M (DC and Nyquist are purely real) */
    const float z0r = buf[0], z0i = buf[1];
    buf[0] = z0r + z0i;
    buf[1] = z0r - z0i;

    for (uint32_t k = 1u; k <= m / 2u; k++) {
        const uint32_t mk = m - k;
        const float zkr = buf[2u * k], zki = buf[2u * k + 1u];
        const float zmr = buf[2u * mk], zmi = buf[2u * mk + 1u];

        const float fer = 0.5f * (zkr + zmr);
        const float fei = 0.5f * (zki - zmi);
        const float for_ = 0.5f * (zki + zmi);
        const float foi = -0.5f * (zkr - zmr);

        const float wr = tw[2u * k], wi = tw[2u * k + 1u];
        const float tr = wr * for_ - wi * foi;   /* W * Fo */
        const float ti = wr * foi + wi * for_;

        buf[2u * k] = fer + tr;
        buf[2u * k + 1u] = fei + ti;
        if (mk != k) {
            buf[2u * mk] = fer - tr;            /* conj(Fe - W Fo) */
            buf[2u * mk + 1u] = -(fei - ti);
        }
    }
}

void rdsp_rfft_power(const float *X, float *p, uint32_t n)
{
    p[0] = X[0] * X[0];
    for (uint32_t k = 1u; k < n / 2u; k++) {
        const float re = X[2u * k], im = X[2u * k + 1u];
        p[k] = re * re + im * im;
    }
}
