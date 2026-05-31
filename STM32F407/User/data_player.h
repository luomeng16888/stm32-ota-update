#ifndef DATA_PLAYER_H
#define DATA_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define DP_CHUNK    512
#define DP_SEQ_LEN  64      /* ??????,???? model_engine.h */

typedef struct {
    uint32_t total;
    uint32_t cursor;
    float    dmin;
    float    dmax;
    float    chunk[DP_CHUNK];
    uint32_t chunk_base;
} DataPlayer;

int   dp_init(DataPlayer *p, const char *path);
float dp_get(DataPlayer *p, uint32_t idx);
float dp_next(DataPlayer *p);
void  dp_close(DataPlayer *p);

#ifdef __cplusplus
}
#endif

#endif /* DATA_PLAYER_H */
