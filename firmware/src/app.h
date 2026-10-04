/**
 * @file app.h
 * @brief Application context shared between main loop and command line.
 */
#ifndef APP_H
#define APP_H

#include "altimeter.h"
#include "config.h"

#define APP_BG_LEN (RADAR_NGAIN * 2u * RADAR_NFFT)

typedef struct {
    config_t cfg;
    altimeter_t alt;
    int16_t bg[APP_BG_LEN];
    uint8_t can_ok;
    uint32_t fe_restarts;
} app_t;

extern app_t g_app;

void app_restart_frontend(void);
int app_save(void);
void app_dump_frame(void);

void cli_poll(void);

#endif /* APP_H */
