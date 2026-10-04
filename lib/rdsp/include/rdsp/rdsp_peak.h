/**
 * @file rdsp_peak.h
 * @brief Step 6 - sub-bin peak interpolation.
 *
 * The FFT gives the beat frequency only on a grid of fs/N. The true peak
 * position k + delta (|delta| <= 0.5) is estimated from the peak bin and its
 * two neighbours:
 *  - PARABOLIC  parabola through the power values (simple, biased)
 *  - GAUSSIAN   parabola through ln(power); bias < ~0.05 bin for Hann
 *  - HANN       exact closed form for a Hann-windowed tone (Grandke):
 *               a = |X[k+-1]| / |X[k]| (larger neighbour),
 *               delta = +-(2a - 1) / (a + 1)
 *               Residual errors come only from noise and from the negative
 *               frequency image (relevant for peaks below ~3 bins).
 */
#ifndef RDSP_PEAK_H
#define RDSP_PEAK_H

#include "rdsp_common.h"

typedef enum {
    RDSP_INTERP_NONE = 0,
    RDSP_INTERP_PARABOLIC,
    RDSP_INTERP_GAUSSIAN,
    RDSP_INTERP_HANN
} rdsp_interp_t;

/**
 * Fractional offset of the peak at bin i of the power spectrum p[0..n-1].
 * @return delta in [-0.5, 0.5]; 0 at the spectrum edges or on bad data
 */
float rdsp_peak_interp(const float *p, uint32_t n, uint32_t i, rdsp_interp_t m);

#endif /* RDSP_PEAK_H */
