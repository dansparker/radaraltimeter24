/**
 * @file uart.c
 * @brief UART with interrupt driven ring buffers.
 *        Rev A: USART1 PA9/PA10 (APB2), rev B: USART2 PA2/PA3 (APB1).
 */
#include "bsp.h"

#if defined(BOARD_REVB)
#define UARTx            USART2
#define UARTx_IRQn       USART2_IRQn
#define UARTx_IRQHandler USART2_IRQHandler
#define UARTx_PCLK_HZ    BSP_PCLK1_HZ
#define UARTx_PORT       GPIOA
#define UARTx_TX         2u
#define UARTx_RX         3u
#else
#define UARTx            USART1
#define UARTx_IRQn       USART1_IRQn
#define UARTx_IRQHandler USART1_IRQHandler
#define UARTx_PCLK_HZ    BSP_PCLK2_HZ
#define UARTx_PORT       GPIOA
#define UARTx_TX         9u
#define UARTx_RX         10u
#endif

#define TX_SIZE 2048u   /* power of two */
#define RX_SIZE 256u

static uint8_t s_tx[TX_SIZE];
static volatile uint32_t s_tx_head, s_tx_tail;
static uint8_t s_rx[RX_SIZE];
static volatile uint32_t s_rx_head, s_rx_tail;
static volatile uint32_t s_dropped;

void uart_init(uint32_t baud)
{
#if defined(BOARD_REVB)
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR;
#else
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
#endif
    gpio_cfg(UARTx_PORT, UARTx_TX, 2u, 0u, 7u);
    gpio_cfg(UARTx_PORT, UARTx_RX, 2u, 1u, 7u);
    UARTx->CR1 = 0u;
    UARTx->BRR = (UARTx_PCLK_HZ + baud / 2u) / baud;
    UARTx->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    NVIC_SetPriority(UARTx_IRQn, 2u);
    NVIC_EnableIRQ(UARTx_IRQn);
}

static uint32_t tx_free(void)
{
    return TX_SIZE - 1u - ((s_tx_head - s_tx_tail) & (TX_SIZE - 1u));
}

size_t uart_write(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    if (len > tx_free()) {
        s_dropped++;
        return 0u;
    }
    uint32_t h = s_tx_head;
    for (size_t i = 0; i < len; i++) {
        s_tx[h] = p[i];
        h = (h + 1u) & (TX_SIZE - 1u);
    }
    s_tx_head = h;
    UARTx->CR1 |= USART_CR1_TXEIE;
    return len;
}

void uart_write_blocking(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len > 0u) {
        size_t chunk = (len > 64u) ? 64u : len;
        while (tx_free() < chunk) { iwdg_kick(); }
        (void)uart_write(p, chunk);
        p += chunk;
        len -= chunk;
    }
}

void uart_puts(const char *s)
{
    size_t n = 0u;
    while (s[n] != '\0') { n++; }
    uart_write_blocking(s, n);
}

int uart_getc(void)
{
    if (s_rx_head == s_rx_tail) { return -1; }
    const uint8_t c = s_rx[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1u) & (RX_SIZE - 1u);
    return c;
}

uint32_t uart_dropped(void)
{
    return s_dropped;
}

void UARTx_IRQHandler(void)
{
    const uint32_t sr = UARTx->SR;
    if ((sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0u) {
        const uint8_t c = (uint8_t)UARTx->DR;     /* reading DR clears the flags */
        const uint32_t n = (s_rx_head + 1u) & (RX_SIZE - 1u);
        if (((sr & USART_SR_RXNE) != 0u) && (n != s_rx_tail)) {
            s_rx[s_rx_head] = c;
            s_rx_head = n;
        }
    }
    if (((UARTx->CR1 & USART_CR1_TXEIE) != 0u) && ((sr & USART_SR_TXE) != 0u)) {
        if (s_tx_tail != s_tx_head) {
            UARTx->DR = s_tx[s_tx_tail];
            s_tx_tail = (s_tx_tail + 1u) & (TX_SIZE - 1u);
        } else {
            UARTx->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}
