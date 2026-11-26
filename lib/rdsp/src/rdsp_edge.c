/**
 * @file rdsp_edge.c
 * @brief Step 5 - leading edge detection (see rdsp_edge.h).
 */
#include "rdsp/rdsp_edge.h"

#include <math.h>

#define MED_MAX (4u * RDSP_EDGE_MAX_WIN)

float rdsp_median(const float *p, uint32_t lo, uint32_t hi)
{
    float b[MED_MAX];
    if (hi <= lo) { return 0.0f; }
    uint32_t n = hi - lo;
    if (n > MED_MAX) { n = MED_MAX; }
    for (uint32_t i = 0; i < n; i++) {           /* insertion sort, n is small */
        const float v = p[lo + i];
        uint32_t j = i;
        while ((j > 0u) && (b[j - 1u] > v)) { b[j] = b[j - 1u]; j--; }
        b[j] = v;
    }
    return b[n / 2u];
}

static float ref_noise(const rdsp_edge_cfg_t *c, const float *p, const float *lg, uint32_t b,
                       uint32_t b_lo, float ng)
{
    if (b < b_lo + c->guard + c->min_ref) { return ng; }
    const uint32_t hi = b - c->guard;
    const uint32_t lo = (hi >= b_lo + c->win) ? (hi - c->win) : b_lo;
    float n = rdsp_median(p, lo, hi);

    /* least squares line through ln(p) over the window, evaluated at b */
    const uint32_t m = hi - lo;
    const float xm = 0.5f * (float)(lo + hi - 1u);
    float sy = 0.0f, sxy = 0.0f, sxx = 0.0f;
    for (uint32_t i = lo; i < hi; i++) {
        const float dx = (float)i - xm;
        sy += lg[i];
        sxy += dx * lg[i];
        sxx += dx * dx;
    }
    if (sxx > 0.0f) {
        const float slope = sxy / sxx;
        if (slope > 0.0f) {                       /* rising noise only; falling: median */
            const float t = expf(sy / (float)m + slope * ((float)b - xm));
            if (t > n) { n = t; }
        }
    }
    const float fl = c->floor_frac * ng;
    return (n > fl) ? n : fl;
}

int rdsp_edge_detect(const rdsp_edge_cfg_t *c, const float *p, uint32_t nbins,
                     uint32_t b_lo, uint32_t b_hi, float noise_global, rdsp_edge_t *e)
{
    float lg[RDSP_EDGE_MAX_BINS];
    if ((b_hi >= nbins) || (nbins > RDSP_EDGE_MAX_BINS) || (b_lo > b_hi) || (c->confirm == 0u)) { return 0; }
    for (uint32_t i = b_lo; i <= b_hi; i++) { lg[i] = logf(RDSP_MAX(p[i], RDSP_EPS)); }
    for (uint32_t b = b_lo; b + c->confirm - 1u <= b_hi; b++) {
        const float n = ref_noise(c, p, lg, b, b_lo, noise_global);
        if (!(n > 0.0f)) { continue; }
        const float t = c->thr * n;
        uint32_t k = 0u;
        while ((k < c->confirm) && (p[b + k] > t)) { k++; }
        if (k < c->confirm) { continue; }

        /* plateau maximum */
        uint32_t pb = b;
        const uint32_t end = RDSP_MIN(b + c->span, nbins - 1u);
        for (uint32_t i = b + 1u; i <= end; i++) { if (p[i] > p[pb]) { pb = i; } }
        const float pk = p[pb];
        const float lvl = n + 0.5f * (pk - n);

        /* 50 % crossing: last bin below the level before the maximum */
        uint32_t i = pb;
        while ((i > b_lo) && (p[i - 1u] >= lvl)) { i--; }
        if (i == b_lo) {                             /* starts below the analysed range (DC, */
            b = pb;                                  /* leakage): skip this hill, go on      */
            continue;
        }
        const float lo_v = p[i - 1u], hi_v = p[i];
        const float d = hi_v - lo_v;
        const float pos = (float)(i - 1u) + ((d > 0.0f) ? (lvl - lo_v) / d : 0.5f);
        e->pos = pos;
        e->bin = b;
        e->peak_bin = pb;
        e->noise = n;
        e->peak = pk;
        e->snr_db = 10.0f * log10f(RDSP_MAX(pk, RDSP_EPS) / n);
        return 1;
    }
    return 0;
}
