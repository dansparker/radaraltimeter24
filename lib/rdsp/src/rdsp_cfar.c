/**
 * @file rdsp_cfar.c
 * @brief Step 5 - CFAR detection (see rdsp_cfar.h).
 */
#include "rdsp/rdsp_cfar.h"
#include <math.h>

float rdsp_cfar_alpha_ca(uint32_t n, float pfa)
{
    if ((n == 0u) || (pfa <= 0.0f) || (pfa >= 1.0f)) { return 1.0f; }
    return (float)n * (powf(pfa, -1.0f / (float)n) - 1.0f);
}

/* ln Pfa(alpha) for OS-CFAR: Pfa = prod_{i=0}^{k-1} (n-i)/(n-i+alpha) */
static float os_log_pfa(uint32_t n, uint32_t k, float alpha)
{
    float s = 0.0f;
    for (uint32_t i = 0; i < k; i++) {
        const float a = (float)(n - i);
        s += logf(a / (a + alpha));
    }
    return s;
}

float rdsp_cfar_alpha_os(uint32_t n, uint32_t k, float pfa)
{
    if ((n == 0u) || (k == 0u) || (k > n) || (pfa <= 0.0f) || (pfa >= 1.0f)) { return 1.0f; }
    const float target = logf(pfa);
    float lo = 0.0f, hi = 1.0f;
    /* Pfa decreases monotonically with alpha: find an upper bracket */
    for (int it = 0; (it < 60) && (os_log_pfa(n, k, hi) > target); it++) { hi *= 2.0f; }
    for (int it = 0; it < 60; it++) {
        const float mid = 0.5f * (lo + hi);
        if (os_log_pfa(n, k, mid) > target) { lo = mid; } else { hi = mid; }
    }
    return 0.5f * (lo + hi);
}

int rdsp_cfar_init(rdsp_cfar_t *c, rdsp_cfar_type_t type, uint16_t n_train,
                   uint16_t n_guard, uint16_t os_k, float pfa)
{
    if ((c == NULL) || (n_train == 0u) || (n_train > RDSP_CFAR_MAX_TRAIN)) { return -1; }
    const uint32_t n = 2u * n_train;
    c->type = type;
    c->n_train = n_train;
    c->n_guard = n_guard;
    c->os_k = (os_k == 0u) ? (uint16_t)((3u * n) / 4u) : os_k;
    if (c->os_k > n) { return -1; }
    switch (type) {
    case RDSP_CFAR_OS:
        c->alpha = rdsp_cfar_alpha_os(n, c->os_k, pfa);
        break;
    case RDSP_CFAR_GO:
    case RDSP_CFAR_SO:
        /* one side has n_train cells; CA formula on one side is conservative for GO */
        c->alpha = rdsp_cfar_alpha_ca((type == RDSP_CFAR_GO) ? n : n_train, pfa);
        break;
    case RDSP_CFAR_CA:
    default:
        c->alpha = rdsp_cfar_alpha_ca(n, pfa);
        break;
    }
    return 0;
}

/* k-th smallest (1-based) of v[0..n-1]; v is sorted in place.
 * Insertion sort: n <= 2*RDSP_CFAR_MAX_TRAIN, simple and deterministic. */
static float kth_smallest(float *v, uint32_t n, uint32_t k)
{
    for (uint32_t i = 1u; i < n; i++) {
        const float x = v[i];
        uint32_t j = i;
        while ((j > 0u) && (v[j - 1u] > x)) {
            v[j] = v[j - 1u];
            j--;
        }
        v[j] = x;
    }
    return v[k - 1u];
}

float rdsp_cfar_noise(const rdsp_cfar_t *c, const float *p, uint32_t n, uint32_t i)
{
    const int32_t g = (int32_t)c->n_guard, t = (int32_t)c->n_train;
    const int32_t ii = (int32_t)i, nn = (int32_t)n;
    float lead[RDSP_CFAR_MAX_TRAIN], lag[RDSP_CFAR_MAX_TRAIN];
    uint32_t nl = 0u, nr = 0u;
    float sl = 0.0f, sr = 0.0f;

    for (int32_t j = ii - g - t; j < ii - g; j++) {
        if ((j >= 0) && (j < nn)) { lead[nl++] = p[j]; sl += p[j]; }
    }
    for (int32_t j = ii + g + 1; j <= ii + g + t; j++) {
        if ((j >= 0) && (j < nn)) { lag[nr++] = p[j]; sr += p[j]; }
    }
    if ((nl + nr) == 0u) { return RDSP_EPS; }

    const float ml = (nl > 0u) ? sl / (float)nl : -1.0f;
    const float mr = (nr > 0u) ? sr / (float)nr : -1.0f;
    float noise;

    switch (c->type) {
    case RDSP_CFAR_GO:
        noise = RDSP_MAX(ml, mr);
        break;
    case RDSP_CFAR_SO:
        noise = (ml < 0.0f) ? mr : ((mr < 0.0f) ? ml : RDSP_MIN(ml, mr));
        break;
    case RDSP_CFAR_OS: {
        float all[2u * RDSP_CFAR_MAX_TRAIN];
        uint32_t na = 0u;
        for (uint32_t j = 0; j < nl; j++) { all[na++] = lead[j]; }
        for (uint32_t j = 0; j < nr; j++) { all[na++] = lag[j]; }
        /* scale the rank to the number of available cells (edges) */
        uint32_t k = (uint32_t)(((uint32_t)c->os_k * na + (2u * (uint32_t)t) / 2u) / (2u * (uint32_t)t));
        if (k < 1u) { k = 1u; }
        if (k > na) { k = na; }
        noise = kth_smallest(all, na, k);
        break;
    }
    case RDSP_CFAR_CA:
    default:
        noise = (sl + sr) / (float)(nl + nr);
        break;
    }
    return RDSP_MAX(noise, RDSP_EPS);
}

uint32_t rdsp_cfar_detect(const rdsp_cfar_t *c, const float *p, uint32_t n,
                          uint32_t i_min, uint32_t i_max,
                          rdsp_det_t *det, uint32_t max_det)
{
    uint32_t nd = 0u;
    if ((n < 3u) || (max_det == 0u)) { return 0u; }
    if (i_min < 1u) { i_min = 1u; }
    if (i_max > n - 2u) { i_max = n - 2u; }

    for (uint32_t i = i_min; i <= i_max; i++) {
        const float v = p[i];
        if (!((v > p[i - 1u]) && (v >= p[i + 1u]))) { continue; }   /* local max only */
        const float nz = rdsp_cfar_noise(c, p, n, i);
        if (!(v > c->alpha * nz)) { continue; }                     /* also rejects NaN */

        rdsp_det_t d;
        d.bin = (uint16_t)i;
        d.power = v;
        d.noise = nz;
        d.snr_db = 10.0f * log10f(RDSP_MAX(v, RDSP_EPS) / nz);

        if (nd < max_det) {
            det[nd++] = d;
        } else {
            /* replace the weakest if this one is stronger, keep bin order */
            uint32_t w = 0u;
            for (uint32_t j = 1u; j < nd; j++) { if (det[j].power < det[w].power) { w = j; } }
            if (d.power > det[w].power) {
                for (uint32_t j = w; j + 1u < nd; j++) { det[j] = det[j + 1u]; }
                det[nd - 1u] = d;
            }
        }
    }
    return nd;
}
