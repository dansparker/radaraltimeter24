/**
 * @file frontend.c
 * @brief Radar acquisition (see frontend.h).
 */
#include "frontend.h"
#include "bsp.h"
#include "ramp.h"
#include <string.h>

#define FE_LEN_MAX (2u * RADAR_FRAME_LEN_MAX)

/* DMA buffers must be in SRAM (not CCM) */
static uint16_t s_dac[FE_LEN_MAX];
static volatile uint16_t s_adc[FE_LEN_MAX];
static uint32_t s_len;                   /* frame length of the running mode */
static uint8_t s_rmode;

static volatile uint32_t s_frame_id;      /* completed frames */
static volatile uint32_t s_lost;          /* frames not fetched in time */
static volatile uint32_t s_err;
static volatile uint8_t s_ready;
static volatile alt_frame_info_t s_info;  /* info of the frame ready to fetch */
static volatile uint8_t s_cur_gain, s_pending_gain, s_settling;
static uint32_t s_lost_seen;

/* DMA stream CR fields */
#define DMA_CHSEL(n)  ((uint32_t)(n) << 25)
#define DMA_PL_VHIGH  (3u << 16)
#define DMA_MSIZE_16  (1u << 13)
#define DMA_PSIZE_16  (1u << 11)
#define DMA_MINC      (1u << 10)
#define DMA_CIRC      (1u << 8)
#define DMA_DIR_M2P   (1u << 6)
#define DMA_TCIE      (1u << 4)
#define DMA_HTIE      (1u << 3)
#define DMA_TEIE      (1u << 2)
#define DMA_EN        (1u << 0)

void frontend_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN | RCC_APB1ENR_DACEN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;
    NVIC_SetPriority(DMA2_Stream0_IRQn, 0u);
    NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

static void stream_disable(DMA_Stream_TypeDef *s)
{
    s->CR &= ~DMA_EN;
    while ((s->CR & DMA_EN) != 0u) { }
}

void frontend_stop(void)
{
    TIM2->CR1 &= ~TIM_CR1_CEN;
    ADC1->CR2 &= ~(ADC_CR2_EXTEN | ADC_CR2_DMA);
    DAC->CR &= ~DAC_CR_DMAEN1;
    stream_disable(DMA2_Stream0);
    stream_disable(DMA1_Stream5);
    DMA2->LIFCR = 0x3Du;      /* stream 0 flags */
    DMA1->HIFCR = 0xF40u;     /* stream 5 flags */
    s_ready = 0u;
}

void frontend_start(const cfg_module_t *m, uint8_t dac_buffer, uint8_t rmode)
{
    frontend_stop();
    s_rmode = (rmode < RADAR_NMODES) ? rmode : RADAR_RMODE_LONG;
    s_len = radar_frame_len(s_rmode);
    ramp_build(s_dac, s_len, m->dac_lo, m->dac_hi, m->ramp_q);

    /* TIM2: 84 MHz / 280 = 300 kHz, TRGO on update; no UG (would trigger) */
    TIM2->CR1 = 0u;
    TIM2->PSC = 0u;
    TIM2->ARR = (BSP_TIMCLK1_HZ / RADAR_FS_HZ) - 1u;
    TIM2->CNT = 0u;
    TIM2->CR2 = (2u << 4);    /* MMS = update */

    /* DAC ch1: trigger TIM2 TRGO (TSEL=100), DMA, optional output buffer */
    DAC->CR = 0u;
    DAC->SR = DAC_SR_DMAUDR1;
    DAC->DHR12R1 = s_dac[0];
    DMA1_Stream5->PAR = (uint32_t)&DAC->DHR12R1;
    DMA1_Stream5->M0AR = (uint32_t)s_dac;
    DMA1_Stream5->NDTR = 2u * s_len;
    DMA1_Stream5->FCR = 0u;
    DMA1_Stream5->CR = DMA_CHSEL(7) | DMA_PL_VHIGH | DMA_MSIZE_16 | DMA_PSIZE_16 |
                       DMA_MINC | DMA_CIRC | DMA_DIR_M2P;
    DMA1_Stream5->CR |= DMA_EN;
    DAC->CR = DAC_CR_EN1 | DAC_CR_TEN1 | (4u << 3) | DAC_CR_DMAEN1 | (dac_buffer ? 0u : DAC_CR_BOFF1);

    /* ADC1: PCLK2/4 = 21 MHz, 12 bit, ch14 15 cycles, trigger TIM2 TRGO */
    ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0;
    ADC1->CR1 = 0u;
    ADC1->SQR1 = 0u;
    ADC1->SQR3 = 14u;
    ADC1->SMPR1 = (ADC1->SMPR1 & ~(7u << 12)) | (1u << 12);
    ADC1->CR2 = ADC_CR2_ADON;
    bsp_delay_ms(1u);
    ADC1->SR = 0u;

    DMA2_Stream0->PAR = (uint32_t)&ADC1->DR;
    DMA2_Stream0->M0AR = (uint32_t)s_adc;
    DMA2_Stream0->NDTR = 2u * s_len;
    DMA2_Stream0->FCR = 0u;
    DMA2_Stream0->CR = DMA_CHSEL(0) | DMA_PL_VHIGH | DMA_MSIZE_16 | DMA_PSIZE_16 |
                       DMA_MINC | DMA_CIRC | DMA_TCIE | DMA_HTIE | DMA_TEIE;
    DMA2_Stream0->CR |= DMA_EN;
    ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_DMA | ADC_CR2_DDS | (6u << 24) | ADC_CR2_EXTEN_0;

    __disable_irq();
    board_set_gain(s_pending_gain);
    s_cur_gain = s_pending_gain;
    s_settling = 1u;           /* first frame after start contains the start transient */
    s_ready = 0u;
    __enable_irq();

    TIM2->CR1 = TIM_CR1_CEN;
}

void frontend_set_gain(uint8_t level)
{
    s_pending_gain = (uint8_t)(level & 3u);
}

uint8_t frontend_rmode(void)
{
    return s_rmode;
}

uint32_t frontend_errors(void)
{
    return s_err;
}

void DMA2_Stream0_IRQHandler(void)
{
    const uint32_t f = DMA2->LISR & 0x3Du;
    DMA2->LIFCR = f;

    /* DMA error, ADC overrun or DAC DMA underrun stop the acquisition loop;
     * they are counted here and the main loop restarts the frontend. */
    if ((f & (1u << 3)) != 0u) { s_err++; }                     /* TEIF0 */
    if ((ADC1->SR & ADC_SR_OVR) != 0u) { ADC1->SR = ~ADC_SR_OVR; s_err++; }
    if ((DAC->SR & DAC_SR_DMAUDR1) != 0u) { DAC->SR = DAC_SR_DMAUDR1; s_err++; }

    uint8_t half;
    if ((f & (1u << 5)) != 0u) { half = 1u; }                  /* TCIF0: falling ramp done */
    else if ((f & (1u << 4)) != 0u) { half = 0u; }             /* HTIF0: rising ramp done */
    else { return; }

    if (s_ready) { s_lost++; }
    s_frame_id++;
    s_info.id = s_frame_id;
    s_info.rmode = s_rmode;
    s_info.dac_dir = half;
    s_info.gain = s_cur_gain;
    s_info.settling = s_settling;
    s_info.overrun = 0u;
    s_ready = 1u;

    /* the next frame starts now: apply a pending gain change at the boundary */
    s_settling = 0u;
    if (s_pending_gain != s_cur_gain) {
        board_set_gain(s_pending_gain);
        s_cur_gain = s_pending_gain;
        s_settling = 1u;
    }
}

int frontend_fetch(uint16_t *dst, alt_frame_info_t *info)
{
    if (!s_ready) { return 0; }
    __disable_irq();
    alt_frame_info_t fi;
    fi.id = s_info.id;
    fi.rmode = s_info.rmode;
    fi.dac_dir = s_info.dac_dir;
    fi.gain = s_info.gain;
    fi.settling = s_info.settling;
    fi.overrun = 0u;
    const uint32_t lost = s_lost;
    s_ready = 0u;
    __enable_irq();

    const uint32_t len = radar_frame_len(fi.rmode);
    memcpy(dst, (const void *)&s_adc[fi.dac_dir ? len : 0u], len * sizeof(uint16_t));

    /* our half is overwritten only after the NEXT boundary: still same id -> data intact */
    if ((s_frame_id != fi.id) || (lost != s_lost_seen)) {
        fi.overrun = 1u;
        s_lost_seen = lost;
    }
    *info = fi;
    return 1;
}
