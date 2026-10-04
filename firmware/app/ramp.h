/**
 * @file ramp.h
 * @brief Triangular DAC modulation table.
 *
 * The table holds 2*frame_len codes: a rising ramp lo -> hi followed by the
 * mirrored falling ramp hi -> lo. It is played in a circular DMA loop, so the
 * VCO voltage is continuous at both corners (no flyback step that would
 * excite the high-pass filters of the IF amplifier).
 *
 * Optional quadratic predistortion u' = u + q*u*(1-u) (u = 0..1) can be used
 * to compensate a curved VCO tuning characteristic.
 */
#ifndef RAMP_H
#define RAMP_H

#include <stdint.h>

/** Build the table (2*frame_len entries). */
void ramp_build(uint16_t *tbl, uint32_t frame_len, uint16_t lo, uint16_t hi, float q);

/** Sweep slope [Hz/s] for a sweep of sweep_hz over one ramp. */
float ramp_slope_hz_s(float sweep_hz, uint32_t frame_len, uint32_t fs_hz);

#endif /* RAMP_H */
