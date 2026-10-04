/**
 * @file rdsp_spectrum.h
 * @brief Step 4 - power spectral density and spectral averaging.
 *
 * Three building blocks:
 *  - rdsp_psd_onesided():  |X|^2 -> one-sided PSD [unit^2/Hz]
 *  - rdsp_welch():         classic Welch PSD of one long record
 *                          (segments + overlap + window, averaged)
 *  - rdsp_psd_avg_*():     Welch-style averaging ACROSS frames (ramps):
 *                          running mean for the first 1/alpha frames, then an
 *                          exponential average with weight alpha.
 *
 * For FMCW ranging, averaging across ramps is preferred over segmenting a
 * single ramp, because segmenting reduces the swept bandwidth per segment and
 * therefore the range resolution (see docs/signal_chain.md).
 */
#ifndef RDSP_SPECTRUM_H
#define RDSP_SPECTRUM_H

#include "rdsp_common.h"
#include "rdsp_fft.h"

/**
 * Convert a power spectrum p = |X|^2 (nbins = N/2 bins, DC..fs/2) of a
 * windowed record to a one-sided PSD in place:
 *   PSD[k] = c_k * p[k] / (fs * s2),  c_0 = 1, c_k = 2 otherwise
 * s2 = sum(w^2) of the window used.
 */
void rdsp_psd_onesided(float *p, uint32_t nbins, float fs, float s2);

typedef struct {
    const rdsp_rfft_t *fft;  /**< FFT of length nseg */
    const float *win;        /**< window of length nseg */
    float s2;                /**< sum(win^2) */
    uint32_t nseg;           /**< segment length (= fft->n) */
    uint32_t hop;            /**< segment advance, e.g. nseg/2 for 50 % overlap */
    float fs;                /**< sample rate [Hz] */
    float *work;             /**< scratch of nseg floats */
} rdsp_welch_t;

/**
 * Welch PSD of x[0..len-1]. Each segment has its mean removed before
 * windowing. psd receives nseg/2 bins.
 * @return number of averaged segments (0 if len < nseg)
 */
uint32_t rdsp_welch(const rdsp_welch_t *w, const float *x, uint32_t len, float *psd);

typedef struct {
    float *acc;        /**< averaged spectrum (nbins) */
    uint32_t nbins;
    float alpha;       /**< steady state weight of a new frame (0 < alpha <= 1) */
    uint32_t count;    /**< frames averaged since reset */
} rdsp_psd_avg_t;

void rdsp_psd_avg_init(rdsp_psd_avg_t *a, float *buf, uint32_t nbins, float alpha);
void rdsp_psd_avg_reset(rdsp_psd_avg_t *a);
void rdsp_psd_avg_update(rdsp_psd_avg_t *a, const float *psd);

#endif /* RDSP_SPECTRUM_H */
