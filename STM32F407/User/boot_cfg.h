/**
 * ================================================================
 *  boot_cfg.h — Bootloader 和 App 共用的 boot.cfg 管理
 * ================================================================
 *
 *  boot.cfg 文件格式 (存在 SD 卡 0:/OTA/boot.cfg):
 *
 *    active=A
 *    ver_a=1.0.0
 *    ver_b=0.0
 *
 *  使用方式:
 *    1. 两个工程都在 Keil 的 Include Path 中添加此文件所在目录
 *    2. #include "boot_cfg.h"
 *    3. 确保两个工程都链接了 FATFS (ff.h)
 *
 *  注意: 调用任何函数前, FATFS 必须已经 f_mount 成功
 * ================================================================
 */

#ifndef __BOOT_CFG_H
#define __BOOT_CFG_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "./FATFS/source/ff.h"

/* ==============================================================
 *  路径和常量
 * ============================================================== */
#define BOOT_CFG_DIR        "0:/OTA"
#define BOOT_CFG_FILE       "0:/OTA/boot.cfg"

#define BOOT_CFG_SYS_VER    "1.0.0"     /* 首次烧录时的默认版本 */

/* ==============================================================
 *  配置结构体
 * ============================================================== */
typedef struct {
    char active[4];     /* "A" 或 "B" */
    char ver_a[16];     /* 分区 A 的固件版本 */
    char ver_b[16];     /* 分区 B 的固件版本 */
} BootCfg;

/* ==============================================================
 *  函数实现 (static inline, 两个工程各自编译,不冲突)
 * ============================================================== */

/**
 * @brief  设置默认配置
 */
static inline void boot_cfg_default(BootCfg *cfg)
{
    memset(cfg, 0, sizeof(BootCfg));
    strcpy(cfg->active, "A");
    strcpy(cfg->ver_a, BOOT_CFG_SYS_VER);
    strcpy(cfg->ver_b, "0.0");
}

/**
 * @brief  从 SD 卡读取 boot.cfg
 * @param  cfg: 输出参数
 * @return true=读取成功, false=文件不存在或损坏(此时 cfg 为默认值)
 *
 *  调用前必须确保:
 *    - SD 卡已初始化 (sd_init() 成功)
 *    - FATFS 已挂载 (f_mount() 成功)
 */
static inline bool boot_cfg_read(BootCfg *cfg)
{
    FIL fil;
    UINT br;
    char buf[256];
    char *p, *e;

    /* 先设默认值 */
    boot_cfg_default(cfg);

    /* 确保目录存在 */
    f_mkdir(BOOT_CFG_DIR);

    /* 打开文件 */
    if (f_open(&fil, BOOT_CFG_FILE, FA_READ) != FR_OK) {
        printf("[BOOT_CFG] File not found, using defaults\r\n");
        return false;
    }

    memset(buf, 0, sizeof(buf));
    f_read(&fil, buf, sizeof(buf) - 1, &br);
    f_close(&fil);

    if (br < 5) {
        printf("[BOOT_CFG] File too small (%u bytes)\r\n", br);
        return false;
    }

    /* 解析 active= */
    p = strstr(buf, "active=");
    if (p) {
        char c = p[7];
        if (c == 'A' || c == 'B') {
            cfg->active[0] = c;
            cfg->active[1] = '\0';
        }
    }

    /* 解析 ver_a= */
    p = strstr(buf, "ver_a=");
    if (p) {
        p += 6;
        e = strchr(p, '\n');
        if (!e) e = strchr(p, '\r');
        if (!e) e = p + strlen(p);
        int len = (int)(e - p);
        if (len > 15) len = 15;
        if (len > 0) {
            memcpy(cfg->ver_a, p, len);
            cfg->ver_a[len] = '\0';
        }
    }

    /* 解析 ver_b= */
    p = strstr(buf, "ver_b=");
    if (p) {
        p += 6;
        e = strchr(p, '\n');
        if (!e) e = strchr(p, '\r');
        if (!e) e = p + strlen(p);
        int len = (int)(e - p);
        if (len > 15) len = 15;
        if (len > 0) {
            memcpy(cfg->ver_b, p, len);
            cfg->ver_b[len] = '\0';
        }
    }

    /* 合法性检查 */
    if (cfg->active[0] != 'A' && cfg->active[0] != 'B') {
        cfg->active[0] = 'A';
        cfg->active[1] = '\0';
    }

    printf("[BOOT_CFG] Read OK: active=%c ver_a=%s ver_b=%s\r\n",
           cfg->active[0], cfg->ver_a, cfg->ver_b);
    return true;
}

/**
 * @brief  写入 boot.cfg 到 SD 卡
 * @param  cfg: 要写入的配置
 * @return true=写入成功
 *
 *  注意: 写入后会调用 f_sync() 确保数据落盘
 */
static inline bool boot_cfg_write(BootCfg *cfg)
{
    FIL fil;
    UINT bw;
    char buf[128];
    int len;

    /* 合法性检查 */
    if (cfg->active[0] != 'A' && cfg->active[0] != 'B') {
        cfg->active[0] = 'A';
        cfg->active[1] = '\0';
    }

    f_mkdir(BOOT_CFG_DIR);

    if (f_open(&fil, BOOT_CFG_FILE, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        printf("[BOOT_CFG] Write open fail\r\n");
        return false;
    }

    len = snprintf(buf, sizeof(buf),
        "active=%s\nver_a=%s\nver_b=%s\n",
        cfg->active, cfg->ver_a, cfg->ver_b);

    f_write(&fil, buf, (UINT)len, &bw);
    f_sync(&fil);
    f_close(&fil);

    printf("[BOOT_CFG] Write OK: active=%c ver_a=%s ver_b=%s (%u bytes)\r\n",
           cfg->active[0], cfg->ver_a, cfg->ver_b, bw);
    return true;
}

/**
 * @brief  获取当前活跃分区的版本号
 * @return 指向 ver_a 或 ver_b 的指针 (不要修改返回值)
 */
static inline const char *boot_cfg_active_ver(const BootCfg *cfg)
{
    return (cfg->active[0] == 'B') ? cfg->ver_b : cfg->ver_a;
}

/**
 * @brief  获取当前活跃分区的 Flash 起始地址
 */
static inline uint32_t boot_cfg_active_addr(const BootCfg *cfg)
{
    return (cfg->active[0] == 'B') ? 0x080A0000UL : 0x08040000UL;
}

/**
 * @brief  获取指定分区的 Flash 起始地址
 */
static inline uint32_t boot_cfg_partition_addr(char part)
{
    return (part == 'B') ? 0x080A0000UL : 0x08040000UL;
}

/**
 * @brief  更新指定分区的版本号并写入 SD 卡
 * @param  part: 'A' 或 'B'
 * @param  ver:  新版本号
 * @return true=成功
 *
 *  典型用法 (App 下载固件完成后):
 *    BootCfg cfg;
 *    boot_cfg_read(&cfg);
 *    boot_cfg_update_ver(&cfg, 'B', "2.0.0");
 *    boot_cfg_set_active(&cfg, 'B');
 *    boot_cfg_write(&cfg);
 *    NVIC_SystemReset();
 */
static inline bool boot_cfg_update_ver(BootCfg *cfg, char part, const char *ver)
{
    if (part == 'A') {
        strncpy(cfg->ver_a, ver, 15);
        cfg->ver_a[15] = '\0';
    } else if (part == 'B') {
        strncpy(cfg->ver_b, ver, 15);
        cfg->ver_b[15] = '\0';
    } else {
        return false;
    }
    return true;
}

/**
 * @brief  设置活跃分区 (不写入 SD 卡,需要额外调用 boot_cfg_write)
 */
static inline void boot_cfg_set_active(BootCfg *cfg, char part)
{
    if (part == 'A' || part == 'B') {
        cfg->active[0] = part;
        cfg->active[1] = '\0';
    }
}

/**
 * @brief  打印当前配置 (调试用)
 */
static inline void boot_cfg_dump(const BootCfg *cfg)
{
    printf("=== BootCfg ===\r\n");
    printf("  active : %s (Partition %c)\r\n", cfg->active, cfg->active[0]);
    printf("  ver_a  : %s @ 0x08040000\r\n", cfg->ver_a);
    printf("  ver_b  : %s @ 0x080A0000\r\n", cfg->ver_b);
    printf("  cur_ver: %s\r\n", boot_cfg_active_ver(cfg));
    printf("  cur_addr: 0x%08lX\r\n",
           (unsigned long)boot_cfg_active_addr(cfg));
    printf("===============\r\n");
}

#endif /* __BOOT_CFG_H */
