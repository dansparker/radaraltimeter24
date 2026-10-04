/**
 * @file rdsp_window.h
 * @brief Step 2 - window functions.
 *
 * All windows are generated "periodic" (DFT-even), which is the correct form
 * for spectral analysis. The info struct returns the sums needed for correct
 * amplitude / PSD scaling:
 *   s1   = sum(w)          coherent gain cg = s1/N (amplitude of a tone)
 *   s2   = sum(w^2)        used for power spectral density scaling
 *   enbw = N*s2/s1^2       equivalent noise bandwidth in bins
 */
#ifndef RDSP_WINDOW_H
#define RDSP_WINDOW_H

#include "rdsp_common.h"

typedef enum {
    RDSP_WIN_RECT = 0,
    RDSP_WIN_HANN,             /**< -31 dB sidelobes, 1.5 bin ENBW  (default) */
    RDSP_WIN_HAMMING,          /**< -43 dB sidelobes, 1.36 bin ENBW */
    RDSP_WIN_BLACKMAN_HARRIS   /**< -92 dB sidelobes, 2.0 bin ENBW (strong clutter) */
} rdsp_win_type_t;

typedef struct {
    float s1;
    float s2;
    float cg;
    float enbw_bins;
} rdsp_win_info_t;

/** Fill w[0..n-1]. info may be NULL. Returns 0 on success, -1 on bad args. */
int rdsp_window_make(float *w, uint32_t n, rdsp_win_type_t type, rdsp_win_info_t *info);

/** x[i] *= w[i] */
void rdsp_window_apply(float *x, const float *w, uint32_t n);

#endif /* RDSP_WINDOW_H */
