/**
 * @file test_alt.c
 * @brief End-to-end tests: simulator -> altimeter pipeline -> output.
 */
#include "unit.h"
#include "sim.h"
#include "altimeter.h"
#include "proto.h"
#include "ramp.h"
#include <string.h>
#include <stdlib.h>

static config_t cfg;
static altimeter_t alt;
static sim_t sim;
static uint16_t frame[RADAR_FRAME_LEN_MAX];
static int16_t bg[RADAR_BG_LEN];
static uint32_t frame_id;
static uint8_t cur_gain, cur_rmode;
static float last_dt;   /* pair duration of the last step */

#define SWEEP_HZ 90.0e6f

static void setup(float sweep_true)
{
    cfg_defaults(&cfg);
    cfg.mod[MOD_KLC1A].calibrated = 1u;
    alt_init(&alt, &cfg, bg, 0u);
    sim_init(&sim, sweep_true);
    frame_id = 0u;
    cur_gain = alt_gain_request(&alt);
    cur_rmode = alt_rmode_request(&alt);
    last_dt = radar_pair_dt(cur_rmode);
}

/* one ramp pair as the STM32 frontend would deliver it; returns the output */
static const alt_output_t *step_pair(void)
{
    /* the firmware restarts the frontend when the pipeline requests another ramp */
    const uint8_t rm = alt_rmode_request(&alt);
    const int restart = (rm != cur_rmode);
    cur_rmode = rm;
    sim_set_rmode(&sim, rm);
    for (int d = 0; d < 2; d++) {
        const uint8_t req = alt_gain_request(&alt);
        alt_frame_info_t fi = { ++frame_id, rm, (uint8_t)d, req,
                                (uint8_t)((req != cur_gain) || (restart && (d == 0))), 0u };
        cur_gain = req;
        sim_frame(&sim, d, cur_gain, frame);
        const int out = alt_process_frame(&alt, frame, &fi);
        if (d == 0) { CHECK(out == 0); }
    }
    last_dt = radar_pair_dt(rm);
    sim_advance(&sim, last_dt);
    return &alt.out;
}

static float amp_for(float r) { return 1500.0f * 3.0f / r; }   /* extended target ~ 1/R */

static void set_target(int i, float r, float vr, float amp)
{
    sim.tgt[i].range_m = r;
    sim.tgt[i].vr_mps = vr;
    sim.tgt[i].amp_lsb = amp;
    if (sim.n_tgt <= i) { sim.n_tgt = i + 1; }
}

static int valid(const alt_output_t *o) { return (o->status & ALT_ST_VALID) != 0u; }

static void test_static_altitudes(void)
{
    const float alts[] = { 5.0f, 7.0f, 10.0f, 25.0f, 50.0f, 100.0f, 150.0f, 200.0f, 240.0f };
    for (unsigned k = 0; k < sizeof(alts) / sizeof(alts[0]); k++) {
        setup(SWEEP_HZ);
        set_target(0, alts[k], 0.0f, amp_for(alts[k]));
        double emax = 0.0;
        int nvalid = 0;
        for (int i = 0; i < 120; i++) {
            const alt_output_t *o = step_pair();
            if (i >= 60) {
                nvalid += valid(o);
                emax = fmax(emax, fabs(o->altitude_m - alts[k]));
            }
        }
        const double tol = fmax(0.25, 0.005 * alts[k]);
        printf("  %6.1f m: max error %.3f m (tol %.2f), valid %d/60, gain %u, %s ramp\n",
               alts[k], emax, tol, nvalid, alt.out.gain, alt.out.rmode ? "short" : "long");
        CHECK(nvalid == 60);
        CHECK(emax < tol);
    }
}

static void test_descent_and_climb(void)
{
    const float vr[] = { -5.0f, 3.0f, -12.0f };
    for (unsigned k = 0; k < 3u; k++) {
        setup(SWEEP_HZ);
        set_target(0, 120.0f, vr[k], amp_for(120.0f));
        double emax = 0.0, vmax = 0.0;
        int nvalid = 0;
        for (int i = 0; i < 400; i++) {
            sim.tgt[0].amp_lsb = amp_for(sim.tgt[0].range_m);
            const alt_output_t *o = step_pair();
            if (i >= 100) {
                nvalid += valid(o);
                /* the output refers to the measurement time (before sim_advance) */
                const double truth = sim.tgt[0].range_m - vr[k] * last_dt;
                emax = fmax(emax, fabs(o->altitude_m - truth));
                if (fabs(o->vspeed_mps - vr[k]) > 0.3) {
                    printf("    i=%d R=%.2f vs=%.2f st=%04X gain=%u fu=%.1f fd=%.1f\n", i, truth,
                           o->vspeed_mps, o->status, o->gain, o->f_rise_hz, o->f_fall_hz);
                }
                vmax = fmax(vmax, fabs(o->vspeed_mps - vr[k]));
            }
        }
        printf("  v=%5.1f m/s: max alt error %.3f m, max speed error %.3f m/s, valid %d/300\n",
               vr[k], emax, vmax, nvalid);
        CHECK(nvalid == 300);
        CHECK(emax < 0.6);
        CHECK(vmax < 0.5);
    }
}

static void test_noise_only(void)
{
    setup(SWEEP_HZ);
    int nvalid = 0;
    for (int i = 0; i < 1500; i++) { nvalid += valid(step_pair()); }
    printf("  noise only: %d valid outputs of 1500 (gain %u)\n", nvalid, alt.out.gain);
    CHECK(nvalid == 0);
    CHECK((alt.out.status & ALT_ST_NO_TARGET) != 0u);
}

static void test_multipath(void)
{
    setup(SWEEP_HZ);
    set_target(0, 40.0f, 0.0f, amp_for(40.0f));
    set_target(1, 80.0f, 0.0f, amp_for(40.0f) * 0.3f);   /* double bounce, -10 dB */
    set_target(2, 120.0f, 0.0f, amp_for(40.0f) * 0.1f);
    const alt_output_t *o = NULL;
    for (int i = 0; i < 100; i++) { o = step_pair(); }
    CHECK(valid(o));
    CHECK_NEAR(o->altitude_m, 40.0, 0.3);
}

static void test_outliers(void)
{
    setup(SWEEP_HZ);
    set_target(0, 50.0f, 0.0f, amp_for(50.0f));
    double emax = 0.0;
    int nvalid = 0;
    for (int i = 0; i < 400; i++) {
        /* every 40th pair a strong spurious target closer than the ground */
        set_target(1, 20.0f, 0.0f, ((i % 40) == 39) ? 3000.0f : 0.0f);
        const alt_output_t *o = step_pair();
        if (i >= 60) { nvalid += valid(o); emax = fmax(emax, fabs(o->altitude_m - 50.0)); }
    }
    printf("  outliers: max error %.3f m, valid %d/340\n", emax, nvalid);
    CHECK(nvalid == 340);
    CHECK(emax < 0.4);
}

static void test_dropout(void)
{
    setup(SWEEP_HZ);
    set_target(0, 30.0f, 0.0f, amp_for(30.0f));
    for (int i = 0; i < 80; i++) { step_pair(); }
    CHECK(valid(&alt.out));

    /* short dropout: output coasts and stays valid for <= ALT_COAST_VALID pairs */
    sim.tgt[0].amp_lsb = 0.0f;
    int nvalid = 0;
    for (int i = 0; i < 40; i++) { nvalid += valid(step_pair()); }
    printf("  dropout: %d valid pairs while target missing\n", nvalid);
    CHECK(nvalid <= (int)alt.coast_valid + 3);
    CHECK(!valid(&alt.out));

    /* target back */
    sim.tgt[0].amp_lsb = amp_for(30.0f);
    int first = -1;
    for (int i = 0; i < 60; i++) {
        if (valid(step_pair()) && (first < 0)) { first = i; }
    }
    printf("  re-acquired after %d pairs\n", first);
    CHECK((first >= 0) && (first < 15));
    CHECK_NEAR(alt.out.altitude_m, 30.0, 0.3);

    /* long dropout -> LOST */
    sim.tgt[0].amp_lsb = 0.0f;
    for (int i = 0; i < 100; i++) { step_pair(); }
    CHECK(alt.out.track_state == RDSP_TRK_LOST);
}

static void test_step_change(void)
{
    setup(SWEEP_HZ);
    set_target(0, 60.0f, 0.0f, amp_for(60.0f));
    for (int i = 0; i < 80; i++) { step_pair(); }
    sim.tgt[0].range_m = 45.0f;                     /* building edge */
    sim.tgt[0].amp_lsb = amp_for(45.0f);
    int settled = -1;
    for (int i = 0; i < 60; i++) {
        const alt_output_t *o = step_pair();
        if ((settled < 0) && valid(o) && (fabs(o->altitude_m - 45.0) < 0.3)) { settled = i; }
    }
    printf("  step 60->45 m settled after %d pairs\n", settled);
    CHECK((settled >= 0) && (settled < 20));
}

static void test_agc_and_clipping(void)
{
    setup(SWEEP_HZ);
    set_target(0, 15.0f, 0.0f, 20000.0f);          /* clips at every gain */
    int nclip = 0, nvalid = 0;
    for (int i = 0; i < 60; i++) {
        const alt_output_t *o = step_pair();
        nclip += (o->status & ALT_ST_CLIPPED) != 0u;
        nvalid += valid(o);
    }
    CHECK(alt.out.gain == 0u);
    CHECK(nclip > 50);
    CHECK(nvalid == 0);

    /* weak target: AGC raises the gain until it is well visible */
    setup(SWEEP_HZ);
    set_target(0, 180.0f, 0.0f, 8.0f);
    for (int i = 0; i < 300; i++) { step_pair(); }
    printf("  weak target: gain %u, alt %.2f, snr %.1f dB\n", alt.out.gain, alt.out.altitude_m, alt.out.snr_db);
    CHECK(alt.out.gain >= 2u);
    CHECK(valid(&alt.out));
    CHECK_NEAR(alt.out.altitude_m, 180.0, 0.9);
}

static void test_landing_low_altitude(void)
{
    /* descent at 3 m/s to 4 m: below ~6.5 m the rising-ramp beat is mirrored /
     * near DC; the falling ramp + predicted Doppler keeps the track */
    setup(SWEEP_HZ);
    set_target(0, 30.0f, -3.0f, amp_for(30.0f));
    double emax = 0.0;
    int ndeg = 0, ninv = 0;
    while (sim.tgt[0].range_m > 4.5f) {
        sim.tgt[0].amp_lsb = amp_for(sim.tgt[0].range_m);
        const alt_output_t *o = step_pair();
        const double truth = sim.tgt[0].range_m + 3.0 * last_dt;
        if (truth < 25.0) {
            ninv += !valid(o);
            ndeg += (o->status & ALT_ST_DEGRADED) != 0u;
            if (fabs(o->altitude_m - truth) > 0.6) {
                printf("    R=%.2f alt=%.2f vs=%.2f st=%04X trk=%u fu=%.1f fd=%.1f\n", truth, o->altitude_m,
                       o->vspeed_mps, o->status, o->track_state, o->f_rise_hz, o->f_fall_hz);
            }
            emax = fmax(emax, fabs(o->altitude_m - truth));
        }
    }
    printf("  landing: max error %.3f m, invalid %d, degraded %d, %s ramp\n", emax, ninv, ndeg,
           alt.out.rmode ? "short" : "long");
    CHECK(ninv == 0);
    CHECK(emax < 0.3);
    CHECK(alt.out.rmode == RADAR_RMODE_SHORT);
}

/* flare: the sink rate drops from 3 to 0.8 m/s between 12 m and 5 m. This
 * is the worst case for the single-ramp solution (rate prediction error). */
static void test_flare(void)
{
    for (int mode = 0; mode < 2; mode++) {
        setup(SWEEP_HZ);
        cfg.ramp_mode = mode ? CFG_RMODE_AUTO : CFG_RMODE_LONG;
        alt_apply_config(&alt);
        set_target(0, 30.0f, -3.0f, amp_for(30.0f));
        double emax = 0.0;
        int ninv = 0;
        while (sim.tgt[0].range_m > 4.8f) {
            const float r = sim.tgt[0].range_m;
            sim.tgt[0].vr_mps = (r > 12.0f) ? -3.0f : -(0.8f + 2.2f * (r - 5.0f) / 7.0f);
            sim.tgt[0].amp_lsb = amp_for(r);
            const float vr = sim.tgt[0].vr_mps;
            const alt_output_t *o = step_pair();
            const double truth = sim.tgt[0].range_m - vr * last_dt;
            if (truth < 20.0) {
                ninv += !valid(o);
                emax = fmax(emax, fabs(o->altitude_m - truth));
            }
        }
        printf("  flare, %s ramp: max error %.3f m, invalid %d\n", mode ? "auto (short)" : "long only", emax, ninv);
        CHECK(ninv == 0);
        if (mode) { CHECK(emax < 0.5); }
    }
}

/* automatic ramp switching while descending and climbing through the thresholds */
static void test_rmode_switch(void)
{
    setup(SWEEP_HZ);
    set_target(0, 45.0f, -4.0f, amp_for(45.0f));
    double emax = 0.0;
    int ninv = 0, nshort = 0;
    for (int i = 0; i < 1800; i++) {
        if (sim.tgt[0].range_m < 12.0f) { sim.tgt[0].vr_mps = 4.0f; }
        sim.tgt[0].amp_lsb = amp_for(sim.tgt[0].range_m);
        const float vr = sim.tgt[0].vr_mps;
        const alt_output_t *o = step_pair();
        if (i > 80) {
            const double truth = sim.tgt[0].range_m - vr * last_dt;
            ninv += !valid(o);
            emax = fmax(emax, fabs(o->altitude_m - truth));
            nshort += (o->rmode == RADAR_RMODE_SHORT);
        }
        if (sim.tgt[0].range_m > 50.0f) { break; }
    }
    printf("  ramp switching: max error %.3f m, invalid %d, short-ramp pairs %d, now %s\n",
           emax, ninv, nshort, alt.out.rmode ? "short" : "long");
    CHECK(nshort > 100);
    CHECK(ninv < 3);
    CHECK(emax < 0.5);
    CHECK(alt.out.rmode == RADAR_RMODE_LONG);
}

static void test_hw_fault(void)
{
    setup(SWEEP_HZ);
    set_target(0, 30.0f, 0.0f, amp_for(30.0f));
    for (int i = 0; i < 60; i++) { step_pair(); }
    CHECK(valid(&alt.out));
    sim.stuck = 1;
    for (int i = 0; i < 15; i++) { step_pair(); }
    CHECK((alt.out.status & ALT_ST_HW_FAULT) != 0u);
    CHECK(!valid(&alt.out));
    sim.stuck = 0;
    for (int i = 0; i < 40; i++) { step_pair(); }
    CHECK((alt.out.status & ALT_ST_HW_FAULT) == 0u);
    CHECK(valid(&alt.out));
}

static void test_vco_sign(void)
{
    setup(SWEEP_HZ);
    cfg.vco_sign = -1;
    alt_apply_config(&alt);
    sim.vco_sign = -1;
    set_target(0, 80.0f, -6.0f, amp_for(80.0f));
    for (int i = 0; i < 200; i++) { step_pair(); }
    CHECK(valid(&alt.out));
    CHECK_NEAR(alt.out.altitude_m, sim.tgt[0].range_m + 6.0 * last_dt, 0.5);
    CHECK_NEAR(alt.out.vspeed_mps, -6.0, 0.4);
}

static void test_background(void)
{
    /* strong static reflection of the landing gear at 4.5 m */
    setup(SWEEP_HZ);
    set_target(0, 4.5f, 0.0f, 1500.0f);
    alt_bg_start(&alt, 32u);
    int guard = 0;
    while (!alt_bg_done(&alt) && (guard++ < 2000)) { step_pair(); }
    CHECK(alt_bg_done(&alt));
    printf("  background valid mask 0x%X after %d pairs\n", alt.bg_valid, guard);
    CHECK(alt.bg_valid != 0u);

    CHECK((alt.bg_valid & 0x0Fu) != 0u);    /* long ramp */
    CHECK((alt.bg_valid & 0xF0u) != 0u);    /* short ramp */
    /* without subtraction the nearest strong target (gear) is reported */
    set_target(1, 35.0f, 0.0f, amp_for(35.0f));
    for (int i = 0; i < 100; i++) { step_pair(); }
    printf("  without bg: %.2f m\n", alt.out.altitude_m);
    CHECK(fabs(alt.out.altitude_m - 35.0) > 5.0);

    cfg.bg_enable = 1u;
    alt_apply_config(&alt);
    for (int i = 0; i < 100; i++) { step_pair(); }
    printf("  with bg: %.2f m\n", alt.out.altitude_m);
    CHECK(valid(&alt.out));
    CHECK_NEAR(alt.out.altitude_m, 35.0, 0.3);

    /* low altitude: short ramp + its own background */
    sim.tgt[1].range_m = 15.0f;
    sim.tgt[1].amp_lsb = amp_for(15.0f);
    for (int i = 0; i < 300; i++) { step_pair(); }
    printf("  with bg, short ramp: %.2f m (%s)\n", alt.out.altitude_m, alt.out.rmode ? "short" : "long");
    CHECK(alt.out.rmode == RADAR_RMODE_SHORT);
    CHECK(valid(&alt.out));
    CHECK_NEAR(alt.out.altitude_m, 15.0, 0.3);
}

static void test_calibration(void)
{
    /* the radar sweeps 110 MHz but the config assumes 90 MHz: 2-point calibration */
    const float true_sweep = 110.0e6f;
    const float ref[2] = { 10.0f, 40.0f };
    for (unsigned rm = 0; rm < RADAR_NMODES; rm++) {
    float f[2];
    for (int p = 0; p < 2; p++) {
        setup(true_sweep);
        cfg.ramp_mode = rm ? CFG_RMODE_SHORT : CFG_RMODE_LONG;
        alt_apply_config(&alt);
        set_target(0, ref[p], 0.0f, amp_for(ref[p]));
        for (int i = 0; i < 60; i++) { step_pair(); }
        alt_cal_start(&alt, 100u);
        float m = 0.0f, sd = 0.0f;
        uint16_t n = 0u;
        int guard = 0;
        while (!alt_cal_result(&alt, &m, &sd, &n) && (guard++ < 500)) { step_pair(); }
        CHECK(n == 100u);
        f[p] = m;
    }
    const float slope = RDSP_C0_MPS * (f[1] - f[0]) / (2.0f * (ref[1] - ref[0]));
    const float sweep = slope * (float)(radar_frame_len(rm) - 1u) / (float)RADAR_FS_HZ;
    printf("  %s ramp: calibrated sweep %.3f MHz (true 110)\n", rm ? "short" : "long", sweep * 1e-6);
    CHECK_NEAR(sweep, true_sweep, 0.005 * true_sweep);
    }
}

static void test_protocol(void)
{
    char b[128];
    CHECK(proto_fmt_fixed(b, -0.004f, 2u) == 4u && strcmp(b, "0.00") == 0);
    proto_fmt_fixed(b, -12.345f, 2u);
    CHECK(strcmp(b, "-12.35") == 0 || strcmp(b, "-12.34") == 0);
    proto_fmt_fixed(b, 7.05f, 1u);
    CHECK(strcmp(b, "7.1") == 0 || strcmp(b, "7.0") == 0);
    CHECK(proto_fmt_fixed(b, NAN, 2u) == 0u);

    alt_output_t o;
    memset(&o, 0, sizeof(o));
    o.seq = 7u; o.altitude_m = 123.456f; o.vspeed_mps = -1.5f; o.snr_db = 25.3f;
    o.status = ALT_ST_VALID; o.gain = 2u;
    const size_t n = proto_ralt(b, sizeof(b), &o);
    CHECK(strcmp(b, "$RALT,7,123.46,-1.50,25.3,2,0001*") == 0 || (n > 0u && strncmp(b, "$RALT,7,123.46,-1.50,25.3,2,0001*", 33) == 0));
    const char *star = strchr(b, '*');
    CHECK(star != NULL);
    if (star != NULL) {
        unsigned cs = (unsigned)strtoul(star + 1, NULL, 16);
        CHECK(cs == proto_nmea_cs(b + 1, (size_t)(star - b - 1)));
    }
    uint8_t d[8];
    proto_can_alt(d, &o, 5u);
    CHECK((d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24)) == 123456u);
    CHECK((int16_t)(d[4] | (d[5] << 8)) == -150);
    CHECK(d[7] == (5u | (2u << 4)));
    o.status = 0u;                       /* invalid -> no altitude on the bus */
    proto_can_alt(d, &o, 0u);
    CHECK(d[0] == 0u && d[1] == 0u);
    proto_ralt(b, sizeof(b), &o);
    CHECK(strncmp(b, "$RALT,7,,,25.3,", 15) == 0);
}

static void test_config(void)
{
    config_t c;
    cfg_defaults(&c);
    CHECK(cfg_is_valid(&c));
    c.mod[0].sweep_hz = 100e6f;
    CHECK(!cfg_is_valid(&c));            /* CRC */
    cfg_seal(&c);
    CHECK(cfg_is_valid(&c));
    c.module = 7u;
    cfg_seal(&c);
    CHECK(!cfg_is_valid(&c));

    static uint16_t t[2u * RADAR_FRAME_LEN_MAX];
    for (unsigned rm = 0; rm < RADAR_NMODES; rm++) {
        const uint32_t n = radar_frame_len(rm);
        ramp_build(t, n, 0u, 2480u, 0.0f);
        CHECK(t[0] == 0u && t[n - 1u] == 2480u);
        CHECK(t[n] == 2480u && t[2u * n - 1u] == 0u);
        int mono = 1;
        for (uint32_t i = 1; i < n; i++) { if (t[i] < t[i - 1u]) { mono = 0; } }
        CHECK(mono);
        ramp_build(t, n, 75u, 4091u, 0.0f);
        CHECK(t[0] == 75u && t[n - 1u] == 4091u);
    }
    CHECK(radar_frame_len(RADAR_RMODE_LONG) == 2432u);
    CHECK(radar_frame_len(RADAR_RMODE_SHORT) == 704u);
}

void test_alt_all(void)
{
    RUN(test_config);
    RUN(test_protocol);
    RUN(test_static_altitudes);
    RUN(test_descent_and_climb);
    RUN(test_noise_only);
    RUN(test_multipath);
    RUN(test_outliers);
    RUN(test_dropout);
    RUN(test_step_change);
    RUN(test_agc_and_clipping);
    RUN(test_landing_low_altitude);
    RUN(test_flare);
    RUN(test_rmode_switch);
    RUN(test_hw_fault);
    RUN(test_vco_sign);
    RUN(test_background);
    RUN(test_calibration);
}
