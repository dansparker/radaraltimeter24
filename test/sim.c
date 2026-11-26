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
    s->sweep_hz = sweep_hz;
    sim_set_rmode(s, RADAR_RMODE_LONG);
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
    s->random_phase = 1;
}

void sim_set_rmode(sim_t *s, unsigned rmode)
{
    s->frame_len = radar_frame_len(rmode);
    s->slope_hz_s = ramp_slope_hz_s(s->sweep_hz, s->frame_len, RADAR_FS_HZ);
}

/* ---- extended ground ------------------------------------------------------ */

#define SIM_PI 3.14159265358979

/* improvement of the IF noise at beat f against the data sheet value at 500 Hz:
 * module 1/f noise (45 nV/sqrt(Hz) @ 500 Hz, -9.1 dB/decade) + TLV9064 + 470 ohm */
static double gnd_ni_db(double f)
{
    if (f < 50.0) { f = 50.0; }
    const double em = 45e-9 * pow(f / 500.0, -0.455);
    const double eo = 10e-9 * sqrt(1.0 + 1500.0 / f);
    const double et = sqrt(em * em + eo * eo + 2.8e-9 * 2.8e-9);
    return 20.0 * log10(45e-9 / et);
}

static void gnd_extent(const sim_t *s, double *X, double *Y)
{
    const double h = s->gnd.h_m;
    *X = h * tan(fmin(s->gnd.beam_along_deg, 60.0) * SIM_PI / 180.0);
    *Y = h * tan(fmin(s->gnd.beam_across_deg, 60.0) * SIM_PI / 180.0);
}

static void gnd_new(sim_t *s, sim_scat_t *p)
{
    p->c = (float)sqrt(-log(urand(s)));          /* Rayleigh, E[c^2] = 1 */
    p->ph0 = (float)(2.0 * SIM_PI * urand(s));
}

void sim_ground(sim_t *s, float h_m, float vh_mps, float vz_mps, float sigma0_db)
{
    sim_ground_t *g = &s->gnd;
    g->enabled = 1;
    g->h_m = h_m;
    g->vh_mps = vh_mps;
    g->vz_mps = vz_mps;
    g->sigma0_db = sigma0_db;
    g->beam_along_deg = 34.0f;    /* K-LC1a: 34 deg plane along the flight direction */
    g->beam_across_deg = 80.0f;
    g->loss_db = 3.0f;
    g->n = SIM_MAX_SCAT;
    double X, Y;
    gnd_extent(s, &X, &Y);
    for (int i = 0; i < g->n; i++) {
        g->s[i].x = (float)(X * (2.0 * urand(s) - 1.0));
        g->s[i].y = (float)(Y * (2.0 * urand(s) - 1.0));
        gnd_new(s, &g->s[i]);
    }
}

/* ground echo of one frame (at gain level 0) into acc[0..frame_len-1] */
static void gnd_frame(sim_t *s, int rising_f, double *acc)
{
    const sim_ground_t *g = &s->gnd;
    const double c0 = 299792458.0, ts = 1.0 / s->fs_hz;
    const double h = g->h_m;
    double X, Y;
    gnd_extent(s, &X, &Y);
    const double dA = 4.0 * X * Y / (double)g->n;
    /* point target SNR (sigma = 1 m^2) in a Hann bin of the long ramp, data sheet
     * based; a Hann bin of N samples gives SNR = a^2 N / (6 sigma_n^2) */
    const double nfft_l = (double)RADAR_NFFT_LONG;
    const double enbw = 1.5 * s->fs_hz / nfft_l;
    const double r1 = 0.0167 * pow(10.0, 114.0 / 40.0);
    const double sn2 = (double)s->noise_front_lsb * (double)s->noise_front_lsb;
    const double k = 6.0 * sn2 / nfft_l * pow(10.0, 0.1 * g->sigma0_db) * dA;
    const double ta = g->beam_along_deg * SIM_PI / 180.0, tc = g->beam_across_deg * SIM_PI / 180.0;

    for (int n = 0; n < g->n; n++) {
        const sim_scat_t *p = &g->s[n];
        const double r = sqrt(h * h + (double)p->x * p->x + (double)p->y * p->y);
        const double ua = asin(p->x / r), uc = asin(p->y / r);
        const double g1 = exp(-4.0 * log(2.0) * ((ua / ta) * (ua / ta) + (uc / tc) * (uc / tc)));
        if (g1 < 1.0e-3) { continue; }
        const double fbl = 2.0 * s->slope_hz_s * r / c0;     /* IF noise at the actual beat */
        const double snr = pow(10.0, 0.1 * (6.0 + 10.0 * log10(500.0 / enbw) + gnd_ni_db(fbl) - g->loss_db)) *
                           pow(r1 / r, 4.0);
        const double a = sqrt(k * snr) * g1 * p->c;
        const double rdot = (-(double)p->x * g->vh_mps + h * g->vz_mps) / r;
        const double fr = 2.0 * s->slope_hz_s * r / c0;
        const double fd = -2.0 * rdot / s->lambda_m;
        const double fb = rising_f ? (fr - fd) : -(fr + fd);
        const double ph = 4.0 * SIM_PI * r / s->lambda_m + p->ph0;
        double zr = cos(ph), zi = sin(ph);
        const double wr = cos(2.0 * SIM_PI * fb * ts), wi = sin(2.0 * SIM_PI * fb * ts);
        for (uint32_t i = 0; i < s->frame_len; i++) {
            acc[i] += a * zr;
            const double t = zr * wr - zi * wi;
            zi = zr * wi + zi * wr;
            zr = t;
        }
    }
}

void sim_advance(sim_t *s, float dt)
{
    if (s->gnd.enabled) {
        sim_ground_t *g = &s->gnd;
        g->h_m += g->vz_mps * dt;
        double X, Y;
        gnd_extent(s, &X, &Y);
        for (int i = 0; i < g->n; i++) {
            sim_scat_t *p = &g->s[i];
            p->x -= g->vh_mps * dt;
            if (p->x < -X) {                         /* passed behind: new terrain in front */
                p->x += (float)(2.0 * X);
                p->y = (float)(Y * (2.0 * urand(s) - 1.0));
                gnd_new(s, p);
            } else if ((p->x > X) || (fabsf(p->y) > Y)) {   /* footprint shrank (descent) */
                p->x = (float)(X * (2.0 * urand(s) - 1.0));
                p->y = (float)(Y * (2.0 * urand(s) - 1.0));
                gnd_new(s, p);
            }
        }
    }
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
        if (s->random_phase && !t->fixed) { ph[k] += 2.0 * 3.14159265358979 * urand(s); }
    }

    static double gacc[RADAR_FRAME_LEN_MAX];
    memset(gacc, 0, sizeof(gacc));
    if (s->gnd.enabled) { gnd_frame(s, rising_f, gacc); }

    for (uint32_t i = 0; i < s->frame_len; i++) {
        const double tt = (double)i * ts;
        double x = 0.0;
        for (int k = 0; k < s->n_tgt; k++) {
            x += s->tgt[k].amp_lsb * cos(2.0 * 3.14159265358979 * fb[k] * tt + ph[k]);
        }
        /* leakage: very low beat frequency, depends on the ramp direction */
        const double u = (double)i / (double)(s->frame_len - 1u);
        x += s->leak_amp_lsb * (rising_f ? (u - 0.5) : (0.5 - u)) * 2.0;
        x += gacc[i];
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
