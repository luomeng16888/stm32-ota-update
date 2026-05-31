extern "C" {
#include "model_engine.h"
#include "./FATFS/source/ff.h"
#include "./SYSTEM/delay/delay.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
}

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

/* ---- ????? ---- */
static uint8_t model_buf[ME_MODEL_MAX] __attribute__((aligned(16)));
static uint8_t arena[ME_ARENA_SIZE]    __attribute__((aligned(16)));

static size_t m_size = 0;
static const tflite::Model *model = nullptr;

/* ? placement new ?? interpreter, ?????? */
static uint8_t interp_mem[sizeof(tflite::MicroInterpreter)]
    __attribute__((aligned(4)));
static tflite::MicroInterpreter *interp = nullptr;
static tflite::AllOpsResolver   resolver;

extern "C" {

int me_load(const char *sd_path)
{
    FIL f; UINT br;

    if (f_open(&f, sd_path, FA_READ) != FR_OK) {
        printf("[ME] open fail: %s\r\n", sd_path);
        return -1;
    }

    m_size = f_size(&f);
    if (m_size > ME_MODEL_MAX) {
        printf("[ME] too large: %u > %d\r\n", (unsigned)m_size, ME_MODEL_MAX);
        f_close(&f);
        return -2;
    }

    FRESULT fr = f_read(&f, model_buf, m_size, &br);
    f_close(&f);
    if (fr != FR_OK || br != m_size) {
        printf("[ME] read fail\r\n");
        return -3;
    }

    model = tflite::GetModel(model_buf);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        printf("[ME] schema mismatch\r\n");
        return -4;
    }

    /* ??? interpreter */
    if (interp) {
        interp->~MicroInterpreter();
        interp = nullptr;
    }

    /* placement new ??? interpreter */
    interp = new (interp_mem) tflite::MicroInterpreter(
        model, resolver, arena, ME_ARENA_SIZE);

    if (interp->AllocateTensors() != kTfLiteOk) {
        printf("[ME] AllocateTensors fail, need=%d\r\n",
               (int)interp->arena_used_bytes());
        interp = nullptr;
        return -5;
    }

    printf("[ME] loaded: %s (%u B, arena %d/%d)\r\n",
           sd_path, (unsigned)m_size,
           (int)interp->arena_used_bytes(), ME_ARENA_SIZE);
    return 0;
}

MeResult me_predict(float *seq, float *future, float dmin, float dmax)
{
    MeResult r;
    memset(&r, 0, sizeof(r));

    if (!interp) return r;

    /* ---- ???? ---- */
    TfLiteTensor *tin = interp->input(0);
    int8_t *id = tin->data.int8;
    float sc = tin->params.scale;
    int   zp = tin->params.zero_point;
    float range = dmax - dmin;

    for (int t = 0; t < ME_SEQ_LEN; t++) {
        float n = (seq[t] - dmin) / range * 2.0f - 1.0f;
        if (n < -1.0f) n = -1.0f;
        if (n >  1.0f) n =  1.0f;
        int q = (int)(n / sc + zp);
        if (q < -128) q = -128;
        if (q >  127) q =  127;
        id[t] = (int8_t)q;
    }

    /* ---- ?? ---- */
    uint32_t t0 = HAL_GetTick();
    TfLiteStatus st = interp->Invoke();
    r.time_ms = HAL_GetTick() - t0;

    if (st != kTfLiteOk) {
        printf("[ME] Invoke fail\r\n");
        return r;
    }

    /* ---- ????? ---- */
    TfLiteTensor *tout = interp->output(0);
    int8_t *od = tout->data.int8;
    float osc = tout->params.scale;
    int   ozp = tout->params.zero_point;

    float sum = 0;
    for (int i = 0; i < ME_PRED_LEN; i++) {
        float deq = (od[i] - ozp) * osc;
        r.pred[i]   = (deq + 1.0f) / 2.0f * range + dmin;
        r.actual[i] = future[i];
        float d = r.pred[i] - r.actual[i];
        sum += d * d;
    }
    r.mse = sum / ME_PRED_LEN;
    r.ok  = 1;
    return r;
}

uint32_t me_size(void) { return m_size; }

} /* extern "C" */
