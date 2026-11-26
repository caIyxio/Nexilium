#include "NXHorizon.h"
#include "Arm64/esr.h"
#include "Arm64/mmu.h"

#include <inttypes.h>
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

static bool readGuestString(CPU *cpu, uint64_t address, char *buffer, size_t capacity) {
    if (!buffer || capacity == 0) return false;
    for (size_t i = 0; i < capacity; ++i) {
        uint64_t byte = 0;
        if (!mem_read(cpu, address + i, 1, &byte)) return false;
        buffer[i] = (char)byte;
        if (buffer[i] == '\0') return true;
    }
    buffer[capacity - 1] = '\0';
    return false;
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
                while (nanosleep(&request, &request) != 0) { }
            }
            cpu->x[0] = NX_RESULT_SUCCESS;
            return true;
        }

        case 0x10: /* GetCurrentProcessorNumber */
            cpu->x[0] = 0; /* Nexilium currently schedules one guest core. */
            return true;

        case 0x1E: /* GetSystemTick */
            cpu->x[0] = systemTicks();
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
