/**
 * @file proto.c
 * @brief Output protocols (see proto.h).
 */
#include "proto.h"
#include <math.h>
#include <string.h>

static size_t put_u32(char *d, uint32_t v)
{
    char t[10];
    size_t n = 0u;
    do { t[n++] = (char)('0' + (v % 10u)); v /= 10u; } while (v != 0u);
    for (size_t i = 0; i < n; i++) { d[i] = t[n - 1u - i]; }
    return n;
}

size_t proto_fmt_fixed(char *dst, float v, unsigned decimals)
{
    static const float pw[] = { 1.0f, 10.0f, 100.0f, 1000.0f };
    if (decimals > 3u) { decimals = 3u; }
    if (!isfinite(v)) { dst[0] = '\0'; return 0u; }
    float s = fabsf(v) * pw[decimals] + 0.5f;
    if (s > 2.0e9f) { s = 2.0e9f; }
    uint32_t q = (uint32_t)s;
    size_t n = 0u;
    if ((v < 0.0f) && (q != 0u)) { dst[n++] = '-'; }
    const uint32_t div = (uint32_t)pw[decimals];
    n += put_u32(&dst[n], q / div);
    if (decimals > 0u) {
        uint32_t f = q % div;
        dst[n++] = '.';
        for (unsigned i = decimals; i > 0u; i--) {
            dst[n + i - 1u] = (char)('0' + (f % 10u));
            f /= 10u;
        }
        n += decimals;
    }
    dst[n] = '\0';
    return n;
}

uint8_t proto_nmea_cs(const char *s, size_t len)
{
    uint8_t cs = 0u;
    for (size_t i = 0; i < len; i++) { cs ^= (uint8_t)s[i]; }
    return cs;
}

/* small bounded writer */
typedef struct { char *b; size_t cap, n; } wr_t;

static void w_str(wr_t *w, const char *s)
{
    while ((*s != '\0') && (w->n + 1u < w->cap)) { w->b[w->n++] = *s++; }
}
static void w_fix(wr_t *w, float v, unsigned dec)
{
    char t[24];
    (void)proto_fmt_fixed(t, v, dec);
    w_str(w, t);
}
static void w_u32(wr_t *w, uint32_t v)
{
    char t[12];
    t[put_u32(t, v)] = '\0';
    w_str(w, t);
}
static void w_hex(wr_t *w, uint32_t v, unsigned digits)
{
    static const char hx[] = "0123456789ABCDEF";
    char t[9];
    for (unsigned i = 0; i < digits; i++) { t[i] = hx[(v >> (4u * (digits - 1u - i))) & 0xFu]; }
    t[digits] = '\0';
    w_str(w, t);
}
static size_t w_finish(wr_t *w)
{
    /* checksum over everything after '$' */
    const uint8_t cs = proto_nmea_cs(&w->b[1], w->n - 1u);
    w_str(w, "*");
    w_hex(w, cs, 2u);
    w_str(w, "\r\n");
    w->b[w->n] = '\0';
    return w->n;
}

size_t proto_ralt(char *buf, size_t cap, const alt_output_t *o)
{
    wr_t w = { buf, cap, 0u };
    if (cap < 64u) { return 0u; }
    const int valid = (o->status & ALT_ST_VALID) != 0u;
    w_str(&w, "$RALT,");
    w_u32(&w, o->seq);
    w_str(&w, ",");
    w_fix(&w, valid ? o->altitude_m : NAN, 2u);
    w_str(&w, ",");
    w_fix(&w, valid ? o->vspeed_mps : NAN, 2u);
    w_str(&w, ",");
    w_fix(&w, o->snr_db, 1u);
    w_str(&w, ",");
    w_u32(&w, o->gain);
    w_str(&w, ",");
    w_hex(&w, o->status, 4u);
    return w_finish(&w);
}

size_t proto_rdbg(char *buf, size_t cap, const alt_output_t *o)
{
    wr_t w = { buf, cap, 0u };
    if (cap < 80u) { return 0u; }
    w_str(&w, "$RDBG,");
    w_u32(&w, o->seq);
    w_str(&w, ",");
    w_fix(&w, o->raw_range_m, 3u);
    w_str(&w, ",");
    w_fix(&w, o->f_rise_hz, 1u);
    w_str(&w, ",");
    w_fix(&w, o->f_fall_hz, 1u);
    w_str(&w, ",");
    w_fix(&w, o->f_r_hz, 1u);
    w_str(&w, ",");
    w_u32(&w, o->track_state);
    return w_finish(&w);
}

static int32_t sat_i32(float v)
{
    if (!isfinite(v)) { return 0; }
    if (v > 2147483000.0f) { return 2147483000; }
    if (v < -2147483000.0f) { return -2147483000; }
    return (int32_t)lrintf(v);
}

static int16_t sat_i16(float v)
{
    if (!isfinite(v)) { return 0; }
    if (v > 32767.0f) { return 32767; }
    if (v < -32768.0f) { return -32768; }
    return (int16_t)lrintf(v);
}

void proto_can_alt(uint8_t d[8], const alt_output_t *o, uint8_t counter)
{
    const int valid = (o->status & ALT_ST_VALID) != 0u;
    const uint32_t alt = (uint32_t)(valid ? sat_i32(o->altitude_m * 1000.0f) : 0);
    const uint16_t vs = (uint16_t)(valid ? sat_i16(o->vspeed_mps * 100.0f) : 0);
    d[0] = (uint8_t)alt;
    d[1] = (uint8_t)(alt >> 8);
    d[2] = (uint8_t)(alt >> 16);
    d[3] = (uint8_t)(alt >> 24);
    d[4] = (uint8_t)vs;
    d[5] = (uint8_t)(vs >> 8);
    d[6] = (uint8_t)(o->status & 0xFFu);
    d[7] = (uint8_t)((counter & 0x0Fu) | ((o->gain & 3u) << 4) |
                     (((o->status & ALT_ST_DEGRADED) != 0u) ? 0x40u : 0u) |
                     (((o->status & ALT_ST_BUSY) != 0u) ? 0x80u : 0u));
}

void proto_can_diag(uint8_t d[8], const alt_output_t *o)
{
    float snr = o->snr_db;
    if (!(snr > 0.0f)) { snr = 0.0f; }
    if (snr > 255.0f) { snr = 255.0f; }
    d[0] = (uint8_t)o->status;
    d[1] = (uint8_t)(o->status >> 8);
    d[2] = (uint8_t)snr;
    d[3] = o->track_state;
    d[4] = (uint8_t)o->seq;
    d[5] = (uint8_t)(o->seq >> 8);
    d[6] = (uint8_t)(o->seq >> 16);
    d[7] = (uint8_t)(o->seq >> 24);
}
