/**
 * @file frontend.h
 * @brief Radar acquisition: TIM2 triggers DAC (ramp) and ADC (IF) together.
 *
 *   TIM2 TRGO (300 kHz) --+--> DAC1 ch1 <- DMA1 Stream5 (circular, 2 frames triangle)
 *                         +--> ADC1 ch14 -> DMA2 Stream0 (circular, 2 frames)
 *
 * Both DMA loops have the same length, so DAC sample i and ADC sample i are
 * locked forever (no software timing, no restart between ramps). The DMA
 * half-transfer / transfer-complete interrupts mark the end of the rising /
 * falling ramp. Gain changes are applied exactly at frame boundaries.
 */
#ifndef FRONTEND_H
#define FRONTEND_H

#include <stdint.h>
#include "config.h"
#include "altimeter.h"

void frontend_init(void);
void frontend_start(const cfg_module_t *m, uint8_t dac_buffer);
void frontend_stop(void);

/** Request a gain level; applied at the next frame boundary. */
void frontend_set_gain(uint8_t level);

/**
 * Copy the latest complete frame (RADAR_FRAME_LEN samples) into dst.
 * @return 1 if a frame was copied
 */
int frontend_fetch(uint16_t *dst, alt_frame_info_t *info);

uint32_t frontend_errors(void);   /**< DMA / ADC overrun errors */

#endif /* FRONTEND_H */
