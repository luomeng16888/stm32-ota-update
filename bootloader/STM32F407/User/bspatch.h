#ifndef __BSPATCH_H
#define __BSPATCH_H

#include <stdbool.h>

/**
 * @brief  ?? BSDIFF40 ????
 * @param  old_path:   ?????
 * @param  patch_path: ??????
 * @param  new_path:   ???????
 * @retval true=??, false=??
 */
bool bspatch(const char *old_path, const char *patch_path, const char *new_path);

#endif
