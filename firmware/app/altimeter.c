/**
 * @file altimeter.c
 * @brief Radar altimeter application pipeline (see altimeter.h).
 */
#include "altimeter.h"
#include "ramp.h"
#include <math.h>
#include <string.h>

#ifndef ALT_CCM
#define ALT_CCM   /* placed in CCM RAM on the target if available (defined by the build) */
#endif

static ALT_CCM float s_tw[RADAR_NFFT];
static ALT_CCM float s_win[RADAR_NSAMP];
static ALT_CCM float s_work[RADAR_NFFT];
static ALT_CCM float s_avg[2][RADAR_NBINS];          /* averaged PSD per physical direction */
/* running background of bins 1 .. ALT_BG_BINS_MAX-1: complex mean and power */
static float s_bg_m[RADAR_NGAIN][2][2u * (ALT_BG_BINS_MAX - 1u)];
static float s_bg_p[RADAR_NGAIN][2][ALT_BG_BINS_MAX - 1u];

/* result of one ramp direction in a measurement cycle */
typedef struct {
    int ok;
    float f;            /**< edge (or peak in calibration) [Hz] */
    float pos;          /**< same in bins */
    float snr_db;
} dir_meas_t;

typedef struct {
    float range_m;
    float f_r;
    float closing;      /**< closing speed from Doppler [m/s] */
    float snr_db;
    uint8_t degraded;
    uint8_t single;     /**< one direction + predicted Doppler (depends on the track rate) */
} meas_t;

/* ------------------------------------------------------------------------ */

static int tracking(const altimeter_t *a)
{
    return (a->trk.state == RDSP_TRK_CONFIRMED) || (a->trk.state == RDSP_TRK_COAST);
}

static void reset_dynamic(altimeter_t *a)
{
    for (unsigned d = 0; d < 2u; d++) {
        a->dir[d].n = 0u;
        a->dir[d].gain = 0xFFu;
        a->dir[d].clip = 0u;
        a->dir[d].fresh = 0u;
    }
    a->pairs_in_cycle = 0u;
    a->tau = ALT_AVG_TAU_MAX_S;
    a->vz_dopp = 0.0f;
    a->vz_dopp_ok = 0u;
    rdsp_track_reset(&a->trk);
}

void alt_apply_config(altimeter_t *a)
{
    const config_t *c = a->cfg;
    const cfg_module_t *m = cfg_active_module(c);

    a->fm.slope_hz_s = ramp_slope_hz_s(m->sweep_hz, RADAR_FRAME_LEN, RADAR_FS_HZ);
    a->fm.lambda_m = RADAR_LAMBDA_M;
    a->fm.r_offset_m = m->r_offset_m;
    a->bin_hz = (float)RADAR_FS_HZ / (float)RADAR_NFFT;
    a->fd_max_hz = rdsp_fmcw_doppler(&a->fm, ALT_V_MAX_MPS);
    float fmax = rdsp_fmcw_beat(&a->fm, c->max_range_m) + a->fd_max_hz;
    if (fmax < 0.0f) { fmax = 0.0f; }
    uint32_t bmax = (uint32_t)(fmax / a->bin_hz) + ALT_EDGE_SPAN + 1u;
    if (bmax > RADAR_NBINS - 2u) { bmax = RADAR_NBINS - 2u; }
    a->bin_max = (uint16_t)bmax;
    float bgb = rdsp_fmcw_beat(&a->fm, ALT_BG_MAX_M) / a->bin_hz + 3.0f;   /* + Hann main lobe */
    if (!(bgb >= 2.0f)) { bgb = 2.0f; }
    if (bgb > (float)ALT_BG_BINS_MAX) { bgb = (float)ALT_BG_BINS_MAX; }
    a->bg_bins = (uint16_t)bgb;
    memset(a->bg_n, 0, sizeof(a->bg_n));     /* new parameters: rebuild the background */

    a->meas_dt = (float)ALT_MEAS_PAIRS * RADAR_PAIR_DT_S;
    a->trk.cfg.max_coast = (uint16_t)(ALT_TRK_MAX_COAST_S / a->meas_dt);
    a->coast_valid = (uint16_t)(ALT_COAST_VALID_S / a->meas_dt);
    a->uncal = (m->calibrated != 0u) ? 0u : 1u;
    a->gain_req = (c->gain_mode == CFG_GAIN_AUTO) ? a->agc.level : c->gain_mode;
    reset_dynamic(a);
}

void alt_init(altimeter_t *a, const config_t *cfg)
{
    memset(a, 0, sizeof(*a));
    a->cfg = cfg;
    (void)rdsp_rfft_init(&a->fft, RADAR_NFFT, s_tw);
    (void)rdsp_window_make(s_win, RADAR_NSAMP, ALT_WINDOW, &a->wi);
    rdsp_agc_init(&a->agc, RADAR_NGAIN, 0u, ALT_AGC_HI, ALT_AGC_LO,
                  (uint16_t)(ALT_AGC_UP_S / RADAR_FRAME_DT_S));
    a->agc.attack_frames = ALT_AGC_ATTACK;

    const rdsp_track_cfg_t tc = {
        .alpha = ALT_TRK_ALPHA, .beta = ALT_TRK_BETA, .gamma = ALT_TRK_GAMMA,
        .gate_abs = ALT_TRK_GATE_ABS, .gate_rel = ALT_TRK_GATE_REL,
        .v_max = ALT_V_MAX_MPS, .confirm_n = ALT_TRK_CONFIRM,
        .max_coast = 60u, .reacq_n = ALT_TRK_REACQ
    };
    rdsp_track_init(&a->trk, &tc);
    a->edge.win = ALT_EDGE_WIN;
    a->edge.guard = ALT_EDGE_GUARD;
    a->edge.min_ref = ALT_EDGE_MIN_REF;
    a->edge.confirm = ALT_EDGE_CONFIRM;
    a->edge.span = ALT_EDGE_SPAN;
    a->edge.floor_frac = ALT_EDGE_FLOOR;
    a->edge.thr = 2.0f;
    a->mode = ALT_MODE_RUN;
    a->out.raw_range_m = NAN;
    alt_apply_config(a);
}

uint8_t alt_gain_request(const altimeter_t *a)
{
    return a->gain_req;
}

void alt_set_ext_status(altimeter_t *a, uint16_t bits)
{
    a->ext_status = bits;
}

/* ---- running background ------------------------------------------------ */

/* @return 0 while the estimate is still warming up (frame not cleaned) */
/* Remove the echoes that are fixed relative to the aircraft (leakage,
 * radome, gear) from the complex spectrum X (packed rfft output) of the near
 * range, see rdsp_clutter_update(): the mean is subtracted weighted with the
 * phase coherence, so the moving ground echo stays untouched even when it is
 * inside the band (final approach) or the estimate is new. One estimate per
 * gain / direction; warm-up ALT_BG_WARMUP frames before subtracting. */
static int bg_running(altimeter_t *a, float *X, const alt_frame_info_t *fi)
{
    const uint32_t d = fi->dac_dir ? 1u : 0u;
    const uint32_t g = fi->gain & (RADAR_NGAIN - 1u);
    uint16_t *n = &a->bg_n[g][d];
    float beta = 1.0f / (float)(*n + 1u);
    const float beta_ss = RADAR_PAIR_DT_S / ALT_BG_TAU_S;
    if (beta < beta_ss) { beta = beta_ss; }
    /* X[0] = DC, X[1] = Nyquist, then re/im of bin 1, 2, ...; the estimate starts at bin 1 */
    const int cleaned = (*n >= ALT_BG_WARMUP);
    rdsp_clutter_update(&X[2], s_bg_m[g][d], s_bg_p[g][d], a->bg_bins - 1u, beta, cleaned);
    if (*n < 0xFFFFu) { (*n)++; }
    return cleaned;
}

/* ---- calibration -------------------------------------------------------- */

void alt_cal_start(altimeter_t *a, uint16_t n)
{
    a->cal_target = (n == 0u) ? 1u : n;
    a->cal_n = 0u;
    a->cal_sum = 0.0;
    a->cal_sum2 = 0.0;
    a->mode = ALT_MODE_CAL;
}

int alt_cal_result(const altimeter_t *a, float *mean_hz, float *std_hz, uint16_t *n)
{
    if ((a->cal_target == 0u) || (a->cal_n < a->cal_target)) { return 0; }
    const double m = a->cal_sum / (double)a->cal_n;
    double var = a->cal_sum2 / (double)a->cal_n - m * m;
    if (var < 0.0) { var = 0.0; }
    *mean_hz = (float)m;
    *std_hz = (float)sqrt(var);
    *n = a->cal_n;
    return 1;
}

void alt_cal_abort(altimeter_t *a)
{
    a->cal_target = 0u;
    if (a->mode == ALT_MODE_CAL) { a->mode = ALT_MODE_RUN; }
}

/* ---- per frame ---------------------------------------------------------- */

static void process_frame(altimeter_t *a, const uint16_t *frame, const alt_frame_info_t *fi, uint8_t pd)
{
    const config_t *c = a->cfg;
    float *x = s_work;
    rdsp_frame_stats_t s;

    rdsp_frame_condition_u16(frame + RADAR_HEAD, RADAR_NSAMP, x,
                             RADAR_ADC_CLIP_LO, RADAR_ADC_CLIP_HI, RADAR_ADC_MAX, &s);

    /* health: frozen data or railed bias point */
    const int bad = (s.rms < ALT_FAULT_RMS_MIN) || (s.mean < ALT_FAULT_MEAN_LO) ||
                    (s.mean > ALT_FAULT_MEAN_HI);
    if (bad) {
        a->good_cnt = 0u;
        if (++a->fault_cnt >= ALT_FAULT_FRAMES) { a->hw_fault = 1u; a->fault_cnt = ALT_FAULT_FRAMES; }
        return;
    }
    a->fault_cnt = 0u;
    if (++a->good_cnt >= ALT_FAULT_FRAMES) { a->hw_fault = 0u; a->good_cnt = ALT_FAULT_FRAMES; }

    /* gain control; only frames taken with the currently requested gain count */
    const int clipped = (s.n_clip > ALT_CLIP_MAX) ? 1 : 0;
    a->last_gain = fi->gain;
    if (c->gain_mode == CFG_GAIN_AUTO) {
        if ((fi->gain == a->agc.level) && rdsp_agc_update(&a->agc, s.peak_frac, clipped)) {
            a->gain_changed = 1u;
            a->st.gain_changes++;
        }
        a->gain_req = a->agc.level;
    } else {
        a->gain_req = c->gain_mode;
    }
    if (clipped) {
        a->dir[pd].clip = 1u;
        a->st.clipped++;
        return;
    }
    if (c->detrend) { rdsp_frame_detrend(x, RADAR_NSAMP); }

    /* spectrum: window over the samples, zero padding to RADAR_NFFT */
    rdsp_window_apply(x, s_win, RADAR_NSAMP);
    for (uint32_t i = RADAR_NSAMP; i < RADAR_NFFT; i++) { x[i] = 0.0f; }
    rdsp_rfft(&a->fft, x);
    /* not during calibration: the reference target is static */
    if (c->bg_enable && (a->mode == ALT_MODE_RUN) && !bg_running(a, x, fi)) {
        return;                                      /* fixed echoes not yet removed: not averaged */
    }
    rdsp_rfft_power(x, x, RADAR_NFFT);
    rdsp_psd_onesided(x, RADAR_NBINS, (float)RADAR_FS_HZ, a->wi.s2);
    if (!rdsp_all_finite(x, RADAR_NBINS)) {
        a->st.nonfinite++;
        return;
    }

    /* robust average per physical direction; never mix gains */
    alt_dir_state_t *ds = &a->dir[pd];
    if (ds->gain != fi->gain) {
        ds->gain = fi->gain;
        ds->n = 0u;
    }
    if (ds->n == 0u) {
        memcpy(s_avg[pd], x, RADAR_NBINS * sizeof(float));
    } else {
        float beta = 1.0f / (float)(ds->n + 1u);
        const float beta_ss = RADAR_PAIR_DT_S / a->tau;
        if (beta < beta_ss) { beta = beta_ss; }
        rdsp_psd_avg_robust(s_avg[pd], x, RADAR_NBINS, beta, ALT_AVG_CLIP);
    }
    if (ds->n < 0xFFFFu) { ds->n++; }
    ds->fresh = 1u;
}

/* ---- per measurement cycle ---------------------------------------------- */

/* systematic offset of the edge point (see ALT_EDGE_CORR_*) */
static float edge_corr(float r)
{
    static const float th[] = ALT_EDGE_CORR_H;
    static const float tc[] = ALT_EDGE_CORR_M;
    const uint32_t n = sizeof(th) / sizeof(th[0]);
    if (!(r > th[0])) { return tc[0]; }
    for (uint32_t i = 1u; i < n; i++) {
        if (r <= th[i]) { return tc[i - 1u] + (tc[i] - tc[i - 1u]) * (r - th[i - 1u]) / (th[i] - th[i - 1u]); }
    }
    return tc[n - 1u];
}

/* frames averaged effectively (cumulative mean first, then exponential) */
static float avg_frames(const altimeter_t *a, uint16_t n)
{
    const float alpha = RADAR_PAIR_DT_S / a->tau;
    const float k_exp = (2.0f - alpha) / alpha;
    return RDSP_MIN((float)n, k_exp);
}

/* age of the averaged spectrum (median of the weights) [s] */
static float avg_lag(const altimeter_t *a, uint16_t n)
{
    const float alpha = RADAR_PAIR_DT_S / a->tau;
    const float pairs = ((float)n < 1.0f / alpha) ? 0.5f * (float)n : 0.6931f / alpha;
    return pairs * RADAR_PAIR_DT_S;
}

static void detect_dir(altimeter_t *a, uint8_t pd, dir_meas_t *m)
{
    const alt_dir_state_t *ds = &a->dir[pd];
    const float *p = s_avg[pd];
    m->ok = 0;
    if (!ds->fresh || (ds->n < ALT_AVG_MIN_N)) { return; }
    const float ng = rdsp_median(p, (3u * RADAR_NBINS) / 4u, RADAR_NBINS - 1u);

    if (a->mode == ALT_MODE_CAL) {
        /* point reflector: strongest peak */
        uint32_t b = ALT_MIN_BIN + 1u;
        for (uint32_t i = b + 1u; i <= a->bin_max; i++) { if (p[i] > p[b]) { b = i; } }
        const float snr = 10.0f * log10f(RDSP_MAX(p[b], RDSP_EPS) / RDSP_MAX(ng, RDSP_EPS));
        if (snr < 10.0f) { return; }
        m->pos = (float)b + rdsp_peak_interp(p, RADAR_NBINS, b, ALT_INTERP);
        m->snr_db = snr;
    } else {
        rdsp_edge_cfg_t ec = a->edge;
        ec.thr = 1.0f + ALT_EDGE_SIGMA / sqrtf(avg_frames(a, ds->n));
        if (ec.thr < ALT_EDGE_THR_MIN) { ec.thr = ALT_EDGE_THR_MIN; }
        rdsp_edge_t e;
        if (!rdsp_edge_detect(&ec, p, RADAR_NBINS, ALT_MIN_BIN, a->bin_max, ng, &e)) { return; }
        if (e.snr_db < ALT_EDGE_MIN_SNR_DB) { return; }
        m->pos = e.pos;
        m->snr_db = e.snr_db;
    }
    m->f = m->pos * a->bin_hz;
    m->ok = 1;
}

static int combine(const altimeter_t *a, const dir_meas_t *u, const dir_meas_t *d, meas_t *m)
{
    const rdsp_fmcw_t *fm = &a->fm;
    float fr, fd;
    m->degraded = 0u;
    m->single = 0u;
    if (u->ok && d->ok) {
        /* rising frequency ramp: fR - fD, falling: fR + fD */
        fr = 0.5f * (u->f + d->f);
        fd = 0.5f * (d->f - u->f);
        if (fabsf(fd) > a->fd_max_hz) { return 0; }
        m->snr_db = RDSP_MIN(u->snr_db, d->snr_db);
        m->degraded = (uint8_t)(RDSP_MIN(u->pos, d->pos) < ALT_RELIABLE_BIN);
    } else if ((u->ok || d->ok) && tracking(a) && (a->mode == ALT_MODE_RUN)) {
        fd = rdsp_fmcw_doppler(fm, -a->trk.v);          /* predicted */
        fr = u->ok ? (u->f + fd) : (d->f - fd);
        m->snr_db = u->ok ? u->snr_db : d->snr_db;
        m->degraded = 1u;
        m->single = 1u;
    } else {
        return 0;
    }
    m->f_r = fr;
    m->range_m = rdsp_fmcw_range(fm, fr);
    m->closing = rdsp_fmcw_closing_speed(fm, fd);
    return 1;
}

static int finish_cycle(altimeter_t *a)
{
    alt_output_t *o = &a->out;
    const config_t *c = a->cfg;
    const uint8_t pr = ALT_DIR_RISE, pf = ALT_DIR_FALL;
    dir_meas_t mu, md;
    meas_t m = { 0 };
    uint16_t st = 0u;
    int have = 0;

    a->st.cycles++;
    mu.ok = md.ok = 0;
    if (!a->hw_fault) {
        detect_dir(a, pr, &mu);
        detect_dir(a, pf, &md);
        have = combine(a, &mu, &md, &m);
        if (have) {
            /* The averaged spectrum shows the past: move it to "now" with the
             * smoothed Doppler rate. Not with the tracker rate: that is derived
             * from these measurements (feedback loop, unstable at weak echoes). */
            if (!m.degraded && (fabsf(m.closing) <= ALT_V_MAX_MPS)) {
                const float k = a->vz_dopp_ok ? (a->meas_dt / ALT_VZ_SMOOTH_S) : 1.0f;
                a->vz_dopp += k * (-m.closing - a->vz_dopp);
                a->vz_dopp_ok = 1u;
            }
            const uint16_t n = RDSP_MIN(a->dir[pr].n, a->dir[pf].n);
            if (a->mode == ALT_MODE_RUN) {
                m.range_m += (a->vz_dopp_ok ? a->vz_dopp : 0.0f) * avg_lag(a, n) + edge_corr(m.range_m);
            }
            if (!isfinite(m.range_m) || (m.range_m > 1.1f * c->max_range_m)) { have = 0; }
            /* one direction only: a weak, unconfirmed edge - it may refine the
             * track, but never start or move it (no re-acquisition) */
            if (have && m.single &&
                (fabsf(m.range_m - rdsp_track_predict(&a->trk, a->meas_dt)) >
                 rdsp_track_gate(&a->trk, a->meas_dt))) {
                have = 0;
            }
        }
    }
    if (a->dir[pr].clip || a->dir[pf].clip) { st |= ALT_ST_CLIPPED; }

    /* range rate for the tracker: this cycle's Doppler if strong, else the smoothed one */
    float vz = NAN;
    if (have && !m.degraded && (m.snr_db >= ALT_VZ_MIN_SNR_DB)) { vz = -m.closing; }
    else if (have && a->vz_dopp_ok) { vz = a->vz_dopp; }
    if (tracking(a) && (fabsf(vz - a->trk.v) > ALT_V_GATE_MPS)) { vz = NAN; }   /* implausible */
    if (a->mode == ALT_MODE_CAL) {
        if (have && !m.single) {
            a->cal_sum += (double)m.f_r;
            a->cal_sum2 += (double)m.f_r * (double)m.f_r;
            if (++a->cal_n >= a->cal_target) { a->mode = ALT_MODE_RUN; }
        }
    } else if (have && m.single) {
        (void)rdsp_track_update_pos(&a->trk, m.range_m, a->meas_dt);
    } else {
        (void)rdsp_track_update(&a->trk, have, have ? m.range_m : 0.0f, vz, a->meas_dt);
    }
    if (!have) {
        st |= ALT_ST_NO_TARGET;
        a->st.no_meas++;
    }

    const rdsp_trk_state_t ts = a->trk.state;
    if (ts == RDSP_TRK_COAST) { st |= ALT_ST_COAST; }
    if (have && m.degraded) { st |= ALT_ST_DEGRADED; }
    if (a->hw_fault) { st |= ALT_ST_HW_FAULT; }
    if (a->uncal) { st |= ALT_ST_UNCAL; }
    if (a->ovr_hold > 0u) { st |= ALT_ST_OVERRUN; a->ovr_hold--; }
    if (a->mode != ALT_MODE_RUN) { st |= ALT_ST_BUSY; }
    if (a->gain_changed) { st |= ALT_ST_GAIN_CHG; a->gain_changed = 0u; }
    st |= a->ext_status;

    const int trk_ok = (ts == RDSP_TRK_CONFIRMED) ||
                       ((ts == RDSP_TRK_COAST) && (a->trk.misses <= a->coast_valid));
    if (trk_ok && !a->hw_fault && (a->mode == ALT_MODE_RUN) && isfinite(a->trk.x) &&
        (a->trk.x <= 1.1f * c->max_range_m)) {
        st |= ALT_ST_VALID;
    }

    /* averaging time for the next cycle: short when low (strong, fast) */
    float tau = ALT_AVG_TAU_MAX_S;
    if (have) { a->last_snr_db = m.snr_db; }
    if (((st & ALT_ST_VALID) != 0u) && (a->last_snr_db >= ALT_AVG_STRONG_DB)) {
        tau = ALT_AVG_TAU_MIN_S + ALT_AVG_TAU_PER_M * a->trk.x;
        if (tau > ALT_AVG_TAU_MAX_S) { tau = ALT_AVG_TAU_MAX_S; }
        if (tau < ALT_AVG_TAU_MIN_S) { tau = ALT_AVG_TAU_MIN_S; }
    }
    a->tau = tau;

    o->seq++;
    o->altitude_m = a->trk.x;
    o->vspeed_mps = a->trk.v;
    o->raw_range_m = have ? m.range_m : NAN;
    o->f_rise_hz = mu.ok ? mu.f : NAN;
    o->f_fall_hz = md.ok ? md.f : NAN;
    o->f_r_hz = have ? m.f_r : NAN;
    o->snr_db = have ? m.snr_db : 0.0f;
    o->tau_s = a->tau;
    o->status = st;
    o->gain = a->last_gain & 3u;
    o->track_state = (uint8_t)ts;

    for (unsigned d = 0; d < 2u; d++) {
        a->dir[d].clip = 0u;
        a->dir[d].fresh = 0u;
    }
    return 1;
}

int alt_process_frame(altimeter_t *a, const uint16_t *frame, const alt_frame_info_t *fi)
{
    const uint8_t dac_dir = fi->dac_dir ? 1u : 0u;
    /* physical direction: with a falling VCO characteristic the DAC-rising
     * ramp is the falling frequency ramp */
    const uint8_t pd = (a->cfg->vco_sign < 0) ? (uint8_t)(1u - dac_dir) : dac_dir;

    a->st.frames++;
    if (fi->overrun) {
        a->st.overruns++;
        a->ovr_hold = 8u;
    }
    if (fi->settling) { a->st.settling++; }
    if (!(fi->settling || fi->overrun || (frame == NULL))) {
        process_frame(a, frame, fi, pd);
    }
    if (dac_dir == 1u) {
        a->st.pairs++;
        if (++a->pairs_in_cycle >= ALT_MEAS_PAIRS) {
            a->pairs_in_cycle = 0u;
            return finish_cycle(a);
        }
    }
    return 0;
}
