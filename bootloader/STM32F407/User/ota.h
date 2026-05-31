#ifndef OTA_H
#define OTA_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "boot_cfg.h"

#define DEVICE_MODEL    "STM32F407"
#define SERVER_IP       SRV_HOST
#define SERVER_PORT     8080

#define API_REGISTER    "/api/device/register"
#define API_HEARTBEAT   "/api/device/heartbeat"
#define API_CHECK_FW    "/api/firmware/check"
#define API_CHECK_MD    "/api/model/check"

#define RESP_SZ         OTA_BUF_SIZE

typedef struct {
    char id[32];
    char ssid[32];
    char pass[64];
    bool srv_ok;
    char boot_active;
    char sys_ver[16];
    char md_ver[16];
    bool fw_upd;
    char fw_ver[16];
    uint32_t fw_sz;
    bool fw_patch;
    uint32_t fw_patch_sz;
    bool md_upd;
    char md_ver_new[16];
    char md_name[32];
    uint32_t md_sz;
    bool md_patch;
    uint32_t md_patch_sz;
} Dev_t;

typedef enum {
    ST_BOOT = 0, ST_MAIN, ST_CHECKING, ST_UPDATE_SEL,
    ST_FW_SEL, ST_MD_SEL, ST_RESULT, ST_RUNNING
} State_t;

extern Dev_t D;
extern char g_resp[];
extern char g_msg[];

/* ota_net.c */
void ota_init(void);
bool wifi_connect(void);
bool http_post(const char *path, const char *body, char *resp, int rsz);
void srv_register(void);
void srv_heartbeat(void);
void check_fw(void);
void check_md(void);

/* ? Network helpers — used by ota_update.c */
bool tcp_conn(void);
bool cipsend(const char *data, int len);
void safeclose(void);

/* ota_update.c */
bool fw_full(void);
bool fw_diff(void);
bool md_full(void);
bool md_diff(void);
void load_md_ver(char *ver, int len);

extern void lcd_dl(const char *lbl, uint32_t cur, uint32_t tot, uint32_t speed);

#endif
