/**
 * @file rdsp_track.c
 * @brief Step 8 - alpha-beta tracker (see rdsp_track.h).
 */
#include "rdsp/rdsp_track.h"
#include <math.h>

void rdsp_track_init(rdsp_track_t *t, const rdsp_track_cfg_t *cfg)
{
    t->cfg = *cfg;
    if (t->cfg.confirm_n == 0u) { t->cfg.confirm_n = 1u; }
    if (t->cfg.reacq_n == 0u) { t->cfg.reacq_n = 1u; }
    rdsp_track_reset(t);
}

void rdsp_track_reset(rdsp_track_t *t)
{
    t->state = RDSP_TRK_LOST;
    t->x = 0.0f;
    t->v = 0.0f;
    t->hits = 0u;
    t->misses = 0u;
    t->alt_hits = 0u;
    t->alt_x = 0.0f;
    t->last_resid = 0.0f;
}

float rdsp_track_predict(const rdsp_track_t *t, float dt)
{
    return t->x + t->v * dt;
}

float rdsp_track_gate(const rdsp_track_t *t, float dt)
{
    const float xp = rdsp_track_predict(t, dt);
    /* the gate grows while coasting because the prediction becomes uncertain */
    return (t->cfg.gate_abs + t->cfg.gate_rel * fabsf(xp)) * (1.0f + 0.1f * (float)t->misses);
}

static float clamp_v(const rdsp_track_t *t, float v)
{
    if (!isfinite(v)) { return 0.0f; }
    if (v > t->cfg.v_max) { return t->cfg.v_max; }
    if (v < -t->cfg.v_max) { return -t->cfg.v_max; }
    return v;
}

static void start(rdsp_track_t *t, float z, float vz, rdsp_trk_state_t st)
{
    t->x = z;
    t->v = isfinite(vz) ? clamp_v(t, vz) : 0.0f;
    t->hits = 1u;
    t->misses = 0u;
    t->alt_hits = 0u;
    t->state = st;
    t->last_resid = 0.0f;
}

static void miss(rdsp_track_t *t, float dt)
{
    if (t->state == RDSP_TRK_LOST) { return; }   /* nothing to extrapolate */
    t->x = rdsp_track_predict(t, dt);
    if (t->misses < 0xFFFFu) { t->misses++; }
    switch (t->state) {
    case RDSP_TRK_CONFIRMED:
    case RDSP_TRK_COAST:
        t->state = (t->misses > t->cfg.max_coast) ? RDSP_TRK_LOST : RDSP_TRK_COAST;
        break;
    case RDSP_TRK_TENTATIVE:
        if (t->misses > 2u) { t->state = RDSP_TRK_LOST; }
        break;
    case RDSP_TRK_LOST:
    default:
        break;
    }
    if (t->state == RDSP_TRK_LOST) { t->hits = 0u; t->alt_hits = 0u; t->v = 0.0f; }
}

int rdsp_track_update(rdsp_track_t *t, int has_z, float z, float vz, float dt)
{
    if (!(dt > 0.0f)) { dt = 1.0e-3f; }
    if (has_z && !isfinite(z)) { has_z = 0; }

    if (!has_z) {
        miss(t, dt);
        return 0;
    }
    if (t->state == RDSP_TRK_LOST) {
        start(t, z, vz, (t->cfg.confirm_n <= 1u) ? RDSP_TRK_CONFIRMED : RDSP_TRK_TENTATIVE);
        return 1;
    }

    const float xp = rdsp_track_predict(t, dt);
    const float gate = rdsp_track_gate(t, dt);
    const float r = z - xp;

    if (fabsf(r) <= gate) {
        t->x = xp + t->cfg.alpha * r;
        t->v = clamp_v(t, t->v + t->cfg.beta * r / dt);
        if (isfinite(vz) && (t->cfg.gamma > 0.0f)) {
            t->v = clamp_v(t, t->v + t->cfg.gamma * (vz - t->v));
        }
        t->last_resid = r;
        t->misses = 0u;
        t->alt_hits = 0u;
        if (t->hits < 0xFFFFu) { t->hits++; }
        if (t->state == RDSP_TRK_COAST) {
            t->state = RDSP_TRK_CONFIRMED;
        } else if ((t->state == RDSP_TRK_TENTATIVE) && (t->hits >= t->cfg.confirm_n)) {
            t->state = RDSP_TRK_CONFIRMED;
        }
        return 1;
    }

    /* out of gate */
    if (t->state == RDSP_TRK_TENTATIVE) {
        start(t, z, vz, RDSP_TRK_TENTATIVE);   /* tentative tracks follow the data */
        return 1;
    }
    /* confirmed/coasting: alternative hypothesis for re-acquisition */
    const float alt_gate = t->cfg.gate_abs + t->cfg.gate_rel * fabsf(z);
    if ((t->alt_hits > 0u) && (fabsf(z - t->alt_x) <= alt_gate)) {
        t->alt_hits++;
    } else {
        t->alt_hits = 1u;
    }
    t->alt_x = z;
    if (t->alt_hits >= t->cfg.reacq_n) {
        start(t, z, vz, RDSP_TRK_CONFIRMED);
        return 1;
    }
    {
        const uint16_t ah = t->alt_hits;
        const float ax = t->alt_x;
        miss(t, dt);
        if (t->state != RDSP_TRK_LOST) { t->alt_hits = ah; t->alt_x = ax; }
    }
    return 0;
}
