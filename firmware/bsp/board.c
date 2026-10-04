/**
 * @file board.c
 * @brief GPIO setup, gain switch, LEDs, debug pin.
 */
#include "bsp.h"

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

    gpio_cfg(GPIOA, 4u, 3u, 0u, 0u);    /* DAC analog */
    gpio_cfg(GPIOC, 4u, 3u, 0u, 0u);    /* ADC analog */
    board_set_gain(0u);                 /* lowest gain before enabling outputs */
    gpio_cfg(GPIOB, 0u, 1u, 0u, 0u);
    gpio_cfg(GPIOB, 1u, 1u, 0u, 0u);
    gpio_cfg(GPIOC, 14u, 1u, 0u, 0u);   /* LED2 */
    gpio_cfg(GPIOC, 15u, 1u, 0u, 0u);   /* LED1 */
    gpio_cfg(GPIOC, 13u, 1u, 0u, 0u);   /* FCT2 debug */
    gpio_cfg(GPIOB, 12u, 0u, 1u, 0u);   /* FCT1 input pull-up */
}

/* Level -> PB0 (A) / PB1 (B) as in the legacy firmware (inverted by BC847):
 * 0: A=1 B=1, 1: A=0 B=1, 2: A=1 B=0, 3: A=0 B=0. One BSRR write = glitch free. */
void board_set_gain(uint8_t level)
{
    static const uint32_t bsrr[4] = {
        (1u << 0) | (1u << 1),
        (1u << (0 + 16)) | (1u << 1),
        (1u << 0) | (1u << (1 + 16)),
        (1u << (0 + 16)) | (1u << (1 + 16)),
    };
    GPIOB->BSRR = bsrr[level & 3u];
}

void board_led_valid(int on)
{
    GPIOC->BSRR = on ? (1u << 14) : (1u << (14 + 16));
}

void board_led_toggle(void)
{
    GPIOC->ODR ^= (1u << 15);
}

void board_dbg(int on)
{
    GPIOC->BSRR = on ? (1u << 13) : (1u << (13 + 16));
}
