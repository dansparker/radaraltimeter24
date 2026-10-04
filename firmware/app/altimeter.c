/**
 * @file altimeter.c
 * @brief Radar altimeter application pipeline (see altimeter.h).
 */
#include "altimeter.h"
#include "ramp.h"
#include <math.h>
#include <string.h>

#ifndef ALT_CCM
#define ALT_CCM   /* placed in CCM RAM on the target (defined by the build) */
#endif

static ALT_CCM float s_tw[RADAR_NFFT];
static ALT_CCM float s_win[RADAR_NFFT];
static ALT_CCM float s_work[RADAR_NFFT];
static ALT_CCM float s_avgbuf[2][RADAR_NBINS];
static ALT_CCM float s_bgacc[2][RADAR_NFFT];

typedef struct {
    float range_m;
    float f_r;
    float f_rise;
    float f_fall;
    float closing;      /**< closing speed from Doppler [m/s] */
    float snr_db;
    uint8_t degraded;
} meas_t;

/* ------------------------------------------------------------------------ */

static void reset_dynamic(altimeter_t *a)
{
    for (unsigned d = 0; d < 2u; d++) {
        rdsp_psd_avg_reset(&a->avg[d]);
        a->avg_gain[d] = 0xFFu;
        a->cand_ok[d] = 0u;
        a->cand_clip[d] = 0u;
        a->ncand[d] = 0u;
    }
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
    uint32_t bmax = (uint32_t)(fmax / a->bin_hz) + ALT_CFAR_GUARD + 1u;
    if (bmax > RADAR_NBINS - 2u) { bmax = RADAR_NBINS - 2u; }
    a->bin_min = (uint16_t)ALT_MIN_BIN;
    a->bin_max = (uint16_t)bmax;
    a->uncal = (m->calibrated != 0u) ? 0u : 1u;
    a->gain_req = (c->gain_mode == CFG_GAIN_AUTO) ? a->agc.level : c->gain_mode;
    reset_dynamic(a);
}

void alt_init(altimeter_t *a, const config_t *cfg, int16_t *bg_storage, uint8_t bg_valid)
{
    memset(a, 0, sizeof(*a));
    a->cfg = cfg;
    a->bg = bg_storage;
    a->bg_valid = (bg_storage != NULL) ? bg_valid : 0u;

    (void)rdsp_rfft_init(&a->fft, RADAR_NFFT, s_tw);
    (void)rdsp_window_make(s_win, RADAR_NFFT, ALT_WINDOW, &a->wi);
    (void)rdsp_cfar_init(&a->cfar, ALT_CFAR_TYPE, ALT_CFAR_TRAIN, ALT_CFAR_GUARD, 0u, ALT_CFAR_PFA);
    rdsp_agc_init(&a->agc, RADAR_NGAIN, 0u, ALT_AGC_HI, ALT_AGC_LO, ALT_AGC_UP_FRAMES);

    const rdsp_track_cfg_t tc = {
        .alpha = ALT_TRK_ALPHA, .beta = ALT_TRK_BETA, .gamma = ALT_TRK_GAMMA,
        .gate_abs = ALT_TRK_GATE_ABS, .gate_rel = ALT_TRK_GATE_REL,
        .v_max = ALT_V_MAX_MPS, .confirm_n = ALT_TRK_CONFIRM,
        .max_coast = ALT_TRK_MAX_COAST, .reacq_n = ALT_TRK_REACQ
    };
    rdsp_track_init(&a->trk, &tc);
    for (unsigned d = 0; d < 2u; d++) {
        rdsp_psd_avg_init(&a->avg[d], s_avgbuf[d], RADAR_NBINS, ALT_PSD_ALPHA);
    }
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

/* ---- background capture ------------------------------------------------- */

void alt_bg_start(altimeter_t *a, uint16_t frames_per_dir)
{
    if ((a->bg == NULL) || (frames_per_dir == 0u)) { return; }
    memset(s_bgacc, 0, sizeof(s_bgacc));
    a->bg_gain = 0u;
    a->bg_frames = frames_per_dir;
    a->bg_cnt[0] = a->bg_cnt[1] = 0u;
    a->bg_tries = 0u;
    a->bg_valid = 0u;
    a->gain_req = 0u;
    a->mode = ALT_MODE_BG;
    reset_dynamic(a);
}

int alt_bg_done(const altimeter_t *a)
{
    return (a->mode != ALT_MODE_BG) ? 1 : 0;
}

static void bg_next_gain(altimeter_t *a)
{
    memset(s_bgacc, 0, sizeof(s_bgacc));
    a->bg_cnt[0] = a->bg_cnt[1] = 0u;
    a->bg_tries = 0u;
    if (++a->bg_gain >= RADAR_NGAIN) {
        a->mode = ALT_MODE_RUN;
        a->gain_req = (a->cfg->gain_mode == CFG_GAIN_AUTO) ? a->agc.level : a->cfg->gain_mode;
        reset_dynamic(a);
    } else {
        a->gain_req = a->bg_gain;
    }
}

/* x = conditioned frame or NULL (frame unusable, e.g. clipped) */
static void bg_accumulate(altimeter_t *a, const float *x, const alt_frame_info_t *fi)
{
    const uint8_t d = fi->dac_dir ? 1u : 0u;
    if (fi->gain != a->bg_gain) { return; }
    a->bg_tries++;
    if ((x != NULL) && (a->bg_cnt[d] < a->bg_frames)) {
        for (uint32_t i = 0; i < RADAR_NFFT; i++) { s_bgacc[d][i] += x[i]; }
        a->bg_cnt[d]++;
    }
    if ((a->bg_cnt[0] >= a->bg_frames) && (a->bg_cnt[1] >= a->bg_frames)) {
        const float k = 1.0f / ((float)a->bg_frames * ALT_BG_SCALE);
        for (uint32_t dd = 0; dd < 2u; dd++) {
            int16_t *dst = &a->bg[((uint32_t)a->bg_gain * 2u + dd) * RADAR_NFFT];
            for (uint32_t i = 0; i < RADAR_NFFT; i++) {
                float v = s_bgacc[dd][i] * k;
                v = (v > 32767.0f) ? 32767.0f : ((v < -32768.0f) ? -32768.0f : v);
                dst[i] = (int16_t)lrintf(v);
            }
        }
        a->bg_valid |= (uint8_t)(1u << a->bg_gain);
        bg_next_gain(a);
    } else if (a->bg_tries > 8u * a->bg_frames) {
        bg_next_gain(a);   /* e.g. permanent clipping at this gain: leave it invalid */
    }
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

/* Sub-bin peak position with the IF high-pass response removed: the steep
 * analog high-pass tilts the spectrum around low beat frequencies and would
 * bias the interpolation towards higher frequencies. */
static float interp_peak(const altimeter_t *a, const float *p, uint32_t b)
{
    if ((b == 0u) || (b + 1u >= RADAR_NBINS)) { return (float)b; }
    float q[3];
    for (uint32_t j = 0; j < 3u; j++) {
        q[j] = p[b - 1u + j];
#if ALT_IF_HP_ORDER > 0
        const float r = ((float)(b - 1u + j) * a->bin_hz) / ALT_IF_HP_FC_HZ;
        const float h2 = (r * r) / (1.0f + r * r);           /* |H|^2 of one stage */
        float g = 1.0f;
        for (int s = 0; s < ALT_IF_HP_ORDER; s++) { g *= h2; }
        q[j] = (g > 1.0e-6f) ? q[j] / g : q[j] * 1.0e6f;
#endif
    }
    return (float)b + rdsp_peak_interp(q, 3u, 1u, ALT_INTERP);
}

static void process_spectrum(altimeter_t *a, const uint16_t *frame,
                             const alt_frame_info_t *fi, uint8_t pd)
{
    const config_t *c = a->cfg;
    float *x = s_work;
    rdsp_frame_stats_t s;

    rdsp_frame_condition_u16(frame + RADAR_N_HEAD, RADAR_NFFT, x,
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
    if (a->mode == ALT_MODE_BG) {
        a->gain_req = a->bg_gain;
    } else if (c->gain_mode == CFG_GAIN_AUTO) {
        if ((fi->gain == a->agc.level) && rdsp_agc_update(&a->agc, s.peak_frac, clipped)) {
            a->gain_changed = 1u;
            a->st.gain_changes++;
        }
        a->gain_req = a->agc.level;
    } else {
        a->gain_req = c->gain_mode;
    }

    if (clipped) {
        a->cand_clip[pd] = 1u;
        a->st.clipped++;
        if (a->mode == ALT_MODE_BG) { bg_accumulate(a, NULL, fi); }
        return;
    }
    if (c->detrend) { rdsp_frame_detrend(x, RADAR_NFFT); }

    if (a->mode == ALT_MODE_BG) {
        bg_accumulate(a, x, fi);
        return;
    }

    const uint8_t g = (uint8_t)(fi->gain & 3u);
    if (c->bg_enable && (a->bg != NULL) && ((a->bg_valid & (1u << g)) != 0u)) {
        const uint32_t dd = fi->dac_dir ? 1u : 0u;
        rdsp_frame_sub_bg_i16(x, &a->bg[((uint32_t)g * 2u + dd) * RADAR_NFFT], RADAR_NFFT, ALT_BG_SCALE);
    }

    /* spectrum */
    rdsp_window_apply(x, s_win, RADAR_NFFT);
    rdsp_rfft(&a->fft, x);
    rdsp_rfft_power(x, x, RADAR_NFFT);
    rdsp_psd_onesided(x, RADAR_NBINS, (float)RADAR_FS_HZ, a->wi.s2);
    if (!rdsp_all_finite(x, RADAR_NBINS)) {
        a->st.nonfinite++;
        rdsp_psd_avg_reset(&a->avg[pd]);
        return;
    }
    if (a->avg_gain[pd] != fi->gain) {          /* never mix gains in the average */
        rdsp_psd_avg_reset(&a->avg[pd]);
        a->avg_gain[pd] = fi->gain;
    }
    rdsp_psd_avg_update(&a->avg[pd], x);

    /* detection */
    rdsp_det_t det[ALT_MAX_CAND];
    const float *p = a->avg[pd].acc;
    const uint32_t nd = rdsp_cfar_detect(&a->cfar, p, RADAR_NBINS, a->bin_min, a->bin_max,
                                         det, ALT_MAX_CAND);
    /* The average gives a stable detection; the current frame must confirm the
     * peak and is used for the frequency estimate. This avoids the lag of the
     * average for moving targets and lets a vanished target drop out at once. */
    const float k_cur = powf(10.0f, 0.1f * ALT_SNR_CUR_DB);
    uint8_t nc = 0u;
    for (uint32_t i = 0; i < nd; i++) {
        if (det[i].snr_db < ALT_SNR_MIN_DB) { continue; }
        uint32_t b = det[i].bin;
        if ((b + 1u < RADAR_NBINS) && (x[b + 1u] > x[b])) { b++; }
        if ((b > 1u) && (x[b - 1u] > x[b])) { b--; }
        if (!(x[b] >= k_cur * det[i].noise)) { continue; }
        a->cand[pd][nc].freq_hz = interp_peak(a, x, b) * a->bin_hz;
        a->cand[pd][nc].power = det[i].power;
        a->cand[pd][nc].snr_db = 10.0f * log10f(x[b] / det[i].noise);
        nc++;
    }
    a->ncand[pd] = nc;
    a->cand_ok[pd] = 1u;
    a->cand_gain[pd] = fi->gain;
    a->cand_id[pd] = fi->id;
}

/* ---- per pair ----------------------------------------------------------- */

/* nearest candidate whose power is within ALT_SEL_REL_DB of the strongest */
static int select_nearest_strong(const alt_cand_t *c, uint8_t n)
{
    if (n == 0u) { return -1; }
    float pmax = 0.0f;
    for (uint8_t i = 0; i < n; i++) { if (c[i].power > pmax) { pmax = c[i].power; } }
    const float thr = pmax * powf(10.0f, -0.1f * ALT_SEL_REL_DB);
    for (uint8_t i = 0; i < n; i++) {           /* candidates are in ascending frequency */
        if (c[i].power >= thr) { return (int)i; }
    }
    return -1;
}

static void fill_meas(const altimeter_t *a, meas_t *m, float fr, float fd, float fu, float fdn,
                      float snr, uint8_t degraded)
{
    m->f_r = fr;
    m->range_m = rdsp_fmcw_range(&a->fm, fr);
    m->closing = rdsp_fmcw_closing_speed(&a->fm, fd);
    m->f_rise = fu;
    m->f_fall = fdn;
    m->snr_db = snr;
    m->degraded = degraded;
}

static int choose_measurement(const altimeter_t *a, int paired, meas_t *m)
{
    const alt_cand_t *U = a->cand[ALT_DIR_RISE];
    const alt_cand_t *D = a->cand[ALT_DIR_FALL];
    const uint8_t nu = a->cand_ok[ALT_DIR_RISE] ? a->ncand[ALT_DIR_RISE] : 0u;
    const uint8_t nd = a->cand_ok[ALT_DIR_FALL] ? a->ncand[ALT_DIR_FALL] : 0u;
    const rdsp_track_t *t = &a->trk;
    const float dt = RADAR_PAIR_DT_S;
    float fr, fd;

    if ((t->state == RDSP_TRK_CONFIRMED) || (t->state == RDSP_TRK_COAST)) {
        const float rp = rdsp_track_predict(t, dt);
        const float gate = rdsp_track_gate(t, dt);
        const float vcp = -t->v;                       /* predicted closing speed */
        const float fdp = rdsp_fmcw_doppler(&a->fm, vcp);
        const float sig_r = 0.25f * gate, sig_v = 2.0f;
        float best = 1.0e30f;
        int found = 0;

        if (paired) {
            for (uint8_t i = 0; i < nu; i++) {
                for (uint8_t j = 0; j < nd; j++) {
                    for (int h = 0; h < 3; h++) {
                        if (rdsp_fmcw_updown(U[i].freq_hz, D[j].freq_hz, (rdsp_ud_hyp_t)h, &fr, &fd) != 0) { continue; }
                        if (fabsf(fd) > a->fd_max_hz) { continue; }
                        const float r = rdsp_fmcw_range(&a->fm, fr);
                        const float dv = rdsp_fmcw_closing_speed(&a->fm, fd) - vcp;
                        if ((fabsf(r - rp) > gate) || (fabsf(dv) > ALT_V_GATE_MPS)) { continue; }
                        const float er = (r - rp) / sig_r;
                        const float ev = dv / sig_v;
                        const float cost = er * er + ev * ev + ((h != 0) ? 4.0f : 0.0f);
                        if (cost < best) {
                            best = cost;
                            found = 1;
                            const float fmin = RDSP_MIN(U[i].freq_hz, D[j].freq_hz);
                            fill_meas(a, m, fr, fd, U[i].freq_hz, D[j].freq_hz,
                                      RDSP_MIN(U[i].snr_db, D[j].snr_db),
                                      (uint8_t)((h != 0) || (fmin < ALT_RELIABLE_BIN * a->bin_hz)));
                        }
                    }
                }
            }
        }
        if (!found) {
            /* single ramp with the predicted Doppler (e.g. one beat near DC) */
            for (int dir = 0; dir < 2; dir++) {
                const alt_cand_t *C = (dir == 0) ? U : D;
                const uint8_t n = (dir == 0) ? nu : nd;
                for (uint8_t i = 0; i < n; i++) {
                    const float f = C[i].freq_hz;
                    float cands[2];
                    if (dir == 0) { cands[0] = f + fdp; cands[1] = fdp - f; }   /* f = |fR - fD| */
                    else          { cands[0] = f - fdp; cands[1] = -f - fdp; }  /* f = |fR + fD| */
                    for (int k = 0; k < 2; k++) {
                        if (cands[k] < 0.0f) { continue; }
                        const float r = rdsp_fmcw_range(&a->fm, cands[k]);
                        if (fabsf(r - rp) > gate) { continue; }
                        const float er = (r - rp) / sig_r;
                        const float cost = er * er + 9.0f;
                        if (cost < best) {
                            best = cost;
                            found = 1;
                            fill_meas(a, m, cands[k], fdp, (dir == 0) ? f : NAN, (dir == 1) ? f : NAN,
                                      C[i].snr_db, 1u);
                        }
                    }
                }
            }
        }
        if (found) { return 1; }
        /* nothing near the prediction: offer an acquisition measurement so that
         * the tracker can re-acquire after a real jump */
    }

    if (!paired) { return 0; }
    const int iu = select_nearest_strong(U, nu);
    const int id = select_nearest_strong(D, nd);
    if ((iu < 0) || (id < 0)) { return 0; }
    if (rdsp_fmcw_updown(U[iu].freq_hz, D[id].freq_hz, RDSP_UD_NORMAL, &fr, &fd) != 0) { return 0; }
    if (fabsf(fd) > a->fd_max_hz) { return 0; }
    fill_meas(a, m, fr, fd, U[iu].freq_hz, D[id].freq_hz, RDSP_MIN(U[iu].snr_db, D[id].snr_db), 0u);
    return 1;
}

static int finish_pair(altimeter_t *a)
{
    alt_output_t *o = &a->out;
    const config_t *c = a->cfg;
    uint16_t st = 0u;
    meas_t m = { 0 };
    int have = 0;

    a->st.pairs++;
    const uint32_t did = (a->cand_id[0] > a->cand_id[1]) ? (a->cand_id[0] - a->cand_id[1])
                                                         : (a->cand_id[1] - a->cand_id[0]);
    const int paired = a->cand_ok[0] && a->cand_ok[1] && (a->cand_gain[0] == a->cand_gain[1]) && (did == 1u);

    if ((a->mode != ALT_MODE_BG) && !a->hw_fault) {
        have = choose_measurement(a, paired, &m);
        if (have && (!isfinite(m.range_m) || (m.range_m > 1.1f * c->max_range_m))) { have = 0; }
    }
    if (a->cand_clip[0] || a->cand_clip[1]) { st |= ALT_ST_CLIPPED; }

    float vz = (have && !m.degraded) ? -m.closing : NAN;         /* range rate */
    if (((a->trk.state == RDSP_TRK_CONFIRMED) || (a->trk.state == RDSP_TRK_COAST)) &&
        (fabsf(vz - a->trk.v) > ALT_V_GATE_MPS)) {
        vz = NAN;                                                /* implausible Doppler */
    }
    (void)rdsp_track_update(&a->trk, have, have ? m.range_m : 0.0f, vz, RADAR_PAIR_DT_S);
    if (!have) {
        st |= ALT_ST_NO_TARGET;
        a->st.no_meas++;
    }

    if ((a->mode == ALT_MODE_CAL) && have && !m.degraded) {
        a->cal_sum += (double)m.f_r;
        a->cal_sum2 += (double)m.f_r * (double)m.f_r;
        if (++a->cal_n >= a->cal_target) { a->mode = ALT_MODE_RUN; }
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
                       ((ts == RDSP_TRK_COAST) && (a->trk.misses <= ALT_COAST_VALID));
    if (trk_ok && !a->hw_fault && (a->mode != ALT_MODE_BG) && isfinite(a->trk.x) &&
        (a->trk.x <= 1.1f * c->max_range_m)) {
        st |= ALT_ST_VALID;
    }

    o->seq++;
    o->altitude_m = a->trk.x;
    o->vspeed_mps = a->trk.v;
    o->raw_range_m = have ? m.range_m : NAN;
    o->f_rise_hz = have ? m.f_rise : NAN;
    o->f_fall_hz = have ? m.f_fall : NAN;
    o->f_r_hz = have ? m.f_r : NAN;
    o->snr_db = have ? m.snr_db : 0.0f;
    o->status = st;
    o->gain = a->cand_gain[ALT_DIR_FALL];
    o->track_state = (uint8_t)ts;

    a->cand_ok[0] = a->cand_ok[1] = 0u;     /* consumed */
    a->cand_clip[0] = a->cand_clip[1] = 0u;
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

    if (fi->settling || fi->overrun || (frame == NULL)) {
        a->cand_ok[pd] = 0u;
        if ((a->mode == ALT_MODE_BG) && fi->settling) { a->gain_req = a->bg_gain; }
    } else {
        process_spectrum(a, frame, fi, pd);
    }
    return (dac_dir == 1u) ? finish_pair(a) : 0;
}
