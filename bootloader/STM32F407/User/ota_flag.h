/**
 * @file    ota_flag.h
 * @brief   Dual-APP OTA: config structure + CRC32 + flash layout
 *
 * Flash Layout:
 *   Boot:    0x08000000  sectors 0-4   128KB
 *   APP_C:   0x08020000  sectors 5-7   384KB
 *   APP_D:   0x08080000  sectors 8-10  384KB
 *   Config:  0x080E0000  sector 11     (first 20 bytes used)
 */
#ifndef OTA_FLAG_H
#define OTA_FLAG_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ============================================================
 *  Flash Layout
 * ============================================================ */
#define FLASH_BOOT_ADDR         0x08000000
#define FLASH_BOOT_SIZE         (128U * 1024U)
#define FLASH_BOOT_SEC_START    0
#define FLASH_BOOT_SEC_COUNT    5

#define FLASH_APP_C_ADDR        0x08020000
#define FLASH_APP_C_SIZE        (384U * 1024U)
#define FLASH_APP_C_SEC_START   5
#define FLASH_APP_C_SEC_COUNT   3

#define FLASH_APP_D_ADDR        0x08080000
#define FLASH_APP_D_SIZE        (384U * 1024U)
#define FLASH_APP_D_SEC_START   8
#define FLASH_APP_D_SEC_COUNT   3

#define FLASH_CONFIG_ADDR       0x080E0000
#define FLASH_CONFIG_SEC        11

/* Partition IDs */
#define PARTITION_C             0
#define PARTITION_D             1

/* ============================================================
 *  Config structure (20 bytes, stored in flash at CONFIG_ADDR)
 *
 *  Layout:
 *    [0..3]   magic             0x4F544346 "OTCF"
 *    [4..7]   active_partition  PARTITION_C (0) or PARTITION_D (1)
 *    [8..11]  version_c         firmware version in APP_C (numeric)
 *    [12..15] version_d         firmware version in APP_D (numeric)
 *    [16..19] checksum          CRC32 of bytes [0..15]
 * ============================================================ */
#define OTA_CONFIG_MAGIC         0x4F544346   /* "OTCF" */
#define OTA_CONFIG_DATA_SIZE     16           /* bytes before checksum */

/* Backup Register definitions */
#define BKUP_MAGIC               0x4F544234   /* "OTB4" */
#define BKUP_HEALTHY             0x48454C50   /* "HELP" */
#define BKUP_MAX_BOOT_COUNT      3

#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION < 6000000)
    #pragma pack(push, 1)
    typedef struct {
        uint32_t magic;             /* OTA_CONFIG_MAGIC            */
        uint32_t active_partition;  /* PARTITION_C or PARTITION_D  */
        uint32_t version_c;         /* version in APP_C (e.g. 105) */
        uint32_t version_d;         /* version in APP_D (e.g. 106) */
        uint32_t checksum;          /* CRC32 of above 16 bytes     */
    } OtaConfig_t;
    #pragma pack(pop)
#else
    typedef struct {
        uint32_t magic;
        uint32_t active_partition;
        uint32_t version_c;
        uint32_t version_d;
        uint32_t checksum;
    } __attribute__((packed)) OtaConfig_t;
#endif

/* ============================================================
 *  CRC32 — ISO 3309
 * ============================================================ */

static inline uint32_t ota_crc32_update(uint32_t crc,
                                         const uint8_t *data,
                                         uint32_t len)
{
    uint32_t i, j;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320U & (-(crc & 1)));
    }
    return crc;
}

static inline uint32_t ota_crc32_init(void)         { return 0xFFFFFFFF; }
static inline uint32_t ota_crc32_final(uint32_t crc) { return ~crc; }

static inline uint32_t ota_crc32(const uint8_t *data, uint32_t len)
{
    return ota_crc32_final(ota_crc32_update(ota_crc32_init(), data, len));
}

/* ============================================================
 *  Config operations (inline for shared use by boot + app)
 * ============================================================ */

static inline uint32_t ota_config_calc_checksum(const OtaConfig_t *cfg)
{
    return ota_crc32_final(
        ota_crc32_update(ota_crc32_init(),
                         (const uint8_t *)cfg,
                         OTA_CONFIG_DATA_SIZE));
}

static inline bool ota_config_valid(const OtaConfig_t *cfg)
{
    if (cfg->magic != OTA_CONFIG_MAGIC) return false;
    if (cfg->active_partition != PARTITION_C &&
        cfg->active_partition != PARTITION_D) return false;
    if (cfg->checksum != ota_config_calc_checksum(cfg)) return false;
    return true;
}

static inline void ota_config_fill(OtaConfig_t *cfg,
                                    uint32_t active,
                                    uint32_t ver_c,
                                    uint32_t ver_d)
{
    cfg->magic            = OTA_CONFIG_MAGIC;
    cfg->active_partition = active;
    cfg->version_c        = ver_c;
    cfg->version_d        = ver_d;
    cfg->checksum         = ota_config_calc_checksum(cfg);
}

/** Get the flash address of a partition */
static inline uint32_t ota_partition_addr(uint32_t partition)
{
    return (partition == PARTITION_C) ? FLASH_APP_C_ADDR : FLASH_APP_D_ADDR;
}

/** Get the sector start of a partition */
static inline uint8_t ota_partition_sec_start(uint32_t partition)
{
    return (partition == PARTITION_C) ?
           FLASH_APP_C_SEC_START : FLASH_APP_D_SEC_START;
}

/** Get the sector count of a partition */
static inline uint8_t ota_partition_sec_count(uint32_t partition)
{
    return (partition == PARTITION_C) ?
           FLASH_APP_C_SEC_COUNT : FLASH_APP_D_SEC_COUNT;
}

/** Get the size of a partition */
static inline uint32_t ota_partition_size(uint32_t partition)
{
    return (partition == PARTITION_C) ?
           FLASH_APP_C_SIZE : FLASH_APP_D_SIZE;
}

/** Get the inactive partition ID */
static inline uint32_t ota_inactive_partition(uint32_t active)
{
    return (active == PARTITION_C) ? PARTITION_D : PARTITION_C;
}

#endif /* OTA_FLAG_H */
