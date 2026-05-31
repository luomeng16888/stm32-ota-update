/**
 * @file    bspatch.h
 * @brief   BDIF ?????????
 *
 * BDIF ??:
 *   Header (16 bytes):
 *     "BDIF"          4B
 *     old_size        4B  uint32 LE
 *     new_size        4B  uint32 LE
 *     num_ranges      4B  uint32 LE
 *   Ranges (??):
 *     offset          4B  uint32 LE  (????????)
 *     length          4B  uint32 LE  (????)
 *     data            length bytes   (???????????)
 *
 * ????:
 *   1. ?? old ??? new ??
 *   2. ? new ?????, ?????
 *   3. ? range ??????
 */

#ifndef __BSPATCH_H
#define __BSPATCH_H

#include <stdbool.h>

bool bspatch(const char *old_path, const char *patch_path,
             const char *new_path);

#endif
