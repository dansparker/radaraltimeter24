/**
 * @file rdsp_cfar.h
 * @brief Step 5 - CFAR (constant false alarm rate) detection on a power spectrum.
 *
 * For every cell under test (CUT) the local noise level is estimated from
 * n_train cells on each side, skipping n_guard cells next to the CUT:
 *
 *     [train...][guard][CUT][guard][...train]
 *
 * Variants:
 *  - CA  cell averaging       best in homogeneous noise
 *  - GO  greatest-of (CA)     robust at clutter edges (fewer false alarms)
 *  - SO  smallest-of (CA)     better for closely spaced targets
 *  - OS  ordered statistic    robust with multiple targets and clutter edges
 *                             (default, k-th smallest of all training cells)
 *
 * A detection is reported for a CUT that exceeds alpha * noise AND is a local
 * maximum, so every spectral peak produces exactly one detection.
 * The scale factor alpha follows from the design false-alarm probability Pfa
 * for exponentially distributed (square-law, single look) noise. When the
 * input spectrum is averaged over L frames, the noise variance is reduced and
 * the real Pfa becomes much lower - the design is therefore conservative.
 * Near the spectrum edges only the available training cells are used.
 */
#ifndef RDSP_CFAR_H
#define RDSP_CFAR_H

#include "rdsp_common.h"

#define RDSP_CFAR_MAX_TRAIN 64u   /**< maximum training cells per side */

typedef enum {
    RDSP_CFAR_CA = 0,
    RDSP_CFAR_GO,
    RDSP_CFAR_SO,
    RDSP_CFAR_OS
} rdsp_cfar_type_t;

typedef struct {
    rdsp_cfar_type_t type;
    uint16_t n_train;   /**< training cells per side */
    uint16_t n_guard;   /**< guard cells per side */
    uint16_t os_k;      /**< OS rank (1..2*n_train), ignored for CA/GO/SO */
    float alpha;        /**< threshold factor (linear power) */
} rdsp_cfar_t;

typedef struct {
    uint16_t bin;       /**< integer bin of the peak */
    float power;        /**< power of the CUT */
    float noise;        /**< estimated noise level */
    float snr_db;       /**< 10*log10(power/noise) */
} rdsp_det_t;

/** alpha for CA-CFAR with n cells: n*(Pfa^(-1/n) - 1) */
float rdsp_cfar_alpha_ca(uint32_t n, float pfa);

/** alpha for OS-CFAR with n cells and rank k (solved numerically) */
float rdsp_cfar_alpha_os(uint32_t n, uint32_t k, float pfa);

/**
 * Initialise a CFAR configuration and compute alpha from pfa.
 * os_k = 0 selects the default rank 3/4 * 2*n_train.
 * @return 0 on success, -1 on invalid parameters
 */
int rdsp_cfar_init(rdsp_cfar_t *c, rdsp_cfar_type_t type, uint16_t n_train,
                   uint16_t n_guard, uint16_t os_k, float pfa);

/** Noise estimate for cell i of p[0..n-1]. */
float rdsp_cfar_noise(const rdsp_cfar_t *c, const float *p, uint32_t n, uint32_t i);

/**
 * Detect peaks in p[i_min .. i_max] (inclusive, clamped to 1..n-2).
 * If more than max_det peaks are found the strongest are kept.
 * Detections are returned sorted by ascending bin.
 * @return number of detections
 */
uint32_t rdsp_cfar_detect(const rdsp_cfar_t *c, const float *p, uint32_t n,
                          uint32_t i_min, uint32_t i_max,
                          rdsp_det_t *det, uint32_t max_det);

#endif /* RDSP_CFAR_H */
