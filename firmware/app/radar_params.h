/**
 * @file radar_params.h
 * @brief Compile-time parameters of the 24 GHz FMCW radar altimeter.
 *
 * Two ramp modes (one trigger timer drives DAC and ADC, fs = 300 kHz):
 *   LONG   head 192 + 2048 + tail 192 = 2432 samples = 8.107 ms per ramp
 *          -> best sensitivity, used for acquisition and above ~30 m
 *   SHORT  head  96 +  512 + tail  96 =  704 samples = 2.347 ms per ramp
 *          -> 3.5x less range/Doppler coupling, beats 3.5x further from DC
 *             and from the IF high-passes, used at low altitude
 * One frame = one ramp (rising or falling), one measurement = 2 frames.
 */
#ifndef RADAR_PARAMS_H
#define RADAR_PARAMS_H

#include "rdsp/rdsp.h"

/* ---- acquisition ---------------------------------------------------------- */
#define RADAR_FS_HZ        300000u   /**< ADC sample rate = DAC update rate */
#define RADAR_NGAIN        4u        /**< CD4052 gain levels, 0 = lowest */

#define RADAR_NMODES       2u
#define RADAR_RMODE_LONG   0u
#define RADAR_RMODE_SHORT  1u
#define RADAR_NFFT_LONG    2048u
#define RADAR_HEAD_LONG    192u      /**< skipped at ramp start and end (turn-around) */
#define RADAR_NFFT_SHORT   512u
#define RADAR_HEAD_SHORT   96u

#define RADAR_NFFT_MAX     RADAR_NFFT_LONG
#define RADAR_NBINS_MAX    (RADAR_NFFT_MAX / 2u)
#define RADAR_FRAME_LEN_MAX (2u * RADAR_HEAD_LONG + RADAR_NFFT_LONG)
/** background storage: [mode][gain][dir][nfft(mode)] */
#define RADAR_BG_LEN       (RADAR_NGAIN * 2u * (RADAR_NFFT_LONG + RADAR_NFFT_SHORT))

static inline uint32_t radar_nfft(uint32_t m) { return (m != 0u) ? RADAR_NFFT_SHORT : RADAR_NFFT_LONG; }
static inline uint32_t radar_head(uint32_t m) { return (m != 0u) ? RADAR_HEAD_SHORT : RADAR_HEAD_LONG; }
static inline uint32_t radar_frame_len(uint32_t m) { return 2u * radar_head(m) + radar_nfft(m); }
static inline float radar_pair_dt(uint32_t m) { return 2.0f * (float)radar_frame_len(m) / (float)RADAR_FS_HZ; }
static inline uint32_t radar_bg_offset(uint32_t m, uint32_t g, uint32_t d)
{
    return (m != 0u) ? (RADAR_NGAIN * 2u * RADAR_NFFT_LONG + (g * 2u + d) * RADAR_NFFT_SHORT)
                     : ((g * 2u + d) * RADAR_NFFT_LONG);
}

#define RADAR_F0_HZ        24.125e9f
#define RADAR_LAMBDA_M     (RDSP_C0_MPS / RADAR_F0_HZ)

#define RADAR_ADC_MAX      4095u
#define RADAR_ADC_CLIP_LO  8u
#define RADAR_ADC_CLIP_HI  (RADAR_ADC_MAX - 8u)

/* ---- ramp mode selection (AUTO) ------------------------------------------- */
#define ALT_SHORT_ENTER_M  25.0f     /**< valid altitude below -> SHORT */
#define ALT_SHORT_EXIT_M   35.0f     /**< valid altitude above or track lost -> LONG */
#define ALT_OUT_PERIOD_S   0.016f    /**< output decimation (~60 Hz in both modes) */

/* ---- signal processing ---------------------------------------------------- */
#define ALT_WINDOW         RDSP_WIN_HANN
#define ALT_INTERP         RDSP_INTERP_HANN
#define ALT_PSD_ALPHA      0.5f      /**< weight of a new ramp in the PSD average */
#define ALT_CFAR_TYPE      RDSP_CFAR_OS
#define ALT_CFAR_TRAIN     12u
#define ALT_CFAR_GUARD     3u
#define ALT_CFAR_PFA       1.0e-4f
#define ALT_SNR_MIN_DB     12.0f     /**< SNR of the averaged spectrum */
#define ALT_SNR_CUR_DB     9.0f      /**< SNR the current frame must confirm */
#define ALT_RELIABLE_BIN   4.0f      /**< beats below: no Doppler fusion (DC, image) */
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
#define ALT_TRK_GAMMA      0.20f
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
#define ALT_FAULT_FRAMES   20u

#endif /* RADAR_PARAMS_H */
