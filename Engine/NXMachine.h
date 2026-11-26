#ifndef NEXILIUM_MACHINE_H
#define NEXILIUM_MACHINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "Arm64/cpu.h"
#include "NXCPU.h"
#include "NXVFS.h"

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

typedef enum {
    NX_SERVICE_NONE = 0,
    NX_SERVICE_MANAGER,
    NX_SERVICE_FSP,
    NX_SERVICE_SETTINGS,
    NX_SERVICE_PROCESS_MANAGER,
    NX_SERVICE_APPLET,
    NX_SERVICE_VIDEO,
    NX_SERVICE_HID,
    NX_SERVICE_TIME,
    NX_SERVICE_NVDRV,
    NX_SERVICE_CONTENT_MANAGER,
    NX_SERVICE_LOADER
} NXServiceKind;

typedef struct {
    uint32_t handle;
    NXServiceKind service;
    bool active;
} NXSessionHandle;

#define NX_MAX_SESSION_HANDLES 128
#define NX_TLS_BASE UINT64_C(0x70000000)
#define NX_TLS_SIZE 0x1000u
#define NX_HIPC_TLS_OFFSET 0x80u

typedef struct Machine {
    NXMemoryRegion regions[NX_MAX_MEMORY_REGIONS];
    size_t regionCount;
    NXPendingException pendingException;
    uint64_t timerTicks;
    uint64_t heapBase;
    uint64_t heapSize;
    bool processExited;
    uint64_t tlsBase;
    uint32_t nextHandle;
    NXSessionHandle handles[NX_MAX_SESSION_HANDLES];
    NXVFS vfs;
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
