/**
 * @file sim.h
 * @brief FMCW radar frontend simulator for host tests.
 *
 * Generates ADC frames (uint16, 12 bit) exactly as the STM32 frontend would
 * deliver them: beat tones of point targets incl. Doppler, antenna leakage,
 * switched gain, front-end + ADC noise, IF high-pass filters, quantisation
 * and clipping.
 */
#ifndef SIM_H
#define SIM_H

#include <stdint.h>

#define SIM_MAX_TGT 8

typedef struct {
    float range_m;
    float vr_mps;      /**< range rate (climb > 0) */
    float amp_lsb;     /**< beat amplitude at gain level 0 [LSB] */
} sim_target_t;

typedef struct {
    /* radar */
    float sweep_hz;
    float slope_hz_s;
    float lambda_m;
    float fs_hz;
    uint32_t frame_len;
    int vco_sign;            /**< +1 / -1 */
    /* analog */
    float gain_lin[4];
    float noise_front_lsb;   /**< rms at gain 0, scaled with gain */
    float noise_adc_lsb;     /**< rms, not scaled */
    float adc_mid;
    float leak_amp_lsb;      /**< leakage amplitude at gain 0 */
    float hp_fc_hz;          /**< IF high-pass corner (0 = off) */
    int hp_order;
    int stuck;               /**< 1: deliver a constant frame (fault) */
    /* state */
    uint32_t rng;
    double t;
    double hp_x[4], hp_y[4];
    sim_target_t tgt[SIM_MAX_TGT];
    int n_tgt;
} sim_t;

void sim_init(sim_t *s, float sweep_hz);
/** Select the ramp mode (frame length and slope). */
void sim_set_rmode(sim_t *s, unsigned rmode);
/** Generate one frame for the given DAC direction and gain level. */
void sim_frame(sim_t *s, int dac_dir, int gain, uint16_t *out);
/** Move all targets by their range rate over dt. */
void sim_advance(sim_t *s, float dt);
float sim_randn(sim_t *s);

#endif /* SIM_H */
