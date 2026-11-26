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

static void writeU64LE(uint8_t *bytes, uint64_t value) {
    writeU32LE(bytes, (uint32_t)value);
    writeU32LE(bytes + 4, (uint32_t)(value >> 32));
}

static NXSessionHandle *findSession(Machine *machine, uint32_t handle) {
    if (!machine || handle == 0) return NULL;
    for (size_t i = 0; i < NX_MAX_SESSION_HANDLES; ++i) {
        NXSessionHandle *entry = &machine->handles[i];
        if (entry->active && entry->handle == handle) return entry;
    }
    return NULL;
}

static NXServiceKind serviceFromName(const char *name) {
    if (!name) return NX_SERVICE_NONE;
    if (strcmp(name, "fsp-srv") == 0) return NX_SERVICE_FSP;
    if (strcmp(name, "set:sys") == 0) return NX_SERVICE_SETTINGS;
    if (strcmp(name, "pm:shell") == 0 || strcmp(name, "pm:dmnt") == 0)
        return NX_SERVICE_PROCESS_MANAGER;
    if (strcmp(name, "appletOE") == 0) return NX_SERVICE_APPLET;
    if (strcmp(name, "vi:u") == 0) return NX_SERVICE_VIDEO;
    if (strcmp(name, "hid") == 0) return NX_SERVICE_HID;
    if (strcmp(name, "time:u") == 0) return NX_SERVICE_TIME;
    if (strcmp(name, "nvdrv") == 0) return NX_SERVICE_NVDRV;
    if (strcmp(name, "ncm") == 0) return NX_SERVICE_CONTENT_MANAGER;
    if (strcmp(name, "ldr:ro") == 0) return NX_SERVICE_LOADER;
    return NX_SERVICE_NONE;
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

static uint32_t createFilesystemSession(Machine *machine,
                                            const char *mountName) {
    if (!machine || !mountName) return 0;
    bool mounted = false;
    for (size_t i = 0; i < NX_VFS_MAX_MOUNTS; ++i) {
        const NXVFSMount *mount = &machine->vfs.mounts[i];
        if (mount->active && strcmp(mount->name, mountName) == 0) {
            mounted = true;
            break;
        }
    }
    if (!mounted) return 0;

    uint32_t handle = createSession(machine, NX_SERVICE_FILESYSTEM);
    NXSessionHandle *session = findSession(machine, handle);
    if (!session) return 0;
    size_t length = strlen(mountName);
    if (length > NX_VFS_MOUNT_NAME_MAX) {
        memset(session, 0, sizeof(*session));
        return 0;
    }
    memcpy(session->mountName, mountName, length + 1u);
    return handle;
}

static uint32_t createFileSession(Machine *machine, const NXVFSFile *file) {
    if (!machine || !file || !file->bytes) return 0;
    uint32_t handle = createSession(machine, NX_SERVICE_FILE);
    NXSessionHandle *session = findSession(machine, handle);
    if (!session) return 0;
    session->file = *file;
    return handle;
}

static uint32_t createDirectorySession(Machine *machine,
                                             const char *mountName,
                                             const char *path,
                                             uint64_t filterFlags) {
    if (!machine || !mountName || !path || (filterFlags & ~UINT64_C(3)) != 0)
        return 0;
    bool mounted = false;
    for (size_t i = 0; i < NX_VFS_MAX_MOUNTS; ++i) {
        const NXVFSMount *mount = &machine->vfs.mounts[i];
        if (mount->active && strcmp(mount->name, mountName) == 0) {
            mounted = true;
            break;
        }
    }
    if (!mounted) return 0;

    uint32_t handle = createSession(machine, NX_SERVICE_DIRECTORY);
    NXSessionHandle *session = findSession(machine, handle);
    if (!session) return 0;
    const size_t pathLength = strlen(path);
    if (pathLength >= sizeof(session->directoryPath)) {
        memset(session, 0, sizeof(*session));
        return 0;
    }
    size_t mountLength = strlen(mountName);
    if (mountLength > NX_VFS_MOUNT_NAME_MAX) {
        memset(session, 0, sizeof(*session));
        return 0;
    }
    memcpy(session->mountName, mountName, mountLength + 1u);
    memcpy(session->directoryPath, path, pathLength + 1u);
    session->directoryFilterFlags = filterFlags;
    session->directoryIndex = 0;
    return handle;
}

static bool nextFilteredDirectoryItem(Machine *machine,
                                     NXSessionHandle *session,
                                     NXVFSDirectoryItem *item) {
    if (!machine || !session || !item) return false;
    for (;;) {
        NXVFSDirectoryItem candidate;
        if (!NXVFSReadDirectory(&machine->vfs, session->mountName,
                                session->directoryPath,
                                session->directoryIndex, &candidate))
            return false;
        ++session->directoryIndex;
        const bool selected =
            (candidate.isDirectory && (session->directoryFilterFlags & 1u)) ||
            (!candidate.isDirectory && (session->directoryFilterFlags & 2u));
        if (selected) {
            *item = candidate;
            return true;
        }
    }
}

static uint64_t countFilteredDirectoryItems(const Machine *machine,
                                            const NXSessionHandle *session) {
    if (!machine || !session) return 0;
    uint64_t count = 0;
    for (size_t index = 0; ; ++index) {
        NXVFSDirectoryItem candidate;
        if (!NXVFSReadDirectory(&machine->vfs, session->mountName,
                                session->directoryPath, index, &candidate))
            break;
        const bool selected =
            (candidate.isDirectory && (session->directoryFilterFlags & 1u)) ||
            (!candidate.isDirectory && (session->directoryFilterFlags & 2u));
        if (selected) ++count;
    }
    return count;
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

static bool hipcGetBufferDescriptor(const uint8_t *buffer,
                                      uint32_t bufferType, uint32_t index,
                                      uint64_t *address, uint64_t *size) {
    if (!buffer || !address || !size) return false;
    const uint32_t header0 = readU32LE(buffer);
    const uint32_t header1 = readU32LE(buffer + 4);
    const size_t pointerCount = (header0 >> 16) & 0xFu;
    const size_t sendCount = (header0 >> 20) & 0xFu;
    const size_t receiveCount = (header0 >> 24) & 0xFu;
    const size_t exchangeCount = (header0 >> 28) & 0xFu;

    size_t offset = 8;
    if ((header1 >> 31) != 0) {
        const uint32_t special = readU32LE(buffer + offset);
        offset += 4;
        if ((special & 1u) != 0) offset += 8;
        const size_t copyCount = (special >> 1) & 0xFu;
        const size_t moveCount = (special >> 5) & 0xFu;
        offset += (copyCount + moveCount) * 4u;
    }
    if (offset > 0x100) return false;

    const size_t pointerOffset = offset;
    const size_t sendOffset = pointerOffset + pointerCount * 8u;
    const size_t receiveOffset = sendOffset + sendCount * 12u;
    const size_t exchangeOffset = receiveOffset + receiveCount * 12u;
    size_t descriptorOffset = 0;

    if (bufferType == 0x9u || bufferType == 0x19u) {
        /* Both input pointer buffer variants use the 8-byte PointerData descriptor. */
        if (index >= pointerCount) return false;
        descriptorOffset = pointerOffset + (size_t)index * 8u;
        const uint32_t data0 = readU32LE(buffer + descriptorOffset);
        const uint32_t data1 = readU32LE(buffer + descriptorOffset + 4u);
        *size = (data0 >> 16) & 0xFFFFu;
        *address = (uint64_t)data1 |
                   ((uint64_t)((data0 >> 12) & 0xFu) << 32) |
                   ((uint64_t)((data0 >> 6) & 0x7u) << 36);
    } else if (bufferType == 0x6u) {
        if (index >= receiveCount) return false;
        descriptorOffset = receiveOffset + (size_t)index * 12u;
        const uint32_t data0 = readU32LE(buffer + descriptorOffset);
        const uint32_t data1 = readU32LE(buffer + descriptorOffset + 4u);
        const uint32_t data2 = readU32LE(buffer + descriptorOffset + 8u);
        *size = (uint64_t)data0 | ((uint64_t)((data2 >> 24) & 0xFu) << 32);
        *address = (uint64_t)data1 |
                   ((uint64_t)((data2 >> 28) & 0xFu) << 32) |
                   ((uint64_t)((data2 >> 2) & 0x7u) << 36);
    } else {
        (void)exchangeOffset;
        return false;
    }

    return *size != 0 && *address <= UINT64_MAX - *size;
}

static bool readGuestPath(CPU *cpu, uint64_t address, uint64_t size,
                          char *path, size_t capacity) {
    if (!cpu || !path || capacity < 2 || size == 0 ||
        size > capacity || size > SIZE_MAX)
        return false;
    const uint8_t *source = (const uint8_t *)mem_host_ptr(
        cpu, address, (size_t)size, ACC_READ
    );
    if (!source) return false;

    size_t length = 0;
    while (length < (size_t)size && source[length] != 0) ++length;
    if (length == (size_t)size || length >= capacity) return false;
    memcpy(path, source, length);
    path[length] = '\0';
    return true;
}

static void writeCmifResponse(uint8_t *buffer, uint32_t result,
                              uint32_t movedHandle);

static void writeCmifResponseData(uint8_t *buffer, uint32_t result,
                                  uint32_t movedHandle,
                                  const void *data, size_t dataSize) {
    writeCmifResponse(buffer, result, movedHandle);
    if (data && dataSize && dataSize <= 0xE0)
        memcpy(buffer + 0x20, data, dataSize);
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
    uint8_t extraData[16] = {0};
    size_t extraDataSize = 0;

    if (session->service == NX_SERVICE_MANAGER) {
        switch (methodID) {
            case 0: /* RegisterClient; caller PID is carried in the HIPC special header. */
                result = NX_RESULT_SUCCESS;
                break;
            case 1: { /* GetServiceHandle */
                if (rawSize >= 24) {
                    const uint64_t encodedName = readU64LE(raw + 16);
                    char serviceName[9] = {0};
                    for (size_t i = 0; i < 8; ++i) {
                        serviceName[i] = (char)((encodedName >> (i * 8)) & 0xFF);
                    }
                    NXServiceKind service = serviceFromName(serviceName);
                    if (service != NX_SERVICE_NONE) {
                        movedHandle = createSession(&engine->machine, service);
                        result = movedHandle ? NX_RESULT_SUCCESS : NX_RESULT_OUT_OF_MEMORY;
                    }
                }
                break;
            }
            default:
                break;
        }
    } else if (session->service == NX_SERVICE_FSP) {
        if (methodID == 2 && rawSize >= 16) {
            /* OpenDataFileSystemByCurrentProcess -> this process's read-only RomFS. */
            movedHandle = createFilesystemSession(&engine->machine, "romfs");
            result = movedHandle ? NX_RESULT_SUCCESS : NX_RESULT_NOT_FOUND;
        }
    } else if (session->service == NX_SERVICE_FILESYSTEM) {
        if (methodID == 7 && rawSize >= 16) { /* GetEntryType */
            uint64_t pathAddress = 0, pathSize = 0;
            char path[0x302];
            if (hipcGetBufferDescriptor(buffer, 0x9u, 0,
                                        &pathAddress, &pathSize) &&
                readGuestPath(&engine->cpu, pathAddress, pathSize,
                              path, sizeof(path))) {
                NXVFSFile file;
                if (NXVFSOpenFile(&engine->machine.vfs,
                                  session->mountName, path, &file)) {
                    const uint32_t entryType = 1; /* File. */
                    memcpy(extraData, &entryType, sizeof(entryType));
                    extraDataSize = sizeof(entryType);
                    result = NX_RESULT_SUCCESS;
                }
            }
        } else if (methodID == 8 && rawSize >= 20) { /* OpenFile */
            uint64_t pathAddress = 0, pathSize = 0;
            char path[0x302];
            const uint32_t mode = readU32LE(raw + 16);
            if ((mode & 1u) != 0 &&
                (mode & 2u) == 0 &&
                hipcGetBufferDescriptor(buffer, 0x19u, 0,
                                        &pathAddress, &pathSize) &&
                readGuestPath(&engine->cpu, pathAddress, pathSize,
                              path, sizeof(path))) {
                NXVFSFile file;
                if (NXVFSOpenFile(&engine->machine.vfs,
                                  session->mountName, path, &file)) {
                    movedHandle = createFileSession(&engine->machine, &file);
                    result = movedHandle ? NX_RESULT_SUCCESS : NX_RESULT_OUT_OF_MEMORY;
                }
            }
        }
    } else if (session->service == NX_SERVICE_FILE) {
        if (methodID == 4 && rawSize >= 16) { /* GetSize */
            const uint64_t fileSize = session->file.size;
            memcpy(extraData, &fileSize, sizeof(fileSize));
            extraDataSize = sizeof(fileSize);
            result = NX_RESULT_SUCCESS;
        } else if (methodID == 0 && rawSize >= 32) { /* Read */
            uint64_t destinationAddress = 0, destinationSize = 0;
            const uint64_t fileOffset = readU64LE(raw + 16);
            const uint64_t requestedSize = readU64LE(raw + 24);
            if (hipcGetBufferDescriptor(buffer, 0x6u, 0,
                                        &destinationAddress, &destinationSize)) {
                const uint64_t boundedSize =
                    requestedSize < destinationSize ? requestedSize : destinationSize;
                if (boundedSize <= SIZE_MAX) {
                    void *destination = mem_host_ptr(
                        &engine->cpu, destinationAddress, (size_t)boundedSize,
                        ACC_WRITE
                    );
                    if (destination || boundedSize == 0) {
                        size_t bytesRead = 0;
                        if (NXVFSReadFile(&session->file, fileOffset,
                                          destination, (size_t)boundedSize,
                                          &bytesRead)) {
                            const uint64_t outputSize = bytesRead;
                            memcpy(extraData, &outputSize, sizeof(outputSize));
                            extraDataSize = sizeof(outputSize);
                            result = NX_RESULT_SUCCESS;
                        } else {
                            result = NX_RESULT_INVALID_ADDRESS;
                        }
                    } else {
                        result = NX_RESULT_INVALID_ADDRESS;
                    }
                } else {
                    result = NX_RESULT_INVALID_SIZE;
                }
            }
        } else if (methodID == 2 && rawSize >= 16) { /* Flush: read-only files need no action. */
            result = NX_RESULT_SUCCESS;
        }
    }

    if (extraDataSize != 0)
        writeCmifResponseData(buffer, result, movedHandle,
                              extraData, extraDataSize);
    else
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
