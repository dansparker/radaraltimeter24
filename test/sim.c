/**
 * @file sim.c
 * @brief FMCW radar frontend simulator (see sim.h).
 */
#include "sim.h"
#include "radar_params.h"
#include "ramp.h"
#include <math.h>
#include <string.h>

static double urand(sim_t *s)
{
    s->rng = s->rng * 1664525u + 1013904223u;
    return ((double)(s->rng >> 8) + 0.5) / 16777216.0;
}

float sim_randn(sim_t *s)
{
    const double u1 = urand(s), u2 = urand(s);
    return (float)(sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979 * u2));
}

void sim_init(sim_t *s, float sweep_hz)
{
    memset(s, 0, sizeof(*s));
    s->fs_hz = (float)RADAR_FS_HZ;
    s->frame_len = RADAR_FRAME_LEN;
    s->slope_hz_s = ramp_slope_hz_s(sweep_hz, RADAR_FRAME_LEN, RADAR_FS_HZ);
    s->lambda_m = RADAR_LAMBDA_M;
    s->vco_sign = 1;
    s->gain_lin[0] = 1.0f;
    s->gain_lin[1] = 3.16f;
    s->gain_lin[2] = 10.0f;
    s->gain_lin[3] = 31.6f;
    s->noise_front_lsb = 0.5f;
    s->noise_adc_lsb = 0.7f;
    s->adc_mid = 2048.0f;
    s->leak_amp_lsb = 30.0f;
    s->hp_fc_hz = 1100.0f;
    s->hp_order = 3;
    s->rng = 12345u;
}

void sim_advance(sim_t *s, float dt)
{
    for (int i = 0; i < s->n_tgt; i++) { s->tgt[i].range_m += s->tgt[i].vr_mps * dt; }
}

void sim_frame(sim_t *s, int dac_dir, int gain, uint16_t *out)
{
    const double c0 = 299792458.0;
    const double ts = 1.0 / s->fs_hz;
    const int rising_f = (s->vco_sign > 0) ? (dac_dir == 0) : (dac_dir != 0);
    const double g = s->gain_lin[gain & 3];
    const double a_hp = (s->hp_fc_hz > 0.0f) ? 1.0 / (1.0 + 2.0 * 3.14159265358979 * s->hp_fc_hz * ts) : 1.0;

    double fb[SIM_MAX_TGT], ph[SIM_MAX_TGT];
    for (int k = 0; k < s->n_tgt; k++) {
        const sim_target_t *t = &s->tgt[k];
        const double fr = 2.0 * s->slope_hz_s * t->range_m / c0;
        const double fd = 2.0 * (-t->vr_mps) / s->lambda_m;          /* closing -> positive */
        fb[k] = rising_f ? (fr - fd) : -(fr + fd);
        ph[k] = 4.0 * 3.14159265358979 * t->range_m / s->lambda_m;   /* carrier phase */
    }

    for (uint32_t i = 0; i < s->frame_len; i++) {
        const double tt = (double)i * ts;
        double x = 0.0;
        for (int k = 0; k < s->n_tgt; k++) {
            x += s->tgt[k].amp_lsb * cos(2.0 * 3.14159265358979 * fb[k] * tt + ph[k]);
        }
        /* leakage: very low beat frequency, depends on the ramp direction */
        const double u = (double)i / (double)(s->frame_len - 1u);
        x += s->leak_amp_lsb * (rising_f ? (u - 0.5) : (0.5 - u)) * 2.0;
        x += s->noise_front_lsb * sim_randn(s);
        x *= g;
        /* IF high-pass chain (state continues across frames) */
        if (s->hp_fc_hz > 0.0f) {
            for (int st = 0; st < s->hp_order; st++) {
                const double y = a_hp * (s->hp_y[st] + x - s->hp_x[st]);
                s->hp_x[st] = x;
                s->hp_y[st] = y;
                x = y;
            }
        }
        x += s->noise_adc_lsb * sim_randn(s) + s->adc_mid;
        if (s->stuck) { x = s->adc_mid; }
        long v = lround(x);
        if (v < 0) { v = 0; }
        if (v > 4095) { v = 4095; }
        out[i] = (uint16_t)v;
    }
    s->t += (double)s->frame_len * ts;
}
