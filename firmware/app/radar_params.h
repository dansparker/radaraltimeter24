/**
 * @file radar_params.h
 * @brief Compile-time parameters of the 24 GHz FMCW radar altimeter.
 *
 * Waveform: one short triangular ramp, the same for all altitudes.
 *   head 48 + 256 + tail 48 = 352 samples = 1.173 ms per ramp at 300 kHz,
 *   one measurement pair (rising + falling) = 2.35 ms.
 * Short ramps keep the Doppler spread of the ground echo small in range
 * units (range error per Hz of Doppler ~ ramp duration): at 60 m/s ground
 * speed the hill is about +-2 m wide instead of +-14 m with an 8 ms ramp. The
 * sensitivity per ramp is about the same (the beat frequencies are 7x higher,
 * where the 1/f noise of the mixer is lower), and 7x more ramps per second
 * are averaged.
 */
#ifndef RADAR_PARAMS_H
#define RADAR_PARAMS_H

#include "rdsp/rdsp.h"

/* ---- acquisition ---------------------------------------------------------- */
#define RADAR_FS_HZ        300000u   /**< ADC sample rate = DAC update rate */
#define RADAR_NGAIN        4u        /**< switched gain levels, 0 = lowest */
#define RADAR_NSAMP        256u      /**< evaluated samples per ramp */
#define RADAR_NFFT         512u      /**< FFT length: 2x zero padding (finer edge sampling) */
#define RADAR_NBINS        (RADAR_NFFT / 2u)
#define RADAR_HEAD         48u       /**< skipped at ramp start and end (turn-around) */
#define RADAR_FRAME_LEN    (2u * RADAR_HEAD + RADAR_NSAMP)
#define RADAR_FRAME_DT_S   ((float)RADAR_FRAME_LEN / (float)RADAR_FS_HZ)
#define RADAR_PAIR_DT_S    (2.0f * RADAR_FRAME_DT_S)

#define RADAR_F0_HZ        24.125e9f
#define RADAR_LAMBDA_M     (RDSP_C0_MPS / RADAR_F0_HZ)

#define RADAR_ADC_MAX      4095u
#define RADAR_ADC_CLIP_LO  8u
#define RADAR_ADC_CLIP_HI  (RADAR_ADC_MAX - 8u)

/* ---- measurement cycle ---------------------------------------------------- */
#define ALT_MEAS_PAIRS     7u        /**< one altitude per 7 pairs = 16.4 ms (~61 Hz) */

/* ---- spectrum and averaging ----------------------------------------------- */
#define ALT_WINDOW         RDSP_WIN_HANN
#define ALT_INTERP         RDSP_INTERP_GAUSSIAN   /* calibration peak (zero padded spectrum) */
/* time constant of the spectrum average per ramp direction: short at low
 * altitude (strong echo, fast changes), long at high altitude */
#define ALT_AVG_TAU_MIN_S  0.04f
#define ALT_AVG_TAU_MAX_S  0.25f
#define ALT_AVG_TAU_PER_M  0.001f    /**< tau = MIN + h * PER_M, <= MAX */
#define ALT_AVG_CLIP       10.0f     /**< new value limited to 10x the average */
#define ALT_AVG_STRONG_DB  8.0f      /**< weaker echo: always the long time constant */
#define ALT_AVG_MIN_N      8u        /**< frames in the average before detection */

/* ---- leading edge detection (rdsp_edge), bins of the zero padded FFT ------ */
#define ALT_EDGE_SIGMA     5.0f      /**< threshold = 1 + SIGMA / sqrt(averaged frames) */
#define ALT_EDGE_THR_MIN   1.4f      /**< lower limit of the threshold factor (1.5 dB) */
#define ALT_EDGE_WIN       24u       /**< lagging noise window [bins] */
#define ALT_EDGE_GUARD     2u
#define ALT_EDGE_MIN_REF   8u
#define ALT_EDGE_CONFIRM   3u        /**< consecutive bins above the threshold */
#define ALT_EDGE_SPAN      12u       /**< plateau search after the edge [bins] */
#define ALT_EDGE_FLOOR     0.7f      /**< local noise >= 0.7 x global noise */
#define ALT_EDGE_MIN_SNR_DB 3.0f     /**< plateau above the noise */
#define ALT_MIN_BIN        2u        /**< lowest analysed bin (DC) */
/* Correction of the 50 % edge point to the nadir range [m] vs. altitude.
 * At low altitude the echo is narrow (like a point target: the 50 % point
 * lies 0.7 bin before its centre), at high altitude the beam broadens it.
 * Determined with the extended ground model of the simulator (34 x 80 deg
 * beam, 90 MHz sweep, test/range_sim.c 'b'); to be checked with flight data. */
#define ALT_EDGE_CORR_H    { 0.0f, 5.0f, 10.0f, 20.0f, 40.0f, 60.0f, 80.0f, 100.0f, 125.0f, 150.0f,                              175.0f, 200.0f, 225.0f, 250.0f, 300.0f }
#define ALT_EDGE_CORR_M    { 1.70f, 1.55f, 1.30f, 1.05f, 0.85f, 0.66f, 0.55f, 0.38f, 0.15f, -0.13f,                              -0.45f, -0.70f, -0.95f, -1.25f, -1.75f }
#define ALT_RELIABLE_BIN   4.0f      /**< edges below: degraded (DC, leakage) */
#define ALT_V_MAX_MPS      25.0f     /**< max. vertical speed (Doppler window) */
#define ALT_V_GATE_MPS     3.0f      /**< Doppler speed gate around the track */
#define ALT_VZ_MIN_SNR_DB  6.0f      /**< weaker: Doppler speed too noisy for the tracker */
#define ALT_VZ_SMOOTH_S    0.5f      /**< smoothing of the Doppler rate for the lag compensation */
#define ALT_CLIP_MAX       3u        /**< clipped samples tolerated per frame */

/* running background removal (flight): echoes fixed relative to the aircraft
 * (leakage, radome, gear) keep their phase from ramp to ramp, the moving
 * ground echo does not. Coherence weighted mean in the complex spectrum of the
 * near range, one estimate per gain level and ramp direction. */
#define ALT_BG_TAU_S       2.0f      /**< time constant of the estimate */
#define ALT_BG_WARMUP      8u        /**< frames before the first subtraction */
#define ALT_BG_MAX_M       25.0f     /**< range covered by the estimate (the side lobes
                                          of a strong gear echo must end inside) */
#define ALT_BG_BINS_MAX    40u       /**< storage limit [bins] */

/* gain control */
#define ALT_AGC_HI         0.85f
#define ALT_AGC_LO         0.20f
#define ALT_AGC_UP_S       0.3f      /**< low level this long before a gain increase */
#define ALT_AGC_ATTACK     3u        /**< overdriven frames in a row before a decrease */

/* tracker (one update per measurement cycle) */
#define ALT_TRK_ALPHA      0.40f
#define ALT_TRK_BETA       0.01f     /* small: the rate comes mainly from Doppler */
#define ALT_TRK_GAMMA      0.03f     /* Doppler rate weight per cycle (~0.5 s) */
#define ALT_TRK_GATE_ABS   1.5f      /**< [m] */
#define ALT_TRK_GATE_REL   0.03f
#define ALT_TRK_CONFIRM    3u
#define ALT_TRK_MAX_COAST_S 1.0f     /**< coasting time until LOST */
#define ALT_TRK_REACQ      6u
#define ALT_COAST_VALID_S  0.25f     /**< output stays VALID while coasting */

/* health monitoring */
#define ALT_FAULT_RMS_MIN  0.05f     /**< [LSB] below: ADC/DMA stuck (frozen data) */
#define ALT_FAULT_MEAN_LO  200.0f
#define ALT_FAULT_MEAN_HI  3895.0f
#define ALT_FAULT_FRAMES   40u

#endif /* RADAR_PARAMS_H */
