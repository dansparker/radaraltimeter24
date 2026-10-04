/**
 * @file startup_stm32f405.c
 * @brief Vector table and reset handler for the STM32F405RG (C, no assembler).
 */
#include <stdint.h>
#include "stm32f4xx.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _sccm, _eccm, _estack;
extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);

#define WEAK __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK;
void HardFault_Handler(void) WEAK;
void MemManage_Handler(void) WEAK;
void BusFault_Handler(void) WEAK;
void UsageFault_Handler(void) WEAK;
void SVC_Handler(void) WEAK;
void DebugMon_Handler(void) WEAK;
void PendSV_Handler(void) WEAK;
void SysTick_Handler(void) WEAK;
void DMA2_Stream0_IRQHandler(void) WEAK;
void USART1_IRQHandler(void) WEAK;

typedef void (*vec_t)(void);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
__attribute__((section(".isr_vector"), used))
const vec_t g_vectors[16u + 82u] = {
    [0 ... 97] = Default_Handler,
    [0] = (vec_t)(uintptr_t)&_estack,
    [1] = Reset_Handler,
    [2] = NMI_Handler,
    [3] = HardFault_Handler,
    [4] = MemManage_Handler,
    [5] = BusFault_Handler,
    [6] = UsageFault_Handler,
    [7] = 0, [8] = 0, [9] = 0, [10] = 0,
    [11] = SVC_Handler,
    [12] = DebugMon_Handler,
    [13] = 0,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    [16 + DMA2_Stream0_IRQn] = DMA2_Stream0_IRQHandler,
    [16 + USART1_IRQn] = USART1_IRQHandler,
};
#pragma GCC diagnostic pop

void Reset_Handler(void)
{
    /* FPU full access before any floating point code runs */
    SCB->CPACR |= (0xFu << 20);
    __DSB();
    __ISB();

    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata;) { *dst++ = *src++; }
    for (uint32_t *dst = &_sbss; dst < &_ebss;) { *dst++ = 0u; }
    for (uint32_t *dst = &_sccm; dst < &_eccm;) { *dst++ = 0u; }

    (void)main();
    for (;;) { }
}

/* Unexpected interrupt / fault: stop here, the independent watchdog resets
 * the MCU (outputs go silent -> receivers detect the missing data). */
void Default_Handler(void)
{
    __disable_irq();
    for (;;) { }
}
