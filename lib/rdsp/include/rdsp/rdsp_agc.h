/**
 * @file rdsp_agc.h
 * @brief Step 0 - automatic gain control for a switched-gain IF amplifier.
 *
 * Level 0 is the lowest gain, n_levels-1 the highest.
 *  - fast attack: on clipping or peak_frac > hi_frac the gain is reduced
 *    immediately (one step per frame)
 *  - slow release: the gain is increased only after up_frames consecutive
 *    frames with peak_frac < lo_frac
 * To avoid limit cycles lo_frac * step_ratio must be < hi_frac, where
 * step_ratio is the amplitude ratio between neighbouring gain levels.
 */
#ifndef RDSP_AGC_H
#define RDSP_AGC_H

#include "rdsp_common.h"

typedef struct {
    uint8_t n_levels;
    float hi_frac;
    float lo_frac;
    uint16_t up_frames;
    uint8_t level;
    uint16_t lo_count;
} rdsp_agc_t;

void rdsp_agc_init(rdsp_agc_t *a, uint8_t n_levels, uint8_t start_level,
                   float hi_frac, float lo_frac, uint16_t up_frames);

/** @return 1 if the level changed */
int rdsp_agc_update(rdsp_agc_t *a, float peak_frac, int clipped);

#endif /* RDSP_AGC_H */
