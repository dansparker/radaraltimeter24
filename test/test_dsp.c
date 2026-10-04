/**
 * @file test_dsp.c
 * @brief Unit tests of the rdsp library modules.
 */
#include "unit.h"
#include "rdsp/rdsp.h"
#include "sim.h"
#include <string.h>
#include <stdlib.h>

#define PI_D 3.14159265358979323846

static float tw[4096], buf[4096], ref_re[2049], ref_im[2049], w[4096];

static void test_fft_vs_dft(void)
{
    const uint32_t sizes[] = { 8u, 64u, 2048u };
    for (unsigned s = 0; s < 3u; s++) {
        const uint32_t n = sizes[s];
        rdsp_rfft_t f;
        CHECK(rdsp_rfft_init(&f, n, tw) == 0);
        uint32_t r = 1u;
        for (uint32_t i = 0; i < n; i++) { r = r * 1103515245u + 12345u; buf[i] = (float)((r >> 16) & 0x7FFF) / 16384.0f - 1.0f; }
        double scale = 0.0;
        for (uint32_t k = 0; k <= n / 2u; k++) {
            double re = 0.0, im = 0.0;
            for (uint32_t i = 0; i < n; i++) {
                const double a = -2.0 * PI_D * (double)k * (double)i / (double)n;
                re += buf[i] * cos(a);
                im += buf[i] * sin(a);
            }
            ref_re[k] = (float)re; ref_im[k] = (float)im;
            scale = fmax(scale, sqrt(re * re + im * im));
        }
        rdsp_rfft(&f, buf);
        double err = fabs(buf[0] - ref_re[0]) + fabs(buf[1] - ref_re[n / 2u]);
        for (uint32_t k = 1; k < n / 2u; k++) {
            err = fmax(err, fabs(buf[2 * k] - ref_re[k]) + fabs(buf[2 * k + 1] - ref_im[k]));
        }
        CHECK(err / scale < 2e-5);
    }
    rdsp_rfft_t f;
    CHECK(rdsp_rfft_init(&f, 1000u, tw) != 0);
}

static void test_window(void)
{
    rdsp_win_info_t wi;
    CHECK(rdsp_window_make(w, 2048u, RDSP_WIN_HANN, &wi) == 0);
    CHECK_NEAR(wi.cg, 0.5, 1e-4);
    CHECK_NEAR(wi.enbw_bins, 1.5, 1e-3);
    CHECK_NEAR(w[0], 0.0, 1e-7);
    CHECK_NEAR(w[1024], 1.0, 1e-6);
    CHECK(rdsp_window_make(w, 2048u, RDSP_WIN_BLACKMAN_HARRIS, &wi) == 0);
    CHECK_NEAR(wi.enbw_bins, 2.0, 0.01);
}

/* one-sided PSD of white noise sigma^2 is 2*sigma^2/fs */
static void test_psd_white_noise(void)
{
    const uint32_t n = 1024u;
    const float fs = 1000.0f;
    static float x[16384], psd[512], acc[512];
    rdsp_rfft_t f;
    rdsp_win_info_t wi;
    sim_t s;
    sim_init(&s, 90e6f);
    rdsp_rfft_init(&f, n, tw);
    rdsp_window_make(w, n, RDSP_WIN_HANN, &wi);
    for (uint32_t i = 0; i < 16384u; i++) { x[i] = 2.0f * sim_randn(&s); }

    rdsp_welch_t we = { &f, w, wi.s2, n, n / 2u, fs, buf };
    const uint32_t segs = rdsp_welch(&we, x, 16384u, psd);
    CHECK(segs == 31u);
    double m = 0.0;
    for (uint32_t k = 10; k < 500u; k++) { m += psd[k]; }
    m /= 490.0;
    CHECK_NEAR(m, 2.0 * 4.0 / fs, 0.05 * 8.0 / fs);

    /* averaging across frames converges to the same level */
    rdsp_psd_avg_t av;
    rdsp_psd_avg_init(&av, acc, 512u, 0.1f);
    for (uint32_t fr = 0; fr < 16u; fr++) {
        memcpy(buf, &x[fr * n], n * sizeof(float));
        rdsp_window_apply(buf, w, n);
        rdsp_rfft(&f, buf);
        rdsp_rfft_power(buf, buf, n);
        rdsp_psd_onesided(buf, 512u, fs, wi.s2);
        rdsp_psd_avg_update(&av, buf);
    }
    m = 0.0;
    for (uint32_t k = 10; k < 500u; k++) { m += acc[k]; }
    m /= 490.0;
    CHECK_NEAR(m, 8.0 / fs, 0.1 * 8.0 / fs);
}

static void test_cfar(void)
{
    rdsp_cfar_t c;
    /* CA closed form: N=24, Pfa=1e-4 */
    CHECK_NEAR(rdsp_cfar_alpha_ca(24u, 1e-4f), 24.0 * (pow(1e-4, -1.0 / 24.0) - 1.0), 1e-3);
    CHECK(rdsp_cfar_init(&c, RDSP_CFAR_OS, 12u, 3u, 0u, 1e-2f) == 0);
    CHECK(c.os_k == 18u);

    /* empirical false alarm rate on exponential noise (local maxima only,
     * so the measured rate must be <= design) */
    static float p[1024];
    sim_t s;
    sim_init(&s, 90e6f);
    rdsp_det_t det[64];
    uint32_t fa = 0u, cells = 0u;
    for (int trial = 0; trial < 200; trial++) {
        for (uint32_t i = 0; i < 1024u; i++) {
            const float a = sim_randn(&s), b = sim_randn(&s);
            p[i] = 0.5f * (a * a + b * b);
        }
        fa += rdsp_cfar_detect(&c, p, 1024u, 20u, 1003u, det, 64u);
        cells += 984u;
    }
    const double pfa = (double)fa / (double)cells;
    printf("  OS-CFAR measured Pfa %.4f (design 0.01)\n", pfa);
    CHECK(pfa < 0.01);
    CHECK(pfa > 0.0005);

    /* single target 20 dB above noise is found at the right bin */
    for (uint32_t i = 0; i < 1024u; i++) { p[i] = 1.0f; }
    p[300] = 100.0f; p[299] = 30.0f; p[301] = 25.0f;
    uint32_t nd = rdsp_cfar_detect(&c, p, 1024u, 2u, 1000u, det, 8u);
    CHECK(nd == 1u);
    CHECK(det[0].bin == 300u);
    CHECK_NEAR(det[0].snr_db, 20.0, 0.1);

    /* two close targets are both detected by OS-CFAR */
    p[310] = 80.0f;
    nd = rdsp_cfar_detect(&c, p, 1024u, 2u, 1000u, det, 8u);
    CHECK(nd == 2u);

    /* max_det keeps the strongest, sorted by bin */
    p[100] = 50.0f; p[500] = 200.0f;
    nd = rdsp_cfar_detect(&c, p, 1024u, 2u, 1000u, det, 2u);
    CHECK(nd == 2u);
    CHECK((det[0].bin == 300u) && (det[1].bin == 500u));

    /* NaN never produces a detection */
    p[600] = NAN;
    nd = rdsp_cfar_detect(&c, p, 1024u, 590u, 610u, det, 8u);
    CHECK(nd == 0u);
}

static void test_peak_interp(void)
{
    const uint32_t n = 2048u;
    rdsp_rfft_t f;
    rdsp_win_info_t wi;
    static float p[1024];
    rdsp_rfft_init(&f, n, tw);
    rdsp_window_make(w, n, RDSP_WIN_HANN, &wi);
    double emax_hann = 0.0, emax_gauss = 0.0;
    for (int j = -10; j <= 10; j++) {
        const double k0 = 100.0 + 0.05 * j;
        for (uint32_t i = 0; i < n; i++) { buf[i] = (float)cos(2.0 * PI_D * k0 * i / n + 0.3) * w[i]; }
        rdsp_rfft(&f, buf);
        rdsp_rfft_power(buf, p, n);
        uint32_t im = 95u;
        for (uint32_t i = 95u; i < 106u; i++) { if (p[i] > p[im]) { im = i; } }
        const double eh = fabs(im + rdsp_peak_interp(p, 1024u, im, RDSP_INTERP_HANN) - k0);
        const double eg = fabs(im + rdsp_peak_interp(p, 1024u, im, RDSP_INTERP_GAUSSIAN) - k0);
        emax_hann = fmax(emax_hann, eh);
        emax_gauss = fmax(emax_gauss, eg);
    }
    printf("  max interpolation error: Hann %.4f bin, Gaussian %.4f bin\n", emax_hann, emax_gauss);
    CHECK(emax_hann < 0.005);
    CHECK(emax_gauss < 0.06);
    CHECK(rdsp_peak_interp(p, 1024u, 0u, RDSP_INTERP_HANN) == 0.0f);
}

static void test_fmcw(void)
{
    rdsp_fmcw_t f = { 1.0e10f, 0.0124f, 0.5f };
    CHECK_NEAR(rdsp_fmcw_range(&f, rdsp_fmcw_beat(&f, 100.0f)), 100.0, 1e-3);
    float fr, fd;
    /* normal */
    CHECK(rdsp_fmcw_updown(900.0f, 1100.0f, RDSP_UD_NORMAL, &fr, &fd) == 0);
    CHECK_NEAR(fr, 1000.0, 1e-3); CHECK_NEAR(fd, 100.0, 1e-3);
    /* up mirrored: fR=300, fD=500 -> up |300-500|=200, down 800 */
    CHECK(rdsp_fmcw_updown(200.0f, 800.0f, RDSP_UD_NORMAL, &fr, &fd) == 0);   /* ambiguous: 500/300 */
    CHECK(rdsp_fmcw_updown(200.0f, 800.0f, RDSP_UD_UP_MIRRORED, &fr, &fd) == 0);
    CHECK_NEAR(fr, 300.0, 1e-3); CHECK_NEAR(fd, 500.0, 1e-3);
    /* down mirrored: fR=300, fD=-500 -> up 800, down |300-500|=200 */
    CHECK(rdsp_fmcw_updown(800.0f, 200.0f, RDSP_UD_DOWN_MIRRORED, &fr, &fd) == 0);
    CHECK_NEAR(fr, 300.0, 1e-3); CHECK_NEAR(fd, -500.0, 1e-3);
    /* (900, 1100) is also explained by fR=100, fD=1000 (mirrored): only the
     * tracker / Doppler limit can resolve this ambiguity */
    CHECK(rdsp_fmcw_updown(900.0f, 1100.0f, RDSP_UD_UP_MIRRORED, &fr, &fd) == 0);
    CHECK_NEAR(fr, 100.0, 1e-3); CHECK_NEAR(fd, 1000.0, 1e-3);
    CHECK(rdsp_fmcw_updown(900.0f, 1100.0f, RDSP_UD_DOWN_MIRRORED, &fr, &fd) != 0);
}

static void test_tracker(void)
{
    rdsp_track_cfg_t cfg = { 0.4f, 0.05f, 0.0f, 1.5f, 0.03f, 30.0f, 3u, 20u, 4u };
    rdsp_track_t t;
    rdsp_track_init(&t, &cfg);
    const float dt = 0.016f;
    float x = 100.0f;
    for (int i = 0; i < 300; i++) {
        x += -4.0f * dt;
        rdsp_track_update(&t, 1, x, NAN, dt);
        if (i == 2) { CHECK(t.state == RDSP_TRK_CONFIRMED); }
    }
    CHECK_NEAR(t.x, x, 0.05);
    CHECK_NEAR(t.v, -4.0, 0.1);

    /* single outlier rejected */
    x += -4.0f * dt;
    CHECK(rdsp_track_update(&t, 1, 150.0f, NAN, dt) == 0);
    CHECK_NEAR(t.x, x, 0.1);
    CHECK(t.state == RDSP_TRK_COAST);
    x += -4.0f * dt;
    rdsp_track_update(&t, 1, x, NAN, dt);
    CHECK(t.state == RDSP_TRK_CONFIRMED);

    /* real jump: re-acquired after reacq_n consistent measurements */
    for (int i = 0; i < 4; i++) { rdsp_track_update(&t, 1, 40.0f, NAN, dt); }
    CHECK(t.state == RDSP_TRK_CONFIRMED);
    CHECK_NEAR(t.x, 40.0, 1e-3);

    /* coasting and loss */
    for (int i = 0; i < 20; i++) { rdsp_track_update(&t, 0, 0.0f, NAN, dt); }
    CHECK(t.state == RDSP_TRK_COAST);
    rdsp_track_update(&t, 0, 0.0f, NAN, dt);
    CHECK(t.state == RDSP_TRK_LOST);

    /* NaN measurement counts as a miss */
    rdsp_track_reset(&t);
    CHECK(rdsp_track_update(&t, 1, NAN, NAN, dt) == 0);
    CHECK(t.state == RDSP_TRK_LOST);
}

static void test_agc(void)
{
    rdsp_agc_t a;
    rdsp_agc_init(&a, 4u, 0u, 0.85f, 0.2f, 5u);
    for (int i = 0; i < 4; i++) { rdsp_agc_update(&a, 0.1f, 0); }
    CHECK(a.level == 0u);
    CHECK(rdsp_agc_update(&a, 0.1f, 0) == 1);
    CHECK(a.level == 1u);
    CHECK(rdsp_agc_update(&a, 0.5f, 1) == 1);  /* clipping: immediate */
    CHECK(a.level == 0u);
    CHECK(rdsp_agc_update(&a, 2.0f, 0) == 0);  /* already lowest */
    CHECK(rdsp_agc_update(&a, NAN, 0) == 0);
    CHECK(a.level == 0u);
}

static void test_frame(void)
{
    static uint16_t raw[100];
    static float x[100];
    rdsp_frame_stats_t st;
    for (int i = 0; i < 100; i++) { raw[i] = (uint16_t)(1000 + 5 * i); }
    raw[10] = 4095u;
    rdsp_frame_condition_u16(raw, 100u, x, 8u, 4087u, 4095u, &st);
    CHECK(st.n_clip == 1u);
    CHECK(st.max == 4095u);
    raw[10] = 1050u;
    rdsp_frame_condition_u16(raw, 100u, x, 8u, 4087u, 4095u, &st);
    CHECK_NEAR(st.mean, 1247.5, 1e-3);
    rdsp_frame_detrend(x, 100u);
    double m = 0.0;
    for (int i = 0; i < 100; i++) { m = fmax(m, fabs(x[i])); }
    CHECK(m < 1e-3);
    x[5] = NAN;
    CHECK(rdsp_all_finite(x, 100u) == 0);
}

void test_dsp_all(void)
{
    RUN(test_fft_vs_dft);
    RUN(test_window);
    RUN(test_psd_white_noise);
    RUN(test_cfar);
    RUN(test_peak_interp);
    RUN(test_fmcw);
    RUN(test_tracker);
    RUN(test_agc);
    RUN(test_frame);
}
