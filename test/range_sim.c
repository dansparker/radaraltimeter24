/**
 * @file range_sim.c
 * @brief Achievable altitude range of the complete firmware pipeline against
 *        the extended ground model of the simulator (K-LC1a data sheet
 *        sensitivity, 34 x 80 deg beam, Doppler spread from the ground speed).
 *
 *   make -C test range        (not part of the unit tests, a few minutes)
 *   build/range_sim s         short ramp only
 *
 * For every ground type, ground speed and height the pipeline runs from a
 * cold start for 1 s, then the output is evaluated for 3 s:
 *   valid  share of valid outputs
 *   bias   mean error of the valid outputs [m]
 *   e95    95 % quantile of |error| [m]
 */
#include "sim.h"
#include "altimeter.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static config_t cfg;
static altimeter_t alt;
static sim_t sim;
static uint16_t frame[RADAR_FRAME_LEN_MAX];

static int cmp_f(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static uint8_t g_rmode = CFG_RMODE_AUTO;

static void run(float h, float vh, float s0, float *valid, float *bias, float *e95)
{
    static float err[4000];
    cfg_defaults(&cfg);
    cfg.mod[MOD_KLC1A].calibrated = 1u;
    cfg.ramp_mode = g_rmode;
    alt_init(&alt, &cfg);
    sim_init(&sim, 90.0e6f);
    sim.leak_amp_lsb = 0.0f;
    sim_ground(&sim, h, vh, 0.0f, s0);
    uint32_t id = 0;
    uint8_t cur_gain = alt_gain_request(&alt), cur_rm = alt_rmode_request(&alt);
    float t = 0.0f;
    int nv = 0, ne = 0, nout = 0;
    double sum = 0.0;
    while (t < 4.0f) {
        const uint8_t rm = alt_rmode_request(&alt);
        const int restart = (rm != cur_rm);
        cur_rm = rm;
        sim_set_rmode(&sim, rm);
        for (int d = 0; d < 2; d++) {
            const uint8_t req = alt_gain_request(&alt);
            alt_frame_info_t fi = { ++id, rm, (uint8_t)d, req,
                                    (uint8_t)((req != cur_gain) || (restart && (d == 0))), 0u };
            cur_gain = req;
            sim_frame(&sim, d, cur_gain, frame);
            (void)alt_process_frame(&alt, frame, &fi);
        }
        const float dt = radar_pair_dt(rm);
        sim_advance(&sim, dt);
        t += dt;
        if (t > 1.0f) {
            nout++;
            if ((alt.out.status & ALT_ST_VALID) != 0u) {
                const float e = alt.out.altitude_m - h;
                nv++;
                sum += e;
                if (ne < 4000) { err[ne++] = fabsf(e); }
            }
        }
    }
    *valid = (float)nv / (float)nout;
    *bias = (nv > 0) ? (float)(sum / nv) : NAN;
    if (ne > 0) {
        qsort(err, (size_t)ne, sizeof(float), cmp_f);
        *e95 = err[(int)(0.95f * (float)(ne - 1))];
    } else {
        *e95 = NAN;
    }
}

int main(int argc, char **argv)
{
    if ((argc > 1) && (argv[1][0] == 's')) { g_rmode = CFG_RMODE_SHORT; }
    printf("ramp mode %s\n", (g_rmode == CFG_RMODE_SHORT) ? "short" : "auto");
    const float s0s[] = { 0.0f, 10.0f };
    const float vhs[] = { 10.0f, 30.0f, 60.0f };
    const float hs[] = { 5.0f, 10.0f, 20.0f, 30.0f, 50.0f, 75.0f, 100.0f, 125.0f, 150.0f, 200.0f, 250.0f };
    printf("sigma0  v_h   h[m]  valid   bias[m]  e95[m]\n");
    for (unsigned a = 0; a < 2u; a++) {
        for (unsigned b = 0; b < 3u; b++) {
            float hmax = 0.0f;
            for (unsigned c = 0; c < sizeof(hs) / sizeof(hs[0]); c++) {
                float v, bias, e95;
                run(hs[c], vhs[b], s0s[a], &v, &bias, &e95);
                printf("%+5.0f  %4.0f  %5.0f  %5.2f  %+7.2f  %6.2f\n", s0s[a], vhs[b], hs[c], v, bias, e95);
                const float tol = fmaxf(1.0f, 0.03f * hs[c]);
                if ((v >= 0.9f) && (fabsf(bias) <= tol) && (e95 <= 2.0f * tol)) { hmax = hs[c]; }
                if (v < 0.2f) { break; }
            }
            printf("==> sigma0 %+.0f dB, %2.0f m/s: max. height %.0f m\n\n", s0s[a], vhs[b], hmax);
        }
    }
    return 0;
}
