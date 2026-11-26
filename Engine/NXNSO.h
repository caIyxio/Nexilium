#ifndef NEXILIUM_NXNSO_H
#define NEXILIUM_NXNSO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "NXMachine.h"

typedef struct {
    uint64_t loadAddress;
    uint64_t entryPoint;
    uint64_t imageSize;
    uint32_t textSize;
    uint32_t rodataSize;
    uint32_t dataSize;
    uint32_t bssSize;
} NXLoadedNSO;

bool NXEngineLoadNSO(NXEngine *engine, const uint8_t *fileBytes,
                     size_t fileSize, uint64_t loadBase,
                     NXLoadedNSO *loadedImage);

#endif
