/**
 * ================================================================
 *  bspatch.c — BSPatch 差分合成引擎 (BSDIFF40 + deflate)
 *
 *  依赖: ota.h, ff.h, malloc.h
 *  提供: bspatch_apply()
 * ================================================================
 */
#include "ota.h"
#include "./FATFS/source/ff.h"
#include "./MALLOC/malloc.h"
#include <stdlib.h>

/* ================================================================
 *  uzlib 精简 deflate 解码器
 * ================================================================ */

typedef struct {
    FIL        *fp;
    uint32_t    comp_remain;
    uint32_t    bitbuf;
    int         bitcnt;
    uint8_t    *window;
    uint32_t    win_size;
    uint32_t    win_pos;
    int         btype;
    int         bfinal;
    int         out_count;
    int         out_limit;
} uzlib_state_t;

static uint16_t g_fixed_litlen[288];
static uint16_t g_fixed_dist[32];
static int      g_tables_inited = 0;

static void uzlib_build_fixed_tables(void)
{
    int i, code;
    code = 0;
    for (i = 0; i <= 143; i++)
        g_fixed_litlen[i] = (8 << 9) | (code++ & 0x1FF);
    for (i = 144; i <= 255; i++)
        g_fixed_litlen[i] = (9 << 9) | (code++ & 0x1FF);
    code = 0;
    for (i = 256; i <= 279; i++)
        g_fixed_litlen[i] = (7 << 9) | (code++ & 0x1FF);
    code = 48;
    for (i = 280; i <= 287; i++)
        g_fixed_litlen[i] = (8 << 9) | (code++ & 0x1FF);
    code = 0;
    for (i = 0; i < 32; i++)
        g_fixed_dist[i] = (5 << 9) | (code++ & 0x1FF);
    g_tables_inited = 1;
}

static int uzlib_read_bit(uzlib_state_t *uz)
{
    while (uz->bitcnt < 1) {
        if (uz->comp_remain == 0) return -1;
        uint8_t c; UINT br;
        if (f_read(uz->fp, &c, 1, &br) != FR_OK || br != 1) return -1;
        uz->bitbuf |= (uint32_t)c << uz->bitcnt;
        uz->bitcnt += 8;
        uz->comp_remain--;
    }
    int bit = uz->bitbuf & 1;
    uz->bitbuf >>= 1;
    uz->bitcnt--;
    return bit;
}

static int uzlib_read_bits(uzlib_state_t *uz, int n)
{
    while (uz->bitcnt < n) {
        if (uz->comp_remain == 0) return -1;
        uint8_t c; UINT br;
        if (f_read(uz->fp, &c, 1, &br) != FR_OK || br != 1) return -1;
        uz->bitbuf |= (uint32_t)c << uz->bitcnt;
        uz->bitcnt += 8;
        uz->comp_remain--;
    }
    int val = uz->bitbuf & ((1u << n) - 1);
    uz->bitbuf >>= n;
    uz->bitcnt -= n;
    return val;
}

static int uzlib_emit(uzlib_state_t *uz, uint8_t c)
{
    if (uz->out_limit >= 0 && uz->out_count >= uz->out_limit)
        return -2;
    if (uz->window) {
        uz->window[uz->win_pos] = c;
        uz->win_pos = (uz->win_pos + 1) % uz->win_size;
    }
    uz->out_count++;
    return (int)c;
}

static const int len_extra_bits[] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
    3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const int len_base[] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
    35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const int dist_extra_bits[] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
    7,7,8,8,9,9,10,10,11,11,12,12,13,13
};
static const int dist_base[] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};

/* ---- 固定 Huffman 表解码 ---- */
static int uz_decode_fixed_litlen(uzlib_state_t *uz)
{
    int code = 0, first = 0, bit;
    for (int bits = 1; bits <= 9; bits++) {
        bit = uzlib_read_bit(uz);
        if (bit < 0) return -1;
        code = (code << 1) | bit;
        int count = 0;
        for (int i = 0; i < 288; i++)
            if ((g_fixed_litlen[i] >> 9) == bits) count++;
        for (int i = 0; i < 288; i++) {
            if ((g_fixed_litlen[i] >> 9) == bits) {
                if (code == (g_fixed_litlen[i] & 0x1FF))
                    return i;
            }
        }
        first = (first + count) << 1;
    }
    return -1;
}

static int uz_decode_fixed_dist(uzlib_state_t *uz)
{
    int code = 0, bit;
    for (int bits = 1; bits <= 5; bits++) {
        bit = uzlib_read_bit(uz);
        if (bit < 0) return -1;
        code = (code << 1) | bit;
        for (int i = 0; i < 32; i++) {
            if ((g_fixed_dist[i] >> 9) == bits) {
                if (code == (g_fixed_dist[i] & 0x1FF))
                    return i;
            }
        }
    }
    return -1;
}

/* ---- 固定 Huffman 块处理 ---- */
static int uz_process_fixed_block(uzlib_state_t *uz)
{
    for (;;) {
        int sym = uz_decode_fixed_litlen(uz);
        if (sym < 0) return -1;
        if (sym < 256) {
            int r = uzlib_emit(uz, (uint8_t)sym);
            if (r < 0) return r;
        } else if (sym == 256) {
            return 0;
        } else {
            int idx = sym - 257;
            if (idx < 0 || idx >= 29) return -1;
            int length = len_base[idx];
            if (len_extra_bits[idx] > 0) {
                int extra = uzlib_read_bits(uz, len_extra_bits[idx]);
                if (extra < 0) return -1;
                length += extra;
            }
            int dsym = uz_decode_fixed_dist(uz);
            if (dsym < 0) return -1;
            int distance = dist_base[dsym];
            if (dist_extra_bits[dsym] > 0) {
                int extra = uzlib_read_bits(uz, dist_extra_bits[dsym]);
                if (extra < 0) return -1;
                distance += extra;
            }
            uint32_t src = (uz->win_pos + uz->win_size - distance) % uz->win_size;
            for (int i = 0; i < length; i++) {
                uint8_t c = uz->window[src];
                src = (src + 1) % uz->win_size;
                int r = uzlib_emit(uz, c);
                if (r < 0) return r;
            }
        }
    }
}


/* ================================================================
 *  BSPatch 引擎
 * ================================================================ */

static int64_t read_int64_le_file(FIL *fp)
{
    uint8_t buf[8]; UINT br;
    if (f_read(fp, buf, 8, &br) != FR_OK || br != 8) return -1;
    int64_t val = 0;
    for (int i = 7; i >= 0; i--)
        val = (val << 8) | buf[i];
    return val;
}

/*
 *  从 deflate 压缩文件段中逐字节解压输出
 *  使用固定 Huffman 表 (对应 Python zlib.compress 的默认输出)
 *
 *  fp           : 已 seek 到压缩数据起始位置的文件句柄
 *  comp_size    : 该段压缩数据字节数
 *  out_buf      : 输出缓冲区
 *  out_buf_size : 输出缓冲区大小
 *  window       : LZ77 滑动窗口
 *  win_size     : 窗口大小
 *
 *  返回: 实际解压输出的字节数, <0 表示错误
 */
static int deflate_decompress_segment(FIL *fp, uint32_t comp_size,
                                      uint8_t *out_buf, uint32_t out_buf_size,
                                      uint8_t *window, uint32_t win_size)
{
    uzlib_state_t uz;
    memset(&uz, 0, sizeof(uz));
    uz.fp = fp;
    uz.comp_remain = comp_size;
    uz.window = window;
    uz.win_size = win_size;
    uz.win_pos = 0;
    uz.out_limit = (int)out_buf_size;
    uz.out_count = 0;

    if (!g_tables_inited) uzlib_build_fixed_tables();

    /* 尝试检测并跳过 zlib 头 */
    {
        uint8_t hdr[2]; UINT br;
        FSIZE_t save = f_tell(fp);
        if (f_read(fp, hdr, 2, &br) == FR_OK && br == 2) {
            if (((hdr[0] * 256 + hdr[1]) % 31 == 0) && ((hdr[0] & 0x0F) == 8)) {
                uz.comp_remain -= 2;
                if (hdr[1] & 0x20) {
                    uint8_t d[4];
                    f_read(fp, d, 4, &br);
                    uz.comp_remain -= 4;
                }
            } else {
                f_lseek(fp, save);
            }
        } else {
            f_lseek(fp, save);
        }
    }

    uint32_t out_pos = 0;

    while (out_pos < out_buf_size) {
        /* 读块头 */
        if (uz.bfinal == 0 || uz.btype >= 0) {
            int bf = uzlib_read_bit(&uz);
            int bt = uzlib_read_bits(&uz, 2);
            if (bf < 0 || bt < 0) break;
            uz.bfinal = bf;
            uz.btype = bt;
        }

        if (uz.btype == 0) {
            /* non-compressed */
            uz.bitbuf >>= uz.bitcnt % 8;
            uz.bitcnt -= uz.bitcnt % 8;
            uint8_t hdr4[4]; UINT br;
            if (f_read(uz.fp, hdr4, 4, &br) != FR_OK || br != 4) break;
            uz.comp_remain -= 4;
            uint16_t len = hdr4[0] | (hdr4[1] << 8);
            for (uint16_t i = 0; i < len && out_pos < out_buf_size; i++) {
                uint8_t c; UINT r;
                if (f_read(uz.fp, &c, 1, &r) != FR_OK || r != 1) break;
                uz.comp_remain--;
                out_buf[out_pos++] = c;
                if (uz.window) {
                    uz.window[uz.win_pos] = c;
                    uz.win_pos = (uz.win_pos + 1) % uz.win_size;
                }
            }
        } else if (uz.btype == 1) {
            /* fixed Huffman - 输出到临时计数器 */
            int save_count = uz.out_count;
            uz.out_count = 0;
            uz.out_limit = (int)(out_buf_size - out_pos);
            int ret = uz_process_fixed_block(&uz);
            /* 将 window 中的最新数据拷贝到 out_buf */
            int produced = uz.out_count;
            uz.out_count = save_count + produced;
            uz.out_limit = (int)out_buf_size;
            if (produced > 0) {
                uint32_t rd_pos = (uz.win_pos + win_size - produced) % win_size;
                for (int i = 0; i < produced && out_pos < out_buf_size; i++) {
                    out_buf[out_pos++] = uz.window[rd_pos];
                    rd_pos = (rd_pos + 1) % win_size;
                }
            }
            if (ret < 0 && ret != -2) break;
        } else {
            /* dynamic Huffman 不支持 — zlib.compress 默认生成 fixed */
            printf("[BSPATCH] ERROR: dynamic Huffman not supported\r\n");
            return -1;
        }

        if (uz.bfinal) break;
    }

    return (int)out_pos;
}


int bspatch_apply(const char *old_path,
                  const char *patch_path,
                  const char *new_path)
{
    FIL old_f, patch_f, new_f;
    UINT br, bw;
    int ret = -1;

    printf("[BSPATCH] %s + %s -> %s\r\n", old_path, patch_path, new_path);

    if (f_open(&old_f, old_path, FA_READ) != FR_OK) {
        printf("[BSPATCH] open old fail\r\n"); return -1;
    }
    if (f_open(&patch_f, patch_path, FA_READ) != FR_OK) {
        printf("[BSPATCH] open patch fail\r\n");
        f_close(&old_f); return -1;
    }
    f_unlink(new_path);
    if (f_open(&new_f, new_path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        printf("[BSPATCH] open new fail\r\n");
        f_close(&old_f); f_close(&patch_f); return -1;
    }

    /* ---- 读取文件头 ---- */
    uint8_t magic[8];
    if (f_read(&patch_f, magic, 8, &br) != FR_OK || br != 8 ||
        memcmp(magic, "BSDIFF40", 8) != 0) {
        printf("[BSPATCH] bad magic\r\n");
        ret = -2; goto cleanup;
    }

    int64_t ctrl_comp_len  = read_int64_le_file(&patch_f);
    int64_t diff_comp_len  = read_int64_le_file(&patch_f);
    int64_t new_file_size  = read_int64_le_file(&patch_f);

    if (ctrl_comp_len <= 0 || diff_comp_len <= 0 || new_file_size <= 0) {
        printf("[BSPATCH] invalid header\r\n");
        ret = -2; goto cleanup;
    }

    printf("[BSPATCH] ctrl=%lld diff=%lld newsize=%lld\r\n",
           (long long)ctrl_comp_len, (long long)diff_comp_len,
           (long long)new_file_size);

    uint32_t ctrl_file_off  = 32;
    uint32_t diff_file_off  = ctrl_file_off + (uint32_t)ctrl_comp_len;
    uint32_t extra_file_off = diff_file_off + (uint32_t)diff_comp_len;

    /* ---- 分配工作缓冲区 ---- */
    #define BSPATCH_WIN_SZ  4096
    uint8_t *win_buf = (uint8_t *)mymalloc(SRAMIN, BSPATCH_WIN_SZ);
    uint8_t *seg_buf = (uint8_t *)mymalloc(SRAMIN, BSPATCH_WIN_SZ);
    if (!win_buf || !seg_buf) {
        printf("[BSPATCH] malloc fail\r\n");
        goto cleanup;
    }

    /* ---- 解压三个段到内存 ---- */
    /* ctrl 段: 每个三元组 3×8=24 字节, 最多 N 个三元组 */
    #define MAX_CTRL_TRIPLES  512
    #define CTRL_BUF_SZ       (MAX_CTRL_TRIPLES * 24)

    uint8_t *ctrl_buf = (uint8_t *)mymalloc(SRAMIN, CTRL_BUF_SZ);
    if (!ctrl_buf) {
        printf("[BSPATCH] malloc ctrl fail\r\n"); goto cleanup;
    }

    f_lseek(&patch_f, ctrl_file_off);
    int ctrl_len = deflate_decompress_segment(
        &patch_f, (uint32_t)ctrl_comp_len,
        ctrl_buf, CTRL_BUF_SZ,
        win_buf, BSPATCH_WIN_SZ);
    if (ctrl_len <= 0) {
        printf("[BSPATCH] ctrl decompress fail: %d\r\n", ctrl_len);
        goto cleanup;
    }
    printf("[BSPATCH] ctrl decompressed: %d bytes\r\n", ctrl_len);

    /* diff 段: 整段解压 */
    #define DIFF_BUF_SZ  (128 * 1024)
    uint8_t *diff_buf = (uint8_t *)mymalloc(SRAMEX, DIFF_BUF_SZ);
    if (!diff_buf) {
        diff_buf = (uint8_t *)mymalloc(SRAMIN, 32 * 1024);
        if (!diff_buf) {
            printf("[BSPATCH] malloc diff fail\r\n"); goto cleanup;
        }
    }

    f_lseek(&patch_f, diff_file_off);
    int diff_len = deflate_decompress_segment(
        &patch_f, (uint32_t)diff_comp_len,
        diff_buf, DIFF_BUF_SZ,
        win_buf, BSPATCH_WIN_SZ);
    if (diff_len < 0) {
        printf("[BSPATCH] diff decompress fail: %d\r\n", diff_len);
        goto cleanup;
    }
    printf("[BSPATCH] diff decompressed: %d bytes\r\n", diff_len);

    /* extra 段: 整段解压 */
    #define EXTRA_BUF_SZ  (64 * 1024)
    uint8_t *extra_buf = (uint8_t *)mymalloc(SRAMEX, EXTRA_BUF_SZ);
    if (!extra_buf) {
        extra_buf = (uint8_t *)mymalloc(SRAMIN, 32 * 1024);
        if (!extra_buf) {
            printf("[BSPATCH] malloc extra fail\r\n"); goto cleanup;
        }
    }

    uint32_t extra_comp_len = f_size(&patch_f) - extra_file_off;
    f_lseek(&patch_f, extra_file_off);
    int extra_len = deflate_decompress_segment(
        &patch_f, extra_comp_len,
        extra_buf, EXTRA_BUF_SZ,
        win_buf, BSPATCH_WIN_SZ);
    if (extra_len < 0) {
        printf("[BSPATCH] extra decompress fail: %d\r\n", extra_len);
        goto cleanup;
    }
    printf("[BSPATCH] extra decompressed: %d bytes\r\n", extra_len);

    /* ---- 合成循环 ---- */
    int64_t  new_pos  = 0;
    int64_t  old_pos  = 0;
    uint32_t ctrl_idx = 0;
    uint32_t diff_idx = 0;
    uint32_t extra_idx = 0;
    uint32_t t0 = HAL_GetTick();

    while (new_pos < new_file_size && ctrl_idx + 24 <= (uint32_t)ctrl_len) {
        /* 读控制三元组 */
        int64_t add_len  = 0, copy_len = 0, old_off = 0;
        for (int i = 0; i < 8; i++)
            add_len |= (int64_t)ctrl_buf[ctrl_idx + i] << (i * 8);
        for (int i = 0; i < 8; i++)
            copy_len |= (int64_t)ctrl_buf[ctrl_idx + 8 + i] << (i * 8);
        for (int i = 0; i < 8; i++)
            old_off |= (int64_t)ctrl_buf[ctrl_idx + 16 + i] << (i * 8);
        ctrl_idx += 24;

        /* 阶段一: diff 叠加 */
        for (int64_t i = 0; i < add_len; i++) {
            uint8_t old_byte = 0;
            if (f_read(&old_f, &old_byte, 1, &br) != FR_OK || br != 1) {
                printf("[BSPATCH] old read fail\r\n");
                ret = -3; goto cleanup;
            }
            uint8_t diff_byte = 0;
            if (diff_idx < (uint32_t)diff_len)
                diff_byte = diff_buf[diff_idx++];
            uint8_t new_byte = (uint8_t)((old_byte + diff_byte) & 0xFF);
            if (f_write(&new_f, &new_byte, 1, &bw) != FR_OK) {
                ret = -3; goto cleanup;
            }
            new_pos++;
        }

        /* 阶段二: extra 直接复制 */
        for (int64_t i = 0; i < copy_len; i++) {
            uint8_t b = 0;
            if (extra_idx < (uint32_t)extra_len)
                b = extra_buf[extra_idx++];
            if (f_write(&new_f, &b, 1, &bw) != FR_OK) {
                ret = -3; goto cleanup;
            }
            new_pos++;
        }

        /* 阶段三: 调整旧文件偏移 */
        old_pos += old_off;
        f_lseek(&old_f, (FSIZE_t)old_pos);

        /* 进度 */
        if (HAL_GetTick() - t0 > 500) {
            ota_ui_progress((uint32_t)new_pos, (uint32_t)new_file_size);
            t0 = HAL_GetTick();
        }
    }

    f_sync(&new_f);

    if (new_pos != new_file_size) {
        printf("[BSPATCH] size mismatch: %lld vs %lld\r\n",
               (long long)new_pos, (long long)new_file_size);
        ret = -4; goto cleanup;
    }

    printf("[BSPATCH] Done: %lld bytes\r\n", (long long)new_pos);
    ret = 0;

cleanup:
    if (ctrl_buf) myfree(SRAMIN, ctrl_buf);
    if (diff_buf) myfree(SRAMEX, diff_buf);
    if (extra_buf) myfree(SRAMEX, extra_buf);
    if (win_buf) myfree(SRAMIN, win_buf);
    if (seg_buf) myfree(SRAMIN, seg_buf);
    f_close(&old_f);
    f_close(&patch_f);
    f_close(&new_f);
    return ret;
}
