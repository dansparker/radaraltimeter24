/**
 * @file radar_params.h
 * @brief Compile-time parameters of the 24 GHz FMCW radar altimeter.
 *
 * Timing (one trigger timer drives DAC and ADC together, see docs/hardware.md):
 *   fs = 300 kHz, frame = HEAD + NFFT + TAIL = 2432 samples = 8.107 ms
 *   one frame = one ramp (rising or falling), one measurement = 2 frames
 *   -> 61.7 measurements per second
 */
#ifndef RADAR_PARAMS_H
#define RADAR_PARAMS_H

#include "rdsp/rdsp.h"

/* ---- acquisition ---------------------------------------------------------- */
#define RADAR_FS_HZ        300000u   /**< ADC sample rate = DAC update rate */
#define RADAR_NFFT         2048u     /**< analysed samples per ramp */
#define RADAR_NBINS        (RADAR_NFFT / 2u)
#define RADAR_N_HEAD       192u      /**< skipped at ramp start (turn-around transient) */
#define RADAR_N_TAIL       192u      /**< skipped at ramp end (VCO / DAC corner) */
#define RADAR_FRAME_LEN    (RADAR_N_HEAD + RADAR_NFFT + RADAR_N_TAIL)
#define RADAR_PAIR_DT_S    (2.0f * (float)RADAR_FRAME_LEN / (float)RADAR_FS_HZ)
#define RADAR_NGAIN        4u        /**< CD4052 gain levels, 0 = lowest */

#define RADAR_F0_HZ        24.125e9f
#define RADAR_LAMBDA_M     (RDSP_C0_MPS / RADAR_F0_HZ)

#define RADAR_ADC_MAX      4095u
#define RADAR_ADC_CLIP_LO  8u
#define RADAR_ADC_CLIP_HI  (RADAR_ADC_MAX - 8u)

/* ---- signal processing ---------------------------------------------------- */
#define ALT_WINDOW         RDSP_WIN_HANN
#define ALT_INTERP         RDSP_INTERP_HANN
#define ALT_PSD_ALPHA      0.5f      /**< weight of a new ramp in the PSD average */
#define ALT_CFAR_TYPE      RDSP_CFAR_OS
#define ALT_CFAR_TRAIN     12u
#define ALT_CFAR_GUARD     3u
#define ALT_CFAR_PFA       1.0e-4f
#define ALT_SNR_MIN_DB     12.0f     /**< SNR of the averaged spectrum */
#define ALT_SNR_CUR_DB     6.0f      /**< SNR the current frame must confirm */
#define ALT_MIN_BIN        2u        /**< lowest analysed bin (DC / leakage) */
#define ALT_MAX_CAND       8u        /**< detections kept per ramp */
#define ALT_SEL_REL_DB     15.0f     /**< nearest target within X dB of the strongest */
#define ALT_V_MAX_MPS      25.0f     /**< max. vertical speed (Doppler window) */
#define ALT_V_GATE_MPS     3.0f      /**< Doppler speed gate around the track */
#define ALT_CLIP_MAX       3u        /**< clipped samples tolerated per frame */
#define ALT_BG_SCALE       0.125f    /**< background int16 LSB = 1/8 ADC LSB */

/* gain control */
#define ALT_AGC_HI         0.85f
#define ALT_AGC_LO         0.20f
#define ALT_AGC_UP_FRAMES  20u

/* tracker */
#define ALT_TRK_ALPHA      0.40f
#define ALT_TRK_BETA       0.05f
#define ALT_TRK_GAMMA      0.30f
#define ALT_TRK_GATE_ABS   1.5f      /**< [m] */
#define ALT_TRK_GATE_REL   0.03f
#define ALT_TRK_CONFIRM    3u
#define ALT_TRK_MAX_COAST  60u       /**< ~1 s, then LOST */
#define ALT_TRK_REACQ      6u
#define ALT_COAST_VALID    15u       /**< output stays VALID for ~0.25 s of coasting */

/* health monitoring */
#define ALT_FAULT_RMS_MIN  0.05f     /**< [LSB] below: ADC/DMA stuck (frozen data) */
#define ALT_FAULT_MEAN_LO  200.0f
#define ALT_FAULT_MEAN_HI  3895.0f
#define ALT_FAULT_FRAMES   20u

#endif /* RADAR_PARAMS_H */
