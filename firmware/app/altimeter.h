/**
 * @file altimeter.h
 * @brief Radar altimeter application pipeline (hardware independent).
 *
 * Input:  one ADC frame per ramp (RADAR_FRAME_LEN samples) + frame info
 * Output: one altitude solution per measurement cycle (ALT_MEAS_PAIRS pairs)
 *
 * Per frame:   statistics -> AGC -> clip check -> DC/trend removal -> window
 *              -> FFT -> running background removal (near range) -> PSD
 *              -> robust average per ramp direction
 * Per cycle:   leading edge of the ground echo in both averaged spectra
 *              -> up/down combination (Doppler) -> averaging lag compensation
 *              -> alpha-beta tracker -> status / validity
 *
 * Only one instance is supported (the buffers are static).
 */
#ifndef ALTIMETER_H
#define ALTIMETER_H

#include "radar_params.h"
#include "config.h"

/* output status bits */
#define ALT_ST_VALID       0x0001u  /**< altitude usable */
#define ALT_ST_COAST       0x0002u  /**< no fresh measurement, predicted */
#define ALT_ST_NO_TARGET   0x0004u  /**< no ground echo in this cycle */
#define ALT_ST_CLIPPED     0x0008u  /**< ADC clipping in this cycle */
#define ALT_ST_HW_FAULT    0x0010u  /**< IF/ADC implausible or frontend error */
#define ALT_ST_UNCAL       0x0020u  /**< module not calibrated */
#define ALT_ST_OVERRUN     0x0040u  /**< frames lost (processing too slow) */
#define ALT_ST_DEGRADED    0x0080u  /**< one ramp direction only or edge near DC */
#define ALT_ST_BUSY        0x0100u  /**< calibration running */
#define ALT_ST_GAIN_CHG    0x0200u  /**< gain changed in this cycle */
#define ALT_ST_CLK_FALLBACK 0x0400u /**< running on HSI (set by the BSP) */

typedef enum { ALT_DIR_RISE = 0, ALT_DIR_FALL = 1 } alt_dir_t;

typedef struct {
    uint32_t id;        /**< frame counter, +1 per frame */
    uint8_t dac_dir;    /**< 0 = DAC code rising, 1 = falling */
    uint8_t gain;       /**< gain level used during this frame */
    uint8_t settling;   /**< 1: gain switched at the start of this frame */
    uint8_t overrun;    /**< 1: frames were lost before this one */
} alt_frame_info_t;

typedef struct {
    uint32_t seq;
    float altitude_m;    /**< tracked altitude (incl. offset) */
    float vspeed_mps;    /**< tracked vertical speed, climb > 0 */
    float raw_range_m;   /**< unfiltered measurement (NAN if none) */
    float f_rise_hz;     /**< leading edge, rising frequency ramp */
    float f_fall_hz;     /**< leading edge, falling frequency ramp */
    float f_r_hz;        /**< Doppler-free range beat */
    float snr_db;        /**< plateau above noise (smaller of both ramps) */
    float tau_s;         /**< averaging time constant in use */
    uint16_t status;
    uint8_t gain;
    uint8_t track_state;
} alt_output_t;

typedef struct {
    uint32_t frames;
    uint32_t pairs;
    uint32_t cycles;
    uint32_t overruns;
    uint32_t settling;
    uint32_t clipped;
    uint32_t no_meas;
    uint32_t gain_changes;
    uint32_t nonfinite;
} alt_stats_t;

typedef enum { ALT_MODE_RUN = 0, ALT_MODE_CAL } alt_mode_t;

/** state of one physical ramp direction */
typedef struct {
    uint16_t n;          /**< frames in the average */
    uint8_t gain;        /**< gain level of the average */
    uint8_t clip;        /**< clipping in this cycle */
    uint8_t fresh;       /**< frames added in this cycle */
} alt_dir_state_t;

typedef struct {
    const config_t *cfg;

    /* derived parameters */
    rdsp_rfft_t fft;
    rdsp_win_info_t wi;
    rdsp_fmcw_t fm;
    rdsp_edge_cfg_t edge;
    float bin_hz;
    float fd_max_hz;
    uint16_t bin_max;
    uint16_t bg_bins;            /**< bins 1 .. bg_bins-1 get the running background */
    float meas_dt;               /**< duration of a measurement cycle */

    rdsp_agc_t agc;
    rdsp_track_t trk;
    alt_dir_state_t dir[2];
    float tau;                   /**< averaging time constant */
    float last_snr_db;           /**< SNR of the last measurement */
    float vz_dopp;               /**< smoothed Doppler rate (independent of the tracker) */
    uint8_t vz_dopp_ok;
    uint16_t bg_n[RADAR_NGAIN][2];
    uint16_t coast_valid;        /**< cycles a coasting output stays valid */
    uint8_t pairs_in_cycle;
    uint8_t uncal;

    uint8_t gain_req;
    uint8_t last_gain;           /**< gain of the last processed frame */
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

    alt_output_t out;
    alt_stats_t st;
} altimeter_t;

/** Initialise with the configuration (kept by reference). */
void alt_init(altimeter_t *a, const config_t *cfg);

/** Re-derive parameters after a configuration change and restart tracking. */
void alt_apply_config(altimeter_t *a);

/**
 * Process one frame. a->out is updated at the end of each measurement cycle.
 * @return 1 if a new output is available (~61 Hz)
 */
int alt_process_frame(altimeter_t *a, const uint16_t *frame, const alt_frame_info_t *fi);

/** Gain level the frontend should use from the next frame on. */
uint8_t alt_gain_request(const altimeter_t *a);

/**
 * Calibration with a static reflector (point target, 'bg off'): averages the
 * Doppler-compensated beat of the strongest peak over n measurement cycles.
 */
void alt_cal_start(altimeter_t *a, uint16_t n);
/** @return 1 when done; mean/std of f_R [Hz] */
int alt_cal_result(const altimeter_t *a, float *mean_hz, float *std_hz, uint16_t *n);
void alt_cal_abort(altimeter_t *a);

/** Set status bits from outside (e.g. clock fallback). */
void alt_set_ext_status(altimeter_t *a, uint16_t bits);

#endif /* ALTIMETER_H */
