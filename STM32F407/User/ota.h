/**
 * ================================================================
 *  ota.h — App 层 OTA 接口头文件 (v4.1)
 * ================================================================
 */
#ifndef OTA_H
#define OTA_H

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/* ==================== 服务器配置 ==================== */
#define SERVER_IP       "192.168.43.203"
#define SERVER_PORT     8080
#define DEVICE_MODEL    "STM32F407ZGT6"

/* ==================== 设备配置 ==================== */
#define DEVICE_ID       "STM32_001"
#define WIFI_SSID       "Free"
#define WIFI_PASS       "l88888888"
#define SYS_VERSION     "1.0.0"

/* ==================== API 路径 ==================== */
#define API_REGISTER    "/api/ota/register"
#define API_HEARTBEAT   "/api/ota/heartbeat"
#define API_CHECK_FW    "/api/ota/check_firmware"
#define API_CHECK_MD    "/api/ota/check_model"

/* [v4.0] 新增分块下载 URL 定义 */
#define API_CHUNK_FW      "/api/ota/chunk/"
#define API_CHUNK_FW_A    "/api/ota/chunk_a/"
#define API_CHUNK_FW_B    "/api/ota/chunk_b/"
#define API_CHUNK_MODEL   "/api/ota/chunk_model/"
#define API_CHUNK_PATCH   "/api/ota/chunk_patch/"

/* ==================== 下载配置 ==================== */
#define DL_CHUNK_SIZE   2048
#define DL_RESP_BUF_SZ  4096
#define DL_MAX_RETRY    3
#define DL_LCD_INTERVAL 300

/* ==================== Flash 配置 ==================== */
#define FLASH_APP_A_ADDR    0x08040000
#define FLASH_APP_B_ADDR    0x080A0000
#define FLASH_APP_SIZE      (384 * 1024)

/* ==================== SD 卡路径 ==================== */
#define SD_OTA_DIR          "0:/OTA"
#define SD_FW_FLAG          "0:/OTA/update.flag"
#define SD_FW_NEW           "0:/OTA/fw_new.bin"
#define SD_FW_PATCH         "0:/OTA/patch.bin"
#define SD_MODEL_NEW        "0:/OTA/model_new.bin"
#define SD_MODEL_PATCH      "0:/OTA/model_patch.bin"
#define SD_MD_VER_FILE      "0:/OTA/md_ver.txt"

#define SD_DATA_DIR         "0:/OTA/data"
#define SD_DATA_FILE        "0:/OTA/data/ecg.bin"
#define SD_MODEL_DIR        "0:/OTA/model"

/* ==================== 状态枚举 ==================== */
typedef enum {
    ST_MAIN = 0,
    ST_CHECKING,
    ST_UPDATE_SEL,
    ST_FW_SEL,
    ST_MD_SEL,
    ST_DOWNLOAD,
    ST_RESULT,
    ST_MODEL_RUN,
} AppState_t;

/* ==================== 下载上下文结构体 ==================== */
typedef struct {
    bool     active;
    bool     is_firmware;
    bool     is_diff;
    char     version[16];
    char     from_ver[16];
    uint32_t file_size;
    char     target_part;
    bool     success;
    char     msg[64];
} DlCtx_t;

/* ==================== 设备信息结构体 ==================== */
typedef struct {
    char     id[32];
    char     ssid[64];
    char     pass[128];
    char     sys_ver[16];
    char     md_ver[16];
    bool     srv_ok;
    bool     fw_upd;
    char     fw_ver[16];
    uint32_t fw_sz;
    bool     fw_patch;
    uint32_t fw_patch_sz;
    bool     md_upd;
    char     md_ver_new[16];
    char     md_name[64];
    uint32_t md_sz;
    bool     md_patch;
    uint32_t md_patch_sz;
} Dev_t;

/* ==================== 全局变量声明 ==================== */
extern Dev_t    D;
extern DlCtx_t  g_dl;
extern char     g_current_ver[16];

/* ==================== ota_net.c ==================== */
void ota_init(void);
bool wifi_connect(void);
bool http_post(const char *path, const char *body, char *resp, int rsz);
void srv_register(void);
void srv_heartbeat(void);
void check_fw(void);
void check_md(void);
bool dl_connect(void);
void dl_disconnect(void);
bool dl_is_connected(void);
int  dl_http_get(const char *path, uint8_t *buf, int buf_sz);

/* ==================== ota_update.c ==================== */
void ota_init_version(void);
bool dl_to_sd(const char *chunk_url, uint32_t size, const char *path);
bool start_fw_update(bool is_diff);
bool start_md_update(bool is_diff);
void load_md_ver(char *buf, int sz);
void write_update_flag(const char *ver, char target, bool is_diff);

/* ==================== bspatch.c ==================== */
int  bspatch_apply(const char *old_path,
                   const char *patch_path,
                   const char *new_path);
/* ==================== demo.c ==================== */
void demo_run(void);
void lcd_page_main(void);
void lcd_page_update_sel(void);
void lcd_page_fw_sel(void);
void lcd_page_md_sel(void);
void lcd_page_model_run(void);
void lcd_page_download(void);
void lcd_page_result(bool ok, const char *msg);

extern void ota_ui_progress(uint32_t done, uint32_t total);

#endif /* OTA_H */
