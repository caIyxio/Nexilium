#include "NXNSO.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define NX_NSO_HEADER_SIZE 0x100u
#define NX_NSO_SIGNATURE UINT32_C(0x304F534E)
#define NX_NSO_MAX_IMAGE_SIZE (256u * 1024u * 1024u)

typedef struct {
    uint32_t fileOffset;
    uint32_t memoryOffset;
    uint32_t memorySize;
    uint32_t fileSize;
    bool compressed;
} NXNSOSegment;

static uint32_t readU32LE(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static bool addOverflowU64(uint64_t a, uint64_t b, uint64_t *result) {
    if (a > UINT64_MAX - b) return true;
    *result = a + b;
    return false;
}

/* Raw LZ4 block decoder used by NSO segments; no frame wrapper is expected. */
static bool decodeLZ4Block(const uint8_t *source, size_t sourceSize,
                           uint8_t *destination, size_t destinationSize) {
    if ((!source && sourceSize) || (!destination && destinationSize)) return false;
    size_t input = 0;
    size_t output = 0;

    while (input < sourceSize) {
        uint8_t token = source[input++];
        size_t literalLength = (size_t)(token >> 4);
        if (literalLength == 15) {
            uint8_t extension;
            do {
                if (input >= sourceSize) return false;
                extension = source[input++];
                if (literalLength > SIZE_MAX - extension) return false;
                literalLength += extension;
            } while (extension == 255);
        }

        if (literalLength > sourceSize - input ||
            literalLength > destinationSize - output) return false;
        memcpy(destination + output, source + input, literalLength);
        input += literalLength;
        output += literalLength;

        if (input == sourceSize) break;
        if (sourceSize - input < 2) return false;

        size_t distance = (size_t)source[input] |
                          ((size_t)source[input + 1] << 8);
        input += 2;
        if (distance == 0 || distance > output) return false;

        size_t matchLength = (size_t)(token & 0x0F) + 4;
        if ((token & 0x0F) == 15) {
            uint8_t extension;
            do {
                if (input >= sourceSize) return false;
                extension = source[input++];
                if (matchLength > SIZE_MAX - extension) return false;
                matchLength += extension;
            } while (extension == 255);
        }

        if (matchLength > destinationSize - output) return false;
        for (size_t i = 0; i < matchLength; ++i) {
            destination[output + i] = destination[output + i - distance];
        }
        output += matchLength;
    }

    return input == sourceSize && output == destinationSize;
}

static uint64_t readU64LE(const uint8_t *bytes) {
    return (uint64_t)readU32LE(bytes) |
           ((uint64_t)readU32LE(bytes + 4) << 32);
}

static bool addSignedOffset(uint64_t base, int64_t offset,
                            uint64_t *result) {
    if (!result) return false;
    if (offset >= 0)
        return !addOverflowU64(base, (uint64_t)offset, result);
    const uint64_t magnitude = (uint64_t)(-(offset + 1)) + 1u;
    if (base < magnitude) return false;
    *result = base - magnitude;
    return true;
}

static bool addressInImage(const NXLoadedNSO *image,
                           uint64_t address, size_t size) {
    if (!image || address < image->loadAddress ||
        address > UINT64_MAX - size)
        return false;
    const uint64_t offset = address - image->loadAddress;
    return offset <= image->imageSize &&
           (uint64_t)size <= image->imageSize - offset;
}

static bool readGuestU64(NXEngine *engine, uint64_t address,
                         uint64_t *value) {
    if (!engine || !value) return false;
    const uint8_t *bytes = (const uint8_t *)mem_host_ptr(
        &engine->cpu, address, sizeof(uint64_t), ACC_READ
    );
    if (!bytes) return false;
    *value = readU64LE(bytes);
    return true;
}

static bool writeGuestU64(NXEngine *engine, uint64_t address,
                          uint64_t value) {
    if (!engine) return false;
    uint8_t *bytes = (uint8_t *)mem_host_ptr(
        &engine->cpu, address, sizeof(uint64_t), ACC_WRITE
    );
    if (!bytes) return false;
    for (size_t i = 0; i < 8; ++i)
        bytes[i] = (uint8_t)(value >> (i * 8));
    return true;
}

static bool applyRelaTable(NXEngine *engine, uint64_t loadBase,
                           const NXLoadedNSO *image,
                           uint64_t tableAddress, uint64_t tableSize,
                           NXLoadedNSO *stats) {
    if (tableSize == 0) return true;
    if (tableSize % 24u != 0 || tableSize > SIZE_MAX ||
        !addressInImage(image, tableAddress, (size_t)tableSize))
        return false;

    const uint64_t count = tableSize / 24u;
    for (uint64_t i = 0; i < count; ++i) {
        const uint64_t entryAddress = tableAddress + i * 24u;
        const uint8_t *entry = (const uint8_t *)mem_host_ptr(
            &engine->cpu, entryAddress, 24, ACC_READ
        );
        if (!entry) return false;

        const uint64_t targetOffset = readU64LE(entry);
        const uint64_t info = readU64LE(entry + 8);
        const int64_t addend = (int64_t)readU64LE(entry + 16);
        const uint32_t type = (uint32_t)info;
        const uint32_t symbolIndex = (uint32_t)(info >> 32);

        if (type == 0) continue; /* R_AARCH64_NONE */
        if (type != 1027u || symbolIndex != 0) {
            ++stats->unsupportedRelocationCount;
            continue;
        }

        uint64_t targetAddress = 0;
        uint64_t relocatedValue = 0;
        if (addOverflowU64(loadBase, targetOffset, &targetAddress) ||
            !addressInImage(image, targetAddress, sizeof(uint64_t)) ||
            !addSignedOffset(loadBase, addend, &relocatedValue) ||
            !writeGuestU64(engine, targetAddress, relocatedValue))
            return false;
        ++stats->relativeRelocationCount;
    }
    return true;
}

static bool applyRelTable(NXEngine *engine, uint64_t loadBase,
                          const NXLoadedNSO *image,
                          uint64_t tableAddress, uint64_t tableSize,
                          NXLoadedNSO *stats) {
    if (tableSize == 0) return true;
    if (tableSize % 16u != 0 || tableSize > SIZE_MAX ||
        !addressInImage(image, tableAddress, (size_t)tableSize))
        return false;

    const uint64_t count = tableSize / 16u;
    for (uint64_t i = 0; i < count; ++i) {
        const uint64_t entryAddress = tableAddress + i * 16u;
        const uint8_t *entry = (const uint8_t *)mem_host_ptr(
            &engine->cpu, entryAddress, 16, ACC_READ
        );
        if (!entry) return false;

        const uint64_t targetOffset = readU64LE(entry);
        const uint64_t info = readU64LE(entry + 8);
        const uint32_t type = (uint32_t)info;
        const uint32_t symbolIndex = (uint32_t)(info >> 32);
        if (type == 0) continue;
        if (type != 1027u || symbolIndex != 0) {
            ++stats->unsupportedRelocationCount;
            continue;
        }

        uint64_t targetAddress = 0;
        uint64_t implicitAddend = 0;
        uint64_t relocatedValue = 0;
        if (addOverflowU64(loadBase, targetOffset, &targetAddress) ||
            !addressInImage(image, targetAddress, sizeof(uint64_t)) ||
            !readGuestU64(engine, targetAddress, &implicitAddend) ||
            addOverflowU64(loadBase, implicitAddend, &relocatedValue) ||
            !writeGuestU64(engine, targetAddress, relocatedValue))
            return false;
        ++stats->relativeRelocationCount;
    }
    return true;
}

/*
 * The MOD0 pointer is stored at +4 in the loaded text image. This pass applies
 * only symbol-free R_AARCH64_RELATIVE relocations; rtld-style symbol lookup
 * (ABS64/GLOB_DAT/JUMP_SLOT) remains a separate, unimplemented stage.
 */
static bool applyMOD0RelativeRelocations(NXEngine *engine,
                                         uint64_t loadBase,
                                         uint64_t textAddress,
                                         uint32_t textSize,
                                         NXLoadedNSO *image) {
    if (!engine || !image || textSize < 8u) return true;

    uint64_t pointerAddress = 0;
    if (addOverflowU64(textAddress, 4u, &pointerAddress)) return false;
    const uint8_t *pointerBytes = (const uint8_t *)mem_host_ptr(
        &engine->cpu, pointerAddress, 4, ACC_READ
    );
    if (!pointerBytes) return false;

    const uint32_t mod0Offset = readU32LE(pointerBytes);
    if (mod0Offset == 0 || mod0Offset > textSize ||
        textSize - mod0Offset < 0x1Cu)
        return true;

    uint64_t mod0Address = 0;
    if (addOverflowU64(textAddress, mod0Offset, &mod0Address))
        return false;
    const uint8_t *mod0 = (const uint8_t *)mem_host_ptr(
        &engine->cpu, mod0Address, 0x1Cu, ACC_READ
    );
    if (!mod0 || readU32LE(mod0) != UINT32_C(0x30444F4D))
        return true;

    image->hasModuleHeader = true;
    image->moduleHeaderAddress = mod0Address;
    const int32_t dynamicOffset = (int32_t)readU32LE(mod0 + 4);
    uint64_t dynamicAddress = 0;
    if (!addSignedOffset(mod0Address, dynamicOffset, &dynamicAddress) ||
        !addressInImage(image, dynamicAddress, 16))
        return false;
    image->dynamicAddress = dynamicAddress;

    uint64_t relaOffset = 0, relaSize = 0, relaEntrySize = 0;
    uint64_t relOffset = 0, relSize = 0, relEntrySize = 0;
    const uint64_t maxEntries = image->imageSize / 16u;
    bool terminated = false;

    for (uint64_t i = 0; i < maxEntries; ++i) {
        uint64_t entryAddress = 0;
        if (addOverflowU64(dynamicAddress, i * 16u, &entryAddress) ||
            !addressInImage(image, entryAddress, 16))
            return false;
        const uint8_t *entry = (const uint8_t *)mem_host_ptr(
            &engine->cpu, entryAddress, 16, ACC_READ
        );
        if (!entry) return false;

        const int64_t tag = (int64_t)readU64LE(entry);
        const uint64_t value = readU64LE(entry + 8);
        if (tag == 0) {
            terminated = true;
            break;
        }
        switch (tag) {
            case 7:  relaOffset = value; break; /* DT_RELA */
            case 8:  relaSize = value; break;   /* DT_RELASZ */
            case 9:  relaEntrySize = value; break; /* DT_RELAENT */
            case 17: relOffset = value; break;  /* DT_REL */
            case 18: relSize = value; break;    /* DT_RELSZ */
            case 19: relEntrySize = value; break; /* DT_RELENT */
            default: break;
        }
    }
    if (!terminated) return false;

    if (relaSize != 0) {
        uint64_t tableAddress = 0;
        if (relaEntrySize != 24u ||
            addOverflowU64(loadBase, relaOffset, &tableAddress) ||
            !applyRelaTable(engine, loadBase, image,
                            tableAddress, relaSize, image))
            return false;
    }
    if (relSize != 0) {
        uint64_t tableAddress = 0;
        if (relEntrySize != 16u ||
            addOverflowU64(loadBase, relOffset, &tableAddress) ||
            !applyRelTable(engine, loadBase, image,
                           tableAddress, relSize, image))
            return false;
    }
    return true;
}

static bool loadSegment(NXEngine *engine, const uint8_t *fileBytes,
                        size_t fileSize, const NXNSOSegment *segment,
                        uint64_t loadBase) {
    if (segment->memorySize == 0) return true;
    if (segment->fileSize == 0 ||
        segment->fileOffset > fileSize ||
        segment->fileSize > fileSize - segment->fileOffset) return false;

    uint64_t destinationAddress = 0;
    if (addOverflowU64(loadBase, segment->memoryOffset, &destinationAddress))
        return false;

    uint8_t *output = (uint8_t *)calloc(1, segment->memorySize);
    if (!output) return false;

    const uint8_t *input = fileBytes + segment->fileOffset;
    bool ok;
    if (segment->compressed) {
        ok = decodeLZ4Block(input, segment->fileSize,
                            output, segment->memorySize);
    } else {
        ok = segment->fileSize == segment->memorySize;
        if (ok) memcpy(output, input, segment->memorySize);
    }

    if (ok) {
        ok = NXEngineLoadBytes(engine, destinationAddress,
                               output, segment->memorySize);
    }
    free(output);
    return ok;
}

bool NXEngineLoadNSO(NXEngine *engine, const uint8_t *fileBytes,
                     size_t fileSize, uint64_t loadBase,
                     NXLoadedNSO *loadedImage) {
    if (!engine || !engine->initialized || !fileBytes ||
        fileSize < NX_NSO_HEADER_SIZE || !loadedImage) return false;
    if (readU32LE(fileBytes) != NX_NSO_SIGNATURE) return false;

    const uint32_t flags = readU32LE(fileBytes + 0x0C);
    NXNSOSegment segments[3] = {
        {
            .fileOffset = readU32LE(fileBytes + 0x10),
            .memoryOffset = readU32LE(fileBytes + 0x14),
            .memorySize = readU32LE(fileBytes + 0x18),
            .fileSize = (flags & 1u) ? readU32LE(fileBytes + 0x60) : readU32LE(fileBytes + 0x18),
            .compressed = (flags & 1u) != 0
        },
        {
            .fileOffset = readU32LE(fileBytes + 0x20),
            .memoryOffset = readU32LE(fileBytes + 0x24),
            .memorySize = readU32LE(fileBytes + 0x28),
            .fileSize = (flags & 2u) ? readU32LE(fileBytes + 0x64) : readU32LE(fileBytes + 0x28),
            .compressed = (flags & 2u) != 0
        },
        {
            .fileOffset = readU32LE(fileBytes + 0x30),
            .memoryOffset = readU32LE(fileBytes + 0x34),
            .memorySize = readU32LE(fileBytes + 0x38),
            .fileSize = (flags & 4u) ? readU32LE(fileBytes + 0x68) : readU32LE(fileBytes + 0x38),
            .compressed = (flags & 4u) != 0
        }
    };
    const uint32_t bssSize = readU32LE(fileBytes + 0x3C);

    if (segments[0].memorySize == 0) return false;
    uint64_t minimumOffset = UINT64_MAX;
    uint64_t maximumOffset = 0;

    for (size_t i = 0; i < 3; ++i) {
        const NXNSOSegment *segment = &segments[i];
        if (!segment->memorySize) continue;

        uint64_t end = 0;
        if (addOverflowU64(segment->memoryOffset, segment->memorySize, &end))
            return false;
        if (segment->fileOffset > fileSize ||
            segment->fileSize > fileSize - segment->fileOffset) return false;
        if (segment->memoryOffset < minimumOffset) minimumOffset = segment->memoryOffset;
        if (end > maximumOffset) maximumOffset = end;
    }

    uint64_t bssEnd = 0;
    if (addOverflowU64(maximumOffset, bssSize, &bssEnd)) return false;
    if (bssEnd > NX_NSO_MAX_IMAGE_SIZE || minimumOffset >= bssEnd) return false;

    uint64_t mappedSize64 = bssEnd - minimumOffset;
    if (mappedSize64 > NX_NSO_MAX_IMAGE_SIZE || mappedSize64 > SIZE_MAX)
        return false;

    uint64_t mappedAddress = 0;
    if (addOverflowU64(loadBase, minimumOffset, &mappedAddress)) return false;

    if (!NXEngineMapMemory(engine, mappedAddress, (size_t)mappedSize64,
                           NX_MEMORY_READ | NX_MEMORY_WRITE | NX_MEMORY_EXECUTE)) {
        return false;
    }

    for (size_t i = 0; i < 3; ++i) {
        if (!loadSegment(engine, fileBytes, fileSize, &segments[i], loadBase))
            return false;
    }

    uint64_t entryPoint = 0;
    if (addOverflowU64(loadBase, segments[0].memoryOffset, &entryPoint))
        return false;

    memset(loadedImage, 0, sizeof(*loadedImage));
    loadedImage->loadAddress = mappedAddress;
    loadedImage->entryPoint = entryPoint;
    loadedImage->imageSize = mappedSize64;
    loadedImage->textSize = segments[0].memorySize;
    loadedImage->rodataSize = segments[1].memorySize;
    loadedImage->dataSize = segments[2].memorySize;
    loadedImage->bssSize = bssSize;

    if (!applyMOD0RelativeRelocations(
            engine, loadBase, entryPoint, segments[0].memorySize, loadedImage))
        return false;

    engine->cpu.pc = entryPoint;
    return true;
}
