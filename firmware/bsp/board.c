/**
 * @file board.c
 * @brief GPIO setup, gain switch, LEDs, debug pin, radar supply (see bsp.h).
 */
#include "bsp.h"

#if defined(BOARD_REVB)
#define GAIN_PORT   GPIOC
#define GAIN_A      6u      /* TS3A5017 IN1 */
#define GAIN_B      7u      /* TS3A5017 IN2 */
#define LED_PORT    GPIOB
#define LED_VALID   2u      /* WeAct board LED (check polarity on the board) */
#define DBG_PORT    GPIOB
#define DBG_PIN     13u
#define RF_EN_PORT  GPIOB
#define RF_EN_PIN   12u
#else
#define GAIN_PORT   GPIOB
#define GAIN_A      0u
#define GAIN_B      1u
#define LED_PORT    GPIOC
#define LED_VALID   14u     /* LED2 */
#define LED_HEART   15u     /* LED1 */
#define DBG_PORT    GPIOC
#define DBG_PIN     13u     /* FCT2 */
#endif

void gpio_cfg(GPIO_TypeDef *g, uint32_t pin, uint32_t mode, uint32_t pupd, uint32_t af)
{
    g->MODER = (g->MODER & ~(3u << (2u * pin))) | (mode << (2u * pin));
    g->PUPDR = (g->PUPDR & ~(3u << (2u * pin))) | (pupd << (2u * pin));
    g->OSPEEDR = (g->OSPEEDR & ~(3u << (2u * pin))) | (2u << (2u * pin));
    g->OTYPER &= ~(1u << pin);
    if (pin < 8u) {
        g->AFR[0] = (g->AFR[0] & ~(0xFu << (4u * pin))) | (af << (4u * pin));
    } else {
        g->AFR[1] = (g->AFR[1] & ~(0xFu << (4u * (pin - 8u)))) | (af << (4u * (pin - 8u)));
    }
}

void board_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN |
                    RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;

#if defined(BOARD_REVB)
    board_rf_enable(0);                 /* module stays off until the ramp runs */
    gpio_cfg(RF_EN_PORT, RF_EN_PIN, 1u, 0u, 0u);
#endif
    gpio_cfg(GPIOA, 4u, 3u, 0u, 0u);    /* DAC analog */
    gpio_cfg(GPIOC, 4u, 3u, 0u, 0u);    /* ADC analog */
    board_set_gain(0u);                 /* lowest gain before enabling outputs */
    gpio_cfg(GAIN_PORT, GAIN_A, 1u, 0u, 0u);
    gpio_cfg(GAIN_PORT, GAIN_B, 1u, 0u, 0u);
    gpio_cfg(LED_PORT, LED_VALID, 1u, 0u, 0u);
    gpio_cfg(DBG_PORT, DBG_PIN, 1u, 0u, 0u);
#if defined(BOARD_REVA)
    gpio_cfg(LED_PORT, LED_HEART, 1u, 0u, 0u);
    gpio_cfg(GPIOB, 12u, 0u, 1u, 0u);   /* FCT1 input pull-up */
#endif
}

/* Level -> A / B, one BSRR write = glitch free:
 * 0: A=1 B=1, 1: A=0 B=1, 2: A=1 B=0, 3: A=0 B=0.
 * Rev A: CD4052 via inverting BC847 (as in the legacy firmware).
 * Rev B: TS3A5017 IN1 = A, IN2 = B: 11 -> S4 (x1), 10 -> S3 (x4.04),
 *        01 -> S2 (x14.6), 00 -> S1 (x55.5) - the same level order. */
void board_set_gain(uint8_t level)
{
    static const uint32_t bsrr[4] = {
        (1u << GAIN_A) | (1u << GAIN_B),
        (1u << (GAIN_A + 16u)) | (1u << GAIN_B),
        (1u << GAIN_A) | (1u << (GAIN_B + 16u)),
        (1u << (GAIN_A + 16u)) | (1u << (GAIN_B + 16u)),
    };
    GAIN_PORT->BSRR = bsrr[level & 3u];
}

void board_led_valid(int on)
{
    LED_PORT->BSRR = on ? (1u << LED_VALID) : (1u << (LED_VALID + 16u));
}

void board_led_toggle(void)
{
#if defined(BOARD_REVA)
    LED_PORT->ODR ^= (1u << LED_HEART);
#endif
}

void board_dbg(int on)
{
    DBG_PORT->BSRR = on ? (1u << DBG_PIN) : (1u << (DBG_PIN + 16u));
}

void board_rf_enable(int on)
{
#if defined(BOARD_REVB)
    RF_EN_PORT->BSRR = on ? (1u << RF_EN_PIN) : (1u << (RF_EN_PIN + 16u));
#else
    (void)on;
#endif
}
