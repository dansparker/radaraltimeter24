/**
 * @file rdsp_spectrum.c
 * @brief Step 4 - PSD and averaging (see rdsp_spectrum.h).
 */
#include "rdsp/rdsp_spectrum.h"

void rdsp_psd_onesided(float *p, uint32_t nbins, float fs, float s2)
{
    const float k = 1.0f / (fs * s2);
    if (nbins == 0u) { return; }
    p[0] *= k;
    for (uint32_t i = 1u; i < nbins; i++) {
        p[i] *= 2.0f * k;
    }
}

uint32_t rdsp_welch(const rdsp_welch_t *w, const float *x, uint32_t len, float *psd)
{
    const uint32_t nseg = w->nseg;
    const uint32_t nb = nseg / 2u;
    uint32_t cnt = 0u;

    if ((len < nseg) || (w->hop == 0u)) { return 0u; }
    for (uint32_t k = 0; k < nb; k++) { psd[k] = 0.0f; }

    for (uint32_t s = 0u; s + nseg <= len; s += w->hop) {
        float m = 0.0f;
        for (uint32_t i = 0; i < nseg; i++) { m += x[s + i]; }
        m /= (float)nseg;
        for (uint32_t i = 0; i < nseg; i++) { w->work[i] = (x[s + i] - m) * w->win[i]; }
        rdsp_rfft(w->fft, w->work);
        rdsp_rfft_power(w->work, w->work, nseg);
        for (uint32_t k = 0; k < nb; k++) { psd[k] += w->work[k]; }
        cnt++;
    }
    const float inv = 1.0f / (float)cnt;
    for (uint32_t k = 0; k < nb; k++) { psd[k] *= inv; }
    rdsp_psd_onesided(psd, nb, w->fs, w->s2);
    return cnt;
}

void rdsp_psd_avg_init(rdsp_psd_avg_t *a, float *buf, uint32_t nbins, float alpha)
{
    a->acc = buf;
    a->nbins = nbins;
    a->alpha = ((alpha > 0.0f) && (alpha <= 1.0f)) ? alpha : 1.0f;
    rdsp_psd_avg_reset(a);
}

void rdsp_psd_avg_reset(rdsp_psd_avg_t *a)
{
    a->count = 0u;
    for (uint32_t i = 0; i < a->nbins; i++) { a->acc[i] = 0.0f; }
}

void rdsp_psd_avg_update(rdsp_psd_avg_t *a, const float *psd)
{
    if (a->count < 0xFFFFFFFFu) { a->count++; }
    float w = 1.0f / (float)a->count;          /* running mean during start-up */
    if (w < a->alpha) { w = a->alpha; }        /* then exponential averaging   */
    for (uint32_t i = 0; i < a->nbins; i++) {
        a->acc[i] += w * (psd[i] - a->acc[i]);
    }
}
