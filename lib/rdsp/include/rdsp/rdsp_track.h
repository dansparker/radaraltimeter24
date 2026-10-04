/**
 * @file rdsp_track.h
 * @brief Step 8 - single target alpha-beta tracker with gating.
 *
 * State: position x (e.g. range) and rate v (dx/dt).
 *
 *  predict    xp = x + v*dt
 *  gate       |z - xp| <= gate_abs + gate_rel*|xp|
 *  update     x = xp + alpha*r,  v += beta*r/dt   (r = z - xp)
 *             optional direct rate measurement vz: v += gamma*(vz - v)
 *
 * Track management:
 *  LOST -> TENTATIVE   on the first measurement
 *  TENTATIVE -> CONFIRMED after confirm_n gated hits in a row
 *  CONFIRMED -> COAST  on a missing / out-of-gate measurement (prediction only)
 *  COAST -> LOST       after max_coast misses
 *  Re-acquisition: reacq_n consecutive out-of-gate measurements that agree
 *  with each other re-initialise the track (real jump, e.g. flying over a
 *  building edge), single outliers are rejected.
 */
#ifndef RDSP_TRACK_H
#define RDSP_TRACK_H

#include "rdsp_common.h"

typedef enum {
    RDSP_TRK_LOST = 0,
    RDSP_TRK_TENTATIVE,
    RDSP_TRK_CONFIRMED,
    RDSP_TRK_COAST
} rdsp_trk_state_t;

typedef struct {
    float alpha;        /**< position gain (0..1) */
    float beta;         /**< rate gain (0..~alpha^2/(2-alpha)) */
    float gamma;        /**< weight of a direct rate measurement (0 = unused) */
    float gate_abs;     /**< absolute gate [x units] */
    float gate_rel;     /**< relative gate (fraction of |x|) */
    float v_max;        /**< rate limit [x units/s] */
    uint16_t confirm_n; /**< hits to confirm */
    uint16_t max_coast; /**< misses until LOST */
    uint16_t reacq_n;   /**< consistent out-of-gate hits to re-acquire */
} rdsp_track_cfg_t;

typedef struct {
    rdsp_track_cfg_t cfg;
    rdsp_trk_state_t state;
    float x;
    float v;
    uint16_t hits;
    uint16_t misses;
    uint16_t alt_hits;
    float alt_x;
    float last_resid;
} rdsp_track_t;

void rdsp_track_init(rdsp_track_t *t, const rdsp_track_cfg_t *cfg);
void rdsp_track_reset(rdsp_track_t *t);

/** Predicted position after dt (no state change). */
float rdsp_track_predict(const rdsp_track_t *t, float dt);

/** Current gate half-width at the predicted position. */
float rdsp_track_gate(const rdsp_track_t *t, float dt);

/**
 * One tracker cycle.
 * @param has_z  1 if a measurement z is available
 * @param z      position measurement
 * @param vz     rate measurement or NAN if not available
 * @param dt     time since the last cycle [s]
 * @return 1 if z was accepted (in gate / used for (re)initialisation)
 */
int rdsp_track_update(rdsp_track_t *t, int has_z, float z, float vz, float dt);

/**
 * Like rdsp_track_update() but the rate is NOT adapted (beta = gamma = 0).
 * Use it for measurements that themselves depend on the predicted rate,
 * otherwise rate errors are fed back into the measurement (unstable loop).
 */
int rdsp_track_update_pos(rdsp_track_t *t, float z, float dt);

#endif /* RDSP_TRACK_H */
