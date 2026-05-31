/**
 * @file    bspatch.c
 * @brief   BSDIFF40 patch application for STM32F407 + FATFS
 */

#include "bspatch.h"
#include "./FATFS/source/ff.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define BSPATCH_BUF 4096

/* ?? BSDIFF40 int64: ???, bit63 ???? */
static int64_t offtin(const uint8_t *buf)
{
    int64_t y = buf[7] & 0x7F;
    for (int i = 6; i >= 0; i--)
        y = y * 256 + buf[i];
    if (buf[7] & 0x80)
        y = -y;
    return y;
}

bool bspatch(const char *old_path, const char *patch_path, const char *new_path)
{
    FIL pf, of, nf;
    UINT br, bw;
    uint8_t header[32];
    int64_t ctrl_len, diff_len, new_size;
    int64_t oldpos = 0, newpos = 0;
    DWORD ctrl_off, diff_off, extra_off;
    uint32_t progress = 0;

    printf("bspatch: %s + %s -> %s\r\n", old_path, patch_path, new_path);

    if (f_open(&pf, patch_path, FA_READ) != FR_OK)
    { printf("bspatch: open patch fail\r\n"); return false; }
    if (f_open(&of, old_path, FA_READ) != FR_OK)
    { printf("bspatch: open old fail\r\n"); f_close(&pf); return false; }
    if (f_open(&nf, new_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    { printf("bspatch: open new fail\r\n"); f_close(&pf); f_close(&of); return false; }

    /* ??????? */
    if (f_read(&pf, header, 32, &br) != FR_OK || br != 32)
    { printf("bspatch: read header fail\r\n"); goto fail; }
    if (memcmp(header, "BSDIFF40", 8) != 0)
    { printf("bspatch: bad magic\r\n"); goto fail; }

    ctrl_len = offtin(header + 8);
    diff_len = offtin(header + 16);
    new_size = offtin(header + 24);

    printf("bspatch: ctrl=%ld diff=%ld new=%ld\r\n",
           (long)ctrl_len, (long)diff_len, (long)new_size);

    if (new_size <= 0 || ctrl_len <= 0)
    { printf("bspatch: invalid sizes\r\n"); goto fail; }

    ctrl_off  = 32;
    diff_off  = 32 + (DWORD)ctrl_len;
    extra_off = 32 + (DWORD)ctrl_len + (DWORD)diff_len;

    while (newpos < new_size)
    {
        uint8_t cbuf[24];
        int64_t add_len, copy_len, seek_adj;

        f_lseek(&pf, ctrl_off);
        if (f_read(&pf, cbuf, 24, &br) != FR_OK || br != 24)
        { printf("bspatch: read ctrl fail\r\n"); goto fail; }
        ctrl_off += 24;

        add_len  = offtin(cbuf);
        copy_len = offtin(cbuf + 8);
        seek_adj = offtin(cbuf + 16);

        if (add_len < 0 || copy_len < 0)
        { printf("bspatch: negative len\r\n"); goto fail; }

        /* diff: ?? + ?????? */
        {
            int64_t remaining = add_len;
            while (remaining > 0)
            {
                uint8_t dbuf[BSPATCH_BUF], obuf[BSPATCH_BUF];
                int chunk = (remaining > BSPATCH_BUF) ? BSPATCH_BUF : (int)remaining;
                UINT old_br;

                f_lseek(&pf, diff_off);
                if (f_read(&pf, dbuf, chunk, &br) != FR_OK || br != (UINT)chunk)
                { printf("bspatch: read diff fail\r\n"); goto fail; }
                diff_off += chunk;

                f_lseek(&of, (DWORD)oldpos);
                f_read(&of, obuf, chunk, &old_br);

                for (int i = 0; i < chunk; i++)
                    dbuf[i] += (i < (int)old_br) ? obuf[i] : 0;

                if (f_write(&nf, dbuf, chunk, &bw) != FR_OK || bw != (UINT)chunk)
                { printf("bspatch: write fail\r\n"); goto fail; }

                oldpos += chunk;
                newpos += chunk;
                remaining -= chunk;
            }
        }

        /* extra: ???? */
        {
            int64_t remaining = copy_len;
            while (remaining > 0)
            {
                uint8_t buf[BSPATCH_BUF];
                int chunk = (remaining > BSPATCH_BUF) ? BSPATCH_BUF : (int)remaining;

                f_lseek(&pf, extra_off);
                if (f_read(&pf, buf, chunk, &br) != FR_OK || br != (UINT)chunk)
                { printf("bspatch: read extra fail\r\n"); goto fail; }
                extra_off += chunk;

                if (f_write(&nf, buf, chunk, &bw) != FR_OK || bw != (UINT)chunk)
                { printf("bspatch: write extra fail\r\n"); goto fail; }

                newpos += chunk;
                remaining -= chunk;
            }
        }

        oldpos += seek_adj;

        if (new_size > 0)
        {
            uint32_t pct = (uint32_t)(newpos * 100 / new_size);
            if (pct >= progress + 10)
            { progress = pct; printf("bspatch: %lu%%\r\n", (unsigned long)pct); }
        }
    }

    f_close(&pf); f_close(&of); f_close(&nf);
    printf("bspatch: OK, %ld bytes\r\n", (long)new_size);
    return true;

fail:
    f_close(&pf); f_close(&of); f_close(&nf);
    f_unlink(new_path);
    printf("bspatch: FAILED\r\n");
    return false;
}
