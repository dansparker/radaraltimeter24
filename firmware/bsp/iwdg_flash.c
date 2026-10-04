/**
 * @file iwdg_flash.c
 * @brief Independent watchdog and configuration storage in flash sector 11.
 *
 * Sector 11 layout (128 KB @ 0x080E0000):
 *   0x0000  config_t (CRC inside)
 *   0x1000  bg header { magic, mask, n, crc }
 *   0x1010  background int16[n]
 */
#include "bsp.h"
#include <string.h>

/* ---- IWDG (LSI ~32 kHz) ---- */

static void iwdg_cfg(uint32_t pr, uint32_t rlr)
{
    IWDG->KR = 0x5555u;
    while ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0u) { }
    IWDG->PR = pr;
    IWDG->RLR = rlr;
    while ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0u) { }
    IWDG->KR = 0xAAAAu;
}

static uint32_t s_rlr = 249u;

void iwdg_init_ms(uint32_t ms)
{
    /* prescaler /32 -> 1 kHz tick (LSI 32 kHz), max 4095 ms */
    s_rlr = (ms > 4096u) ? 4095u : ((ms < 2u) ? 1u : ms - 1u);
    IWDG->KR = 0xCCCCu;
    iwdg_cfg(3u, s_rlr);
}

void iwdg_kick(void)
{
    IWDG->KR = 0xAAAAu;
}

void iwdg_set_long(int long_timeout)
{
    if (long_timeout) { iwdg_cfg(6u, 4095u); }   /* /256 -> ~32 s */
    else { iwdg_cfg(3u, s_rlr); }
}

/* ---- flash ---- */

#define FS_BASE     0x080E0000u
#define FS_SECTOR   11u
#define FS_BG_OFS   0x1000u
#define BG_MAGIC    0x47424B42u   /* "BKBG" */

typedef struct {
    uint32_t magic;
    uint32_t mask;
    uint32_t n;
    uint32_t crc;
} bg_hdr_t;

int fs_load_config(config_t *c)
{
    memcpy(c, (const void *)FS_BASE, sizeof(*c));
    return cfg_is_valid(c) ? 0 : -1;
}

uint8_t fs_load_bg(int16_t *bg, size_t n)
{
    const bg_hdr_t *h = (const bg_hdr_t *)(FS_BASE + FS_BG_OFS);
    const int16_t *src = (const int16_t *)(FS_BASE + FS_BG_OFS + sizeof(bg_hdr_t));
    if ((h->magic != BG_MAGIC) || (h->n != n) || (h->mask > 0xFFu)) { return 0u; }
    if (cfg_crc32(src, n * sizeof(int16_t)) != h->crc) { return 0u; }
    memcpy(bg, src, n * sizeof(int16_t));
    return (uint8_t)h->mask;
}

static int fl_wait(void)
{
    while ((FLASH->SR & FLASH_SR_BSY) != 0u) { }
    const uint32_t err = FLASH->SR & (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR);
    return (err != 0u) ? -1 : 0;
}

static int fl_program(uint32_t addr, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_PG;      /* 32 bit */
    for (size_t i = 0; i < len; i += 4u) {
        uint32_t w = 0xFFFFFFFFu;
        memcpy(&w, &p[i], ((len - i) >= 4u) ? 4u : (len - i));
        *(volatile uint32_t *)(addr + i) = w;
        if (fl_wait() != 0) { FLASH->CR = 0u; return -1; }
    }
    FLASH->CR = 0u;
    return 0;
}

int fs_save(const config_t *c, const int16_t *bg, size_t n, uint8_t bg_mask)
{
    int rc = 0;
    iwdg_set_long(1);
    __disable_irq();   /* flash is stalled during erase anyway */

    FLASH->KEYR = 0x45670123u;
    FLASH->KEYR = 0xCDEF89ABu;
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_SOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;

    FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_SER | (FS_SECTOR << 3);
    FLASH->CR |= FLASH_CR_STRT;
    if (fl_wait() != 0) { rc = -1; }
    FLASH->CR = 0u;

    if (rc == 0) { rc = fl_program(FS_BASE, c, sizeof(*c)); }
    if ((rc == 0) && (bg != NULL) && (bg_mask != 0u)) {
        bg_hdr_t h = { BG_MAGIC, bg_mask, (uint32_t)n, cfg_crc32(bg, n * sizeof(int16_t)) };
        rc = fl_program(FS_BASE + FS_BG_OFS + sizeof(h), bg, n * sizeof(int16_t));
        if (rc == 0) { rc = fl_program(FS_BASE + FS_BG_OFS, &h, sizeof(h)); }  /* header last */
    }
    FLASH->CR |= FLASH_CR_LOCK;

    /* flush caches so that the new content is read back */
    FLASH->ACR &= ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    FLASH->ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH->ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    FLASH->ACR |= FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    __enable_irq();
    iwdg_set_long(0);

    if ((rc == 0) && (memcmp((const void *)FS_BASE, c, sizeof(*c)) != 0)) { rc = -1; }
    return rc;
}
