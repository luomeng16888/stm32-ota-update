/**
 * stubs.c — Bootloader????
 * ??????????(NORFLASH?FATTTESTER?USMART)
 */
#include <stdint.h>

/* NORFLASH ???(diskio.c??) */
uint16_t norflash_read_id(void) { return 0; }
void norflash_init(void) {}
void norflash_read(uint8_t *buf, uint32_t addr, uint16_t len) { (void)buf; (void)addr; (void)len; }
void norflash_write(uint8_t *buf, uint32_t addr, uint16_t len) { (void)buf; (void)addr; (void)len; }
void norflash_erase_sector(uint32_t addr) { (void)addr; }
void norflash_write_enable(void) {}
void norflash_wait_busy(void) {}

/* FATTTESTER ???(exfuns.c??) */
uint8_t mf_init(uint8_t part) { (void)part; return 0; }
