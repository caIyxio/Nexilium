#ifndef NEXILIUM_NXPROCESS_H
#define NEXILIUM_NXPROCESS_H

#include "NXMachine.h"
#include "NXNSO.h"
#include "NXNPDM.h"

typedef struct {
    uint64_t titleID;
    uint64_t entryPoint;
    uint64_t stackBase;
    uint64_t stackSize;
    uint8_t mainThreadPriority;
    uint8_t defaultCPU;
    NXLoadedNSO mainImage;
} NXPreparedProcess;

/*
 * Loads an NSO main image and prepares its initial EL0 stack/register state.
 * This is not a complete Horizon process launch: dynamic relocations,
 * module dependencies, kernel process objects, and service permissions
 * still need to be implemented.
 */
bool NXEnginePrepareMainThread(NXEngine *engine,
                               const uint8_t *npdmBytes, size_t npdmSize,
                               const uint8_t *nsoBytes, size_t nsoSize,
                               uint64_t loadBase,
                               NXPreparedProcess *prepared);

#endif
