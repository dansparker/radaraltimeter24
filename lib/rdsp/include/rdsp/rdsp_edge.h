/**
 * @file rdsp_edge.h
 * @brief Step 5 - leading edge of an extended target (altimeter ground echo).
 *
 * Seen from an aircraft the ground is not a point target: the beam
 * illuminates an area whose echo starts at the nadir range and extends to
 * larger ranges (beam width), and the ground speed spreads it in Doppler.
 * In the averaged spectrum this is a broad hill. The altitude is the start
 * (leading edge) of the hill, not its maximum.
 *
 * For an altimeter nothing can be closer than the ground, so the bins below
 * the edge contain only noise. The detector therefore uses a LAGGING
 * reference window (closer ranges only):
 *
 *   noise(b)  = max of
 *                 - median of p[b-guard-win .. b-guard-1]
 *                 - log-linear trend of that window extrapolated to b: a noise
 *                   density that rises with frequency (IF high-pass shaping)
 *                   would otherwise look like a leading edge
 *                 - floor_frac * global noise
 *               (global noise if fewer than min_ref bins are available)
 *   edge bin  = first b with p[b..b+confirm-1] > thr * noise(b)
 *   plateau   = max of p[b .. b+span]
 *   position  = 50 % crossing between noise and plateau, linear
 *               interpolation between the two bins around the crossing
 *
 * The input must be an AVERAGED power spectrum (many ramps), otherwise the
 * noise fluctuation is too large for a small threshold.
 */
#ifndef RDSP_EDGE_H
#define RDSP_EDGE_H

#include "rdsp_common.h"

#define RDSP_EDGE_MAX_WIN 32u
#define RDSP_EDGE_MAX_BINS 512u

typedef struct {
    float thr;          /**< threshold factor over the noise (power, > 1) */
    uint32_t win;       /**< lagging reference window [bins], <= RDSP_EDGE_MAX_WIN */
    uint32_t guard;     /**< guard bins between window and test bin */
    uint32_t min_ref;   /**< fewer reference bins: use the global noise */
    uint32_t confirm;   /**< consecutive bins above the threshold */
    uint32_t span;      /**< bins after the edge searched for the plateau */
    float floor_frac;   /**< noise(b) >= floor_frac * noise_global (0 = off): protects
                             against a falling noise shape towards DC (IF high-pass),
                             which would make the lagging window too optimistic */
} rdsp_edge_cfg_t;

typedef struct {
    float pos;          /**< edge position [bins, fractional] */
    uint32_t bin;       /**< first bin above the threshold */
    uint32_t peak_bin;  /**< plateau maximum */
    float noise;        /**< noise level at the edge */
    float peak;         /**< plateau level */
    float snr_db;       /**< plateau / noise */
} rdsp_edge_t;

/** Median of p[lo..hi-1] (hi - lo <= RDSP_EDGE_MAX_WIN * 4), 0 if empty. */
float rdsp_median(const float *p, uint32_t lo, uint32_t hi);

/**
 * Find the leading edge in p[b_lo .. b_hi].
 * @param noise_global noise used when the lagging window is too short
 * A hill that already starts at b_lo (DC, leakage, filter transients) has no
 * usable edge and is skipped.
 * @return 1 if an edge was found
 */
int rdsp_edge_detect(const rdsp_edge_cfg_t *c, const float *p, uint32_t nbins,
                     uint32_t b_lo, uint32_t b_hi, float noise_global, rdsp_edge_t *e);

#endif /* RDSP_EDGE_H */
