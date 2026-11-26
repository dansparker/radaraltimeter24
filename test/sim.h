/**
 * @file sim.h
 * @brief FMCW radar frontend simulator for host tests.
 *
 * Generates ADC frames (uint16, 12 bit) exactly as the STM32 frontend would
 * deliver them: beat tones of point targets incl. Doppler, antenna leakage,
 * switched gain, front-end + ADC noise, IF high-pass filters, quantisation
 * and clipping. In flight the carrier phase of the ground echo changes from
 * ramp to ramp (mm of height change, speckle); targets therefore get a random
 * phase per frame unless they are marked fixed (e.g. landing gear).
 */
#ifndef SIM_H
#define SIM_H

#include <stdint.h>

#define SIM_MAX_TGT 8
#define SIM_MAX_SCAT 512

/** one ground scatterer, fixed on the ground (aircraft at x = y = 0) */
typedef struct {
    float x, y;        /**< along / across track [m] */
    float c;           /**< Rayleigh amplitude factor, E[c^2] = 1 */
    float ph0;         /**< scattering phase */
} sim_scat_t;

/**
 * Extended ground target, calibrated to the K-LC1a data sheet: sensitivity
 * -114 dBc / 500 Hz (S/N 6 dB), 1/f IF noise (-134 dBm/Hz @ 500 Hz,
 * -164 dBm/Hz @ 1 MHz) plus TLV9064 noise, two-way Gaussian antenna pattern,
 * backscatter sigma0. The aircraft flies along x with vh and climbs with vz.
 * Every scatterer gets its own range, Doppler and phase in every frame, so
 * the Doppler spread of the beam, the range spread of the footprint and the
 * speckle are reproduced. See hardware/rev_b_f446/range_estimate.py.
 */
typedef struct {
    int enabled;
    float h_m;             /**< height above ground */
    float vz_mps;          /**< climb rate (> 0 up) */
    float vh_mps;          /**< ground speed */
    float sigma0_db;       /**< backscatter coefficient */
    float beam_along_deg;  /**< one-way -3 dB beam width along track */
    float beam_across_deg; /**< ... across track */
    float loss_db;         /**< radome, tolerances */
    int n;
    sim_scat_t s[SIM_MAX_SCAT];
} sim_ground_t;

typedef struct {
    float range_m;
    float vr_mps;      /**< range rate (climb > 0) */
    float amp_lsb;     /**< beat amplitude at gain level 0 [LSB] */
    int fixed;         /**< 1: fixed relative to the radar (constant phase) */
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
    int random_phase;        /**< 1: random carrier phase per frame (flight) */
    /* state */
    uint32_t rng;
    double t;
    double hp_x[4], hp_y[4];
    sim_target_t tgt[SIM_MAX_TGT];
    int n_tgt;
    sim_ground_t gnd;
} sim_t;

void sim_init(sim_t *s, float sweep_hz);
/** Generate one frame for the given DAC direction and gain level. */
void sim_frame(sim_t *s, int dac_dir, int gain, uint16_t *out);
/** Move all targets by their range rate over dt. */
void sim_advance(sim_t *s, float dt);
float sim_randn(sim_t *s);
/** Enable the extended ground (34 x 80 deg beam, 3 dB loss, SIM_MAX_SCAT scatterers). */
void sim_ground(sim_t *s, float h_m, float vh_mps, float vz_mps, float sigma0_db);

#endif /* SIM_H */
