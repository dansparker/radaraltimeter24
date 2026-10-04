/**
 * @file rdsp_frame.h
 * @brief Step 1 - ADC frame conditioning.
 *
 * Converts raw unsigned ADC samples to float, collects statistics used for
 * gain control and health monitoring, removes DC (mean) and optionally a
 * linear trend, and subtracts a stored background (antenna leakage) frame.
 */
#ifndef RDSP_FRAME_H
#define RDSP_FRAME_H

#include "rdsp_common.h"

typedef struct {
    float    mean;       /**< mean value [LSB] */
    float    rms;        /**< rms after mean removal [LSB] */
    uint16_t min;        /**< minimum raw sample */
    uint16_t max;        /**< maximum raw sample */
    uint32_t n_clip;     /**< samples at/above clip_hi or at/below clip_lo */
    float    peak_frac;  /**< max excursion / available headroom (0..>1) */
} rdsp_frame_stats_t;

/**
 * Convert raw samples to float with the mean removed and collect statistics.
 * @param in       raw ADC samples
 * @param n        number of samples
 * @param out      output (may not alias in), out[i] = in[i] - mean
 * @param clip_lo  samples <= clip_lo count as clipped
 * @param clip_hi  samples >= clip_hi count as clipped
 * @param adc_max  full scale code (e.g. 4095)
 * @param st       statistics (output)
 */
void rdsp_frame_condition_u16(const uint16_t *in, uint32_t n, float *out,
                              uint16_t clip_lo, uint16_t clip_hi, uint16_t adc_max,
                              rdsp_frame_stats_t *st);

/** Remove the least-squares straight line from x (in place). */
void rdsp_frame_detrend(float *x, uint32_t n);

/** x[i] -= bg[i] * scale (background stored as scaled int16). */
void rdsp_frame_sub_bg_i16(float *x, const int16_t *bg, uint32_t n, float scale);

/** Returns 1 if all values are finite, 0 if a NaN/Inf is found. */
int rdsp_all_finite(const float *x, uint32_t n);

#endif /* RDSP_FRAME_H */
