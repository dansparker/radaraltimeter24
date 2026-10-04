/**
 * @file altimeter.h
 * @brief Radar altimeter application pipeline (hardware independent).
 *
 * Input:  one ADC frame per ramp (radar_frame_len(mode) samples) + frame info
 * Output: one altitude solution per rising/falling ramp pair
 *
 * Per frame:   statistics -> AGC -> clip check -> DC/trend/background removal
 *              -> window -> FFT -> PSD -> averaging -> OS-CFAR -> interpolation
 * Per pair:    target selection + up/down pairing (Doppler compensation)
 *              -> alpha-beta tracker -> status / validity
 *
 * Only one instance is supported (large buffers are static, in CCM RAM on
 * the target).
 */
#ifndef ALTIMETER_H
#define ALTIMETER_H

#include "radar_params.h"
#include "config.h"

/* output status bits */
#define ALT_ST_VALID       0x0001u  /**< altitude usable */
#define ALT_ST_COAST       0x0002u  /**< no fresh measurement, predicted */
#define ALT_ST_NO_TARGET   0x0004u  /**< no target in this cycle */
#define ALT_ST_CLIPPED     0x0008u  /**< ADC clipping in this cycle */
#define ALT_ST_HW_FAULT    0x0010u  /**< IF/ADC implausible or frontend error */
#define ALT_ST_UNCAL       0x0020u  /**< module not calibrated */
#define ALT_ST_OVERRUN     0x0040u  /**< frames lost (processing too slow) */
#define ALT_ST_DEGRADED    0x0080u  /**< single ramp or mirrored solution */
#define ALT_ST_BUSY        0x0100u  /**< calibration / background capture */
#define ALT_ST_GAIN_CHG    0x0200u  /**< gain changed in this cycle */
#define ALT_ST_CLK_FALLBACK 0x0400u /**< running on HSI (set by the BSP) */

typedef enum { ALT_DIR_RISE = 0, ALT_DIR_FALL = 1 } alt_dir_t;

typedef struct {
    uint32_t id;        /**< frame counter, +1 per frame */
    uint8_t rmode;      /**< ramp mode of this frame (RADAR_RMODE_LONG/SHORT) */
    uint8_t dac_dir;    /**< 0 = DAC code rising, 1 = falling */
    uint8_t gain;       /**< gain level used during this frame */
    uint8_t settling;   /**< 1: gain switched at the start of this frame */
    uint8_t overrun;    /**< 1: frames were lost before this one */
} alt_frame_info_t;

typedef struct {
    float freq_hz;
    float power;
    float snr_db;
} alt_cand_t;

typedef struct {
    uint32_t seq;
    float altitude_m;    /**< tracked altitude (incl. offset) */
    float vspeed_mps;    /**< tracked vertical speed, climb > 0 */
    float raw_range_m;   /**< unfiltered measurement (NAN if none) */
    float f_rise_hz;     /**< selected beat, rising frequency ramp */
    float f_fall_hz;     /**< selected beat, falling frequency ramp */
    float f_r_hz;        /**< Doppler-free range beat */
    float snr_db;
    uint16_t status;
    uint8_t gain;
    uint8_t track_state;
    uint8_t rmode;       /**< ramp mode of this measurement */
} alt_output_t;

typedef struct {
    uint32_t frames;
    uint32_t pairs;
    uint32_t overruns;
    uint32_t settling;
    uint32_t clipped;
    uint32_t no_meas;
    uint32_t gain_changes;
    uint32_t nonfinite;
} alt_stats_t;

typedef enum { ALT_MODE_RUN = 0, ALT_MODE_CAL, ALT_MODE_BG } alt_mode_t;

/** parameters derived for one ramp mode */
typedef struct {
    uint16_t nfft, nbins, head, len;
    rdsp_rfft_t fft;
    rdsp_win_info_t wi;
    const float *win;
    rdsp_fmcw_t fm;
    float bin_hz;
    float fd_max_hz;
    float pair_dt;
    uint16_t bin_min;
    uint16_t bin_max;
} alt_mpar_t;

typedef struct {
    const config_t *cfg;
    int16_t *bg;                 /**< RADAR_BG_LEN values, see radar_bg_offset() */
    uint8_t bg_valid;            /**< bit (4*mode + gain): background valid */

    alt_mpar_t mp[RADAR_NMODES];
    uint8_t rmode;               /**< mode of the frames currently processed */
    uint8_t rmode_req;           /**< mode the frontend should use */
    uint16_t coast_valid;        /**< pairs a coasting output stays valid */
    float out_acc;               /**< output decimation */

    rdsp_cfar_t cfar;
    rdsp_agc_t agc;
    rdsp_track_t trk;
    rdsp_psd_avg_t avg[2];
    uint8_t avg_gain[2];
    uint8_t uncal;

    /* candidates per physical ramp direction (0 rising f, 1 falling f) */
    alt_cand_t cand[2][ALT_MAX_CAND];
    uint8_t ncand[2];
    uint8_t cand_ok[2];
    uint8_t cand_clip[2];
    uint8_t cand_gain[2];
    uint32_t cand_id[2];

    uint8_t gain_req;
    uint8_t gain_changed;
    uint8_t ovr_hold;
    uint16_t fault_cnt;
    uint16_t good_cnt;
    uint8_t hw_fault;
    uint16_t ext_status;         /**< status bits injected by the BSP */

    alt_mode_t mode;
    /* calibration accumulation */
    uint16_t cal_target, cal_n;
    double cal_sum, cal_sum2;
    /* background capture */
    uint8_t bg_gain;
    uint8_t bg_rmode;
    uint16_t bg_frames, bg_cnt[2], bg_tries;

    alt_output_t out;
    alt_stats_t st;
} altimeter_t;

/** Initialise; bg_storage must hold RADAR_BG_LEN int16 values (or NULL). */
void alt_init(altimeter_t *a, const config_t *cfg, int16_t *bg_storage, uint8_t bg_valid);

/** Re-derive parameters after a configuration change and restart tracking. */
void alt_apply_config(altimeter_t *a);

/**
 * Process one frame. a->out is updated after every falling DAC ramp.
 * @return 1 if the output is due for transmission (decimated to ~60 Hz)
 */
int alt_process_frame(altimeter_t *a, const uint16_t *frame, const alt_frame_info_t *fi);

/** Gain level the frontend should use from the next frame on. */
uint8_t alt_gain_request(const altimeter_t *a);

/** Ramp mode the frontend should use (restart the frontend when it changes). */
uint8_t alt_rmode_request(const altimeter_t *a);

/** Start averaging the range beat over n Doppler-compensated measurements. */
void alt_cal_start(altimeter_t *a, uint16_t n);
/** @return 1 when done; mean/std of f_R [Hz] */
int alt_cal_result(const altimeter_t *a, float *mean_hz, float *std_hz, uint16_t *n);
void alt_cal_abort(altimeter_t *a);

/** Capture background frames for both ramp modes and all gains, frames_per_dir each. */
void alt_bg_start(altimeter_t *a, uint16_t frames_per_dir);
int alt_bg_done(const altimeter_t *a);

/** Set status bits from outside (e.g. clock fallback). */
void alt_set_ext_status(altimeter_t *a, uint16_t bits);

#endif /* ALTIMETER_H */
