#ifndef MTAR_MEMORY_H
#define MTAR_MEMORY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "microtar.h"

int mtar_open_memory(mtar_t *tar, const void *data, unsigned size);

#ifdef __cplusplus
}
#endif

#endif
