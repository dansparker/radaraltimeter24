/**
 * @file test_alt.c
 * @brief End-to-end tests: simulator -> altimeter pipeline -> output.
 *
 * The ground is the extended ground model of the simulator (beam footprint,
 * Doppler spread from the ground speed, speckle, data sheet sensitivity).
 * Point targets are used for reflectors (calibration), gear echoes and
 * interferers.
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
static uint16_t frame[RADAR_FRAME_LEN];
static uint32_t frame_id;
static uint8_t cur_gain;
static float t_sim;

#define SWEEP_HZ 90.0e6f
#define VH_APPROACH 30.0f    /* typical approach ground speed [m/s] */

static void setup(float sweep_true)
{
    cfg_defaults(&cfg);
    cfg.mod[MOD_KLC1A].calibrated = 1u;
    alt_init(&alt, &cfg);
    sim_init(&sim, sweep_true);
    frame_id = 0u;
    cur_gain = alt_gain_request(&alt);
    t_sim = 0.0f;
}

/* one ramp pair as the STM32 frontend would deliver it; returns 1 on a new output */
static int step_pair(void)
{
    int out = 0;
    for (int d = 0; d < 2; d++) {
        const uint8_t req = alt_gain_request(&alt);
        alt_frame_info_t fi = { ++frame_id, (uint8_t)d, req, (uint8_t)(req != cur_gain), 0u };
        cur_gain = req;
        sim_frame(&sim, d, cur_gain, frame);
        const int o = alt_process_frame(&alt, frame, &fi);
        if (d == 0) { CHECK(o == 0); }
        out |= o;
    }
    sim_advance(&sim, RADAR_PAIR_DT_S);
    t_sim += RADAR_PAIR_DT_S;
    return out;
}

/* run until the next output */
static const alt_output_t *step_out(void)
{
    while (!step_pair()) { }
    return &alt.out;
}

static void run_s(float s)
{
    const float t_end = t_sim + s;
    while (t_sim < t_end) { (void)step_pair(); }
}

static void set_target(int i, float r, float vr, float amp)
{
    sim.tgt[i].range_m = r;
    sim.tgt[i].vr_mps = vr;
    sim.tgt[i].amp_lsb = amp;
    if (sim.n_tgt <= i) { sim.n_tgt = i + 1; }
}

static int valid(const alt_output_t *o) { return (o->status & ALT_ST_VALID) != 0u; }

/* ground truth at the time of the output: the simulator has already advanced */
static double truth(void) { return sim.gnd.h_m - sim.gnd.vz_mps * RADAR_PAIR_DT_S; }

typedef struct { int n, nv; double bias, emax; } stat_t;

static void eval(stat_t *s, float seconds)
{
    memset(s, 0, sizeof(*s));
    const float t_end = t_sim + seconds;
    while (t_sim < t_end) {
        const alt_output_t *o = step_out();
        s->n++;
        if (valid(o)) {
            const double e = o->altitude_m - truth();
            s->nv++;
            s->bias += e;
            s->emax = fmax(s->emax, fabs(e));
        }
    }
    if (s->nv > 0) { s->bias /= s->nv; }
}

/* ------------------------------------------------------------------------ */

static void test_ground_altitudes(void)
{
    const float hs[] = { 5.0f, 10.0f, 20.0f, 50.0f, 100.0f };
    for (unsigned k = 0; k < sizeof(hs) / sizeof(hs[0]); k++) {
        setup(SWEEP_HZ);
        sim_ground(&sim, hs[k], VH_APPROACH, 0.0f, 0.0f);
        run_s(1.5f);
        stat_t s;
        eval(&s, 2.0f);
        printf("  %5.0f m, grass, 30 m/s: valid %d/%d, bias %+.2f m, max %.2f m, snr %.1f dB, gain %u\n",
               hs[k], s.nv, s.n, s.bias, s.emax, alt.out.snr_db, alt.out.gain);
        CHECK(s.nv == s.n);
        CHECK(fabs(s.bias) < fmax(0.8, 0.02 * hs[k]));
        CHECK(s.emax < fmax(1.6, 0.04 * hs[k]));
    }
}

/* acceptance: achievable heights with the K-LC1a (data sheet sensitivity) */
static void test_range(void)
{
    const struct { float h, s0, vh; } c[] = {
        { 100.0f, 0.0f, VH_APPROACH }, { 200.0f, 10.0f, VH_APPROACH }, { 250.0f, 10.0f, 10.0f } };
    for (unsigned k = 0; k < sizeof(c) / sizeof(c[0]); k++) {
        setup(SWEEP_HZ);
        sim_ground(&sim, c[k].h, c[k].vh, 0.0f, c[k].s0);
        run_s(1.5f);
        stat_t s;
        eval(&s, 2.0f);
        printf("  %5.0f m, sigma0 %+.0f dB, %2.0f m/s: valid %d/%d, bias %+.2f m, max %.2f m\n",
               c[k].h, c[k].s0, c[k].vh, s.nv, s.n, s.bias, s.emax);
        CHECK(s.nv >= (95 * s.n) / 100);
        CHECK(fabs(s.bias) < 0.01 * c[k].h);
        CHECK(s.emax < 0.04 * c[k].h);
    }
}

static void test_descent_and_climb(void)
{
    const float vz[] = { -5.0f, 3.0f, -12.0f };
    for (unsigned k = 0; k < 3u; k++) {
        setup(SWEEP_HZ);
        sim_ground(&sim, 100.0f, VH_APPROACH, 0.0f, 5.0f);
        run_s(1.5f);
        sim.gnd.vz_mps = vz[k];
        run_s(1.5f);
        double emax = 0.0, vmax = 0.0, vsum = 0.0;
        int n = 0, nv = 0;
        for (float t_end = t_sim + 2.0f; t_sim < t_end;) {
            const alt_output_t *o = step_out();
            n++;
            if (valid(o)) {
                nv++;
                emax = fmax(emax, fabs(o->altitude_m - truth()));
                vmax = fmax(vmax, fabs(o->vspeed_mps - vz[k]));
                vsum += o->vspeed_mps - vz[k];
            }
        }
        printf("  vz %+5.1f m/s: valid %d/%d, max alt error %.2f m, speed error mean %+.2f max %.2f m/s\n",
               vz[k], nv, n, emax, vsum / nv, vmax);
        CHECK(nv == n);
        CHECK(emax < 2.0);                       /* 2 % at 100 m */
        CHECK(fabs(vsum / nv) < 0.3);            /* unbiased; speckle noise ~0.5 m/s rms */
        CHECK(vmax < 2.0);
    }
}

static void test_noise_only(void)
{
    setup(SWEEP_HZ);
    int nvalid = 0, n = 0;
    for (float t_end = t_sim + 20.0f; t_sim < t_end;) { nvalid += valid(step_out()); n++; }
    printf("  noise only: %d valid outputs of %d (gain %u)\n", nvalid, n, alt.out.gain);
    CHECK(nvalid == 0);
    CHECK((alt.out.status & ALT_ST_NO_TARGET) != 0u);
}

static void test_multipath(void)
{
    /* double bounce ground - aircraft - ground at twice the height */
    setup(SWEEP_HZ);
    sim_ground(&sim, 40.0f, VH_APPROACH, 0.0f, 0.0f);
    set_target(0, 80.0f, 0.0f, 300.0f);
    run_s(1.5f);
    stat_t s;
    eval(&s, 1.0f);
    printf("  ground 40 m + echo at 80 m: valid %d/%d, bias %+.2f m\n", s.nv, s.n, s.bias);
    CHECK(s.nv == s.n);
    CHECK(fabs(s.bias) < 0.8);
}

static void test_outliers(void)
{
    /* every 40th pair a strong spurious target closer than the ground */
    setup(SWEEP_HZ);
    sim_ground(&sim, 50.0f, VH_APPROACH, 0.0f, 0.0f);
    run_s(1.5f);
    int n = 0, nv = 0, pairs = 0;
    double emax = 0.0;
    for (float t_end = t_sim + 3.0f; t_sim < t_end;) {
        set_target(0, 20.0f, 0.0f, ((pairs++ % 40) == 39) ? 3000.0f : 0.0f);
        if (step_pair()) {
            n++;
            if (valid(&alt.out)) { nv++; emax = fmax(emax, fabs(alt.out.altitude_m - truth())); }
        }
    }
    printf("  outliers: valid %d/%d, max error %.2f m\n", nv, n, emax);
    CHECK(nv == n);
    CHECK(emax < 1.6);
}

static void test_dropout(void)
{
    setup(SWEEP_HZ);
    sim_ground(&sim, 30.0f, VH_APPROACH, 0.0f, 0.0f);
    run_s(1.5f);
    CHECK(valid(&alt.out));

    sim.gnd.enabled = 0;                     /* echo gone (e.g. water mirror, steep bank) */
    float t_inv = -1.0f;
    const float t0 = t_sim;
    for (float t_end = t_sim + 2.0f; t_sim < t_end;) {
        if (!valid(step_out()) && (t_inv < 0.0f)) { t_inv = t_sim - t0; }
    }
    printf("  dropout: invalid after %.2f s\n", t_inv);
    CHECK((t_inv > 0.0f) && (t_inv < 1.0f));
    CHECK(!valid(&alt.out));

    sim.gnd.enabled = 1;
    float t_ok = -1.0f;
    const float t1 = t_sim;
    for (float t_end = t_sim + 2.0f; t_sim < t_end;) {
        if (valid(step_out()) && (t_ok < 0.0f)) { t_ok = t_sim - t1; }
    }
    printf("  re-acquired after %.2f s\n", t_ok);
    CHECK((t_ok > 0.0f) && (t_ok < 1.0f));
    CHECK_NEAR(alt.out.altitude_m, 30.0, 1.0);
}

static void test_step_change(void)
{
    /* flying over a building edge: the ground comes 15 m closer */
    setup(SWEEP_HZ);
    sim_ground(&sim, 60.0f, VH_APPROACH, 0.0f, 0.0f);
    run_s(1.5f);
    sim_ground(&sim, 45.0f, VH_APPROACH, 0.0f, 0.0f);
    float t_set = -1.0f;
    const float t0 = t_sim;
    for (float t_end = t_sim + 2.0f; t_sim < t_end;) {
        const alt_output_t *o = step_out();
        if ((t_set < 0.0f) && valid(o) && (fabs(o->altitude_m - 45.0) < 1.0)) { t_set = t_sim - t0; }
    }
    printf("  step 60 -> 45 m settled after %.2f s\n", t_set);
    CHECK((t_set >= 0.0f) && (t_set < 0.6f));
}

static void test_agc_and_clipping(void)
{
    setup(SWEEP_HZ);
    set_target(0, 15.0f, 0.0f, 20000.0f);          /* clips at every gain */
    int nclip = 0, nvalid = 0, n = 0;
    for (float t_end = t_sim + 1.0f; t_sim < t_end;) {
        const alt_output_t *o = step_out();
        n++;
        nclip += (o->status & ALT_ST_CLIPPED) != 0u;
        nvalid += valid(o);
    }
    CHECK(alt.out.gain == 0u);
    CHECK(nclip > (9 * n) / 10);
    CHECK(nvalid == 0);

    /* weak ground: the AGC raises the gain */
    setup(SWEEP_HZ);
    sim_ground(&sim, 150.0f, VH_APPROACH, 0.0f, 5.0f);
    run_s(3.0f);
    printf("  weak ground: gain %u, alt %.2f, snr %.1f dB\n", alt.out.gain, alt.out.altitude_m, alt.out.snr_db);
    CHECK(alt.out.gain >= 2u);
    CHECK(valid(&alt.out));
    CHECK_NEAR(alt.out.altitude_m, 150.0, 3.0);
}

/* approach: 3 m/s sink at 30 m/s ground speed down to 4.5 m */
static void test_landing(void)
{
    setup(SWEEP_HZ);
    sim_ground(&sim, 30.0f, VH_APPROACH, 0.0f, 0.0f);
    run_s(1.5f);
    sim.gnd.vz_mps = -3.0f;
    double emax = 0.0;
    int ninv = 0, n = 0;
    while (sim.gnd.h_m > 4.5f) {
        const alt_output_t *o = step_out();
        if (truth() < 25.0) {
            n++;
            ninv += !valid(o);
            emax = fmax(emax, fabs(o->altitude_m - truth()));
        }
    }
    printf("  landing: max error %.2f m, invalid %d of %d, vs %.2f m/s\n", emax, ninv, n, alt.out.vspeed_mps);
    CHECK(ninv == 0);
    CHECK(emax < 1.2);
    CHECK_NEAR(alt.out.vspeed_mps, -3.0, 0.5);
}

/* flare: the sink rate drops from 3 to 0.8 m/s between 12 and 5 m */
static void test_flare(void)
{
    setup(SWEEP_HZ);
    sim_ground(&sim, 30.0f, VH_APPROACH, -3.0f, 0.0f);
    double emax = 0.0;
    int ninv = 0;
    while (sim.gnd.h_m > 4.8f) {
        const float h = sim.gnd.h_m;
        sim.gnd.vz_mps = (h > 12.0f) ? -3.0f : -(0.8f + 2.2f * (h - 5.0f) / 7.0f);
        const alt_output_t *o = step_out();
        if ((t_sim > 1.5f) && (truth() < 20.0)) {
            ninv += !valid(o);
            emax = fmax(emax, fabs(o->altitude_m - truth()));
        }
    }
    printf("  flare: max error %.2f m, invalid %d\n", emax, ninv);
    CHECK(ninv == 0);
    CHECK(emax < 1.2);
}

static void test_hw_fault(void)
{
    setup(SWEEP_HZ);
    sim_ground(&sim, 30.0f, VH_APPROACH, 0.0f, 0.0f);
    run_s(1.5f);
    CHECK(valid(&alt.out));
    sim.stuck = 1;
    run_s(0.3f);
    CHECK((alt.out.status & ALT_ST_HW_FAULT) != 0u);
    CHECK(!valid(&alt.out));
    sim.stuck = 0;
    run_s(1.5f);
    CHECK((alt.out.status & ALT_ST_HW_FAULT) == 0u);
    CHECK(valid(&alt.out));
}

static void test_vco_sign(void)
{
    setup(SWEEP_HZ);
    cfg.vco_sign = -1;
    alt_apply_config(&alt);
    sim.vco_sign = -1;
    sim_ground(&sim, 80.0f, VH_APPROACH, -6.0f, 5.0f);
    run_s(2.0f);
    double vs = 0.0;
    int n = 0;
    for (float t_end = t_sim + 2.5f; t_sim < t_end;) { vs += step_out()->vspeed_mps; n++; }
    CHECK(valid(&alt.out));
    CHECK_NEAR(alt.out.altitude_m, truth(), 1.6);
    CHECK_NEAR(vs / n, -6.0, 0.5);
}

static void test_background(void)
{
    /* strong reflection of the landing gear at 4.5 m (fixed relative to the
     * radar) and the ground at 35 m */
    setup(SWEEP_HZ);
    cfg.bg_enable = 0u;
    alt_apply_config(&alt);
    set_target(0, 4.5f, 0.0f, 1500.0f);
    sim.tgt[0].fixed = 1;
    sim_ground(&sim, 35.0f, VH_APPROACH, 0.0f, 0.0f);
    run_s(1.5f);
    printf("  without bg: %.2f m (%s)\n", alt.out.altitude_m, valid(&alt.out) ? "valid" : "invalid");
    CHECK(!valid(&alt.out) || (fabs(alt.out.altitude_m - 35.0) > 5.0));

    cfg.bg_enable = 1u;
    alt_apply_config(&alt);
    float t_ok = -1.0f;
    const float t0 = t_sim;
    for (float t_end = t_sim + 2.0f; t_sim < t_end;) {
        const alt_output_t *o = step_out();
        if ((t_ok < 0.0f) && valid(o) && (fabs(o->altitude_m - 35.0) < 1.0)) { t_ok = t_sim - t0; }
    }
    printf("  with running bg: %.2f m after %.2f s\n", alt.out.altitude_m, t_ok);
    CHECK(valid(&alt.out));
    CHECK_NEAR(alt.out.altitude_m, 35.0, 1.0);
    CHECK((t_ok >= 0.0f) && (t_ok < 1.0f));

    /* descent to 7 m with the gear echo */
    double emax = 0.0;
    int ninv = 0;
    while (sim.gnd.h_m > 7.0f) {
        sim.gnd.vz_mps = fmaxf(-3.0f, sim.gnd.vz_mps - 1.5f * RADAR_PAIR_DT_S * ALT_MEAS_PAIRS);
        const alt_output_t *o = step_out();
        ninv += !valid(o);
        emax = fmax(emax, fabs(o->altitude_m - truth()));
    }
    printf("  descent 35 -> 7 m with gear echo: max error %.2f m, invalid %d\n", emax, ninv);
    CHECK(ninv == 0);
    CHECK(emax < 1.5);
}

/* A target in the near range (< ALT_BG_MAX_M) that is fixed relative to the
 * radar is removed by the running background: expected in flight, therefore
 * 'bg off' for static ground tests. */
static void test_background_static_target(void)
{
    setup(SWEEP_HZ);
    set_target(0, 6.0f, 0.0f, 500.0f);
    sim.tgt[0].fixed = 1;
    run_s(2.0f);
    int nvalid = 0, n = 0;
    for (float t_end = t_sim + 1.0f; t_sim < t_end;) { nvalid += valid(step_out()); n++; }
    printf("  static target with bg on: %d valid of %d (expected 0)\n", nvalid, n);
    CHECK(nvalid == 0);
    cfg.bg_enable = 0u;
    alt_apply_config(&alt);
    run_s(1.5f);
    CHECK(valid(&alt.out));
}

static void test_calibration(void)
{
    /* the radar sweeps 110 MHz but the config assumes 90 MHz: 2-point
     * calibration with a reflector (point target), background removal off */
    const float true_sweep = 110.0e6f;
    const float ref[2] = { 10.0f, 40.0f };
    float f[2];
    for (int p = 0; p < 2; p++) {
        setup(true_sweep);
        cfg.bg_enable = 0u;
        alt_apply_config(&alt);
        set_target(0, ref[p], 0.0f, 1500.0f * 3.0f / ref[p]);
        run_s(1.0f);
        alt_cal_start(&alt, 100u);
        float m = 0.0f, sd = 0.0f;
        uint16_t n = 0u;
        int guard = 0;
        while (!alt_cal_result(&alt, &m, &sd, &n) && (guard++ < 400)) { (void)step_out(); }
        CHECK(n == 100u);
        f[p] = m;
    }
    const float slope = RDSP_C0_MPS * (f[1] - f[0]) / (2.0f * (ref[1] - ref[0]));
    const float sweep = slope * (float)(RADAR_FRAME_LEN - 1u) / (float)RADAR_FS_HZ;
    printf("  calibrated sweep %.3f MHz (true 110)\n", sweep * 1e-6);
    CHECK_NEAR(sweep, true_sweep, 0.005 * true_sweep);
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
    o.status = ALT_ST_VALID; o.gain = 2u; o.tau_s = 0.1f;
    const size_t n = proto_ralt(b, sizeof(b), &o);
    CHECK(strcmp(b, "$RALT,7,123.46,-1.50,25.3,2,0001*") == 0 || (n > 0u && strncmp(b, "$RALT,7,123.46,-1.50,25.3,2,0001*", 33) == 0));
    const char *star = strchr(b, '*');
    CHECK(star != NULL);
    if (star != NULL) {
        unsigned cs = (unsigned)strtoul(star + 1, NULL, 16);
        CHECK(cs == proto_nmea_cs(b + 1, (size_t)(star - b - 1)));
    }
    CHECK(proto_rdbg(b, sizeof(b), &o) > 0u);
    CHECK(strstr(b, ",100*") != NULL);          /* averaging time in ms */
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

    static uint16_t t[2u * RADAR_FRAME_LEN];
    const uint32_t n = RADAR_FRAME_LEN;
    ramp_build(t, n, 0u, 2480u, 0.0f);
    CHECK(t[0] == 0u && t[n - 1u] == 2480u);
    CHECK(t[n] == 2480u && t[2u * n - 1u] == 0u);
    int mono = 1;
    for (uint32_t i = 1; i < n; i++) { if (t[i] < t[i - 1u]) { mono = 0; } }
    CHECK(mono);
    ramp_build(t, n, 75u, 4091u, 0.0f);
    CHECK(t[0] == 75u && t[n - 1u] == 4091u);
    CHECK(RADAR_FRAME_LEN == 352u);
}

void test_alt_all(void)
{
    RUN(test_config);
    RUN(test_protocol);
    RUN(test_ground_altitudes);
    RUN(test_range);
    RUN(test_descent_and_climb);
    RUN(test_noise_only);
    RUN(test_multipath);
    RUN(test_outliers);
    RUN(test_dropout);
    RUN(test_step_change);
    RUN(test_agc_and_clipping);
    RUN(test_landing);
    RUN(test_flare);
    RUN(test_hw_fault);
    RUN(test_vco_sign);
    RUN(test_background);
    RUN(test_background_static_target);
    RUN(test_calibration);
}
