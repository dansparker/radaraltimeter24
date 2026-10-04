/**
 * @file ramp.c
 * @brief Triangular DAC modulation table (see ramp.h).
 */
#include "ramp.h"

void ramp_build(uint16_t *tbl, uint32_t frame_len, uint16_t lo, uint16_t hi, float q)
{
    const float span = (float)hi - (float)lo;
    for (uint32_t i = 0; i < frame_len; i++) {
        const float u = (frame_len > 1u) ? (float)i / (float)(frame_len - 1u) : 0.0f;
        float v = u + q * u * (1.0f - u);
        if (v < 0.0f) { v = 0.0f; }
        if (v > 1.0f) { v = 1.0f; }
        uint32_t code = (uint32_t)((float)lo + v * span + 0.5f);
        if (code > 4095u) { code = 4095u; }
        tbl[i] = (uint16_t)code;
        tbl[2u * frame_len - 1u - i] = (uint16_t)code;
    }
}

float ramp_slope_hz_s(float sweep_hz, uint32_t frame_len, uint32_t fs_hz)
{
    /* lo -> hi takes frame_len-1 sample intervals */
    return sweep_hz * (float)fs_hz / (float)(frame_len - 1u);
}
