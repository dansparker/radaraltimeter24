/**
 * @file cli.c
 * @brief Line based command interface on UART1 (non-blocking).
 *
 * Long running actions (calibration, background capture) run inside the
 * normal processing loop; cli_poll() reports their results when done.
 */
#include "app.h"
#include "bsp.h"
#include "frontend.h"
#include "proto.h"
#include "ramp.h"
#include <string.h>
#include <math.h>

#define CLI_LINE   64u
#define CAL_PAIRS  256u
#define BG_FRAMES  128u

static char s_line[CLI_LINE];
static uint32_t s_len;

static enum { OP_NONE = 0, OP_CAL1, OP_CAL2, OP_BG } s_op;
static float s_ref1, s_ref2, s_f1;
static uint8_t s_f1_ok;

/* ---- helpers ------------------------------------------------------------ */

static void out(const char *s) { uart_puts(s); }

static void out_f(const char *label, float v, unsigned dec, const char *unit)
{
    char b[24];
    out(label);
    (void)proto_fmt_fixed(b, v, dec);
    out(b[0] ? b : "-");
    out(unit);
}

static int parse_float(const char *s, float *v)
{
    float sign = 1.0f, r = 0.0f, scale = 0.0f;
    int digits = 0;
    while (*s == ' ') { s++; }
    if (*s == '-') { sign = -1.0f; s++; } else if (*s == '+') { s++; }
    for (; *s != '\0'; s++) {
        if ((*s >= '0') && (*s <= '9')) {
            r = r * 10.0f + (float)(*s - '0');
            if (scale > 0.0f) { scale *= 10.0f; }
            digits++;
        } else if ((*s == '.') && (scale == 0.0f)) {
            scale = 1.0f;
        } else if (*s == ' ') {
            break;
        } else {
            return -1;
        }
    }
    if (digits == 0) { return -1; }
    *v = sign * ((scale > 0.0f) ? r / scale : r);
    return 0;
}

static int parse_hex(const char *s, uint32_t *v)
{
    uint32_t r = 0u;
    int n = 0;
    while (*s == ' ') { s++; }
    if ((s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X'))) { s += 2; }
    for (; *s != '\0'; s++, n++) {
        const char c = *s;
        uint32_t d;
        if ((c >= '0') && (c <= '9')) { d = (uint32_t)(c - '0'); }
        else if ((c >= 'a') && (c <= 'f')) { d = (uint32_t)(c - 'a' + 10); }
        else if ((c >= 'A') && (c <= 'F')) { d = (uint32_t)(c - 'A' + 10); }
        else { return -1; }
        r = (r << 4) | d;
    }
    if (n == 0) { return -1; }
    *v = r;
    return 0;
}

/* "cmd arg" -> returns arg pointer if line starts with cmd as a word */
static const char *match(const char *line, const char *cmd)
{
    const size_t n = strlen(cmd);
    if (strncmp(line, cmd, n) != 0) { return NULL; }
    if (line[n] == '\0') { return &line[n]; }
    if (line[n] == ' ') { return &line[n + 1u]; }
    return NULL;
}

static void config_changed(void)
{
    alt_apply_config(&g_app.alt);
    out("OK (not saved - use 'save')\r\n");
}

/* ---- commands ----------------------------------------------------------- */

static void cmd_help(void)
{
    out("help                 this text\r\n"
        "info                 configuration and calibration\r\n"
        "stat                 counters\r\n"
        "out 0|1|2            output off / $RALT / $RALT+$RDBG\r\n"
        "module klc1a|ivs465  select radar module (ramp)\r\n"
        "gain auto|0..3       gain control\r\n"
        "cal1 <m> / cal2 <m>  2-point calibration at two known heights\r\n"
        "zero [m]             set offset so that the current altitude = m (default 0)\r\n"
        "sweep <MHz>          set RF sweep manually\r\n"
        "bg capture|on|off    background (leakage) capture - antenna to free sky!\r\n"
        "vsign 1|-1           VCO tuning direction\r\n"
        "range <m>            maximum range\r\n"
        "dacbuf 0|1           DAC output buffer\r\n"
        "can on|off|125|250|500|1000|id <hex>\r\n"
        "dump                 print the last raw ADC frames\r\n"
        "save / defaults / reset\r\n");
}

static void cmd_info(void)
{
    const config_t *c = &g_app.cfg;
    const cfg_module_t *m = cfg_active_module(c);
    out((c->module == MOD_IVS465) ? "module IVS-465\r\n" : "module K-LC1a\r\n");
    out_f("dac lo ", (float)m->dac_lo, 0u, "");
    out_f(" hi ", (float)m->dac_hi, 0u, "\r\n");
    out_f("sweep ", m->sweep_hz * 1.0e-6f, 3u, " MHz");
    out(m->calibrated ? " (calibrated)\r\n" : " (NOT calibrated - default value)\r\n");
    out_f("offset ", m->r_offset_m, 3u, " m\r\n");
    out_f("range resolution ", RDSP_C0_MPS * (float)(RADAR_FRAME_LEN - 1u) /
          (2.0f * m->sweep_hz * (float)RADAR_NFFT), 3u, " m/bin\r\n");
    out_f("max range ", c->max_range_m, 1u, " m\r\n");
    out_f("gain ", (c->gain_mode == CFG_GAIN_AUTO) ? -1.0f : (float)c->gain_mode, 0u, " (-1 = auto)\r\n");
    out_f("bg ", (float)c->bg_enable, 0u, "");
    out_f(" valid mask ", (float)g_app.alt.bg_valid, 0u, "\r\n");
    out_f("can ", (float)c->can_enable, 0u, "");
    out_f(" kbps ", (float)c->can_kbps, 0u, "");
    out_f(" id ", (float)c->can_id, 0u, g_app.can_ok ? " (ok)\r\n" : " (init failed)\r\n");
    out_f("vco sign ", (float)c->vco_sign, 0u, "\r\n");
}

static void cmd_stat(void)
{
    const alt_stats_t *s = &g_app.alt.st;
    out_f("frames ", (float)s->frames, 0u, "\r\n");
    out_f("pairs ", (float)s->pairs, 0u, "\r\n");
    out_f("overruns ", (float)s->overruns, 0u, "\r\n");
    out_f("settling ", (float)s->settling, 0u, "\r\n");
    out_f("clipped ", (float)s->clipped, 0u, "\r\n");
    out_f("no measurement ", (float)s->no_meas, 0u, "\r\n");
    out_f("gain changes ", (float)s->gain_changes, 0u, "\r\n");
    out_f("non-finite ", (float)s->nonfinite, 0u, "\r\n");
    out_f("frontend errors ", (float)frontend_errors(), 0u, "\r\n");
    out_f("frontend restarts ", (float)g_app.fe_restarts, 0u, "\r\n");
    out_f("uart dropped ", (float)uart_dropped(), 0u, "\r\n");
    out_f("can bus-off ", (float)can_bus_off(), 0u, "\r\n");
    out_f("gain ", (float)g_app.alt.out.gain, 0u, "\r\n");
}

static void start_cal(const char *arg, int point)
{
    float r;
    if ((parse_float(arg, &r) != 0) || (r < 0.5f) || (r > 300.0f)) {
        out("ERR height in m (0.5..300)\r\n");
        return;
    }
    if ((point == 2) && !s_f1_ok) { out("ERR run cal1 first\r\n"); return; }
    if (point == 1) { s_ref1 = r; s_op = OP_CAL1; } else { s_ref2 = r; s_op = OP_CAL2; }
    alt_cal_start(&g_app.alt, CAL_PAIRS);
    out("measuring - keep the target fixed ...\r\n");
}

static void finish_cal(void)
{
    float mean, sd;
    uint16_t n;
    if (!alt_cal_result(&g_app.alt, &mean, &sd, &n)) { return; }
    out_f("beat f_R = ", mean, 1u, " Hz");
    out_f(" +/- ", sd, 1u, " Hz\r\n");
    if (s_op == OP_CAL1) {
        s_f1 = mean;
        s_f1_ok = 1u;
        out("point 1 stored - move to the 2nd height (>= 2 m difference) and run cal2\r\n");
    } else {
        const float dr = s_ref2 - s_ref1, df = mean - s_f1;
        if ((fabsf(dr) < 2.0f) || (fabsf(df) < 1.0f) || ((dr > 0.0f) != (df > 0.0f))) {
            out("ERR calibration points not usable\r\n");
        } else {
            const float slope = RDSP_C0_MPS * df / (2.0f * dr);                 /* Hz/s */
            const float sweep = slope * (float)(RADAR_FRAME_LEN - 1u) / (float)RADAR_FS_HZ;
            const float off = s_ref1 - RDSP_C0_MPS * s_f1 / (2.0f * slope);
            if ((sweep < 1.0e6f) || (sweep > 5.0e9f) || (fabsf(off) > 50.0f)) {
                out("ERR result implausible\r\n");
            } else {
                cfg_module_t *m = &g_app.cfg.mod[g_app.cfg.module];
                m->sweep_hz = sweep;
                m->r_offset_m = off;
                m->calibrated = 1u;
                out_f("sweep ", sweep * 1.0e-6f, 3u, " MHz");
                out_f(", offset ", off, 3u, " m\r\n");
                config_changed();
            }
        }
        s_f1_ok = 0u;
    }
    alt_cal_abort(&g_app.alt);
    s_op = OP_NONE;
}

static void cmd_zero(const char *arg)
{
    float ref = 0.0f;
    const alt_output_t *o = &g_app.alt.out;
    if ((*arg != '\0') && (parse_float(arg, &ref) != 0)) { out("ERR value\r\n"); return; }
    if ((o->status & ALT_ST_VALID) == 0u) { out("ERR no valid altitude\r\n"); return; }
    cfg_module_t *m = &g_app.cfg.mod[g_app.cfg.module];
    const float off = m->r_offset_m + (ref - o->altitude_m);
    if (fabsf(off) > 50.0f) { out("ERR offset > 50 m\r\n"); return; }
    m->r_offset_m = off;
    out_f("offset ", off, 3u, " m\r\n");
    config_changed();
}

static void cmd_can(const char *a)
{
    config_t *c = &g_app.cfg;
    const char *p;
    uint32_t id;
    if (strcmp(a, "on") == 0) { c->can_enable = 1u; }
    else if (strcmp(a, "off") == 0) { c->can_enable = 0u; }
    else if ((p = match(a, "id")) != NULL) {
        if ((parse_hex(p, &id) != 0) || (id > 0x7FEu)) { out("ERR id 0..7FE\r\n"); return; }
        c->can_id = (uint16_t)id;
    } else {
        float k;
        if ((parse_float(a, &k) != 0) || ((k != 125.0f) && (k != 250.0f) && (k != 500.0f) && (k != 1000.0f))) {
            out("ERR\r\n");
            return;
        }
        c->can_kbps = (uint16_t)k;
        g_app.can_ok = (uint8_t)(can_init(c->can_kbps) == 0);
    }
    out("OK (not saved - use 'save')\r\n");
}

static void execute(char *l)
{
    const char *a;
    float v;
    config_t *c = &g_app.cfg;

    if ((s_op != OP_NONE) && (strcmp(l, "abort") != 0)) { out("BUSY ('abort')\r\n"); return; }

    if ((strcmp(l, "help") == 0) || (strcmp(l, "?") == 0)) { cmd_help(); }
    else if (strcmp(l, "info") == 0) { cmd_info(); }
    else if (strcmp(l, "stat") == 0) { cmd_stat(); }
    else if (strcmp(l, "abort") == 0) { alt_cal_abort(&g_app.alt); s_op = OP_NONE; out("OK\r\n"); }
    else if ((a = match(l, "out")) != NULL) {
        if ((parse_float(a, &v) != 0) || (v < 0.0f) || (v > 2.0f)) { out("ERR\r\n"); return; }
        c->out_mode = (uint8_t)v;
        out("OK\r\n");
    }
    else if ((a = match(l, "module")) != NULL) {
        if (strcmp(a, "klc1a") == 0) { c->module = MOD_KLC1A; }
        else if (strcmp(a, "ivs465") == 0) { c->module = MOD_IVS465; }
        else { out("ERR klc1a|ivs465\r\n"); return; }
        app_restart_frontend();
        out("OK (not saved - use 'save')\r\n");
    }
    else if ((a = match(l, "gain")) != NULL) {
        if (strcmp(a, "auto") == 0) { c->gain_mode = CFG_GAIN_AUTO; }
        else if ((parse_float(a, &v) == 0) && (v >= 0.0f) && (v <= 3.0f)) { c->gain_mode = (uint8_t)v; }
        else { out("ERR\r\n"); return; }
        config_changed();
    }
    else if ((a = match(l, "cal1")) != NULL) { start_cal(a, 1); }
    else if ((a = match(l, "cal2")) != NULL) { start_cal(a, 2); }
    else if ((a = match(l, "zero")) != NULL) { cmd_zero(a); }
    else if ((a = match(l, "sweep")) != NULL) {
        if ((parse_float(a, &v) != 0) || (v < 1.0f) || (v > 5000.0f)) { out("ERR MHz\r\n"); return; }
        c->mod[c->module].sweep_hz = v * 1.0e6f;
        config_changed();
    }
    else if ((a = match(l, "bg")) != NULL) {
        if (strcmp(a, "capture") == 0) {
            alt_bg_start(&g_app.alt, BG_FRAMES);
            s_op = OP_BG;
            out("capturing background for 4 gains ...\r\n");
        } else if (strcmp(a, "on") == 0) { c->bg_enable = 1u; config_changed(); }
        else if (strcmp(a, "off") == 0) { c->bg_enable = 0u; config_changed(); }
        else { out("ERR\r\n"); }
    }
    else if ((a = match(l, "vsign")) != NULL) {
        if ((parse_float(a, &v) != 0) || ((v != 1.0f) && (v != -1.0f))) { out("ERR\r\n"); return; }
        c->vco_sign = (int8_t)v;
        config_changed();
    }
    else if ((a = match(l, "range")) != NULL) {
        if ((parse_float(a, &v) != 0) || (v < 5.0f) || (v > 1000.0f)) { out("ERR\r\n"); return; }
        c->max_range_m = v;
        config_changed();
    }
    else if ((a = match(l, "dacbuf")) != NULL) {
        if ((parse_float(a, &v) != 0) || (v < 0.0f) || (v > 1.0f)) { out("ERR\r\n"); return; }
        c->dac_buffer = (uint8_t)v;
        app_restart_frontend();
        out("OK\r\n");
    }
    else if ((a = match(l, "can")) != NULL) { cmd_can(a); }
    else if (strcmp(l, "dump") == 0) { app_dump_frame(); }
    else if (strcmp(l, "save") == 0) {
        out("saving ...\r\n");
        out((app_save() == 0) ? "OK\r\n" : "ERR flash\r\n");
    }
    else if (strcmp(l, "defaults") == 0) {
        cfg_defaults(c);
        app_restart_frontend();
        out("OK defaults (not saved)\r\n");
    }
    else if (strcmp(l, "reset") == 0) {
        out("reset\r\n");
        bsp_delay_ms(20u);
        NVIC_SystemReset();
    }
    else if (l[0] != '\0') { out("ERR unknown command - 'help'\r\n"); }
}

void cli_poll(void)
{
    int ch;
    while ((ch = uart_getc()) >= 0) {
        if ((ch == '\r') || (ch == '\n')) {
            if (s_len > 0u) {
                s_line[s_len] = '\0';
                execute(s_line);
                s_len = 0u;
            }
        } else if ((ch == 8) || (ch == 127)) {
            if (s_len > 0u) { s_len--; }
        } else if ((s_len + 1u < CLI_LINE) && (ch >= 32) && (ch < 127)) {
            s_line[s_len++] = (char)((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch);
        }
    }

    if ((s_op == OP_CAL1) || (s_op == OP_CAL2)) {
        finish_cal();
    } else if ((s_op == OP_BG) && alt_bg_done(&g_app.alt)) {
        s_op = OP_NONE;
        out_f("background done, valid mask ", (float)g_app.alt.bg_valid, 0u,
              " - 'bg on' + 'save' to use it\r\n");
    }
}
