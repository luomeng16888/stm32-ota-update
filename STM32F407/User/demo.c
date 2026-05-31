/**
 * ================================================================
 *  demo.c — UI界面 + OTA更新 + 模型推理 (v4.2)
 * ================================================================
 */
#include "ota.h"
#include "boot_cfg.h"
#include "./BSP/ATK_MW8266D/atk_mw8266d.h"
#include "./BSP/ATK_MW8266D/atk_mw8266d_uart.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"
#include "./BSP/KEY/key.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/SDIO/sdio_sdcard.h"
#include "./FATFS/source/ff.h"
#include "./MALLOC/malloc.h"
#include "stm32f4xx.h"

#include "model_engine.h"
#include "data_player.h"

#ifndef ORANGE
#define ORANGE      0xFD20
#endif

extern uint32_t g_point_color;
extern uint32_t g_back_color;

static FATFS *gf = NULL;
static AppState_t g_st = ST_MAIN;
static bool g_refresh = true;

/* ==================== 界面参数 ==================== */

#define UI_ML       10
#define UI_W        220
#define UI_BAR_X    10
#define UI_BAR_W    220
#define UI_BAR_H    20

/* ==================== 图表参数 ==================== */

#define SD_DATA_PATH    SD_DATA_FILE
#define HIST_LEN        340
/* 图表布局 */
#define CHART_X         5
#define CHART_Y         40
#define CHART_W         230
#define CHART_H         150
#define HIST_DISP_W     178     /* 历史显示宽度 */
#define PRED_AREA_X     (CHART_X + HIST_DISP_W + 2)
#define PRED_AREA_W     (CHART_W - HIST_DISP_W - 2)
#define INFO_Y          (CHART_Y + CHART_H + 8)

/* 颜色定义 */
#define C_BG            0x0000
#define C_TITLE         0x07FF
#define C_GRID          0x2104
#define C_SEP           0x4208
#define C_HIST          0xFFFF
#define C_PRED          0xFFE0
#define C_ACTUAL        0x07E0
#define C_GRAY2         0xC618
#define C_HINT          0x528A

/* ==================== 下载相关 ==================== */

static uint32_t         g_dl_start_tick = 0;
static volatile uint8_t g_dl_pct = 0;

/* ==================== LCD辅助函数 ==================== */

static void lcd_s(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                  uint8_t sz, const char *s, uint16_t color)
{
    lcd_show_string(x, y, w, h, sz, (char *)s, color);
}

static void lcd_sep(uint16_t y)
{
    lcd_fill(UI_ML, y, UI_ML + UI_W - 1, y + 1, GRAY);
}

static void lcd_title(const char *title)
{
    lcd_clear(WHITE);
    lcd_s(UI_ML, 8, UI_W, 24, 24, title, BLUE);
    lcd_sep(34);
}

#define APP_BKUP_HEALTHY   0x5A5A5A5AUL

static void mark_app_healthy(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR |= PWR_CR_DBP;
    RTC->BKP3R = APP_BKUP_HEALTHY;
}

/* ==================== 进度条 ==================== */

static void lcd_draw_bar(uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h,
                          uint8_t pct)
{
    lcd_fill(x, y, x + w - 1, y + h - 1, 0xC618);
    if (pct > 0) {
        uint8_t p = (pct > 100) ? 100 : pct;
        uint16_t fw = (uint16_t)((uint32_t)(w - 4) * p / 100);
        if (fw > 0)
            lcd_fill(x + 2, y + 2, x + 2 + fw - 1, y + h - 3, GREEN);
    }
    lcd_draw_rectangle(x, y, x + w - 1, y + h - 1, GRAY);
}

void ota_ui_progress(uint32_t done, uint32_t total)
{
    char buf[40];
    uint8_t pct;
    uint32_t elapsed;

    if (total == 0) return;
    pct = (uint8_t)((uint64_t)done * 100 / total);
    if (pct == g_dl_pct && pct < 100) return;
    g_dl_pct = pct;

    lcd_draw_bar(UI_BAR_X, 120, UI_BAR_W, UI_BAR_H, pct);

    snprintf(buf, sizeof(buf), "%3u%%  %luK / %luK",
             pct,
             (unsigned long)(done / 1024),
             (unsigned long)(total / 1024));
    lcd_s(UI_ML, 148, UI_W, 16, 16, buf, BLACK);

    elapsed = HAL_GetTick() - g_dl_start_tick;
    if (elapsed > 1000) {
        float kbps = (float)done / (float)elapsed;
        snprintf(buf, sizeof(buf), "%.1f KB/s", kbps);
    } else {
        snprintf(buf, sizeof(buf), "--- KB/s");
    }
    lcd_s(UI_ML, 170, UI_W, 16, 16, buf, GRAY);
}

/* ==================== OTA页面 ==================== */

void lcd_page_main(void)
{
    char buf[48];
    lcd_title("OTA v4.2 Monitor");

    snprintf(buf, sizeof(buf), "WiFi: %s", D.ssid);
    lcd_s(UI_ML, 50,  UI_W, 16, 16, buf, BLACK);

    lcd_s(UI_ML, 75,  UI_W, 16, 16,
          D.srv_ok ? "Server: Online" : "Server: Offline",
          D.srv_ok ? GREEN : RED);

    snprintf(buf, sizeof(buf), "Device: %s", D.id);
    lcd_s(UI_ML, 100, UI_W, 16, 16, buf, BLACK);

    snprintf(buf, sizeof(buf), "FW: v%s", g_current_ver);
    lcd_s(UI_ML, 125, UI_W, 16, 16, buf, CYAN);

    snprintf(buf, sizeof(buf), "MD: v%s", D.md_ver);
    lcd_s(UI_ML, 150, UI_W, 16, 16, buf, CYAN);

    lcd_s(UI_ML, 190, UI_W, 16, 16, "KEY0: Check Update", GRAY);
    lcd_s(UI_ML, 215, UI_W, 16, 16, "KEY1: Model Run",   GRAY);
}

void lcd_page_update_sel(void)
{
    char buf[48];
    lcd_title("Update Available");

    snprintf(buf, sizeof(buf), "Current: v%s", g_current_ver);
    lcd_s(UI_ML, 50, UI_W, 16, 16, buf, BLACK);

    if (D.fw_upd) {
        snprintf(buf, sizeof(buf), "FW: v%s (%luB)",
                 D.fw_ver, (unsigned long)D.fw_sz);
        lcd_s(UI_ML, 75, UI_W, 16, 16, buf, GREEN);
        if (D.fw_patch)
            lcd_s(UI_ML, 97, UI_W, 16, 16, "  + Diff available", GRAY);
    } else {
        lcd_s(UI_ML, 75, UI_W, 16, 16, "FW: Up to date", GRAY);
    }

    if (D.md_upd) {
        snprintf(buf, sizeof(buf), "MD: v%s (%luB)",
                 D.md_ver_new, (unsigned long)D.md_sz);
        lcd_s(UI_ML, 115, UI_W, 16, 16, buf, GREEN);
    } else {
        lcd_s(UI_ML, 115, UI_W, 16, 16, "MD: Up to date", GRAY);
    }

    lcd_s(UI_ML, 155, UI_W, 16, 16,
          D.fw_upd ? "KEY0: FW Update" : "KEY0: (none)",
          D.fw_upd ? BLACK : GRAY);
    lcd_s(UI_ML, 178, UI_W, 16, 16,
          D.md_upd ? "KEY1: MD Update" : "KEY1: (none)",
          D.md_upd ? BLACK : GRAY);
    lcd_s(UI_ML, 200, UI_W, 16, 16, "KEY2: Back", GRAY);
}

void lcd_page_fw_sel(void)
{
    char buf[48];
    lcd_title("Firmware Update");

    snprintf(buf, sizeof(buf), "v%s -> v%s", g_current_ver, D.fw_ver);
    lcd_s(UI_ML, 50, UI_W, 16, 16, buf, BLACK);

    snprintf(buf, sizeof(buf), "Size: %lu B", (unsigned long)D.fw_sz);
    lcd_s(UI_ML, 75, UI_W, 16, 16, buf, GRAY);

    lcd_s(UI_ML, 110, UI_W, 16, 16, "KEY0: Full Update", BLACK);
    lcd_s(UI_ML, 135, UI_W, 16, 16,
          D.fw_patch ? "KEY1: Diff Update" : "KEY1: (no diff)",
          D.fw_patch ? BLACK : GRAY);
    if (D.fw_patch) {
        snprintf(buf, sizeof(buf), "  Patch: %lu B",
                 (unsigned long)D.fw_patch_sz);
        lcd_s(UI_ML, 155, UI_W, 16, 16, buf, GRAY);
    }
    lcd_s(UI_ML, 190, UI_W, 16, 16, "KEY2: Back", GRAY);
}

void lcd_page_md_sel(void)
{
    char buf[48];
    lcd_title("Model Update");

    snprintf(buf, sizeof(buf), "v%s -> v%s", D.md_ver, D.md_ver_new);
    lcd_s(UI_ML, 50, UI_W, 16, 16, buf, BLACK);

    snprintf(buf, sizeof(buf), "Name: %s", D.md_name);
    lcd_s(UI_ML, 75, UI_W, 16, 16, buf, GRAY);

    snprintf(buf, sizeof(buf), "Size: %lu B", (unsigned long)D.md_sz);
    lcd_s(UI_ML, 100, UI_W, 16, 16, buf, GRAY);

    lcd_s(UI_ML, 135, UI_W, 16, 16, "KEY0: Full Update", BLACK);
    lcd_s(UI_ML, 160, UI_W, 16, 16,
          D.md_patch ? "KEY1: Diff Update" : "KEY1: (no diff)",
          D.md_patch ? BLACK : GRAY);
    lcd_s(UI_ML, 190, UI_W, 16, 16, "KEY2: Back", GRAY);
}

void lcd_page_download(void)
{
    char buf[48];
    lcd_title("Downloading...");

    if (g_dl.is_firmware)
        snprintf(buf, sizeof(buf), "FW v%s -> Part %c",
                 g_dl.version, g_dl.target_part);
    else
        snprintf(buf, sizeof(buf), "MD v%s", g_dl.version);
    lcd_s(UI_ML, 50, UI_W, 16, 16, buf, BLACK);

    lcd_s(UI_ML, 75, UI_W, 16, 16,
          g_dl.is_diff ? "Mode: Diff" : "Mode: Full", GRAY);

    g_dl_pct = 0;
    g_dl_start_tick = HAL_GetTick();
    lcd_draw_bar(UI_BAR_X, 120, UI_BAR_W, UI_BAR_H, 0);

    snprintf(buf, sizeof(buf), "  0%%   0K / %luK",
             (unsigned long)(g_dl.file_size / 1024));
    lcd_s(UI_ML, 148, UI_W, 16, 16, buf, BLACK);
    lcd_s(UI_ML, 170, UI_W, 16, 16, "--- KB/s", GRAY);
}

void lcd_page_result(bool ok, const char *msg)
{
    lcd_clear(WHITE);
    if (ok)
        lcd_s(UI_ML, 70, UI_W, 24, 24, "SUCCESS!", GREEN);
    else
        lcd_s(UI_ML, 70, UI_W, 24, 24, "FAILED!", RED);
    lcd_s(UI_ML, 115, UI_W, 16, 16, (char *)msg, BLACK);
    if (ok)
        lcd_s(UI_ML, 150, UI_W, 16, 16, "Rebooting in 3s...", GRAY);
    else
        lcd_s(UI_ML, 150, UI_W, 16, 16, "Press any key...", GRAY);
}

/* ================================================================
 *  模型推理
 * ================================================================ */

static void draw_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        lcd_fill(x0, y0, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

static int map_y(float v, float lo, float hi)
{
    float r = (v - lo) / (hi - lo);
    if (r < 0.0f) r = 0.0f;
    if (r > 1.0f) r = 1.0f;
    return CHART_Y + CHART_H - 1 - (int)(r * (CHART_H - 1));
}

static void make_model_path(const char *ver, char *buf, int sz)
{
    snprintf(buf, sz, "0:/OTA/model/model.bin");
    (void)ver;
}


static bool file_exists(const char *path)
{
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return false;
    f_close(&f);
    return true;
}

void lcd_page_model_run(void)
{
    char buf[48];
    char mpath[80];

    lcd_title("Model Inference");

    snprintf(buf, sizeof(buf), "Model: %s", D.md_name);
    lcd_s(UI_ML, 50,  UI_W, 16, 16, buf, BLACK);

    snprintf(buf, sizeof(buf), "Version: v%s", D.md_ver);
    lcd_s(UI_ML, 72,  UI_W, 16, 16, buf, CYAN);

    make_model_path(D.md_ver, mpath, sizeof(mpath));
    if (file_exists(mpath))
        lcd_s(UI_ML, 100, UI_W, 16, 16, "Model file: OK", GREEN);
    else
        lcd_s(UI_ML, 100, UI_W, 16, 16, "Model file: MISSING", RED);

    if (file_exists(SD_DATA_PATH))
        lcd_s(UI_ML, 120, UI_W, 16, 16, "Data file:  OK", GREEN);
    else
        lcd_s(UI_ML, 120, UI_W, 16, 16, "Data file:  MISSING", RED);

    lcd_s(UI_ML, 160, UI_W, 16, 16, "KEY0: Run Inference", BLACK);
    lcd_s(UI_ML, 185, UI_W, 16, 16, "KEY1: Back", GRAY);
}

/* ================================================================
 *  model_run_loop() 推理主循环
 * ================================================================ */
static int model_run_loop(void)
{
    char mpath[80];
    char buf[64];
    DataPlayer player;
    MeResult res;
    uint32_t frame_cnt = 0;
    uint32_t saved_pc, saved_bc;

    /* 环形缓冲 */
    float hist[HIST_LEN];
    int   hw = 0;
    int   hc = 0;

    /* ---- 1. 检查文件 ---- */
    make_model_path(D.md_ver, mpath, sizeof(mpath));

    if (!file_exists(mpath)) {
        lcd_clear(WHITE);
        lcd_s(UI_ML, 80,  UI_W, 16, 16, "Model not found!", RED);
        lcd_s(UI_ML, 105, UI_W, 16, 16, mpath, GRAY);
        lcd_s(UI_ML, 140, UI_W, 16, 16, "Press any key...", GRAY);
        while (key_scan(0) == 0) delay_ms(50);
        return -1;
    }
    if (!file_exists(SD_DATA_PATH)) {
        lcd_clear(WHITE);
        lcd_s(UI_ML, 80,  UI_W, 16, 16, "Data not found!", RED);
        lcd_s(UI_ML, 105, UI_W, 16, 16, SD_DATA_PATH, GRAY);
        lcd_s(UI_ML, 140, UI_W, 16, 16, "Press any key...", GRAY);
        while (key_scan(0) == 0) delay_ms(50);
        return -1;
    }

    /* ---- 2. 加载模型 ---- */
    lcd_clear(WHITE);
    lcd_s(UI_ML, 80,  UI_W, 16, 16, "Loading model...", BLACK);
    if (me_load(mpath) != 0) {
        lcd_s(UI_ML, 110, UI_W, 16, 16, "Model load FAILED!", RED);
        lcd_s(UI_ML, 140, UI_W, 16, 16, "Press any key...", GRAY);
        while (key_scan(0) == 0) delay_ms(50);
        return -1;
    }

    /* ---- 3. 加载数据 ---- */
    lcd_s(UI_ML, 110, UI_W, 16, 16, "Loading data...", BLACK);
    if (dp_init(&player, SD_DATA_PATH) != 0) {
        lcd_s(UI_ML, 140, UI_W, 16, 16, "Data load FAILED!", RED);
        lcd_s(UI_ML, 165, UI_W, 16, 16, "Press any key...", GRAY);
        while (key_scan(0) == 0) delay_ms(50);
        return -1;
    }
    delay_ms(500);

    /* ---- 4. 初始化 ---- */
    memset(hist, 0, sizeof(hist));

    saved_pc = g_point_color;
    saved_bc = g_back_color;
    g_back_color  = C_BG;
    g_point_color = C_HIST;

    lcd_fill(0, 0, 239, 37, C_BG);
    lcd_s(8, 8, 220, 16, 16, "GRU Prediction  [KEY1:Exit]", C_TITLE);
    lcd_fill(UI_ML, 28, UI_ML + 219, 29, C_SEP);

    /* ---- 5. 推理循环 ---- */
    while (1) {

        if (key_scan(0) == KEY1_PRES)
            break;

        /* 读取下一采样点 */
        hist[hw] = dp_next(&player);
        hw = (hw + 1) % HIST_LEN;
        hc++;

        if (hc < ME_SEQ_LEN) {
            delay_ms(40);
            continue;
        }

        /* ---- 准备输入序列 ---- */
        float seq[ME_SEQ_LEN];
        for (int t = 0; t < ME_SEQ_LEN; t++) {
            int idx = (hw - ME_SEQ_LEN + t + HIST_LEN) % HIST_LEN;
            seq[t] = hist[idx];
        }

        float fut[ME_PRED_LEN];
        for (int i = 0; i < ME_PRED_LEN; i++)
            fut[i] = dp_get(&player, player.cursor + i);

        /* ---- 推理 ---- */
        res = me_predict(seq, fut, player.dmin, player.dmax);
        if (!res.ok) { delay_ms(40); continue; }

        /* ---- Y轴范围 ---- */
        int disp_n = hc;
        if (disp_n > HIST_DISP_W) disp_n = HIST_DISP_W;

        float lo =  9999.0f;
        float hi = -9999.0f;
        for (int i = 0; i < disp_n; i++) {
            int idx = (hw - disp_n + i + HIST_LEN) % HIST_LEN;
            float v = hist[idx];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        for (int i = 0; i < ME_PRED_LEN; i++) {
            if (res.pred[i] < lo) lo = res.pred[i];
            if (res.pred[i] > hi) hi = res.pred[i];
        }
        float margin = (hi - lo) * 0.1f;
        if (margin < 0.01f) margin = 0.01f;
        lo -= margin;
        hi += margin;

        /* ---- 清空绘图区 ---- */
        lcd_fill(CHART_X, CHART_Y,
                 CHART_X + CHART_W - 1,
                 CHART_Y + CHART_H - 1, C_BG);

        /* ---- 网格线 ---- */
        for (int r = 1; r <= 3; r++) {
            int gy = CHART_Y + r * CHART_H / 4;
            lcd_fill(CHART_X, gy, CHART_X + CHART_W - 1, gy, C_GRID);
        }

        /* ---- 绘制历史曲线（白色） ---- */
        {
            int x_base = CHART_X + HIST_DISP_W - disp_n;
            int prev_y = map_y(
                hist[(hw - disp_n + HIST_LEN) % HIST_LEN], lo, hi);

            for (int i = 1; i < disp_n; i++) {
                int idx = (hw - disp_n + i + HIST_LEN) % HIST_LEN;
                int cur_y = map_y(hist[idx], lo, hi);
                int x = x_base + i;
                int ymin = prev_y < cur_y ? prev_y : cur_y;
                int ymax = prev_y > cur_y ? prev_y : cur_y;
                lcd_fill(x, ymin, x, ymax, C_HIST);
                prev_y = cur_y;
            }
        }

        /* ---- 分隔线 ---- */
        {
            int sep_x = CHART_X + HIST_DISP_W;
            lcd_fill(sep_x, CHART_Y, sep_x, CHART_Y + CHART_H - 1, C_SEP);
        }

        /* ---- 预测曲线（黄色） ---- */
        {
            int last_x = CHART_X + HIST_DISP_W - 1;
            int last_y = map_y(hist[(hw - 1 + HIST_LEN) % HIST_LEN], lo, hi);

            for (int i = 0; i < ME_PRED_LEN; i++) {
                int px = PRED_AREA_X + (i + 1) * PRED_AREA_W / (ME_PRED_LEN + 1);
                int py = map_y(res.pred[i], lo, hi);

                draw_line(last_x, last_y, px, py, C_PRED);
                lcd_fill(px - 1, py - 1, px + 1, py + 1, C_PRED);

                last_x = px;
                last_y = py;
            }
        }

        /* ---- 实际值标记（绿色十字） ---- */
        for (int i = 0; i < ME_PRED_LEN; i++) {
            int px = PRED_AREA_X + (i + 1) * PRED_AREA_W / (ME_PRED_LEN + 1);
            int ay = map_y(res.actual[i], lo, hi);
            lcd_fill(px - 2, ay, px + 2, ay, C_ACTUAL);
            lcd_fill(px, ay - 2, px, ay + 2, C_ACTUAL);
        }

        /* ---- 信息区域 ---- */
        lcd_fill(0, INFO_Y, 239, 319, C_BG);

        uint16_t mc;
        if      (res.mse < 0.005f) mc = C_ACTUAL;
        else if (res.mse < 0.020f) mc = C_PRED;
        else                       mc = 0xF800;

        snprintf(buf, sizeof(buf), "MSE: %.4f", res.mse);
        lcd_s(8, INFO_Y, 130, 16, 16, buf, mc);

        {
            int bw = (int)(res.mse / 0.05f * 90.0f);
            if (bw > 90) bw = 90;
            lcd_fill(145, INFO_Y + 2, 235, INFO_Y + 12, C_GRID);
            if (bw > 0)
                lcd_fill(145, INFO_Y + 2, 145 + bw, INFO_Y + 12, mc);
        }

        snprintf(buf, sizeof(buf), "Time:%lums  Fr:%lu",
                 (unsigned long)res.time_ms,
                 (unsigned long)frame_cnt);
        lcd_s(8, INFO_Y + 20, 220, 16, 16, buf, C_GRAY2);

        snprintf(buf, sizeof(buf), "Model: v%s  (%u B)",
                 D.md_ver, (unsigned)me_size());
        lcd_s(8, INFO_Y + 40, 220, 16, 16, buf, mc);

        const char *desc;
        if      (res.mse < 0.005f) desc = "EXCELLENT";
        else if (res.mse < 0.020f) desc = "GOOD";
        else                       desc = "NEEDS TRAINING";
        lcd_s(8, INFO_Y + 58, 220, 16, 16, desc, mc);

        {
            int ly = INFO_Y + 80;

            lcd_fill(8,   ly + 3, 18,  ly + 9, C_HIST);
            lcd_s(22,  ly, 45, 16, 16, "Hist", C_GRAY2);

            lcd_fill(75,  ly + 3, 85,  ly + 9, C_PRED);
            lcd_s(89,  ly, 45, 16, 16, "Pred", C_GRAY2);

            lcd_fill(145, ly + 6, 155, ly + 6, C_ACTUAL);
            lcd_fill(150, ly + 3, 150, ly + 9, C_ACTUAL);
            lcd_s(160, ly, 45, 16, 16, "Real", C_GRAY2);
        }

        lcd_s(8, 304, 220, 16, 16, "KEY1: Return", C_HINT);

        frame_cnt++;
        delay_ms(40);
    }

    /* ---- 清理 ---- */
    dp_close(&player);

    g_point_color = saved_pc;
    g_back_color  = saved_bc;

    return 0;
}

/* ==================== 主入口 ==================== */

void demo_main(void)
{
    /* VTOR 修正 */
    {
        uint32_t pc;
        __asm volatile("MOV %0, PC" : "=r"(pc));

        uint32_t correct_vtor;
        if (pc >= FLASH_APP_B_ADDR)
            correct_vtor = FLASH_APP_B_ADDR;
        else
            correct_vtor = FLASH_APP_A_ADDR;

        if (SCB->VTOR != correct_vtor) {
            printf("[APP] FIX VTOR: 0x%08lX -> 0x%08lX (PC=0x%08lX)\r\n",
                   (unsigned long)SCB->VTOR,
                   (unsigned long)correct_vtor,
                   (unsigned long)pc);
            SCB->VTOR = correct_vtor;
        }
    }

    {
        uint32_t hb_t = 0;
        uint8_t key;
        bool wifi_ok = false;
        BootCfg cfg;

        atk_mw8266d_uart_init(115200);
        usart_init(115200);
        my_mem_init(SRAMIN);
        __enable_irq();

        g_point_color = BLACK;
        g_back_color  = WHITE;

        printf("\r\n========== OTA v4.2 ==========\r\n");
        printf("[APP] BUILD: %s %s\r\n", __DATE__, __TIME__);
        printf("[APP] VTOR=0x%08lX\r\n", (unsigned long)SCB->VTOR);

        /* ---------- 初始化SD卡 ---------- */
        lcd_clear(WHITE);
        lcd_s(UI_ML, 30, UI_W, 16, 16, "OTA v4.2 - Init SD...", BLACK);
        {
            bool sd_ok = false;
            int r;

            RCC->APB2ENR |= RCC_APB2ENR_SDIOEN;
            SDIO->POWER = 0;
            SDIO->CLKCR = 0;
            RCC->APB2ENR &= ~RCC_APB2ENR_SDIOEN;
            delay_ms(100);
            RCC->APB2ENR |= RCC_APB2ENR_SDIOEN;
            delay_ms(200);

            for (r = 0; r < 3; r++) {
                printf("[SD] APP init attempt %d...\r\n", r + 1);
                if (sd_init() == 0) {
                    sd_ok = true;
                    break;
                }
                SDIO->POWER = 0;
                delay_ms(300);
            }

            if (sd_ok) {
                gf = (FATFS *)mymalloc(SRAMIN, sizeof(FATFS));
                if (gf) {
                    if (f_mount(gf, "0:", 1) != FR_OK) {
                        f_mkfs("0:", 0, 0, FF_MAX_SS);
                        f_mount(gf, "0:", 1);
                    }
                }
                f_mkdir(SD_OTA_DIR);
            } else {
                printf("[SD] APP all attempts failed!\r\n");
            }
        }

        /* ---------- 初始化OTA ---------- */
        ota_init();
        strcpy(D.id, DEVICE_ID);
        strcpy(D.ssid, WIFI_SSID);
        strcpy(D.pass, WIFI_PASS);
        ota_init_version();
        load_md_ver(D.md_ver, sizeof(D.md_ver));

        boot_cfg_read(&cfg);
        snprintf(g_current_ver, sizeof(g_current_ver), "%s",
                 boot_cfg_active_ver(&cfg));

        printf("[DEV] %s sys=%s md=%s\r\n", D.id, g_current_ver, D.md_ver);

        /* ---------- 连接WiFi ---------- */
        lcd_s(UI_ML, 60, UI_W, 16, 16, "Connecting WiFi...", BLACK);
        wifi_ok = wifi_connect();
        if (wifi_ok) {
            lcd_s(UI_ML, 80, UI_W, 16, 16, "WiFi OK!", GREEN);
            srv_register();
            if (D.srv_ok) {
                lcd_s(UI_ML, 100, UI_W, 16, 16, "Server OK!", GREEN);
                mark_app_healthy();
            }
        } else {
            lcd_s(UI_ML, 80, UI_W, 16, 16, "WiFi FAILED!", RED);
        }
        delay_ms(1000);

        g_st = ST_MAIN;
        g_refresh = true;
        hb_t = HAL_GetTick();

        /* ==================== 主循环 ==================== */
        while (1) {

            if (D.srv_ok && (HAL_GetTick() - hb_t >= 60000)) {
                hb_t = HAL_GetTick();
                srv_heartbeat();
            }

            if (!wifi_ok && (HAL_GetTick() - hb_t >= 30000)) {
                hb_t = HAL_GetTick();
                wifi_ok = wifi_connect();
                if (wifi_ok) srv_register();
                g_refresh = true;
            }

            key = key_scan(0);

            switch (g_st) {

            case ST_MAIN:
                if (g_refresh) { lcd_page_main(); g_refresh = false; }
                if (key == KEY0_PRES) {
                    if (!wifi_ok) {
                        wifi_ok = wifi_connect();
                        if (wifi_ok) srv_register();
                    }
                    if (wifi_ok && D.srv_ok) {
                        g_st = ST_CHECKING;
                        g_refresh = true;
                    } else {
                        lcd_clear(WHITE);
                        lcd_s(UI_ML, 80, UI_W, 16, 16,
                              "No WiFi/Server!", RED);
                        lcd_s(UI_ML, 110, UI_W, 16, 16,
                              "Press any key...", GRAY);
                        while (key_scan(0) == 0) delay_ms(50);
                        g_refresh = true;
                    }
                } else if (key == KEY1_PRES) {
                    g_st = ST_MODEL_RUN;
                    g_refresh = true;
                }
                break;

            case ST_CHECKING:
                lcd_clear(WHITE);
                lcd_s(UI_ML, 80, UI_W, 16, 16, "Checking FW...", BLACK);
                check_fw();
                lcd_s(UI_ML, 110, UI_W, 16, 16, "Checking MD...", BLACK);
                check_md();
                if (!D.fw_upd && !D.md_upd) {
                    lcd_clear(WHITE);
                    lcd_s(UI_ML, 80, UI_W, 16, 16,
                          "All up to date!", GREEN);
                    lcd_s(UI_ML, 110, UI_W, 16, 16,
                          "Press any key...", GRAY);
                    while (key_scan(0) == 0) delay_ms(50);
                    g_st = ST_MAIN;
                } else {
                    g_st = ST_UPDATE_SEL;
                }
                g_refresh = true;
                break;

            case ST_UPDATE_SEL:
                if (g_refresh) { lcd_page_update_sel(); g_refresh = false; }
                if (key == KEY0_PRES && D.fw_upd) {
                    g_st = ST_FW_SEL; g_refresh = true;
                } else if (key == KEY1_PRES && D.md_upd) {
                    g_st = ST_MD_SEL; g_refresh = true;
                } else if (key == KEY2_PRES) {
                    g_st = ST_MAIN; g_refresh = true;
                }
                break;

            case ST_FW_SEL:
                if (g_refresh) { lcd_page_fw_sel(); g_refresh = false; }
                if (key == KEY0_PRES) {
                    g_dl.is_firmware = true;
                    g_dl.is_diff = false;
                    snprintf(g_dl.version, sizeof(g_dl.version),
                             "%s", D.fw_ver);
                    g_dl.from_ver[0] = '\0';
                    g_dl.file_size = D.fw_sz;
                    g_dl.target_part = (cfg.active[0] == 'A') ? 'B' : 'A';
                    g_st = ST_DOWNLOAD;
                    g_refresh = true;
                } else if (key == KEY1_PRES && D.fw_patch) {
                    g_dl.is_firmware = true;
                    g_dl.is_diff = true;
                    snprintf(g_dl.version, sizeof(g_dl.version),
                             "%s", D.fw_ver);
                    snprintf(g_dl.from_ver, sizeof(g_dl.from_ver),
                             "%s", g_current_ver);
                    g_dl.file_size = D.fw_patch_sz;
                    g_dl.target_part = (cfg.active[0] == 'A') ? 'B' : 'A';
                    g_st = ST_DOWNLOAD;
                    g_refresh = true;
                } else if (key == KEY2_PRES) {
                    g_st = ST_UPDATE_SEL; g_refresh = true;
                }
                break;

            case ST_MD_SEL:
                if (g_refresh) { lcd_page_md_sel(); g_refresh = false; }
                if (key == KEY0_PRES) {
                    g_dl.is_firmware = false;
                    g_dl.is_diff = false;
                    snprintf(g_dl.version, sizeof(g_dl.version),
                             "%s", D.md_ver_new);
                    g_dl.from_ver[0] = '\0';
                    g_dl.file_size = D.md_sz;
                    g_dl.target_part = '-';
                    g_st = ST_DOWNLOAD;
                    g_refresh = true;
                } else if (key == KEY1_PRES && D.md_patch) {
                    g_dl.is_firmware = false;
                    g_dl.is_diff = true;
                    snprintf(g_dl.version, sizeof(g_dl.version),
                             "%s", D.md_ver_new);
                    snprintf(g_dl.from_ver, sizeof(g_dl.from_ver),
                             "%s", D.md_ver);
                    g_dl.file_size = D.md_patch_sz;
                    g_dl.target_part = '-';
                    g_st = ST_DOWNLOAD;
                    g_refresh = true;
                } else if (key == KEY2_PRES) {
                    g_st = ST_UPDATE_SEL; g_refresh = true;
                }
                break;

            case ST_DOWNLOAD:
                lcd_page_download();
                if (g_dl.is_firmware)
                    g_dl.success = start_fw_update(g_dl.is_diff);
                else
                    g_dl.success = start_md_update(g_dl.is_diff);
                g_st = ST_RESULT;
                g_refresh = true;
                break;

            case ST_RESULT:
                if (g_refresh) {
                    lcd_page_result(g_dl.success, g_dl.msg);
                    g_refresh = false;
                }
                if (g_dl.success) {
                    delay_ms(3000);
                    if (g_dl.is_firmware) {
                        printf("[OTA] FW updated, rebooting...\r\n");
                        NVIC_SystemReset();
                    }
                    g_st = ST_MAIN;
                    g_refresh = true;
                } else {
                    if (key != 0) {
                        g_st = ST_MAIN; g_refresh = true;
                    }
                }
                break;

            case ST_MODEL_RUN:
                if (g_refresh) { lcd_page_model_run(); g_refresh = false; }
                if (key == KEY0_PRES) {
                    model_run_loop();
                    g_refresh = true;
                } else if (key == KEY1_PRES) {
                    g_st = ST_MAIN;
                    g_refresh = true;
                }
                break;

            default:
                g_st = ST_MAIN;
                g_refresh = true;
                break;
            }

            delay_ms(50);
        }
    }
}

void demo_run(void) { demo_main(); }
