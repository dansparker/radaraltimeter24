/**
 * @file can.c
 * @brief bxCAN1 on PA11/PA12, transmit only, automatic bus-off recovery.
 *
 * Bit timing from PCLK1 = 42 MHz with 14 time quanta (1 + 11 + 2),
 * sample point 85.7 %: BRP = 42 MHz / (14 * bitrate).
 */
#include "bsp.h"

static uint8_t s_ok;

int can_init(uint16_t kbps)
{
    uint32_t brp;
    switch (kbps) {
    case 125u:  brp = 24u; break;
    case 250u:  brp = 12u; break;
    case 1000u: brp = 3u;  break;
    case 500u:
    default:    brp = 6u;  break;
    }
    s_ok = 0u;
    RCC->APB1ENR |= RCC_APB1ENR_CAN1EN;
    (void)RCC->APB1ENR;
    gpio_cfg(GPIOA, 11u, 2u, 1u, 9u);    /* RX with pull-up (recessive if no transceiver) */
    gpio_cfg(GPIOA, 12u, 2u, 0u, 9u);

    CAN1->MCR &= ~CAN_MCR_SLEEP;
    CAN1->MCR |= CAN_MCR_INRQ;
    for (uint32_t t = 0; (CAN1->MSR & CAN_MSR_INAK) == 0u; t++) {
        if (t > 200000u) { return -1; }
    }
    CAN1->MCR = CAN_MCR_INRQ | CAN_MCR_ABOM | CAN_MCR_TXFP;
    CAN1->BTR = (0u << 24) | ((2u - 1u) << 20) | ((11u - 1u) << 16) | (brp - 1u);

    /* no receive filters active: the altimeter only transmits */
    CAN1->FMR |= CAN_FMR_FINIT;
    CAN1->FA1R = 0u;
    CAN1->FMR &= ~CAN_FMR_FINIT;

    CAN1->MCR &= ~CAN_MCR_INRQ;
    for (uint32_t t = 0; (CAN1->MSR & CAN_MSR_INAK) != 0u; t++) {
        if (t > 200000u) { return -1; }   /* bus never idle (no transceiver?) */
    }
    s_ok = 1u;
    return 0;
}

int can_send(uint16_t std_id, const uint8_t *data, uint8_t dlc)
{
    if (!s_ok || (dlc > 8u)) { return -1; }
    const uint32_t tsr = CAN1->TSR;
    uint32_t mb;
    if ((tsr & CAN_TSR_TME0) != 0u) { mb = 0u; }
    else if ((tsr & CAN_TSR_TME1) != 0u) { mb = 1u; }
    else if ((tsr & CAN_TSR_TME2) != 0u) { mb = 2u; }
    else { return -1; }

    uint32_t lo = 0u, hi = 0u;
    for (uint8_t i = 0; i < dlc; i++) {
        if (i < 4u) { lo |= (uint32_t)data[i] << (8u * i); }
        else { hi |= (uint32_t)data[i] << (8u * (i - 4u)); }
    }
    CAN1->sTxMailBox[mb].TDTR = dlc;
    CAN1->sTxMailBox[mb].TDLR = lo;
    CAN1->sTxMailBox[mb].TDHR = hi;
    CAN1->sTxMailBox[mb].TIR = ((uint32_t)(std_id & 0x7FFu) << 21) | CAN_TI0R_TXRQ;
    return 0;
}

int can_bus_off(void)
{
    return ((CAN1->ESR & CAN_ESR_BOFF) != 0u) ? 1 : 0;
}
