/**
 * @file config.c
 * @brief Persistent configuration defaults and validation.
 */
#include "config.h"
#include <string.h>
#include <math.h>

uint32_t cfg_crc32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void cfg_defaults(config_t *c)
{
    memset(c, 0, sizeof(*c));          /* deterministic padding for the CRC */
    c->magic = CFG_MAGIC;
    c->version = CFG_VERSION;
    c->module = MOD_KLC1A;
    c->out_mode = 1u;
    c->gain_mode = CFG_GAIN_AUTO;
    c->bg_enable = 0u;
    c->can_enable = 1u;
    c->dac_buffer = 1u;
    c->vco_sign = 1;
    c->detrend = 1u;
    c->can_id = 0x3A0u;
    c->can_kbps = 500u;
    c->max_range_m = 250.0f;
    c->min_range_m = 0.5f;

    /* Sweep values are START values only (module data unknown); they are
     * estimated from the legacy firmware constants and must be calibrated. */
    c->mod[MOD_KLC1A].dac_lo = 0u;
    c->mod[MOD_KLC1A].dac_hi = 2480u;
    c->mod[MOD_KLC1A].sweep_hz = 90.0e6f;
    c->mod[MOD_KLC1A].r_offset_m = 0.0f;

    c->mod[MOD_IVS465].dac_lo = 75u;
    c->mod[MOD_IVS465].dac_hi = 4091u;
    c->mod[MOD_IVS465].sweep_hz = 150.0e6f;
    c->mod[MOD_IVS465].r_offset_m = 0.0f;
    cfg_seal(c);
}

void cfg_seal(config_t *c)
{
    c->size = (uint16_t)sizeof(config_t);
    c->crc = cfg_crc32(c, offsetof(config_t, crc));
}

int cfg_is_valid(const config_t *c)
{
    if ((c->magic != CFG_MAGIC) || (c->version != CFG_VERSION) || (c->size != sizeof(config_t))) { return 0; }
    if (c->crc != cfg_crc32(c, offsetof(config_t, crc))) { return 0; }
    if (c->module >= MOD_COUNT) { return 0; }
    if ((c->gain_mode != CFG_GAIN_AUTO) && (c->gain_mode > 3u)) { return 0; }
    if ((c->vco_sign != 1) && (c->vco_sign != -1)) { return 0; }
    if (!(c->max_range_m > 1.0f) || !(c->max_range_m <= 2000.0f)) { return 0; }
    if (!(c->min_range_m >= 0.0f) || !(c->min_range_m < c->max_range_m)) { return 0; }
    for (unsigned i = 0; i < MOD_COUNT; i++) {
        const cfg_module_t *m = &c->mod[i];
        if ((m->dac_hi <= m->dac_lo) || (m->dac_hi > 4095u)) { return 0; }
        if (!(m->sweep_hz > 1.0e6f) || !(m->sweep_hz < 5.0e9f)) { return 0; }
        if (!isfinite(m->r_offset_m) || (fabsf(m->r_offset_m) > 50.0f)) { return 0; }
        if (!isfinite(m->ramp_q) || (fabsf(m->ramp_q) > 0.5f)) { return 0; }
    }
    return 1;
}

const cfg_module_t *cfg_active_module(const config_t *c)
{
    return &c->mod[(c->module < MOD_COUNT) ? c->module : 0u];
}
