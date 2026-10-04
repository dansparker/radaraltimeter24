/**
 * @file config.h
 * @brief Persistent configuration (stored in flash, CRC protected).
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>

#define CFG_MAGIC      0x544C4152u   /* "RALT" */
#define CFG_VERSION    2u
#define CFG_GAIN_AUTO  0xFFu
#define CFG_RMODE_AUTO  0u
#define CFG_RMODE_LONG  1u
#define CFG_RMODE_SHORT 2u

typedef enum {
    MOD_KLC1A = 0,
    MOD_IVS465 = 1,
    MOD_COUNT
} module_t;

typedef struct {
    uint16_t dac_lo;      /**< DAC code at the low end of the ramp */
    uint16_t dac_hi;      /**< DAC code at the high end of the ramp */
    float sweep_hz;       /**< RF sweep dac_lo -> dac_hi [Hz] (calibrated) */
    float r_offset_m;     /**< range offset R0 [m] (calibrated) */
    float ramp_q;         /**< quadratic ramp predistortion (0 = linear) */
    float sweep_short_hz; /**< sweep in SHORT mode, 0 = same as sweep_hz */
    uint8_t calibrated;   /**< 1 after a successful 2-point calibration */
    uint8_t pad[3];
} cfg_module_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t module;       /**< module_t */
    uint8_t out_mode;     /**< 0 off, 1 $RALT, 2 $RALT + $RDBG */
    uint8_t gain_mode;    /**< 0..3 fixed, CFG_GAIN_AUTO */
    uint8_t bg_enable;    /**< background subtraction on/off */
    uint8_t can_enable;
    uint8_t dac_buffer;   /**< DAC output buffer on/off */
    int8_t vco_sign;      /**< +1: f rises with DAC code, -1: falls */
    uint8_t detrend;      /**< linear detrend of each frame */
    uint8_t ramp_mode;    /**< CFG_RMODE_AUTO / LONG / SHORT */
    uint8_t pad0[3];
    uint16_t can_id;      /**< standard 11-bit id of the altitude frame */
    uint16_t can_kbps;    /**< 125, 250, 500, 1000 */
    float max_range_m;
    float min_range_m;
    cfg_module_t mod[MOD_COUNT];
    uint32_t crc;         /**< CRC-32 over all preceding bytes */
} config_t;

uint32_t cfg_crc32(const void *data, size_t len);
void cfg_defaults(config_t *c);
void cfg_seal(config_t *c);               /**< update size and crc */
int cfg_is_valid(const config_t *c);      /**< magic, version, size, crc, ranges */
const cfg_module_t *cfg_active_module(const config_t *c);
/** effective sweep for ramp mode rmode (0 long, 1 short) */
float cfg_sweep_hz(const cfg_module_t *m, unsigned rmode);

#endif /* CONFIG_H */
