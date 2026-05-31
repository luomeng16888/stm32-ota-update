/**
 * @file    bspatch.c
 * @brief   BDIF ????????? (STM32F407 + FATFS)
 *
 * ??????: BDIF / BSDIFF40 / BSPAT02
 * BDIF ??: ????? + ??????, ?? bz2/RLE
 */

#include "bspatch.h"
#include "./FATFS/source/ff.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define PATCH_BUF 512

/* ???? uint32 LE (?? ARM ?????) */
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool bspatch(const char *old_path, const char *patch_path,
             const char *new_path)
{
    FIL pf, of, nf;
    UINT br, bw;
    uint8_t hdr[16], rhdr[8], buf[PATCH_BUF];
    uint32_t old_size, new_size, num_ranges;
    uint32_t r, offset, length;
    DWORD copied;
    uint32_t progress = 0;

    printf("bspatch: %s + %s -> %s\r\n",
           old_path, patch_path, new_path);

    /* ---- ???? ---- */
    if (f_open(&pf, patch_path, FA_READ) != FR_OK)
    { printf("bspatch: open patch fail\r\n"); return false; }
    if (f_open(&of, old_path, FA_READ) != FR_OK)
    { printf("bspatch: open old fail\r\n");
      f_close(&pf); return false; }
    if (f_open(&nf, new_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    { printf("bspatch: open new fail\r\n");
      f_close(&pf); f_close(&of); return false; }

    /* ---- ??????? (16 ??) ---- */
    if (f_read(&pf, hdr, 16, &br) != FR_OK || br != 16)
    { printf("bspatch: read header fail\r\n"); goto fail; }

    if (memcmp(hdr, "BDIF", 4) != 0)
    {
        printf("bspatch: bad magic: %02x%02x%02x%02x\r\n",
               hdr[0], hdr[1], hdr[2], hdr[3]);
        goto fail;
    }

    old_size    = rd32(hdr + 4);
    new_size    = rd32(hdr + 8);
    num_ranges  = rd32(hdr + 12);

    printf("bspatch: old=%lu new=%lu ranges=%lu\r\n",
           (unsigned long)old_size,
           (unsigned long)new_size,
           (unsigned long)num_ranges);

    if (new_size == 0)
    { printf("bspatch: new_size=0\r\n"); goto fail; }

    /* ---- Step 1: ????????? ---- */
    printf("bspatch: copying old -> new (%luB)\r\n",
           (unsigned long)old_size);
    copied = 0;
    while (copied < old_size)
    {
        int want = (int)((old_size - copied > PATCH_BUF)
                         ? PATCH_BUF : (old_size - copied));
        if (f_read(&of, buf, want, &br) != FR_OK || br == 0)
            break;
        if (f_write(&nf, buf, br, &bw) != FR_OK)
        { printf("bspatch: copy write fail\r\n"); goto fail; }
        copied += br;
    }

    /* ---- Step 2: ??? new_size ---- */
    if (new_size > old_size)
    {
        /* ?????: ?? */
        uint32_t pad = new_size - old_size;
        printf("bspatch: padding +%luB\r\n", (unsigned long)pad);
        memset(buf, 0, PATCH_BUF);
        while (pad > 0)
        {
            int chunk = (pad > PATCH_BUF) ? PATCH_BUF : (int)pad;
            if (f_write(&nf, buf, chunk, &bw) != FR_OK)
            { printf("bspatch: pad write fail\r\n"); goto fail; }
            pad -= chunk;
        }
    }
    else if (new_size < old_size)
    {
        /* ?????: ?? */
        printf("bspatch: truncating to %luB\r\n",
               (unsigned long)new_size);
        f_lseek(&nf, new_size);
        f_truncate(&nf);
    }

    /* ---- Step 3: ?????? ---- */
    for (r = 0; r < num_ranges; r++)
    {
        if (f_read(&pf, rhdr, 8, &br) != FR_OK || br != 8)
        { printf("bspatch: read range hdr fail\r\n"); goto fail; }

        offset = rd32(rhdr);
        length = rd32(rhdr + 4);

        /* ???? */
        if (offset + length > new_size)
        {
            printf("bspatch: range %lu OOB "
                   "(%lu+%lu > %lu)\r\n",
                   (unsigned long)r, (unsigned long)offset,
                   (unsigned long)length, (unsigned long)new_size);
            goto fail;
        }

        /* Seek ????????, ?????? */
        f_lseek(&nf, offset);

        DWORD remain = length;
        while (remain > 0)
        {
            int chunk = (remain > PATCH_BUF)
                        ? PATCH_BUF : (int)remain;
            if (f_read(&pf, buf, chunk, &br) != FR_OK || br == 0)
            { printf("bspatch: read range data fail\r\n"); goto fail; }
            if (f_write(&nf, buf, br, &bw) != FR_OK)
            { printf("bspatch: write range data fail\r\n"); goto fail; }
            remain -= br;
        }

        /* ?? */
        if (num_ranges > 0)
        {
            uint32_t pct = (r + 1) * 100 / num_ranges;
            if (pct >= progress + 25)
            { progress = pct;
              printf("bspatch: %lu%%\r\n", (unsigned long)pct); }
        }
    }

    f_close(&pf); f_close(&of); f_close(&nf);
    printf("bspatch: OK, %lu bytes, %lu ranges\r\n",
           (unsigned long)new_size, (unsigned long)num_ranges);
    return true;

fail:
    f_close(&pf); f_close(&of); f_close(&nf);
    f_unlink(new_path);
    printf("bspatch: FAILED\r\n");
    return false;
}
