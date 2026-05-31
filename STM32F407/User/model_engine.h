#ifndef MODEL_ENGINE_H
#define MODEL_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define ME_SEQ_LEN      64
#define ME_PRED_LEN     8
#define WEIGHT_BUF_SIZE (16 * 1024)

typedef struct {
    float pred[ME_PRED_LEN];
    float actual[ME_PRED_LEN];
    float mse;
    uint32_t time_ms;
    int ok;
} MeResult;

int        me_load(const char *sd_path);
MeResult   me_predict(float *seq64, float *future8, float dmin, float dmax);
uint32_t   me_size(void);

#ifdef __cplusplus
}
#endif

#endif
