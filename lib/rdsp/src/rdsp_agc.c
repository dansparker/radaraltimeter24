/**
 * @file rdsp_agc.c
 * @brief Step 0 - automatic gain control (see rdsp_agc.h).
 */
#include "rdsp/rdsp_agc.h"

void rdsp_agc_init(rdsp_agc_t *a, uint8_t n_levels, uint8_t start_level,
                   float hi_frac, float lo_frac, uint16_t up_frames)
{
    a->n_levels = (n_levels == 0u) ? 1u : n_levels;
    a->level = (start_level < a->n_levels) ? start_level : (uint8_t)(a->n_levels - 1u);
    a->hi_frac = hi_frac;
    a->lo_frac = lo_frac;
    a->up_frames = up_frames;
    a->lo_count = 0u;
}

int rdsp_agc_update(rdsp_agc_t *a, float peak_frac, int clipped)
{
    const uint8_t old = a->level;
    if (clipped || !(peak_frac <= a->hi_frac)) {          /* NaN -> reduce gain */
        a->lo_count = 0u;
        if (a->level > 0u) { a->level--; }
    } else if (peak_frac < a->lo_frac) {
        if (++a->lo_count >= a->up_frames) {
            a->lo_count = 0u;
            if (a->level + 1u < a->n_levels) { a->level++; }
        }
    } else {
        a->lo_count = 0u;
    }
    return (a->level != old) ? 1 : 0;
}
