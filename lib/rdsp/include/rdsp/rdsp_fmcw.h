/**
 * @file rdsp_fmcw.h
 * @brief Step 7 - FMCW conversions and up/down ramp combination.
 *
 * Linear FMCW with sweep slope S [Hz/s]:
 *   range beat     f_R = 2 * S * R / c0         R = c0 * f_R / (2 S) + R0
 *   Doppler        f_D = 2 * v_c / lambda       v_c = closing speed (>0 approaching)
 *
 * Measured beat frequencies (real-valued IF, only |f| is observable):
 *   rising  ramp   f_up = | f_R - f_D |
 *   falling ramp   f_dn = | f_R + f_D |
 *
 * Hypotheses to undo the |.| (needed at low range with high closing speed):
 *   NORMAL         f_R >= |f_D|         f_R = (f_up + f_dn)/2, f_D = (f_dn - f_up)/2
 *   UP_MIRRORED    f_D >  f_R           f_R = (f_dn - f_up)/2, f_D = (f_dn + f_up)/2
 *   DOWN_MIRRORED  -f_D > f_R           f_R = (f_up - f_dn)/2, f_D = -(f_up + f_dn)/2
 * The sum (f_up + f_dn)/2 is independent of the Doppler shift; this is why a
 * triangular modulation is used instead of a single sawtooth.
 */
#ifndef RDSP_FMCW_H
#define RDSP_FMCW_H

#include "rdsp_common.h"

typedef struct {
    float slope_hz_s;   /**< sweep slope S = B / T_ramp [Hz/s] */
    float lambda_m;     /**< carrier wavelength [m] */
    float r_offset_m;   /**< range offset R0 (installation / delay) [m] */
} rdsp_fmcw_t;

typedef enum {
    RDSP_UD_NORMAL = 0,
    RDSP_UD_UP_MIRRORED,
    RDSP_UD_DOWN_MIRRORED
} rdsp_ud_hyp_t;

float rdsp_fmcw_range(const rdsp_fmcw_t *f, float f_r);            /**< beat -> range incl. R0 */
float rdsp_fmcw_beat(const rdsp_fmcw_t *f, float range_m);         /**< range incl. R0 -> beat */
float rdsp_fmcw_doppler(const rdsp_fmcw_t *f, float closing_mps);  /**< v_c -> f_D */
float rdsp_fmcw_closing_speed(const rdsp_fmcw_t *f, float f_d);    /**< f_D -> v_c */

/**
 * Combine one rising and one falling ramp beat under hypothesis h.
 * @return 0 if the result is consistent with h, -1 otherwise
 */
int rdsp_fmcw_updown(float f_up, float f_dn, rdsp_ud_hyp_t h, float *f_r, float *f_d);

#endif /* RDSP_FMCW_H */
