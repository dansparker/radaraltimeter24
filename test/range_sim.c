/**
 * @file range_sim.c
 * @brief Achievable altitude range of the complete firmware pipeline against
 *        the extended ground model of the simulator (K-LC1a data sheet
 *        sensitivity, 34 x 80 deg beam, Doppler spread from the ground speed).
 *
 *   make -C test range        (not part of the unit tests, a few minutes)
 *
 * For every ground type, ground speed and height the pipeline runs from a
 * cold start for 1.5 s, then the output is evaluated for 3 s:
 *   valid  share of valid outputs
 *   bias   mean error of the valid outputs [m]
 *   e95    95 % quantile of |error| [m]
 * The summary per ground type and speed gives the height up to which >= 95 %
 * of the outputs are valid, and the worst bias / e95 in that range.
 */
#include "sim.h"
#include "altimeter.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static config_t cfg;
static altimeter_t alt;
static sim_t sim;
static uint16_t frame[RADAR_FRAME_LEN];

static int cmp_f(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static uint32_t run_seed = 12345u;

static void run(float h, float vh, float s0, float *valid, float *bias, float *e95)
{
    static float err[2000];
    cfg_defaults(&cfg);
    cfg.mod[MOD_KLC1A].calibrated = 1u;
    alt_init(&alt, &cfg);
    sim_init(&sim, 90.0e6f);
    sim.rng = run_seed;
    sim.leak_amp_lsb = 0.0f;
    sim_ground(&sim, h, vh, 0.0f, s0);
    uint32_t id = 0;
    uint8_t cur_gain = alt_gain_request(&alt);
    float t = 0.0f;
    int nv = 0, ne = 0, nout = 0;
    double sum = 0.0;
    while (t < 4.5f) {
        int out = 0;
        for (int d = 0; d < 2; d++) {
            const uint8_t req = alt_gain_request(&alt);
            alt_frame_info_t fi = { ++id, (uint8_t)d, req, (uint8_t)(req != cur_gain), 0u };
            cur_gain = req;
            sim_frame(&sim, d, cur_gain, frame);
            out |= alt_process_frame(&alt, frame, &fi);
        }
        sim_advance(&sim, RADAR_PAIR_DT_S);
        t += RADAR_PAIR_DT_S;
        if (out && (t > 1.5f)) {
            nout++;
            if ((alt.out.status & ALT_ST_VALID) != 0u) {
                const float e = alt.out.altitude_m - h;
                nv++;
                sum += e;
                if (ne < 2000) { err[ne++] = fabsf(e); }
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

/* systematic edge offset vs. height (slow ground speed, strong ground, 4 seeds) */
static void bias_table(void)
{
    const float hs[] = { 4.0f, 6.0f, 8.0f, 10.0f, 13.0f, 16.0f, 20.0f, 25.0f, 30.0f, 40.0f, 50.0f, 65.0f,
                         80.0f, 100.0f, 125.0f, 150.0f, 175.0f, 200.0f, 225.0f, 250.0f };
    for (unsigned c = 0; c < sizeof(hs) / sizeof(hs[0]); c++) {
        double sb = 0.0;
        for (int k = 0; k < 4; k++) {
            float v, bias, e95;
            run_seed = 1000u + (uint32_t)k;
            run(hs[c], 10.0f, 10.0f, &v, &bias, &e95);
            sb += bias;
        }
        printf("%6.1f  %+6.3f", hs[c], sb / 4.0);
        putchar(10);
    }
}

int main(int argc, char **argv)
{
    if ((argc > 1) && (argv[1][0] == 'b')) { bias_table(); return 0; }
    const float s0s[] = { -10.0f, 0.0f, 10.0f };
    const float vhs[] = { 10.0f, 30.0f, 60.0f };
    const float hs[] = { 5.0f, 10.0f, 20.0f, 30.0f, 50.0f, 75.0f, 100.0f, 125.0f, 150.0f, 175.0f,
                         200.0f, 225.0f, 250.0f };
    printf("sigma0  v_h   h[m]  valid   bias[m]  e95[m]\n");
    for (unsigned a = 0; a < 3u; a++) {
        for (unsigned b = 0; b < 3u; b++) {
            float hmax = 0.0f, bmax = 0.0f, emax = 0.0f;
            int gap = 0;
            for (unsigned c = 0; c < sizeof(hs) / sizeof(hs[0]); c++) {
                float v, bias, e95;
                run(hs[c], vhs[b], s0s[a], &v, &bias, &e95);
                printf("%+5.0f  %4.0f  %5.0f  %5.2f  %+7.2f  %6.2f\n", s0s[a], vhs[b], hs[c], v, bias, e95);
                if ((v >= 0.95f) && !gap) {
                    hmax = hs[c];
                    bmax = fmaxf(bmax, fabsf(bias));
                    emax = fmaxf(emax, e95);
                } else {
                    gap = 1;
                }
                if (v < 0.2f) { break; }
            }
            printf("==> sigma0 %+.0f dB, %2.0f m/s: valid >= 95 %% up to %.0f m, |bias| <= %.2f m, e95 <= %.2f m\n\n",
                   s0s[a], vhs[b], hmax, bmax, emax);
        }
    }
    return 0;
}
