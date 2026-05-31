#ifndef __OTA_UPDATE_H
#define __OTA_UPDATE_H

#include <stdint.h>
#include <stdbool.h>

#define OTA_HOST            "192.168.43.203"
#define OTA_PORT            8080
#define OTA_TIMEOUT_MS      30000
#define OTA_HTTP_BUF_SIZE   1024
#define OTA_PART_A          0
#define OTA_PART_B          1

typedef struct {
    bool    update;
    char    version[32];
    char    md5[64];
    char    url_a[128];
    char    url_b[128];
    uint32_t size;
} ota_update_info_t;

/* ??: ???????????? (? boot.cfg ??) */
extern char g_current_ver[16];

/**
 * @brief  ? SD ?/Flash ?? boot.cfg,??? g_current_ver
 *         ??? SD ????????
 */
void ota_init_version(void);

bool ota_update(void);
bool ota_register(void);
bool ota_heartbeat(void);

/* ?? demo.c ???? */
bool fw_full(void);
bool fw_diff(void);
bool md_full(void);
bool md_diff(void);
void load_md_ver(char *buf, int sz);

#endif /* __OTA_UPDATE_H */