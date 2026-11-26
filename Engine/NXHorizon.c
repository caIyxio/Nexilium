#include "NXHorizon.h"
#include "Arm64/esr.h"
#include "Arm64/mmu.h"

#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#define NX_RESULT_SUCCESS          UINT32_C(0x00000000)
#define NX_RESULT_INVALID_SIZE     UINT32_C(0x0000CA01)
#define NX_RESULT_OUT_OF_MEMORY    UINT32_C(0x0000D001)
#define NX_RESULT_INVALID_ADDRESS  UINT32_C(0x0000D401)
#define NX_RESULT_NOT_FOUND        UINT32_C(0x0000F601)
#define NX_HEAP_BASE               UINT64_C(0x80000000)
#define NX_HEAP_MAX_SIZE           (UINT64_C(512) * 1024 * 1024)
#define NX_HEAP_ALIGNMENT          UINT64_C(0x200000)

static uint64_t systemTicks(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    const uint64_t frequency = UINT64_C(24000000);
    return (uint64_t)now.tv_sec * frequency +
           ((uint64_t)now.tv_nsec * frequency) / UINT64_C(1000000000);
}

static NXMemoryRegion *heapRegion(Machine *machine) {
    for (size_t i = 0; i < machine->regionCount; ++i) {
        if (machine->regions[i].guestBase == machine->heapBase &&
            machine->heapSize != 0) return &machine->regions[i];
    }
    return NULL;
}

static bool rangeOverlaps(const NXMemoryRegion *region, uint64_t base, uint64_t size) {
    uint64_t regionEnd = region->guestBase + region->size;
    uint64_t end = base + size;
    return base < regionEnd && region->guestBase < end;
}

static uint32_t setHeapSize(NXEngine *engine, uint64_t requested, uint64_t *outBase) {
    Machine *machine = &engine->machine;
    *outBase = 0;

    if (requested > NX_HEAP_MAX_SIZE ||
        (requested != 0 && requested % NX_HEAP_ALIGNMENT != 0)) {
        return NX_RESULT_INVALID_SIZE;
    }

    NXMemoryRegion *current = heapRegion(machine);
    if (requested == machine->heapSize) {
        *outBase = requested ? machine->heapBase : 0;
        return NX_RESULT_SUCCESS;
    }

    if (requested == 0) {
        if (current) {
            size_t index = (size_t)(current - machine->regions);
            free(current->host);
            for (size_t i = index + 1; i < machine->regionCount; ++i)
                machine->regions[i - 1] = machine->regions[i];
            --machine->regionCount;
            memset(&machine->regions[machine->regionCount], 0,
                   sizeof(machine->regions[0]));
        }
        machine->heapSize = 0;
        machine->heapBase = 0;
        return NX_RESULT_SUCCESS;
    }

    if (!current) {
        for (size_t i = 0; i < machine->regionCount; ++i) {
            if (rangeOverlaps(&machine->regions[i], NX_HEAP_BASE, requested))
                return NX_RESULT_INVALID_ADDRESS;
        }
        if (machine->regionCount >= NX_MAX_MEMORY_REGIONS)
            return NX_RESULT_OUT_OF_MEMORY;
        uint8_t *storage = (uint8_t *)calloc(1, (size_t)requested);
        if (!storage) return NX_RESULT_OUT_OF_MEMORY;
        NXMemoryRegion *region = &machine->regions[machine->regionCount++];
        region->guestBase = NX_HEAP_BASE;
        region->size = (size_t)requested;
        region->host = storage;
        region->permissions = NX_MEMORY_READ | NX_MEMORY_WRITE;
        region->ownsStorage = true;
        machine->heapBase = NX_HEAP_BASE;
        machine->heapSize = requested;
        *outBase = machine->heapBase;
        return NX_RESULT_SUCCESS;
    }

    size_t oldSize = current->size;
    uint8_t *resized = (uint8_t *)realloc(current->host, (size_t)requested);
    if (!resized) return NX_RESULT_OUT_OF_MEMORY;
    if (requested > oldSize)
        memset(resized + oldSize, 0, (size_t)requested - oldSize);
    current->host = resized;
    current->size = (size_t)requested;
    machine->heapSize = requested;
    *outBase = machine->heapBase;
    return NX_RESULT_SUCCESS;
}


static uint32_t readU32LE(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint64_t readU64LE(const uint8_t *bytes) {
    return (uint64_t)readU32LE(bytes) |
           ((uint64_t)readU32LE(bytes + 4) << 32);
}

static void writeU32LE(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static NXSessionHandle *findSession(Machine *machine, uint32_t handle) {
    if (!machine || handle == 0) return NULL;
    for (size_t i = 0; i < NX_MAX_SESSION_HANDLES; ++i) {
        NXSessionHandle *entry = &machine->handles[i];
        if (entry->active && entry->handle == handle) return entry;
    }
    return NULL;
}

static uint32_t createSession(Machine *machine, NXServiceKind service) {
    if (!machine || service == NX_SERVICE_NONE) return 0;
    for (size_t i = 0; i < NX_MAX_SESSION_HANDLES; ++i) {
        NXSessionHandle *entry = &machine->handles[i];
        if (entry->active) continue;

        uint32_t handle = machine->nextHandle++;
        if (handle == 0) handle = machine->nextHandle++;
        entry->handle = handle;
        entry->service = service;
        entry->active = true;
        return handle;
    }
    return 0;
}

static bool readGuestCString(CPU *cpu, uint64_t address,
                             char *destination, size_t capacity) {
    if (!destination || capacity < 2) return false;
    for (size_t i = 0; i < capacity; ++i) {
        uint64_t byte = 0;
        if (!mem_read(cpu, address + i, 1, &byte)) return false;
        destination[i] = (char)byte;
        if (destination[i] == '\0') return true;
    }
    destination[capacity - 1] = '\0';
    return false;
}

uint32_t NXHorizonConnectToNamedPort(NXEngine *engine, uint64_t nameAddress,
                                     uint32_t *outHandle) {
    if (!engine || !engine->initialized || !outHandle)
        return NX_RESULT_INVALID_ADDRESS;
    *outHandle = 0;

    char name[16] = {0};
    if (!readGuestCString(&engine->cpu, nameAddress, name, sizeof(name)))
        return NX_RESULT_INVALID_ADDRESS;

    if (strcmp(name, "sm:") != 0) return NX_RESULT_NOT_FOUND;

    uint32_t handle = createSession(&engine->machine, NX_SERVICE_MANAGER);
    if (handle == 0) return NX_RESULT_OUT_OF_MEMORY;
    *outHandle = handle;
    return NX_RESULT_SUCCESS;
}

void NXHorizonCloseHandle(NXEngine *engine, uint32_t handle) {
    if (!engine || !engine->initialized) return;
    NXSessionHandle *session = findSession(&engine->machine, handle);
    if (session) memset(session, 0, sizeof(*session));
}

static bool hipcRawDataOffset(const uint8_t *buffer, size_t *outOffset,
                              size_t *outRawSize) {
    if (!buffer || !outOffset || !outRawSize) return false;
    const uint32_t header0 = readU32LE(buffer);
    const uint32_t header1 = readU32LE(buffer + 4);

    const size_t pointerCount = (header0 >> 16) & 0xF;
    const size_t sendCount = (header0 >> 20) & 0xF;
    const size_t receiveCount = (header0 >> 24) & 0xF;
    const size_t exchangeCount = (header0 >> 28) & 0xF;
    const size_t rawWords = header1 & 0x3FF;
    const size_t receiveListCount = (header1 >> 10) & 0xF;

    size_t offset = 8;
    if ((header1 >> 31) != 0) {
        uint32_t special = readU32LE(buffer + offset);
        offset += 4;
        if ((special & 1u) != 0) offset += 8; /* Process ID. */
        size_t copyCount = (special >> 1) & 0xF;
        size_t moveCount = (special >> 5) & 0xF;
        offset += (copyCount + moveCount) * 4;
    }

    if (offset > 0x100) return false;
    offset += pointerCount * 8;
    offset += (sendCount + receiveCount + exchangeCount) * 12;
    if (offset > 0x100) return false;

    /* Receive-list descriptors are located at a separate offset. The
       service-manager requests we accept do not use these descriptors. */
    if (receiveListCount != 0) return false;

    offset = (offset + 0xF) & ~(size_t)0xF;
    if (offset > 0x100 || rawWords > (0x100 - offset) / 4) return false;

    *outOffset = offset;
    *outRawSize = rawWords * 4;
    return true;
}

static void writeCmifResponse(uint8_t *buffer, uint32_t result,
                              uint32_t movedHandle) {
    memset(buffer, 0, 0x100);

    writeU32LE(buffer, 0); /* HIPC response tag. */
    writeU32LE(buffer + 4, 4u | (movedHandle ? UINT32_C(0x80000000) : 0));

    if (movedHandle != 0) {
        writeU32LE(buffer + 8, 1u << 5); /* One moved handle. */
        writeU32LE(buffer + 12, movedHandle);
    }

    uint8_t *raw = buffer + 0x10;
    writeU32LE(raw, UINT32_C(0x4F434653)); /* "SFCO". */
    raw[4] = 0;
    raw[5] = 0;
    raw[6] = 0;
    raw[7] = 0;
    writeU32LE(raw + 8, result);
}

uint32_t NXHorizonSendSyncRequest(NXEngine *engine, uint32_t handle) {
    if (!engine || !engine->initialized) return NX_RESULT_INVALID_ADDRESS;

    NXSessionHandle *session = findSession(&engine->machine, handle);
    if (!session) return NX_RESULT_NOT_FOUND;

    uint64_t commandAddress = engine->cpu.tpidrro_el0 + NX_HIPC_TLS_OFFSET;
    uint8_t *buffer = (uint8_t *)mem_host_ptr(
        &engine->cpu, commandAddress, 0x100, ACC_READ | ACC_WRITE
    );
    if (!buffer) return NX_RESULT_INVALID_ADDRESS;

    size_t rawOffset = 0;
    size_t rawSize = 0;
    if (!hipcRawDataOffset(buffer, &rawOffset, &rawSize) || rawSize < 16)
        return NX_RESULT_INVALID_SIZE;

    const uint8_t *raw = buffer + rawOffset;
    if (readU32LE(raw) != UINT32_C(0x49434653)) { /* "SFCI". */
        return NX_RESULT_INVALID_SIZE;
    }

    const uint32_t methodID = readU32LE(raw + 8);
    uint32_t result = NX_RESULT_NOT_FOUND;
    uint32_t movedHandle = 0;

    if (session->service == NX_SERVICE_MANAGER) {
        switch (methodID) {
            case 0: /* RegisterClient; caller PID is carried in the HIPC special header. */
                result = NX_RESULT_SUCCESS;
                break;
            case 1: /* GetServiceHandle: only services with HLE implementations are exposed. */
                result = NX_RESULT_NOT_FOUND;
                if (rawSize >= 24) {
                    const uint64_t encodedName = readU64LE(raw + 16);
                    char serviceName[9] = {0};
                    for (size_t i = 0; i < 8; ++i) {
                        serviceName[i] = (char)((encodedName >> (i * 8)) & 0xFF);
                    }
                    (void)serviceName;
                }
                break;
            default:
                result = NX_RESULT_NOT_FOUND;
                break;
        }
    }

    writeCmifResponse(buffer, result, movedHandle);
    return result;
}

bool NXHorizonDispatchSVC(NXEngine *engine, uint16_t serviceID) {
    if (!engine || !engine->initialized) return false;
    CPU *cpu = &engine->cpu;
    Machine *machine = &engine->machine;

    switch (serviceID) {
        case 0x01: { /* SetHeapSize */
            uint64_t address = 0;
            uint32_t result = setHeapSize(engine, cpu->x[1], &address);
            cpu->x[0] = result;
            cpu->x[1] = address;
            return true;
        }

        case 0x07: /* ExitProcess */
            machine->processExited = true;
            cpu->stop = true;
            return true;

        case 0x0A: /* ExitThread */
            cpu->stop = true;
            return true;

        case 0x0B: { /* SleepThread */
            int64_t nanoseconds = (int64_t)cpu->x[0];
            if (nanoseconds > 0) {
                struct timespec request = {
                    .tv_sec = (time_t)(nanoseconds / INT64_C(1000000000)),
                    .tv_nsec = (long)(nanoseconds % INT64_C(1000000000))
                };
                while (nanosleep(&request, &request) != 0 && errno == EINTR) { }
            }
            cpu->x[0] = NX_RESULT_SUCCESS;
            return true;
        }

        case 0x10: /* GetCurrentProcessorNumber */
            cpu->x[0] = 0; /* Nexilium currently schedules one guest core. */
            return true;

        case 0x16: /* CloseHandle */
            NXHorizonCloseHandle(engine, (uint32_t)cpu->x[0]);
            cpu->x[0] = NX_RESULT_SUCCESS;
            return true;

        case 0x1E: /* GetSystemTick */
            cpu->x[0] = systemTicks();
            return true;

        case 0x1F: { /* ConnectToNamedPort: name in X1, result/handle in W0/W1. */
            uint32_t handle = 0;
            uint32_t result = NXHorizonConnectToNamedPort(
                engine, cpu->x[1], &handle
            );
            cpu->x[0] = result;
            cpu->x[1] = handle;
            return true;
        }

        case 0x21: /* SendSyncRequest: session handle in W0. */
            cpu->x[0] = NXHorizonSendSyncRequest(engine, (uint32_t)cpu->x[0]);
            return true;

        case 0x27: { /* OutputDebugString */
            const uint64_t address = cpu->x[0];
            uint64_t length = cpu->x[1];
            if (length > 0x1000) length = 0x1000;
            char text[0x1001];
            for (uint64_t i = 0; i < length; ++i) {
                uint64_t byte = 0;
                if (!mem_read(cpu, address + i, 1, &byte)) {
                    cpu->x[0] = NX_RESULT_INVALID_ADDRESS;
                    return true;
                }
                text[i] = (char)byte;
            }
            text[length] = '\0';
            fprintf(stderr, "[Horizon] %s", text);
            cpu->x[0] = NX_RESULT_SUCCESS;
            return true;
        }

        case 0x29: { /* GetInfo */
            const uint32_t infoType = (uint32_t)cpu->x[1];
            uint64_t value = 0;
            switch (infoType) {
                case 0: value = 1; break; /* CoreMask: core 0 is online. */
                case 1: value = UINT64_C(0x3FFFFFFFFFFFFFFF); break; /* PriorityMask */
                case 4: value = NX_HEAP_BASE; break; /* HeapRegionAddress */
                case 5: value = NX_HEAP_MAX_SIZE; break; /* HeapRegionSize */
                case 6: value = UINT64_C(0x100000000); break; /* TotalMemorySize */
                case 7: value = machine->heapSize; break; /* UsedMemorySize (heap only) */
                case 8: value = 0; break; /* DebuggerAttached */
                case 12: value = UINT64_C(0x100000000); break; /* ASLR region base */
                case 13: value = UINT64_C(0x100000000); break; /* ASLR region size */
                case 14: value = UINT64_C(0x70000000); break; /* Stack region base */
                case 15: value = UINT64_C(0x08000000); break; /* Stack region size */
                default:
                    cpu->x[0] = NX_RESULT_NOT_FOUND;
                    cpu->x[1] = 0;
                    return true;
            }
            cpu->x[0] = NX_RESULT_SUCCESS;
            cpu->x[1] = value;
            return true;
        }

        case 0x26: /* Break: log the guest reason; continue under the debugger-less runtime. */
            fprintf(stderr, "[Horizon] guest break reason=%" PRIu64 " info=%" PRIu64 "\n",
                    cpu->x[0], cpu->x[2]);
            cpu->x[0] = NX_RESULT_SUCCESS;
            return true;

        default:
            return false;
    }
}
