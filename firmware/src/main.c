/**
 * @file main.c
 * @brief 24 GHz FMCW radar altimeter - STM32F405RG main loop.
 *
 * Interrupts only move data (DMA, UART). All processing runs in the main
 * loop: fetch frame -> alt_process_frame() -> outputs. The watchdog is only
 * kicked while frames keep arriving, so a stalled acquisition resets the MCU.
 */
#include "app.h"
#include "bsp.h"
#include "frontend.h"
#include "proto.h"
#include <string.h>

app_t g_app;
static uint16_t s_buf[RADAR_FRAME_LEN_MAX];
static uint16_t s_frame[2][RADAR_FRAME_LEN_MAX];   /* last frame per DAC direction */
static alt_frame_info_t s_frame_info[2];

void app_restart_frontend(void)
{
    frontend_start(cfg_active_module(&g_app.cfg), g_app.cfg.dac_buffer, alt_rmode_request(&g_app.alt));
    alt_apply_config(&g_app.alt);
    g_app.fe_restarts++;
}

int app_save(void)
{
    cfg_seal(&g_app.cfg);
    frontend_stop();
    const int rc = fs_save(&g_app.cfg, g_app.bg, APP_BG_LEN, g_app.alt.bg_valid);
    app_restart_frontend();
    return rc;
}

void app_dump_frame(void)
{
    char line[96];
    for (unsigned d = 0; d < 2u; d++) {
        const alt_frame_info_t *fi = &s_frame_info[d];
        size_t n = 0u;
        memcpy(line, "#DUMP,", 6u); n = 6u;
        line[n++] = (char)('0' + d);
        line[n++] = ',';
        line[n++] = (char)('0' + (fi->gain & 3u));
        const uint32_t len = radar_frame_len(fi->rmode);
        line[n++] = ',';
        n += proto_fmt_fixed(&line[n], (float)len, 0u);
        line[n++] = '\r';
        line[n++] = '\n';
        uart_write_blocking(line, n);
        for (uint32_t i = 0; i < len; i++) {
            n = proto_fmt_fixed(line, (float)s_frame[d][i], 0u);
            line[n++] = ((i % 16u) == 15u) ? '\n' : ',';
            uart_write_blocking(line, n);
        }
        uart_puts("\r\n#END\r\n");
    }
}

static void emit(const alt_output_t *o)
{
    static uint8_t counter;
    char buf[128];
    if (g_app.cfg.out_mode >= 1u) {
        const size_t n = proto_ralt(buf, sizeof(buf), o);
        (void)uart_write(buf, n);
    }
    if (g_app.cfg.out_mode >= 2u) {
        const size_t n = proto_rdbg(buf, sizeof(buf), o);
        (void)uart_write(buf, n);
    }
    if (g_app.cfg.can_enable && g_app.can_ok) {
        uint8_t d[8];
        proto_can_alt(d, o, counter++);
        (void)can_send(g_app.cfg.can_id, d, 8u);
        if ((o->seq & 7u) == 0u) {
            proto_can_diag(d, o);
            (void)can_send((uint16_t)(g_app.cfg.can_id + 1u), d, 8u);
        }
    }
    board_led_valid((o->status & ALT_ST_VALID) != 0u);
    if ((o->seq & 15u) == 0u) { board_led_toggle(); }
}

int main(void)
{
    (void)clock_init();
    board_init();
    uart_init(115200u);

    if (fs_load_config(&g_app.cfg) != 0) {
        cfg_defaults(&g_app.cfg);
        uart_puts("\r\n# config: defaults (flash empty or invalid)\r\n");
    }
    const uint8_t bgm = fs_load_bg(g_app.bg, APP_BG_LEN);
    alt_init(&g_app.alt, &g_app.cfg, g_app.bg, bgm);

    g_app.can_ok = (uint8_t)(can_init(g_app.cfg.can_kbps) == 0);
    uart_puts("# RADAR ALTIMETER 24GHz FMCW - 'help' for commands\r\n");
    if (!g_app.can_ok) { uart_puts("# CAN init failed (no bus/transceiver?)\r\n"); }

    frontend_init();
    frontend_set_gain(alt_gain_request(&g_app.alt));
    app_restart_frontend();
    iwdg_init_ms(250u);

    uint32_t last_frame_ms = bsp_millis();
    uint32_t last_err = frontend_errors();
    uint32_t last_restart_ms = 0u;

    for (;;) {
        alt_frame_info_t fi;
        if (frontend_fetch(s_buf, &fi)) {
            board_dbg(1);
            alt_set_ext_status(&g_app.alt, bsp_clock_fallback() ? ALT_ST_CLK_FALLBACK : 0u);
            const int out = alt_process_frame(&g_app.alt, s_buf, &fi);
            frontend_set_gain(alt_gain_request(&g_app.alt));
            if (out) { emit(&g_app.alt.out); }
            memcpy(s_frame[fi.dac_dir], s_buf, radar_frame_len(fi.rmode) * sizeof(uint16_t));  /* for 'dump' */
            s_frame_info[fi.dac_dir] = fi;
            /* ramp mode change requested by the pipeline: reconfigure at once */
            if (alt_rmode_request(&g_app.alt) != frontend_rmode()) {
                frontend_start(cfg_active_module(&g_app.cfg), g_app.cfg.dac_buffer,
                               alt_rmode_request(&g_app.alt));
            }
            board_dbg(0);
            last_frame_ms = bsp_millis();
        }

        /* acquisition errors: restart the frontend (rate limited) */
        const uint32_t now = bsp_millis();
        const uint32_t err = frontend_errors();
        if ((err != last_err) && ((now - last_restart_ms) > 100u)) {
            last_err = err;
            last_restart_ms = now;
            app_restart_frontend();
            last_frame_ms = now;
        }

        cli_poll();

        if ((now - last_frame_ms) < 100u) { iwdg_kick(); }
    }
}
