/**
 * ================================================================
 *  ota_update.c — 固件 + 模型下载 + BSPatch调用 (v4.4)
 * ================================================================
 */
#include "ota.h"
#include "boot_cfg.h"
#include "./BSP/LCD/lcd.h"
#include "./FATFS/source/ff.h"
#include "./SYSTEM/delay/delay.h"
#include <stdlib.h>
#include "./MALLOC/malloc.h"

/* ==================== 版本管理 ==================== */

char g_current_ver[16] = "1.0.0";

void ota_init_version(void)
{
    BootCfg cfg;
    boot_cfg_default(&cfg);

    if (boot_cfg_read(&cfg)) {
        const char *ver = boot_cfg_active_ver(&cfg);
        if (ver && strlen(ver) > 0 && strlen(ver) < 16 &&
            ver[0] >= '0' && ver[0] <= '9') {
            int dots = 0;
            for (const char *p = ver; *p; p++) {
                if (*p == '.') dots++;
                else if (*p < '0' || *p > '9') { dots = -1; break; }
            }
            if (dots == 2) {
                snprintf(g_current_ver, sizeof(g_current_ver), "%s", ver);
            } else {
                printf("[VER] Invalid format: %s\r\n", ver);
                boot_cfg_default(&cfg);
                boot_cfg_write(&cfg);
                snprintf(g_current_ver, sizeof(g_current_ver), "%s",
                         SYS_VERSION);
            }
        } else {
            boot_cfg_default(&cfg);
            boot_cfg_write(&cfg);
            snprintf(g_current_ver, sizeof(g_current_ver), "%s",
                     SYS_VERSION);
        }
    } else {
        boot_cfg_default(&cfg);
        boot_cfg_write(&cfg);
        snprintf(g_current_ver, sizeof(g_current_ver), "%s", SYS_VERSION);
    }
    printf("[VER] g_current_ver=%s\r\n", g_current_ver);
}

void load_md_ver(char *buf, int sz)
{
    FIL f; UINT br;
    if (f_open(&f, SD_MD_VER_FILE, FA_READ) == FR_OK) {
        memset(buf, 0, sz);
        f_read(&f, buf, sz - 1, &br);
        f_close(&f);
        for (int i = 0; i < sz && buf[i]; i++)
            if (buf[i] == '\n' || buf[i] == '\r') { buf[i] = '\0'; break; }
    } else {
        snprintf(buf, sz, "1");
    }
}

/* ==================== LCD 进度 ==================== */

static void update_dl_progress(uint32_t done, uint32_t total, uint32_t t0)
{
    uint32_t elapsed = HAL_GetTick() - t0;
    if (elapsed < 100) elapsed = 100;

    uint32_t spd_x10 = done * 10 / elapsed;
    uint32_t remain = total - done;
    uint32_t eta_s = (spd_x10 > 0) ? remain * 10 / spd_x10 : 0;
    uint32_t pct = (total > 0) ? done * 100 / total : 0;

    int bx = 10, by = 120, bw = 220, bh = 20;
    int fw = (total > 0) ? (int)((uint64_t)done * bw / total) : 0;
    lcd_fill(bx, by, bx + bw, by + bh, 0xC618);
    if (fw > 0)
        lcd_fill(bx + 2, by + 2, bx + 2 + fw, by + bh - 2, 0x07E0);
    lcd_draw_rectangle(bx, by, bx + bw, by + bh, GRAY);

    char buf[40];
    snprintf(buf, sizeof(buf), "%u%%  %luK / %luK",
             (unsigned)pct,
             (unsigned long)(done / 1024),
             (unsigned long)(total / 1024));
    lcd_show_string(10, 148, 220, 16, 16, buf, 0x07FF);

    snprintf(buf, sizeof(buf), "Speed: %u.%u KB/s",
             (unsigned)(spd_x10 / 10), (unsigned)(spd_x10 % 10));
    lcd_show_string(10, 170, 220, 16, 16, buf, 0x07FF);

    snprintf(buf, sizeof(buf), "ETA:  %02u:%02u",
             (unsigned)(eta_s / 60), (unsigned)(eta_s % 60));
    lcd_show_string(10, 192, 220, 16, 16, buf, 0x07FF);
}

/* ============================================================
 *  dl_to_sd() — 分块下载到 SD 卡
 * ============================================================ */
bool dl_to_sd(const char *chunk_url_base,
              uint32_t expected_size,
              const char *save_path)
{
    FIL fil;
    UINT bw;
    uint32_t offset = 0;
    uint32_t t0 = HAL_GetTick();
    uint32_t last_lcd = 0;
    int consecutive_fail = 0;
    uint32_t total_reqs = 0;

    uint8_t *body_buf = (uint8_t *)mymalloc(SRAMIN, DL_CHUNK_SIZE);
    if (!body_buf) {
        printf("[DL] malloc fail\r\n");
        snprintf(g_dl.msg, sizeof(g_dl.msg), "Memory error");
        return false;
    }

    f_unlink(save_path);
    if (f_open(&fil, save_path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        printf("[DL] SD open fail: %s\r\n", save_path);
        snprintf(g_dl.msg, sizeof(g_dl.msg), "SD write error");
        myfree(SRAMIN, body_buf);
        return false;
    }

    printf("[DL] Start: %lu B -> %s\r\n",
           (unsigned long)expected_size, save_path);

    while (offset < expected_size) {
        int want = DL_CHUNK_SIZE;
        if (offset + (uint32_t)want > expected_size)
            want = (int)(expected_size - offset);

        if (!dl_is_connected()) {
            dl_disconnect();
            delay_ms(500);
            if (!dl_connect()) {
                consecutive_fail++;
                printf("[DL] reconnect fail %d\r\n", consecutive_fail);
                if (consecutive_fail >= DL_MAX_RETRY) {
                    snprintf(g_dl.msg, sizeof(g_dl.msg),
                             "Reconnect fail @ %lu", (unsigned long)offset);
                    goto fail;
                }
                delay_ms(1000 * consecutive_fail);
                continue;
            }
        }

        char url[160];
        snprintf(url, sizeof(url), "%s?offset=%lu&length=%d",
                 chunk_url_base, (unsigned long)offset, want);

        int n = dl_http_get(url, body_buf, DL_CHUNK_SIZE);

        if (n <= 0) {
            consecutive_fail++;
            printf("[DL] chunk fail %d @ %lu\r\n",
                   consecutive_fail, (unsigned long)offset);
            if (consecutive_fail >= DL_MAX_RETRY) {
                snprintf(g_dl.msg, sizeof(g_dl.msg),
                         "Download error @ %lu", (unsigned long)offset);
                goto fail;
            }
            dl_disconnect();
            delay_ms(1000 * consecutive_fail);
            continue;
        }

        f_write(&fil, body_buf, n, &bw);
        offset += n;
        consecutive_fail = 0;
        total_reqs++;

        if (HAL_GetTick() - last_lcd > DL_LCD_INTERVAL) {
            ota_ui_progress(offset, expected_size);
            last_lcd = HAL_GetTick();
        }
    }

    f_sync(&fil);
    f_close(&fil);
    dl_disconnect();
    myfree(SRAMIN, body_buf);

    ota_ui_progress(expected_size, expected_size);

    if (offset != expected_size) {
        printf("[DL] SIZE MISMATCH: got %lu expect %lu\r\n",
               (unsigned long)offset, (unsigned long)expected_size);
        snprintf(g_dl.msg, sizeof(g_dl.msg), "Size mismatch");
        return false;
    }

    {
        uint32_t elapsed_ms = HAL_GetTick() - t0;
        uint32_t elapsed_s = elapsed_ms / 1000;
        if (elapsed_s == 0) elapsed_s = 1;
        printf("[DL] Done: %lu B in %lu s (%lu B/s, %lu reqs)\r\n",
               (unsigned long)offset, (unsigned long)elapsed_s,
               (unsigned long)(offset / elapsed_s),
               (unsigned long)total_reqs);
    }

    return true;

fail:
    f_close(&fil);
    dl_disconnect();
    myfree(SRAMIN, body_buf);
    return false;
}

/* ==================== update.flag ==================== */

void write_update_flag(const char *ver, char target, bool is_diff)
{
    FIL f; UINT bw;
    char buf[128];
    int len = snprintf(buf, sizeof(buf),
        "target=%c\nversion=%s\nmode=%s\n",
        target, ver, is_diff ? "diff" : "full");

    f_mkdir(SD_OTA_DIR);
    if (f_open(&f, SD_FW_FLAG, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
        f_write(&f, buf, len, &bw);
        f_sync(&f);
        f_close(&f);
        printf("[FLAG] target=%c ver=%s mode=%s\r\n",
               target, ver, is_diff ? "diff" : "full");
    }
}

/* ============================================================
 *  start_fw_update() — 固件更新 (全量/差分)
 * ============================================================ */
bool start_fw_update(bool is_diff)
{
    BootCfg cfg;
    boot_cfg_read(&cfg);

    char cur_active = cfg.active[0];
    char new_active = (cur_active == 'A') ? 'B' : 'A';
    char url[160];

    printf("[FW] %s: v%s -> v%s (%c -> %c)\r\n",
           is_diff ? "DIFF" : "FULL",
           g_current_ver, D.fw_ver, cur_active, new_active);

    if (is_diff) {
        snprintf(url, sizeof(url), "%sfirmware/%s/%s",
                 API_CHUNK_PATCH, g_current_ver, D.fw_ver);

        printf("[FW] Downloading patch...\r\n");
        bool ok = dl_to_sd(url, D.fw_patch_sz, SD_FW_PATCH);
        if (!ok) {
            snprintf(g_dl.msg, sizeof(g_dl.msg), "Patch download failed");
            return false;
        }

        char old_fw_path[64];
        snprintf(old_fw_path, sizeof(old_fw_path),
                 "%s/firmware_%c.bin", SD_OTA_DIR, cur_active);

        char new_fw_path[64];
        snprintf(new_fw_path, sizeof(new_fw_path),
                 "%s/firmware_%c.bin", SD_OTA_DIR, new_active);

        printf("[FW] BSPatch: %s + %s -> %s\r\n",
               old_fw_path, SD_FW_PATCH, new_fw_path);

        int bsp_ret = bspatch_apply(old_fw_path, SD_FW_PATCH, new_fw_path);
        if (bsp_ret != 0) {
            printf("[FW] BSPatch failed: %d\r\n", bsp_ret);
            snprintf(g_dl.msg, sizeof(g_dl.msg),
                     "BSPatch error %d", bsp_ret);
            f_unlink(new_fw_path);
            return false;
        }

        f_unlink(SD_FW_PATCH);
        printf("[FW] Diff update OK\r\n");

    } else {
        snprintf(url, sizeof(url), "%s%s", API_CHUNK_FW, D.fw_ver);
        bool ok = dl_to_sd(url, D.fw_sz, SD_FW_NEW);
        if (!ok) return false;
    }

    write_update_flag(D.fw_ver, new_active, is_diff);

    boot_cfg_update_ver(&cfg, new_active, D.fw_ver);
    boot_cfg_set_active(&cfg, new_active);
    boot_cfg_write(&cfg);

    snprintf(g_dl.msg, sizeof(g_dl.msg),
             "FW v%s -> %c", D.fw_ver, new_active);
    return true;
}

bool start_md_update(bool is_diff)
{
    char url[160];
    char model_path[80];
    char temp_path[80];

    printf("[MD] %s: v%s -> v%s\r\n",
           is_diff ? "DIFF" : "FULL",
           D.md_ver, D.md_ver_new);

    f_mkdir(SD_OTA_DIR);
    f_mkdir(SD_MODEL_DIR);

    snprintf(temp_path, sizeof(temp_path), "%s/model_tmp.bin", SD_MODEL_DIR);
    snprintf(model_path, sizeof(model_path), "%s/model.bin", SD_MODEL_DIR);

    if (is_diff) {
        char patch_path[80];
        snprintf(patch_path, sizeof(patch_path),
                 "%s/model_patch.bin", SD_MODEL_DIR);

        snprintf(url, sizeof(url), "%smodel/%s/%s",
                 API_CHUNK_PATCH, D.md_ver, D.md_ver_new);

        printf("[MD] Downloading patch...\r\n");
        bool ok = dl_to_sd(url, D.md_patch_sz, patch_path);
        if (!ok) {
            snprintf(g_dl.msg, sizeof(g_dl.msg), "Patch download failed");
            f_unlink(patch_path);
            return false;
        }

        printf("[MD] BSPatch: %s + %s -> %s\r\n",
               model_path, patch_path, temp_path);

        int bsp_ret = bspatch_apply(model_path, patch_path, temp_path);
        if (bsp_ret != 0) {
            printf("[MD] BSPatch failed: %d\r\n", bsp_ret);
            snprintf(g_dl.msg, sizeof(g_dl.msg),
                     "BSPatch error %d", bsp_ret);
            f_unlink(temp_path);
            f_unlink(patch_path);
            return false;
        }

        f_unlink(patch_path);
        printf("[MD] Diff update OK\r\n");

    } else {
        snprintf(url, sizeof(url), "%s%s", API_CHUNK_MODEL, D.md_ver_new);
        printf("[MD] Download to: %s\r\n", temp_path);

        bool ok = dl_to_sd(url, D.md_sz, temp_path);
        if (!ok) {
            f_unlink(temp_path);
            return false;
        }
    }

    f_unlink(model_path);
    if (f_rename(temp_path, model_path) != FR_OK) {
        printf("[MD] rename fail\r\n");
        snprintf(g_dl.msg, sizeof(g_dl.msg), "Rename error");
        return false;
    }

    printf("[MD] Model replaced: %s\r\n", model_path);

    FIL f; UINT bw;
    if (f_open(&f, SD_MD_VER_FILE, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
        f_write(&f, D.md_ver_new, strlen(D.md_ver_new), &bw);
        f_sync(&f);
        f_close(&f);
    }

    snprintf(D.md_ver, sizeof(D.md_ver), "%s", D.md_ver_new);
    snprintf(g_dl.msg, sizeof(g_dl.msg),
             "Model v%s OK", D.md_ver_new);
    return true;
}
