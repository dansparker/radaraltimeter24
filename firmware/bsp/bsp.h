/**
 * @file bsp.h
 * @brief Board support: clock, pins, UART, CAN, flash, watchdog.
 *
 * Two boards, selected with BOARD_REVA / BOARD_REVB (Makefile BOARD=reva|revb).
 * Common: PA4 DAC1_OUT1 -> VCO tuning filter, PC4 ADC1_IN14 <- IF chain,
 *         TIM2 300 kHz -> DAC (DMA1 S5) + ADC (DMA2 S0), 168 MHz from 8 MHz HSE.
 *
 * Rev A, STM32F405RG (schematic HF_Radar_uC_24GHz_v5):
 *   PB0/PB1    gain select A/B (CD4052 via BC847, inverted)
 *   PA9/PA10   USART1 TX/RX         PA11/PA12  CAN1 RX/TX
 *   PC14 LED2 (valid)   PC15 LED1 (heartbeat)   PC13 debug   PB12 FCT1 input
 *   config in flash sector 11 (0x080E0000)
 *
 * Rev B, STM32F446RE on the WeAct core board (hardware/rev_b_f446):
 *   PC6/PC7    gain select IN1/IN2 (TS3A5017)    PB12 RF_EN (radar LDO)
 *   PA2/PA3    USART2 TX/RX         PB9/PB8    CAN1 TX/RX (PA11/12 = USB)
 *   PB2 board LED (valid)           PB13 debug
 *   config in flash sector 7 (0x08060000), no CCM RAM
 */
#ifndef BSP_H
#define BSP_H

#include <stdint.h>
#include <stddef.h>
#include "stm32f4xx.h"

#if !defined(BOARD_REVA) && !defined(BOARD_REVB)
#error "define BOARD_REVA or BOARD_REVB"
#endif

#define BSP_SYSCLK_HZ  168000000u
#define BSP_PCLK1_HZ   42000000u
#define BSP_PCLK2_HZ   84000000u
#define BSP_TIMCLK1_HZ 84000000u   /* APB1 timer clock (x2) */

/* ---- clock / time ---- */
int clock_init(void);              /**< 168 MHz from 8 MHz HSE; returns 1 if HSE ok */
uint32_t bsp_millis(void);
uint8_t bsp_clock_fallback(void);  /**< 1 if running from HSI */
void bsp_delay_ms(uint32_t ms);

/* ---- board pins ---- */
/** mode: 0 in, 1 out, 2 AF, 3 analog; pupd: 0 none, 1 up, 2 down */
void gpio_cfg(GPIO_TypeDef *g, uint32_t pin, uint32_t mode, uint32_t pupd, uint32_t af);
void board_init(void);
void board_set_gain(uint8_t level);
void board_led_valid(int on);
void board_led_toggle(void);
void board_dbg(int on);
void board_rf_enable(int on);      /**< radar module supply (rev B; no-op on rev A) */

/* ---- UART (rev A USART1, rev B USART2) ---- */
void uart_init(uint32_t baud);
size_t uart_write(const void *data, size_t len);   /**< all or nothing, non-blocking */
void uart_write_blocking(const void *data, size_t len);
void uart_puts(const char *s);                      /**< blocking */
int uart_getc(void);                                /**< -1 if empty */
uint32_t uart_dropped(void);

/* ---- CAN1 ---- */
int can_init(uint16_t kbps);                        /**< 0 ok, -1 failed */
int can_send(uint16_t std_id, const uint8_t *data, uint8_t dlc);
int can_bus_off(void);

/* ---- independent watchdog ---- */
void iwdg_init_ms(uint32_t ms);
void iwdg_kick(void);
void iwdg_set_long(int long_timeout);               /**< ~32 s during flash erase */

/* ---- flash storage (last sector) ---- */
#include "config.h"
int fs_load_config(config_t *c);                    /**< 0 ok */
int fs_save(const config_t *c);                     /**< 0 ok */

#endif /* BSP_H */
