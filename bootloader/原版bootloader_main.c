/**
 * ================================================================
 *  Bootloader v3.1 — ???? boot_cfg.h
 * ================================================================
 *  ???? (v3.0 ? v3.1):
 *
 *  [v3.1] ???? BootCfg ???? cfg_read/cfg_write/cfg_default
 *  [v3.1] ???? boot_cfg.h ?? boot_cfg_* ??
 *  [v3.1] ???? SD_BOOT_CFG/SD_OTA_DIR ? (?? BOOT_CFG_DIR/BOOT_CFG_FILE)
 *  [v3.1] ????? f_mkdir ?? (boot_cfg_write ?????)
 *
 *  startup_stm32f407xx.s ? Stack_Size ???? 0x2000 (8KB)
 *
 *  Flash ??:
 *    0x08000000 +------------------+ Sector 0~5  (256KB) Bootloader
 *    0x08040000 +------------------¦ Sector 6~8  (384KB) APP_A
 *    0x080A0000 +------------------¦ Sector 9~11 (384KB) APP_B
 *    0x08100000 +------------------+ Flash ?? 1MB
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

#include "boot_cfg.h"   /* [v3.1] ??? boot.cfg ?? (??????) */

/* ==============================================================
 *  Flash ????
 * ============================================================== */
#define FLASH_BOOT_ADDR         0x08000000
#define FLASH_BOOT_SIZE         (256 * 1024)

#define FLASH_APP_A_ADDR        0x08040000
#define FLASH_APP_B_ADDR        0x080A0000
#define FLASH_APP_SIZE          (384 * 1024)

/* SD ?? (????, ?? boot.cfg ?? — ??? boot_cfg.h ?) */
#define SD_FW_FLAG          "0:/OTA/update.flag"
#define SD_FW_BIN           "0:/OTA/fw_new.bin"
#define SD_FW_PATCH         "0:/OTA/patch.bin"
#define SD_FW_OLD           "0:/OTA/fw_old.bin"

#define SYS_VERSION         "1.0.0"
#define BKUP_MAGIC          0x4F544142

/* BKP ????? (? App ??, ???) */
#define BKUP_REG_MAGIC      RTC->BKP0R
#define BKUP_REG_PART       RTC->BKP1R
#define BKUP_REG_VERSION    RTC->BKP2R
#define BKUP_REG_HEALTH     RTC->BKP3R   /* App ????? BKP3R */

/* RGB565 ?? */
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
 *  LCD ????
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
 *  SD ?
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
 *  Backup Registers
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
 *  [v3.1] BootCfg ???, ?? boot_cfg.h ??:
 *
 *    boot_cfg_default(cfg)
 *    boot_cfg_read(cfg)
 *    boot_cfg_write(cfg)
 *    boot_cfg_active_ver(cfg)
 *    boot_cfg_active_addr(cfg)
 *    boot_cfg_partition_addr(part)
 *    boot_cfg_update_ver(cfg, part, ver)
 *    boot_cfg_set_active(cfg, part)
 *    boot_cfg_dump(cfg)
 *
 *  ??: BOOT_CFG_DIR  = "0:/OTA"      (? SD_OTA_DIR)
 *        BOOT_CFG_FILE = "0:/OTA/boot.cfg" (? SD_BOOT_CFG)
 * ============================================================== */

/* get_app_addr ??? Bootloader ?? (??????) */
static uint32_t get_app_addr(char part)
{
    return (part == 'B') ? FLASH_APP_B_ADDR : FLASH_APP_A_ADDR;
}

/* ==============================================================
 *  Flash ?? — WORD ??
 * ============================================================== */

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

    /* === ?? === */
    HAL_FLASH_Unlock();
    while (1) {
        if (f_read(&fil, rw_buf, sizeof(rw_buf), &br) != FR_OK || br == 0)
            break;

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
        if (br > aligned) {
            uint8_t tail[4] = {0xFF, 0xFF, 0xFF, 0xFF};
            memcpy(tail, rw_buf + aligned, br - aligned);
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, *(uint32_t *)tail);
            addr += 4;
        }

        written += br;
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

    /* === ?? === */
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
 *  SDIFF01 ???? Flash (?? SD ???)
 * ============================================================== */

static bool flash_copy_partition(char src_part, char dst_part)
{
    uint32_t src_addr = get_app_addr(src_part);
    uint32_t dst_addr = get_app_addr(dst_part);
    uint32_t chunk_buf[256]; /* 1KB ?? */

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

    /* 1. ? patch ? */
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

    /* 2. ?????? */
    lcd_info(80, "Erasing target...");
    if (!erase_partition(dst_part)) {
        f_close(&pf);
        return false;
    }

    /* 3. Flash?Flash ?? */
    lcd_info(80, "Copying partition...");
    HAL_FLASH_Unlock();
    if (!flash_copy_partition(src_part, dst_part)) {
        HAL_FLASH_Lock();
        f_close(&pf);
        return false;
    }

    /* 4. ?? patch ??? */
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

        if (len > 4096 || offset + len > FLASH_APP_SIZE) {
            printf("[SDIFF] chunk %lu invalid\r\n", (unsigned long)i);
            myfree(SRAMIN, dbuf);
            HAL_FLASH_Lock();
            f_close(&pf);
            return false;
        }

        f_read(&pf, dbuf, len, &br);

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
 *  ?? / ??????
 *  [v3.1] ?? boot_cfg_* ??
 * ============================================================== */

static bool sd_full_update(const char *fw_path, char target_part,
                            const char *new_ver, BootCfg *cfg)
{
    if (!flash_file_to_partition(fw_path, target_part)) {
        lcd_err(80, "Flash write FAILED!");
        return false;
    }

    /* [v3.1] ????????? */
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

    /* [v3.1] ????????? */
    boot_cfg_update_ver(cfg, target_part, new_ver);
    boot_cfg_set_active(cfg, target_part);
    boot_cfg_write(cfg);

    lcd_ok(80, "DIFF UPDATE OK!");
    return true;
}

/* ==============================================================
 *  ??? App
 * ============================================================== */

static bool is_app_valid(uint32_t addr)
{
    uint32_t sp = *(volatile uint32_t *)addr;
    uint32_t pc = *(volatile uint32_t *)(addr + 4);
    return ((sp & 0x2FF00000) == 0x20000000) &&
           ((pc & 0xFF000000) == 0x08000000);
}

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

    while (!(USART1->SR & USART_SR_TC));
    f_mount(NULL, "0:", 0);
    __disable_irq();

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    SDIO->DCTRL = 0;
    SDIO->CLKCR = 0;
    SDIO->POWER = 0;
    SDIO->ICR   = 0x00C007FF;

    DMA1_Stream0->CR &= ~DMA_SxCR_EN;
    DMA1_Stream5->CR &= ~DMA_SxCR_EN;
    DMA2_Stream0->CR &= ~DMA_SxCR_EN;
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    DMA2_Stream6->CR &= ~DMA_SxCR_EN;
    DMA1->LIFCR = 0xFFFFFFFF;
    DMA1->HIFCR = 0xFFFFFFFF;
    DMA2->LIFCR = 0xFFFFFFFF;
    DMA2->HIFCR = 0xFFFFFFFF;

    USART1->CR1 = 0;
    RCC->APB2ENR &= ~(1 << 11);

    USART3->CR1 = 0;
    RCC->APB1ENR &= ~(1 << 18);

    SCB->VTOR = addr;
    __set_MSP(sp);
    __DSB();
    __ISB();

    ((void (*)(void))pc)();

    while (1) { LED0_TOGGLE(); delay_ms(100); }
}

/* ==============================================================
 *  ?? & ??
 * ============================================================== */

static uint8_t key0_pressed(void)
{
    if ((GPIOE->IDR & (1 << 4)) == 0) {
        delay_ms(10);
        if ((GPIOE->IDR & (1 << 4)) == 0) return 1;
    }
    return 0;
}

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
    BootCfg cfg;            /* [v3.1] ???? boot_cfg.h */
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
    printf("  Bootloader v3.1 (boot_cfg.h)\r\n");
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
    lcd_title(10, "Bootloader v3.1");

    /* [v3.1] ?????? */
    boot_cfg_default(&cfg);

    /* ==========================================================
     *  ???? (KEY0 ??)
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
        boot_cfg_set_active(&cfg, 'A');
        if (!is_app_valid(get_app_addr('A')))
            boot_cfg_set_active(&cfg, 'B');
        lcd_warn(40, "Safe mode active");
        lcd_fmt(65, C_CYAN, "Booting Partition %c", cfg.active[0]);
        delay_ms(500);
        goto do_jump;
    }

    /* ==========================================================
     *  SD ????
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
     *  [v3.1] ?? boot.cfg (?????)
     * ========================================================== */
    if (sd_ok) {
        /* [v3.1] boot_cfg_read ??? f_mkdir, ??????? */
        if (!boot_cfg_read(&cfg)) {
            printf("[BOOT] boot.cfg not found, using defaults\r\n");
            boot_cfg_default(&cfg);
        }
        boot_cfg_dump(&cfg);

        /* RTC ??: ???????????? */
        {
            char pend_part;
            char pend_ver[16];
            if (bkup_load_update(&pend_part, pend_ver, sizeof(pend_ver))) {
                uint32_t pend_addr = get_app_addr(pend_part);
                printf("[BKUP] Pending: part=%c ver=%s\r\n",
                       pend_part, pend_ver);
                if (is_app_valid(pend_addr)) {
                    boot_cfg_update_ver(&cfg, pend_part, pend_ver);
                    boot_cfg_set_active(&cfg, pend_part);
                    boot_cfg_write(&cfg);
                    printf("[BKUP] Applied OK\r\n");
                } else {
                    printf("[BKUP] Partition %c invalid, skip\r\n",
                           pend_part);
                }
                bkup_clear_update();
            }
        }

        /* ???????, ?????? */
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
         *  ?? SD ??? update.flag
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

                {
                    char *p = strstr(flag, "target=");
                    if (p) { target[0] = p[7]; target[1] = '\0'; }
                }
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

                if (strlen(new_ver) > 0) {
                    /* [v3.1] ??????????? */
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
             *  ????
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
                    lcd_title(10, "Bootloader v3.1");
                }
            }
        }
    }

    /* ==========================================================
     *  ??? App
     * ========================================================== */
do_jump:
    /* [v3.1] ???????????? */
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
