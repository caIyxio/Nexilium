#ifndef NEXILIUM_MACHINE_H
#define NEXILIUM_MACHINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "Arm64/cpu.h"
#include "NXCPU.h"

#define NX_MAX_MEMORY_REGIONS 64

typedef struct {
    uint64_t guestBase;
    size_t size;
    uint8_t *host;
    unsigned permissions;
    bool ownsStorage;
} NXMemoryRegion;

typedef struct {
    bool valid;
    uint64_t syndrome;
    uint64_t faultAddress;
    uint64_t returnAddress;
    ExcKind kind;
} NXPendingException;

typedef struct Machine {
    NXMemoryRegion regions[NX_MAX_MEMORY_REGIONS];
    size_t regionCount;
    NXPendingException pendingException;
    uint64_t timerTicks;
    uint64_t heapBase;
    uint64_t heapSize;
    bool processExited;
} Machine;

typedef struct {
    Machine machine;
    CPU cpu;
    bool initialized;
} NXEngine;

enum {
    NX_MEMORY_READ = 1u,
    NX_MEMORY_WRITE = 2u,
    NX_MEMORY_EXECUTE = 4u
};

bool NXEngineInit(NXEngine *engine);
void NXEngineDestroy(NXEngine *engine);
bool NXEngineMapMemory(NXEngine *engine, uint64_t guestBase, size_t size,
                       unsigned permissions);
bool NXEngineLoadBytes(NXEngine *engine, uint64_t guestAddress,
                       const void *bytes, size_t length);
NXCPUResult NXEngineRun(NXEngine *engine, uint64_t instructionBudget);
void NXEngineClearException(NXEngine *engine);

#endif
