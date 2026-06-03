/**
 * ================================================================
 *  Bootloader v3.2 - Version Validate + CCM RAM Support
 * ================================================================
 *  Changelog (v3.1 -> v3.2):
 *
 *  [v3.2] Add is_valid_version() to validate version strings
 *  [v3.2] Auto-fix boot.cfg if version fields are corrupted
 *  [v3.2] RTC backup register stores jump addr for APP VTOR
 *  [v3.2] boot_cfg_default reads SYS_VERSION from SD (if exists)
 *  [FIX]  jump_to_app: store jump addr in BKP4R (APP reads VTOR)
 *  [FIX]  is_app_valid: accept CCM RAM (0x1000xxxx) as valid SP
 *  [FIX]  Safe mode: KEY0 held at boot -> skip SD, fallback to A
 *
 *  startup_stm32f407xx.s Stack_Size set to 0x2000 (8KB)
 *
 *  Flash layout:
 *    0x08000000 +------------------+ Sector 0~5  (256KB) Bootloader
 *    0x08040000 +------------------+ Sector 6~8  (384KB) APP_A
 *    0x080A0000 +------------------+ Sector 9~11 (384KB) APP_B
 *    0x08100000 +------------------+ Flash end   1MB
 * ================================================================
 */

#include <stdarg.h>
#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/SDIO/sdio_sdcard.h"
#include "./FATFS/source/ff.h"
#include "./MALLOC/malloc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "stm32f4xx.h"
#include "boot_cfg.h"

/* ==============================================================
 *  Flash address definitions
 * ============================================================== */
#define FLASH_BOOT_ADDR         0x08000000
#define FLASH_BOOT_SIZE         (256 * 1024)

#define FLASH_APP_A_ADDR        0x08040000
#define FLASH_APP_B_ADDR        0x080A0000
#define FLASH_APP_SIZE          (384 * 1024)

/* SD file paths (boot.cfg overrides these defaults) */
#define SD_FW_FLAG          "0:/OTA/update.flag"
#define SD_FW_BIN           "0:/OTA/fw_new.bin"
#define SD_FW_PATCH         "0:/OTA/patch.bin"
#define SD_FW_OLD           "0:/OTA/fw_old.bin"

#define SYS_VERSION         "1.0.0"
#define BKUP_MAGIC          0x4F544142

/* Backup register mapping */
#define BKUP_REG_MAGIC      RTC->BKP0R
#define BKUP_REG_PART       RTC->BKP1R
#define BKUP_REG_VERSION    RTC->BKP2R
#define BKUP_REG_HEALTH     RTC->BKP3R
#define BKUP_REG_JUMP_ADDR  RTC->BKP4R    /* [FIX] jump addr for VTOR */

/* RGB565 color definitions */
#define C_WHITE     0xFFFF
#define C_BLACK     0x0000
#define C_RED       0xF800
#define C_GREEN     0x07E0
#define C_CYAN      0x07FF
#define C_ORANGE    0xFD20
#define C_GRAY      0x8430
#define C_DARKBLUE  0x01CF

static FATFS fs;
static uint8_t rw_buf[4096];

/* ==============================================================
 *  LCD helper functions
 * ============================================================== */
static void lcd_c(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                  uint8_t sz, const char *s, uint16_t color)
{
    lcd_show_string(x, y, w, h, sz, (char *)s, color);
}
static void lcd_title(uint16_t y, const char *s)
{ lcd_c(10, y, 300, 24, 24, s, C_DARKBLUE); }
static void lcd_info(uint16_t y, const char *s)
{ lcd_c(10, y, 300, 16, 16, s, C_BLACK); }
static void lcd_ok(uint16_t y, const char *s)
{ lcd_c(10, y, 300, 16, 16, s, C_GREEN); }
static void lcd_err(uint16_t y, const char *s)
{ lcd_c(10, y, 300, 16, 16, s, C_RED); }
static void lcd_warn(uint16_t y, const char *s)
{ lcd_c(10, y, 300, 16, 16, s, C_ORANGE); }
static void lcd_hint(uint16_t y, const char *s)
{ lcd_c(10, y, 300, 16, 16, s, C_GRAY); }
static void lcd_fmt(uint16_t y, uint16_t color, const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lcd_c(10, y, 300, 16, 16, buf, color);
}

/* ==============================================================
 *  SD card helpers
 * ============================================================== */
static bool sd_safe_init(void)
{
    delay_ms(300);
    for (uint8_t retry = 0; retry < 3; retry++) {
        printf("[SD] Attempt %d...\r\n", retry + 1);
        if (sd_init() == 0) {
            printf("[SD] Init OK\r\n");
            return true;
        }
        delay_ms(300);
    }
    printf("[SD] All attempts failed\r\n");
    return false;
}

static bool sd_reinit(void)
{
    f_mount(NULL, "0:", 0);
    delay_ms(50);
    memset(&fs, 0, sizeof(fs));
    if (f_mount(&fs, "0:", 0) != FR_OK) {
        printf("[SD] re-mount fail\r\n");
        return false;
    }
    delay_ms(50);
    return true;
}

/* ==============================================================
 *  RTC Backup Register helpers
 * ============================================================== */
static void bkup_enable(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_DBP;
}

static uint32_t pack_ver(const char *ver)
{
    int maj = 0, min = 0, pat = 0;
    sscanf(ver, "%d.%d.%d", &maj, &min, &pat);
    return (uint32_t)((maj << 24) | (min << 16) | (pat & 0xFFFF));
}

static void unpack_ver(uint32_t v, char *buf, int len)
{
    snprintf(buf, len, "%d.%d.%d",
             (int)((v >> 24) & 0xFF), (int)((v >> 16) & 0xFF),
             (int)(v & 0xFFFF));
}

static void bkup_save_update(char part, const char *ver)
{
    BKUP_REG_MAGIC   = BKUP_MAGIC;
    BKUP_REG_PART    = (uint32_t)part;
    BKUP_REG_VERSION = pack_ver(ver);
    printf("[BKUP] Save: part=%c ver=%s\r\n", part, ver);
}

static bool bkup_load_update(char *part, char *ver, int verlen)
{
    if (BKUP_REG_MAGIC != BKUP_MAGIC || BKUP_REG_VERSION == 0)
        return false;
    char p = (char)BKUP_REG_PART;
    if (p != 'A' && p != 'B') return false;
    *part = p;
    unpack_ver(BKUP_REG_VERSION, ver, verlen);
    return true;
}

static void bkup_clear_update(void)
{
    BKUP_REG_MAGIC   = 0;
    BKUP_REG_PART    = 0;
    BKUP_REG_VERSION = 0;
    printf("[BKUP] Cleared\r\n");
}

/* ==============================================================
 *  Bootloader address helper
 * ============================================================== */
static uint32_t get_app_addr(char part)
{
    return (part == 'B') ? FLASH_APP_B_ADDR : FLASH_APP_A_ADDR;
}

/* ==============================================================
 *  [v3.2] Version string validator
 *
 *  Format: "X.Y.Z", each segment 1~3 digits, total < 16 chars
 *  Valid:   "1.0.0", "2.3.15", "9.99.999"
 *  Invalid: "90.90.23130" (>3 digits), "abc", "", "1.2"
 * ============================================================== */
static bool is_valid_version(const char *ver)
{
    int dots, digits;

    if (!ver || !ver[0]) return false;
    if (strlen(ver) == 0 || strlen(ver) >= 16) return false;

    dots = 0;
    digits = 0;
    for (const char *p = ver; *p; p++) {
        if (*p == '.') {
            dots++;
            if (digits == 0 || digits > 3) return false;
            digits = 0;
        } else if (*p >= '0' && *p <= '9') {
            digits++;
            if (digits > 3) return false;
        } else {
            return false;
        }
    }
    if (digits == 0 || digits > 3) return false;
    return (dots == 2);
}

/* ==============================================================
 *  Flash operations - WORD aligned
 * ============================================================== */

/* Erase a partition (3 sectors) */
static bool erase_partition(char part)
{
    uint8_t start = (part == 'A') ? 6 : 9;
    printf("[FLASH] Erase %c (Sector %d~%d)\r\n", part, start, start + 2);
    lcd_info(80, "Erasing Flash...");

    HAL_FLASH_Unlock();
    for (uint8_t i = 0; i < 3; i++) {
        FLASH_EraseInitTypeDef erase;
        uint32_t err = 0;
        erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
        erase.Sector       = start + i;
        erase.NbSectors    = 1;
        erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        if (HAL_FLASHEx_Erase(&erase, &err) != HAL_OK) {
            printf("[FLASH] Erase fail sector %d\r\n", start + i);
            HAL_FLASH_Lock();
            return false;
        }
    }
    HAL_FLASH_Lock();
    printf("[FLASH] Erase OK\r\n");
    return true;
}

/* Write file from SD card to Flash partition, then verify */
static bool flash_file_to_partition(const char *path, char part)
{
    FIL fil; UINT br;
    uint32_t addr = get_app_addr(part);
    uint32_t written = 0, fsize, last_pct = 0xFF;

    printf("[FLASH] Flash %c from %s\r\n", part, path);

    if (!erase_partition(part))
        return false;

    lcd_info(80, "Writing firmware...");
    sd_reinit();
    if (f_open(&fil, path, FA_READ) != FR_OK) {
        printf("[FLASH] Open %s fail\r\n", path);
        return false;
    }
    fsize = (uint32_t)f_size(&fil);
    printf("[FLASH] Size: %luB\r\n", (unsigned long)fsize);
    lcd_fmt(100, C_GRAY, "Size: %lu bytes", (unsigned long)fsize);

    /* --- Write --- */
    HAL_FLASH_Unlock();
    while (1) {
        if (f_read(&fil, rw_buf, sizeof(rw_buf), &br) != FR_OK || br == 0)
            break;

        /* Write 4-byte aligned words */
        uint32_t aligned = br & ~3U;
        for (uint32_t i = 0; i < aligned; i += 4) {
            uint32_t word = *(uint32_t *)(rw_buf + i);
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word) != HAL_OK) {
                printf("[FLASH] Write fail @ 0x%08lX\r\n", (unsigned long)addr);
                HAL_FLASH_Lock();
                f_close(&fil);
                return false;
            }
            addr += 4;
        }
        /* Pad trailing bytes with 0xFF */
        if (br > aligned) {
            uint8_t tail[4] = {0xFF, 0xFF, 0xFF, 0xFF};
            memcpy(tail, rw_buf + aligned, br - aligned);
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, *(uint32_t *)tail);
            addr += 4;
        }

        written += br;
        /* Update progress on LCD */
        {
            uint32_t pct = fsize ? (written * 100 / fsize) : 0;
            if (pct != last_pct) {
                lcd_fmt(125, C_CYAN, "Writing... %lu%%", (unsigned long)pct);
                last_pct = pct;
            }
        }
    }
    HAL_FLASH_Lock();
    f_close(&fil);
    printf("[FLASH] Written %luB\r\n", (unsigned long)written);

    /* --- Verify --- */
    lcd_info(150, "Verifying...");
    sd_reinit();
    if (f_open(&fil, path, FA_READ) != FR_OK) {
        printf("[FLASH] Verify: reopen fail\r\n");
        return false;
    }
    {
        uint32_t vaddr = get_app_addr(part);
        uint32_t voff = 0;
        bool verify_ok = true;
        while (1) {
            if (f_read(&fil, rw_buf, sizeof(rw_buf), &br) != FR_OK || br == 0)
                break;
            if (memcmp(rw_buf, (void *)vaddr, br) != 0) {
                printf("[FLASH] Verify mismatch @ offset 0x%08lX\r\n",
                       (unsigned long)voff);
                verify_ok = false;
                break;
            }
            vaddr += br;
            voff += br;
        }
        f_close(&fil);
        if (!verify_ok) return false;
    }
    printf("[FLASH] Verify OK\r\n");
    return true;
}

/* ==============================================================
 *  SDIFF01 patch apply to Flash
 * ============================================================== */

/* Copy entire partition in Flash (src -> dst) */
static bool flash_copy_partition(char src_part, char dst_part)
{
    uint32_t src_addr = get_app_addr(src_part);
    uint32_t dst_addr = get_app_addr(dst_part);
    uint32_t chunk_buf[256];

    printf("[FLASH] Copy %c(0x%08lX) -> %c(0x%08lX)\r\n",
           src_part, (unsigned long)src_addr,
           dst_part, (unsigned long)dst_addr);

    for (uint32_t off = 0; off < FLASH_APP_SIZE; off += sizeof(chunk_buf)) {
        for (int i = 0; i < 256; i++) {
            chunk_buf[i] = *(volatile uint32_t *)(src_addr + off + i * 4);
        }
        for (int i = 0; i < 256; i++) {
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                  dst_addr + off + i * 4,
                                  chunk_buf[i]) != HAL_OK) {
                printf("[FLASH] Copy fail @ 0x%08lX\r\n",
                       (unsigned long)(dst_addr + off + i * 4));
                return false;
            }
        }
    }
    printf("[FLASH] Copy done (%luB)\r\n", (unsigned long)FLASH_APP_SIZE);
    return true;
}

/* Apply SDIFF01 patch: copy src -> dst, then overwrite changed chunks */
static bool apply_sdiff_to_flash(const char *patch_path,
                                  char src_part, char dst_part)
{
    FIL pf;
    UINT br;
    uint8_t hdr[16];
    uint32_t new_sz, count, i;
    uint8_t *dbuf;
    uint32_t patched = 0;
    uint32_t dst_addr = get_app_addr(dst_part);
    uint32_t t0 = HAL_GetTick();

    printf("[SDIFF] Apply %s: %c -> %c\r\n", patch_path, src_part, dst_part);

    /* Open and validate patch header */
    if (f_open(&pf, patch_path, FA_READ) != FR_OK) {
        printf("[SDIFF] open patch fail\r\n");
        return false;
    }
    if (f_read(&pf, hdr, 16, &br) != FR_OK || br != 16) {
        printf("[SDIFF] read hdr fail\r\n");
        f_close(&pf);
        return false;
    }
    if (memcmp(hdr, "SDIFF01", 7) != 0) {
        printf("[SDIFF] bad magic\r\n");
        f_close(&pf);
        return false;
    }
    new_sz = *(uint32_t *)(hdr + 8);
    count  = *(uint32_t *)(hdr + 12);
    printf("[SDIFF] new_sz=%lu count=%lu\r\n",
           (unsigned long)new_sz, (unsigned long)count);

    /* Step 1: Erase target partition */
    lcd_info(80, "Erasing target...");
    if (!erase_partition(dst_part)) {
        f_close(&pf);
        return false;
    }

    /* Step 2: Copy source partition to destination */
    lcd_info(80, "Copying partition...");
    HAL_FLASH_Unlock();
    if (!flash_copy_partition(src_part, dst_part)) {
        HAL_FLASH_Lock();
        f_close(&pf);
        return false;
    }

    /* Step 3: Apply patch chunks */
    lcd_info(80, "Applying patch...");
    dbuf = mymalloc(SRAMIN, 4096);
    if (!dbuf) {
        printf("[SDIFF] malloc fail\r\n");
        HAL_FLASH_Lock();
        f_close(&pf);
        return false;
    }

    for (i = 0; i < count; i++) {
        uint32_t offset, len;
        f_read(&pf, &offset, 4, &br);
        f_read(&pf, &len,    4, &br);

        /* Bounds check */
        if (len > 4096 || offset + len > FLASH_APP_SIZE) {
            printf("[SDIFF] chunk %lu invalid\r\n", (unsigned long)i);
            myfree(SRAMIN, dbuf);
            HAL_FLASH_Lock();
            f_close(&pf);
            return false;
        }

        f_read(&pf, dbuf, len, &br);

        /* Overwrite changed bytes at target address */
        uint32_t target = dst_addr + offset;
        uint32_t aligned = len & ~3U;
        for (uint32_t j = 0; j < aligned; j += 4) {
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              target + j, *(uint32_t *)(dbuf + j));
        }
        if (len > aligned) {
            uint8_t tail[4] = {0xFF, 0xFF, 0xFF, 0xFF};
            memcpy(tail, dbuf + aligned, len - aligned);
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              target + aligned, *(uint32_t *)tail);
        }

        patched += len;

        /* Update progress on LCD */
        if (count > 0 && (i % (count / 10 + 1) == 0 || i == count - 1)) {
            uint32_t pct = (i + 1) * 100 / count;
            lcd_fmt(100, C_CYAN, "Patching... %lu%%", (unsigned long)pct);
        }
    }

    HAL_FLASH_Lock();
    myfree(SRAMIN, dbuf);
    f_close(&pf);

    printf("[SDIFF] OK: %luB patched, %lu blocks, %lu ms\r\n",
           (unsigned long)patched, (unsigned long)count,
           (unsigned long)(HAL_GetTick() - t0));
    return true;
}

/* ==============================================================
 *  Full / Diff update entry points
 * ============================================================== */

static bool sd_full_update(const char *fw_path, char target_part,
                            const char *new_ver, BootCfg *cfg)
{
    if (!flash_file_to_partition(fw_path, target_part)) {
        lcd_err(80, "Flash write FAILED!");
        return false;
    }

    boot_cfg_update_ver(cfg, target_part, new_ver);
    boot_cfg_set_active(cfg, target_part);
    boot_cfg_write(cfg);

    lcd_ok(80, "Firmware written OK!");
    return true;
}

static bool sd_diff_update(const char *patch_path, char target_part,
                            char src_part, const char *new_ver,
                            BootCfg *cfg)
{
    if (!apply_sdiff_to_flash(patch_path, src_part, target_part)) {
        lcd_err(80, "DIFF UPDATE FAILED!");
        return false;
    }

    boot_cfg_update_ver(cfg, target_part, new_ver);
    boot_cfg_set_active(cfg, target_part);
    boot_cfg_write(cfg);

    lcd_ok(80, "DIFF UPDATE OK!");
    return true;
}

/* ==============================================================
 *  [FIX] Validate App before jump - accept CCM RAM (0x1000xxxx)
 *
 *  STM32F407 valid RAM regions:
 *    Main SRAM: 0x20000000 ~ 0x2001FFFF (128KB)
 *    CCM RAM:   0x10000000 ~ 0x1000FFFF (64KB, CPU-only)
 *
 *  Stack pointer from X-CUBE-AI may reside in CCM RAM,
 *  so SP = 0x1000xxxx must not be rejected.
 *  We accept both (sp & 0x2FF00000)==0x20000000 and
 *  (sp & 0xFFF00000)==0x10000000 as valid.
 * ============================================================== */

static bool is_app_valid(uint32_t addr)
{
    uint32_t sp = *(volatile uint32_t *)addr;
    uint32_t pc = *(volatile uint32_t *)(addr + 4);

    /* SP must be in Main SRAM (0x2000xxxx) or CCM RAM (0x1000xxxx) */
    bool sp_ok = ((sp & 0x2FF00000) == 0x20000000) ||
                 ((sp & 0xFFF00000) == 0x10000000);
    bool pc_ok = ((pc & 0xFF000000) == 0x08000000);

    return sp_ok && pc_ok;
}

/* ==============================================================
 *  Jump to Application
 * ============================================================== */

static void jump_to_app(uint32_t addr)
{
    uint32_t sp, pc;
    if (!is_app_valid(addr)) {
        printf("[BOOT] APP invalid @ 0x%08lX!\r\n", (unsigned long)addr);
        lcd_err(130, "APP INVALID! Halted.");
        while (1) { LED0_TOGGLE(); delay_ms(200); }
    }

    sp = *(volatile uint32_t *)addr;
    pc = *(volatile uint32_t *)(addr + 4);

    printf("[BOOT] Jump -> 0x%08lX  SP=0x%08lX  PC=0x%08lX\r\n",
           (unsigned long)addr, (unsigned long)sp, (unsigned long)pc);

    /* ============================================================
     *  [FIX] Store jump address in BKP4R
     *
     *  Problem: Bootloader sets SCB->VTOR = addr,
     *  but APP SystemInit() may reset VTOR to 0x08040000,
     *  causing wrong vector table for Partition B.
     *
     *  Solution: APP reads BKP4R at main() entry to set VTOR:
     *    SCB->VTOR = BKUP_REG_JUMP_ADDR;
     * ============================================================ */
    bkup_enable();
    BKUP_REG_JUMP_ADDR = addr;
    printf("[BKUP] Stored jump addr: 0x%08lX\r\n", (unsigned long)addr);

    /* Wait for USART TX to complete */
    while (!(USART1->SR & USART_SR_TC));

    /* Unmount SD card */
    f_mount(NULL, "0:", 0);

    /* Disable all interrupts */
    __disable_irq();

    /* Stop SysTick */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* Clear all NVIC pending and enabled interrupts */
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    /* Disable SDIO peripheral */
    SDIO->DCTRL = 0;
    SDIO->CLKCR = 0;
    SDIO->POWER = 0;
    SDIO->ICR   = 0x00C007FF;

    /* Disable DMA streams used by SDIO */
    DMA1_Stream0->CR &= ~DMA_SxCR_EN;
    DMA1_Stream5->CR &= ~DMA_SxCR_EN;
    DMA2_Stream0->CR &= ~DMA_SxCR_EN;
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    DMA2_Stream6->CR &= ~DMA_SxCR_EN;
    DMA1->LIFCR = 0xFFFFFFFF;
    DMA1->HIFCR = 0xFFFFFFFF;
    DMA2->LIFCR = 0xFFFFFFFF;
    DMA2->HIFCR = 0xFFFFFFFF;

    /* Disable USART1 (debug serial) */
    USART1->CR1 = 0;
    RCC->APB2ENR &= ~(1 << 11);

    /* Disable USART3 (WiFi module) */
    USART3->CR1 = 0;
    RCC->APB1ENR &= ~(1 << 18);

    /* Set vector table offset */
    SCB->VTOR = addr;

    /* Set MSP and jump */
    __set_MSP(sp);
    __DSB();
    __ISB();

    ((void (*)(void))pc)();

    /* Should never reach here */
    while (1) { LED0_TOGGLE(); delay_ms(100); }
}

/* ==============================================================
 *  Key and Debug helpers
 * ============================================================== */

/* Check if KEY0 (PE4) is pressed with debounce */
static uint8_t key0_pressed(void)
{
    if ((GPIOE->IDR & (1 << 4)) == 0) {
        delay_ms(10);
        if ((GPIOE->IDR & (1 << 4)) == 0) return 1;
    }
    return 0;
}

/* Dump first N words from Flash for debugging */
static void debug_dump_flash(uint32_t addr, int words)
{
    uint32_t *p = (uint32_t *)addr;
    printf("[DUMP] @ 0x%08lX:\r\n", (unsigned long)addr);
    for (int i = 0; i < words; i++) {
        printf("  [%02X] 0x%08lX\r\n", i * 4, (unsigned long)p[i]);
    }
    if (p[0] == 0xFFFFFFFF && p[1] == 0xFFFFFFFF) {
        printf("[DUMP] *** WARNING: Flash is blank! ***\r\n");
    }
}

/* ==============================================================
 *  MAIN
 * ============================================================== */
int main(void)
{
    BootCfg cfg;
    uint32_t target_addr;
    bool skip_sd = false;
    bool sd_ok = false;

    HAL_Init();
    sys_stm32_clock_init(336, 8, 2, 7);
    delay_init(168);
    usart_init(115200);
    led_init();
    lcd_init();
    my_mem_init(SRAMIN);
    bkup_enable();

    g_back_color  = C_WHITE;
    g_point_color = C_BLACK;

    printf("\r\n========================================\r\n");
    printf("  Bootloader v3.2 (version validate)\r\n");
    printf("  APP_A @ 0x%08lX (%luKB)\r\n",
           (unsigned long)FLASH_APP_A_ADDR,
           (unsigned long)(FLASH_APP_SIZE / 1024));
    printf("  APP_B @ 0x%08lX (%luKB)\r\n",
           (unsigned long)FLASH_APP_B_ADDR,
           (unsigned long)(FLASH_APP_SIZE / 1024));
    printf("  Stack: 0x%08lX\r\n",
           (unsigned long)__get_MSP());
    printf("========================================\r\n");

    lcd_clear(C_WHITE);
    lcd_title(10, "Bootloader v3.2");

    boot_cfg_default(&cfg);

    /* ==========================================================
     *  Safe mode (KEY0 held at boot)
     * ========================================================== */
    {
        uint8_t held = 0;
        for (int i = 0; i < 10; i++) {
            delay_ms(50);
            if (key0_pressed()) held++;
        }
        if (held >= 5) {
            printf("[BOOT] Safe mode\r\n");
            skip_sd = true;
        }
    }

    if (skip_sd) {
        /* Safe mode: try SD init, read boot.cfg for partition info */
        bool sd_safe = sd_safe_init();
        if (sd_safe) {
            if (f_mount(&fs, "0:", 1) != FR_OK) {
                sd_safe = false;
            }
        }

        /* Read boot.cfg to get partition versions */
        if (sd_safe) {
            if (!boot_cfg_read(&cfg)) {
                printf("[BOOT] Safe mode: boot.cfg read fail, using defaults\r\n");
                boot_cfg_default(&cfg);
            }
        }
        boot_cfg_dump(&cfg);

        bool a_ok = is_app_valid(get_app_addr('A'));
        bool b_ok = is_app_valid(get_app_addr('B'));

        printf("[BOOT] Safe mode: A=%s B=%s (cur=%c)\r\n",
               a_ok ? "OK" : "INVALID",
               b_ok ? "OK" : "INVALID",
               cfg.active[0]);

        if (a_ok && b_ok) {
            /* Both valid, boot the opposite of current */
            if (cfg.active[0] == 'B') {
                boot_cfg_set_active(&cfg, 'A');
            } else {
                boot_cfg_set_active(&cfg, 'B');
            }
        } else if (a_ok) {
            boot_cfg_set_active(&cfg, 'A');
        } else if (b_ok) {
            boot_cfg_set_active(&cfg, 'B');
        } else {
            lcd_clear(C_WHITE);
            lcd_err(40, "NO VALID PARTITION!");
            lcd_err(65, "Both A & B corrupted!");
            lcd_hint(100, "Recovery options:");
            lcd_hint(120, "1. SD card full update");
            lcd_hint(140, "2. Re-flash via Keil");
            printf("[BOOT] FATAL: No valid partition!\r\n");
            while (1) {
                LED0_TOGGLE();
                delay_ms(300);
            }
        }

        /* Safe mode: save corrected boot.cfg */
        if (sd_safe) {
            boot_cfg_write(&cfg);
            printf("[BOOT] Safe mode: saved active=%c to boot.cfg\r\n",
                   cfg.active[0]);
        }

        lcd_clear(C_WHITE);
        lcd_warn(40, "Safe Mode - Rollback");
        lcd_fmt(65, C_CYAN, "Booting Partition %c",
                cfg.active[0]);
        lcd_fmt(85, C_GREEN, "v%s @ 0x%08lX",
                boot_cfg_active_ver(&cfg),
                (unsigned long)boot_cfg_active_addr(&cfg));
        printf("[BOOT] Safe mode -> Partition %c @ 0x%08lX (v%s)\r\n",
               cfg.active[0],
               (unsigned long)boot_cfg_active_addr(&cfg),
               boot_cfg_active_ver(&cfg));
        delay_ms(500);
        goto do_jump;
    }


    /* ==========================================================
     *  SD card initialization
     * ========================================================== */
    lcd_info(40, "Init SD card...");
    sd_ok = sd_safe_init();

    if (sd_ok) {
        if (f_mount(&fs, "0:", 1) != FR_OK) {
            printf("[BOOT] Mount fail\r\n");
            lcd_err(60, "Mount failed!");
            sd_ok = false;
        } else {
            lcd_ok(60, "SD card OK");
        }
    } else {
        lcd_err(40, "SD card failed!");
        lcd_hint(65, "Booting from Flash...");
    }

    /* ==========================================================
     *  Load boot.cfg + [v3.2] version validation
     * ========================================================== */
    if (sd_ok) {
        if (!boot_cfg_read(&cfg)) {
            printf("[BOOT] boot.cfg not found, using defaults\r\n");
            boot_cfg_default(&cfg);
            boot_cfg_write(&cfg);
        }

        /* === [v3.2] Version field validation === */
        {
            bool need_fix = false;

            if (!is_valid_version(cfg.ver_a)) {
                printf("[BOOT] INVALID ver_a: [%s], reset to %s\r\n",
                       cfg.ver_a, SYS_VERSION);
                strncpy(cfg.ver_a, SYS_VERSION, sizeof(cfg.ver_a) - 1);
                cfg.ver_a[sizeof(cfg.ver_a) - 1] = '\0';
                need_fix = true;
            }
            if (!is_valid_version(cfg.ver_b)) {
                printf("[BOOT] INVALID ver_b: [%s], reset to %s\r\n",
                       cfg.ver_b, SYS_VERSION);
                strncpy(cfg.ver_b, SYS_VERSION, sizeof(cfg.ver_b) - 1);
                cfg.ver_b[sizeof(cfg.ver_b) - 1] = '\0';
                need_fix = true;
            }
            if (cfg.active[0] != 'A' && cfg.active[0] != 'B') {
                printf("[BOOT] INVALID active: [%c], reset to A\r\n",
                       cfg.active[0]);
                cfg.active[0] = 'A';
                cfg.active[1] = '\0';
                need_fix = true;
            }

            if (need_fix) {
                printf("[BOOT] Writing corrected boot.cfg...\r\n");
                boot_cfg_write(&cfg);
                printf("[BOOT] Fixed!\r\n");
            }
        }

        boot_cfg_dump(&cfg);

        /* Check RTC backup: apply pending update if partition is valid */
        {
            char pend_part;
            char pend_ver[16];
            if (bkup_load_update(&pend_part, pend_ver, sizeof(pend_ver))) {
                uint32_t pend_addr = get_app_addr(pend_part);
                printf("[BKUP] Pending: part=%c ver=%s\r\n",
                       pend_part, pend_ver);

                /* [v3.2] Validate pending version */
                if (!is_valid_version(pend_ver)) {
                    printf("[BKUP] Invalid pending ver: %s, skip\r\n", pend_ver);
                    bkup_clear_update();
                } else if (is_app_valid(pend_addr)) {
                    boot_cfg_update_ver(&cfg, pend_part, pend_ver);
                    boot_cfg_set_active(&cfg, pend_part);
                    boot_cfg_write(&cfg);
                    printf("[BKUP] Applied OK\r\n");
                    bkup_clear_update();
                } else {
                    printf("[BKUP] Partition %c invalid, skip\r\n",
                           pend_part);
                    bkup_clear_update();
                }
            }
        }

        /* If current partition is invalid, fallback to the other */
        {
            uint32_t cur_addr = boot_cfg_active_addr(&cfg);
            if (!is_app_valid(cur_addr)) {
                char other = (cfg.active[0] == 'A') ? 'B' : 'A';
                uint32_t other_addr = get_app_addr(other);
                printf("[BOOT] Partition %c invalid!\r\n", cfg.active[0]);
                if (is_app_valid(other_addr)) {
                    boot_cfg_set_active(&cfg, other);
                    printf("[BOOT] Fallback to %c\r\n", other);
                } else {
                    boot_cfg_set_active(&cfg, 'A');
                    printf("[BOOT] No valid partition!\r\n");
                }
                boot_cfg_write(&cfg);
            }
        }

        lcd_fmt(85, C_CYAN, "Active: %c v%s",
                cfg.active[0], boot_cfg_active_ver(&cfg));

        /* ======================================================
         *  Check SD card for update.flag
         * ====================================================== */
        {
            FIL fil;
            UINT br;
            char flag[256] = {0};
            char new_ver[32] = {0};
            char target[4] = {0};
            bool do_update = false;

            sd_reinit();
            FRESULT fr = f_open(&fil, SD_FW_FLAG, FA_READ);
            printf("[BOOT] update.flag: %s\r\n",
                   (fr == FR_OK) ? "FOUND" : "not found");

            if (fr == FR_OK) {
                memset(flag, 0, sizeof(flag));
                f_read(&fil, flag, sizeof(flag) - 1, &br);
                f_close(&fil);
                printf("[BOOT] Flag: [%s]\r\n", flag);

                /* Parse target partition */
                {
                    char *p = strstr(flag, "target=");
                    if (p) { target[0] = p[7]; target[1] = '\0'; }
                }
                /* Parse version string */
                {
                    char *p = strstr(flag, "version=");
                    if (p) {
                        char *s = p + 8;
                        int i = 0;
                        while (*s && *s != '\n' && *s != '\r' && i < 31)
                            new_ver[i++] = *s++;
                        new_ver[i] = '\0';
                    }
                }

                /* [v3.2] Validate new_ver from flag file */
                if (strlen(new_ver) > 0 && !is_valid_version(new_ver)) {
                    printf("[BOOT] update.flag has invalid version: %s, skip\r\n",
                           new_ver);
                    f_unlink(SD_FW_FLAG);
                } else if (strlen(new_ver) > 0) {
                    const char *cur = boot_cfg_active_ver(&cfg);
                    if (strcmp(cur, new_ver) == 0) {
                        printf("[BOOT] Already v%s, skip\r\n", new_ver);
                    } else {
                        do_update = true;
                        printf("[BOOT] Update: v%s -> v%s target=%s\r\n",
                               cur, new_ver, target);
                    }
                }
            }

            /* ==================================================
             *  Perform update
             * ================================================== */
            if (do_update) {
                char target_part;
                bool is_diff = false;

                if (target[0] == 'A' || target[0] == 'B')
                    target_part = target[0];
                else
                    target_part = (cfg.active[0] == 'A') ? 'B' : 'A';

                if (strstr(flag, "mode=diff"))
                    is_diff = true;

                lcd_clear(C_WHITE);
                lcd_title(10, is_diff ? "DIFF UPDATE" : "FULL UPDATE");
                lcd_fmt(45, C_ORANGE, "Version: v%s", new_ver);
                lcd_fmt(65, C_CYAN, "Target: Partition %c", target_part);

                bool ok = false;

                if (is_diff) {
                    char src_part = cfg.active[0];
                    lcd_info(85, "Applying SDIFF01 to Flash...");
                    printf("[BOOT] Diff: src=%c dst=%c\r\n",
                           src_part, target_part);
                    ok = sd_diff_update(SD_FW_PATCH, target_part,
                                        src_part, new_ver, &cfg);
                } else {
                    lcd_info(85, "Writing full firmware...");
                    ok = sd_full_update(SD_FW_BIN, target_part,
                                        new_ver, &cfg);
                }

                if (ok) {
                    f_unlink(SD_FW_FLAG);
                    printf("[BOOT] Update done, rebooting\r\n");
                    lcd_clear(C_WHITE);
                    lcd_ok(30, "UPDATE SUCCESS!");
                    lcd_fmt(65, C_CYAN, "Partition %c: v%s",
                            target_part, new_ver);
                    delay_ms(1000);
                    NVIC_SystemReset();
                } else {
                    lcd_err(30, "UPDATE FAILED!");
                    lcd_warn(65, "Keeping current partition");
                    delay_ms(2000);
                    lcd_clear(C_WHITE);
                    lcd_title(10, "Bootloader v3.2");
                }
            }
        }
    }

    /* ==========================================================
     *  Jump to App
     * ========================================================== */
do_jump:
    target_addr = boot_cfg_active_addr(&cfg);

    lcd_fmt(130, C_GREEN, "Starting Partition %c...", cfg.active[0]);
    lcd_fmt(150, C_CYAN, "v%s @ 0x%08lX",
            boot_cfg_active_ver(&cfg), (unsigned long)target_addr);

    printf("[BOOT] -> Partition %c @ 0x%08lX (v%s)\r\n",
           cfg.active[0], (unsigned long)target_addr,
           boot_cfg_active_ver(&cfg));

    debug_dump_flash(target_addr, 8);

    delay_ms(300);
    jump_to_app(target_addr);

    while (1);
}
