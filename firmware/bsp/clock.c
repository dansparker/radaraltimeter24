/**
 * @file clock.c
 * @brief System clock 168 MHz, SysTick 1 kHz, clock security system.
 *
 * HSE 8 MHz: PLLM=4 -> 2 MHz, PLLN=168 -> 336 MHz, PLLP=2 -> 168 MHz
 * HSI 16 MHz fallback: PLLM=8 gives the same frequencies.
 * If the crystal fails at run time the CSS raises an NMI; the handler
 * restarts the PLL from the HSI so all peripheral timings stay valid.
 * Note: the measured range does not depend on the clock accuracy (fs cancels
 * out, see docs/signal_chain.md), only the Doppler speed scales with it.
 */
#include "bsp.h"

uint32_t SystemCoreClock = 16000000u;
static volatile uint32_t s_ms;
static volatile uint8_t s_fallback;

void SystemInit(void) { }

static int pll_start(int use_hse)
{
    const uint32_t m = use_hse ? 4u : 8u;
    RCC->CR &= ~RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) != 0u) { }
    RCC->PLLCFGR = m | (168u << 6) | (0u << 16) | (7u << 24) |
                   (use_hse ? RCC_PLLCFGR_PLLSRC_HSE : 0u);
    RCC->CR |= RCC_CR_PLLON;
    for (uint32_t t = 0; (RCC->CR & RCC_CR_PLLRDY) == 0u; t++) {
        if (t > 1000000u) { return -1; }
    }
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2 | RCC_CFGR_SW)) |
                RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2 | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }
    SystemCoreClock = BSP_SYSCLK_HZ;
    return 0;
}

int clock_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_VOS;
    FLASH->ACR = FLASH_ACR_LATENCY_5WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    RCC->CR |= RCC_CR_HSEON;
    int hse_ok = 0;
    for (uint32_t t = 0; t < 500000u; t++) {
        if ((RCC->CR & RCC_CR_HSERDY) != 0u) { hse_ok = 1; break; }
    }
    if (!hse_ok || (pll_start(1) != 0)) {
        RCC->CR &= ~RCC_CR_HSEON;
        hse_ok = 0;
        s_fallback = 1u;
        (void)pll_start(0);
    }
    if (hse_ok) { RCC->CR |= RCC_CR_CSSON; }

    SysTick_Config(BSP_SYSCLK_HZ / 1000u);
    NVIC_SetPriority(SysTick_IRQn, 3u);
    return hse_ok;
}

void NMI_Handler(void)
{
    if ((RCC->CIR & RCC_CIR_CSSF) != 0u) {
        RCC->CIR |= RCC_CIR_CSSC;
        s_fallback = 1u;
        (void)pll_start(0);
    }
}

void SysTick_Handler(void)
{
    s_ms++;
}

uint32_t bsp_millis(void)
{
    return s_ms;
}

uint8_t bsp_clock_fallback(void)
{
    return s_fallback;
}

void bsp_delay_ms(uint32_t ms)
{
    const uint32_t t0 = s_ms;
    while ((s_ms - t0) < ms) { }
}
