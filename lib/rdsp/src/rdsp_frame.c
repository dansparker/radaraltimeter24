/**
 * @file rdsp_frame.c
 * @brief Step 1 - ADC frame conditioning (see rdsp_frame.h).
 */
#include "rdsp/rdsp_frame.h"
#include <math.h>

void rdsp_frame_condition_u16(const uint16_t *in, uint32_t n, float *out,
                              uint16_t clip_lo, uint16_t clip_hi, uint16_t adc_max,
                              rdsp_frame_stats_t *st)
{
    uint32_t sum = 0u, n_clip = 0u;
    uint16_t mn = 0xFFFFu, mx = 0u;

    for (uint32_t i = 0; i < n; i++) {
        const uint16_t v = in[i];
        sum += v;
        if (v < mn) { mn = v; }
        if (v > mx) { mx = v; }
        if ((v <= clip_lo) || (v >= clip_hi)) { n_clip++; }
    }
    const float mean = (n > 0u) ? (float)sum / (float)n : 0.0f;

    float ss = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        const float x = (float)in[i] - mean;
        out[i] = x;
        ss += x * x;
    }

    float head = RDSP_MIN(mean, (float)adc_max - mean);
    if (head < 1.0f) { head = 1.0f; }
    const float exc = RDSP_MAX((float)mx - mean, mean - (float)mn);

    st->mean = mean;
    st->rms = (n > 0u) ? sqrtf(ss / (float)n) : 0.0f;
    st->min = mn;
    st->max = mx;
    st->n_clip = n_clip;
    st->peak_frac = exc / head;
}

void rdsp_frame_detrend(float *x, uint32_t n)
{
    if (n < 3u) { return; }
    /* t is centred: t_i = i - (n-1)/2, so sum(t) = 0 and slope = sum(t*x)/sum(t^2) */
    const float c = 0.5f * (float)(n - 1u);
    float sx = 0.0f, stx = 0.0f, stt = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        const float t = (float)i - c;
        sx += x[i];
        stx += t * x[i];
        stt += t * t;
    }
    const float off = sx / (float)n;
    const float k = stx / stt;
    for (uint32_t i = 0; i < n; i++) {
        x[i] -= off + k * ((float)i - c);
    }
}

void rdsp_frame_sub_bg_i16(float *x, const int16_t *bg, uint32_t n, float scale)
{
    for (uint32_t i = 0; i < n; i++) {
        x[i] -= (float)bg[i] * scale;
    }
}

int rdsp_all_finite(const float *x, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (!isfinite(x[i])) { return 0; }
    }
    return 1;
}
