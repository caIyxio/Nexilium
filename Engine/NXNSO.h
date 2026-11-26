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
    uint64_t loadBase;
    uint64_t moduleHeaderAddress;
    uint64_t dynamicAddress;
    uint64_t symbolTableAddress;
    uint64_t symbolCount;
    uint64_t stringTableAddress;
    uint64_t stringTableSize;
    uint32_t relativeRelocationCount;
    uint32_t symbolRelocationCount;
    uint32_t unsupportedRelocationCount;
    bool hasModuleHeader;
} NXLoadedNSO;

#define NX_NSO_MAX_MODULES 32u

typedef struct {
    NXLoadedNSO modules[NX_NSO_MAX_MODULES];
    size_t count;
} NXNSORegistry;

void NXNSORegistryInit(NXNSORegistry *registry);
bool NXNSORegistryRegister(NXNSORegistry *registry,
                           const NXLoadedNSO *module);

bool NXEngineLoadNSOWithRegistry(NXEngine *engine, const uint8_t *fileBytes,
                                 size_t fileSize, uint64_t loadBase,
                                 NXNSORegistry *registry,
                                 NXLoadedNSO *loadedImage);

bool NXEngineLoadNSO(NXEngine *engine, const uint8_t *fileBytes,
                     size_t fileSize, uint64_t loadBase,
                     NXLoadedNSO *loadedImage);

#endif
