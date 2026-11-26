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

    loadedImage->loadAddress = mappedAddress;
    loadedImage->entryPoint = entryPoint;
    loadedImage->imageSize = mappedSize64;
    loadedImage->textSize = segments[0].memorySize;
    loadedImage->rodataSize = segments[1].memorySize;
    loadedImage->dataSize = segments[2].memorySize;
    loadedImage->bssSize = bssSize;
    engine->cpu.pc = entryPoint;
    return true;
}
