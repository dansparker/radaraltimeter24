/**
 * @file rdsp_peak.c
 * @brief Step 6 - sub-bin peak interpolation (see rdsp_peak.h).
 */
#include "rdsp/rdsp_peak.h"
#include <math.h>

static float clamp_half(float d)
{
    if (!isfinite(d)) { return 0.0f; }
    if (d > 0.5f) { return 0.5f; }
    if (d < -0.5f) { return -0.5f; }
    return d;
}

float rdsp_peak_interp(const float *p, uint32_t n, uint32_t i, rdsp_interp_t m)
{
    if ((i == 0u) || (i + 1u >= n)) { return 0.0f; }
    const float l = RDSP_MAX(p[i - 1u], RDSP_EPS);
    const float c = RDSP_MAX(p[i], RDSP_EPS);
    const float r = RDSP_MAX(p[i + 1u], RDSP_EPS);

    switch (m) {
    case RDSP_INTERP_PARABOLIC: {
        const float den = l - 2.0f * c + r;
        return (den < 0.0f) ? clamp_half(0.5f * (l - r) / den) : 0.0f;
    }
    case RDSP_INTERP_GAUSSIAN: {
        const float ll = logf(l), lc = logf(c), lr = logf(r);
        const float den = ll - 2.0f * lc + lr;
        return (den < 0.0f) ? clamp_half(0.5f * (ll - lr) / den) : 0.0f;
    }
    case RDSP_INTERP_HANN: {
        const float ac = sqrtf(c);
        if (r >= l) {
            const float a = sqrtf(r) / ac;
            return clamp_half((2.0f * a - 1.0f) / (a + 1.0f));
        } else {
            const float a = sqrtf(l) / ac;
            return clamp_half(-(2.0f * a - 1.0f) / (a + 1.0f));
        }
    }
    case RDSP_INTERP_NONE:
    default:
        return 0.0f;
    }
}
