#include "data_player.h"
#include "./FATFS/source/ff.h"
#include <stdio.h>

static FIL dp_file;

int dp_init(DataPlayer *p, const char *path)
{
    if (f_open(&dp_file, path, FA_READ) != FR_OK) {
        printf("[DP] open fail: %s\r\n", path);
        return -1;
    }

    UINT br;
    f_read(&dp_file, &p->total, 4, &br);
    f_read(&dp_file, &p->dmin, 4, &br);
    f_read(&dp_file, &p->dmax, 4, &br);

    p->cursor     = 0;
    p->chunk_base = 0xFFFFFFFF;

    f_lseek(&dp_file, 12);
    f_read(&dp_file, p->chunk, DP_CHUNK * 4, &br);
    p->chunk_base = 0;

    printf("[DP] %u pts, [%.3f, %.3f]\r\n",
           (unsigned)p->total, p->dmin, p->dmax);
    return 0;
}

static void dp_load_chunk(DataPlayer *p, uint32_t base)
{
    UINT br;
    f_lseek(&dp_file, 12 + base * 4);
    f_read(&dp_file, p->chunk, DP_CHUNK * 4, &br);
    p->chunk_base = base;
}

float dp_get(DataPlayer *p, uint32_t idx)
{
    if (idx >= p->total) idx %= p->total;
    if (idx < p->chunk_base || idx >= p->chunk_base + DP_CHUNK)
        dp_load_chunk(p, idx);
    return p->chunk[idx - p->chunk_base];
}

float dp_next(DataPlayer *p)
{
    float v = dp_get(p, p->cursor);
    p->cursor++;
    if (p->cursor >= p->total) p->cursor = DP_SEQ_LEN;  /* ? ??? */
    return v;
}

void dp_close(DataPlayer *p)
{
    f_close(&dp_file);
    p->total  = 0;
    p->cursor = 0;
}