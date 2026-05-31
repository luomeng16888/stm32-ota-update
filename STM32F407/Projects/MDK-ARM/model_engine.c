/**
 * model_engine.c — X-CUBE-AI v10.2.0 模型引擎 + SD卡动态加载
 *
 * v2.0: 从 model.bin 加载权重及量化参数 (scale/zero_point)
 *       model.bin 格式: [8字节header][1512字节权重]
 *       header: float scale + int32_t zero_point
 */
#include "model_engine.h"
#include "./AI_APP/network.h"
#include "./AI_APP/network_data_params.h"
#include "./AI_Inc/ai_platform.h"
#include "./FATFS/source/ff.h"
#include "stm32f4xx.h"
#include <stdio.h>
#include <string.h>

/* ==================== 权重缓冲区 (CCM RAM) ==================== */

static uint8_t weight_buf[WEIGHT_BUF_SIZE]
    __attribute__((aligned(4)));

static size_t m_loaded_size = 0;

/* ==================== 量化参数 (从model.bin header读取) ==================== */

static float m_out_scale = 0.002614103f;    /* 默认值, 实际运行时从文件加载 */
static int   m_out_zp    = 127;

/* ==================== X-CUBE-AI ==================== */

static ai_handle net_handle = AI_HANDLE_NULL;

/* 激活缓冲区 */
AI_ALIGNED(4)
static uint8_t activations[AI_NETWORK_DATA_ACTIVATIONS_SIZE];

/* X-CUBE-AI 内部表 (定义在 network_data.c) */
extern ai_handle g_network_activations_table[];
extern ai_handle g_network_weights_table[];

/* ==================== 模型加载 ==================== */

int me_load(const char *sd_path)
{
    FIL f;
    UINT br;

    if (f_open(&f, sd_path, FA_READ) != FR_OK) {
        printf("[ME] open fail: %s\r\n", sd_path);
        return -1;
    }

    uint32_t file_size = f_size(&f);

    if (file_size < 12) {
        printf("[ME] file too small: %u\r\n", (unsigned)file_size);
        f_close(&f);
        return -2;
    }

    /* 1. 读取 header (8字节): float scale + int32 zero_point */
    float    hdr_scale;
    int32_t  hdr_zp;

    if (f_read(&f, &hdr_scale, 4, &br) != FR_OK || br != 4) {
        printf("[ME] header read fail\r\n");
        f_close(&f);
        return -3;
    }
    if (f_read(&f, &hdr_zp, 4, &br) != FR_OK || br != 4) {
        printf("[ME] header read fail\r\n");
        f_close(&f);
        return -3;
    }

    m_out_scale = hdr_scale;
    m_out_zp    = (int)hdr_zp;

    printf("[ME] quant: scale=%.10f zp=%d\r\n", m_out_scale, m_out_zp);

    /* 2. 读取权重 (跳过8字节header) */
    uint32_t weights_size = file_size - 8;
    m_loaded_size = weights_size;

    if (weights_size > WEIGHT_BUF_SIZE) {
        printf("[ME] too large: %u > %d\r\n",
               (unsigned)weights_size, WEIGHT_BUF_SIZE);
        f_close(&f);
        return -2;
    }

    if (f_read(&f, weight_buf, weights_size, &br) != FR_OK
        || br != weights_size) {
        printf("[ME] weights read fail\r\n");
        f_close(&f);
        return -3;
    }
    f_close(&f);
    printf("[ME] weights: %u bytes\r\n", (unsigned)weights_size);

    /* 3. 替换权重指针 */
    g_network_weights_table[1] = (ai_handle)weight_buf;

    /* 4. 替换激活缓冲区指针 */
    g_network_activations_table[1] = (ai_handle)activations;

    /* 5. 销毁旧模型 */
    if (net_handle != AI_HANDLE_NULL) {
        ai_network_destroy(net_handle);
        net_handle = AI_HANDLE_NULL;
    }

    /* 6. 创建并初始化新模型 */
    {
        ai_error err;
        err = ai_network_create_and_init(
            &net_handle,
            AI_NETWORK_DATA_ACTIVATIONS_TABLE_GET(),
            AI_NETWORK_DATA_WEIGHTS_TABLE_GET());

        if (err.type != AI_ERROR_NONE) {
            printf("[ME] init fail: t=%d c=%d\r\n", err.type, err.code);
            return -4;
        }
    }

    /* 7. 打印模型信息 */
    {
        ai_network_report report;
        if (ai_network_get_report(net_handle, &report)) {
            printf("[ME] model: %s, nodes: %d\r\n",
                   report.model_name,
                   (int)report.n_nodes);
        }
    }

    printf("[ME] network ready, act=%d wgt=%d\r\n",
           AI_NETWORK_DATA_ACTIVATIONS_SIZE,
           AI_NETWORK_DATA_WEIGHTS_SIZE);
    return 0;
}

/* ==================== 推理 ==================== */

MeResult me_predict(float *seq, float *future, float dmin, float dmax)
{
    MeResult r;
    memset(&r, 0, sizeof(r));

    if (net_handle == AI_HANDLE_NULL) return r;

    /* 获取输入输出 buffer */
    ai_buffer *inputs  = ai_network_inputs_get(net_handle, NULL);
    ai_buffer *outputs = ai_network_outputs_get(net_handle, NULL);

    float range = dmax - dmin;

    /* ---- 输入量化 (float -> INT8) ---- */
    /* 格式: AI_BUFFER_FORMAT_S8, 64个采样点 */
    /* scale=0.007843138, zero_point=-1 */
    int8_t *in_data = (int8_t *)inputs[0].data;

    for (int t = 0; t < ME_SEQ_LEN; t++) {
        float norm = (seq[t] - dmin) / range * 2.0f - 1.0f;
        if (norm < -1.0f) norm = -1.0f;
        if (norm >  1.0f) norm =  1.0f;
        int q = (int)(norm * 127.0f);
        if (q < -128) q = -128;
        if (q >  127) q =  127;
        in_data[t] = (int8_t)q;
    }

    /* ---- 推理 ---- */
    uint32_t t0 = HAL_GetTick();
    ai_i32 nbatch = ai_network_run(net_handle, inputs, outputs);
    r.time_ms = HAL_GetTick() - t0;

    if (nbatch <= 0) {
        ai_error err = ai_network_get_error(net_handle);
        printf("[ME] run fail: t=%d c=%d\r\n", err.type, err.code);
        return r;
    }

    /* ---- 输出反量化 (INT8 -> float) ---- */
    /* 量化参数来自 model.bin header 中读取的值 */
    int8_t *out_data = (int8_t *)outputs[0].data;

    for (int i = 0; i < ME_PRED_LEN; i++) {
        float deq = (out_data[i] - m_out_zp) * m_out_scale;
        r.pred[i]   = (deq + 1.0f) / 2.0f * range + dmin;
        r.actual[i] = future[i];
    }

    /* ---- MSE ---- */
    float sum = 0;
    for (int i = 0; i < ME_PRED_LEN; i++) {
        float d = r.pred[i] - r.actual[i];
        sum += d * d;
    }
    r.mse = sum / ME_PRED_LEN;
    r.ok  = 1;

    return r;
}

uint32_t me_size(void)
{
    return (uint32_t)m_loaded_size;
}
