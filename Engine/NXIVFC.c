#include "NXIVFC.h"

#include <string.h>

#define NX_IVFC_MAGIC UINT32_C(0x43465649) /* "IVFC" */
#define NX_IVFC_VERSION UINT32_C(0x00020000)
#define NX_IVFC_HEADER_SIZE 0xE0u
#define NX_IVFC_SUPERBLOCK_SIZE 0x138u
#define NX_IVFC_LEVEL_ENTRY_SIZE 0x18u
#define NX_IVFC_MASTER_HASH_SIZE 0x20u

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

static bool toSize(uint64_t value, size_t *out) {
    if (!out || value > (uint64_t)SIZE_MAX) return false;
    *out = (size_t)value;
    return true;
}

static bool rangeFits(size_t offset, size_t size, size_t total) {
    return offset <= total && size <= total - offset;
}

static bool overlaps(const NXIVFCLevel *a, const NXIVFCLevel *b) {
    if (!a->size || !b->size) return false;
    return a->offset < b->offset + b->size &&
           b->offset < a->offset + a->size;
}

static bool hashTreeCapacityValid(const NXIVFCLevel *levels,
                                  uint32_t levelCount) {
    for (uint32_t i = 1; i < levelCount; ++i) {
        const uint64_t blockSize = levels[i].blockSize;
        const uint64_t dataSize = levels[i].size;
        const uint64_t blockCount = dataSize / blockSize +
                                    (dataSize % blockSize != 0 ? 1u : 0u);
        if (blockCount > UINT64_MAX / NX_IVFC_MASTER_HASH_SIZE)
            return false;
        const uint64_t requiredHashBytes =
            blockCount * NX_IVFC_MASTER_HASH_SIZE;
        if ((uint64_t)levels[i - 1].size < requiredHashBytes)
            return false;
    }
    return true;
}

bool NXEngineOpenIVFC(const uint8_t *image, size_t imageSize,
                      NXIVFCView *view) {
    if (!image || !view || imageSize < NX_IVFC_SUPERBLOCK_SIZE)
        return false;

    if (readU32LE(image) != NX_IVFC_MAGIC ||
        readU32LE(image + 4) != NX_IVFC_VERSION ||
        readU32LE(image + 8) != NX_IVFC_MASTER_HASH_SIZE ||
        readU32LE(image + 12) != NX_IVFC_MAX_LEVELS)
        return false;

    NXIVFCView candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.image = image;
    candidate.imageSize = imageSize;
    candidate.version = readU32LE(image + 4);
    candidate.masterHashSize = readU32LE(image + 8);
    candidate.levelCount = readU32LE(image + 12);
    candidate.masterHash = image + 0xC0;

    for (uint32_t i = 0; i < NX_IVFC_MAX_LEVELS; ++i) {
        const uint8_t *raw = image + 0x10u +
                             (size_t)i * NX_IVFC_LEVEL_ENTRY_SIZE;
        size_t offset = 0;
        size_t size = 0;
        const uint32_t blockSizeLog2 = readU32LE(raw + 16);

        if (!toSize(readU64LE(raw), &offset) ||
            !toSize(readU64LE(raw + 8), &size) ||
            size == 0 || blockSizeLog2 > 31u)
            return false;

        const uint32_t blockSize = UINT32_C(1) << blockSizeLog2;
        if (offset < NX_IVFC_SUPERBLOCK_SIZE ||
            !rangeFits(offset, size, imageSize))
            return false;

        candidate.levels[i].offset = offset;
        candidate.levels[i].size = size;
        candidate.levels[i].blockSizeLog2 = blockSizeLog2;
        candidate.levels[i].blockSize = blockSize;
    }

    for (uint32_t i = 0; i < NX_IVFC_MAX_LEVELS; ++i) {
        for (uint32_t j = i + 1; j < NX_IVFC_MAX_LEVELS; ++j) {
            if (overlaps(&candidate.levels[i], &candidate.levels[j]))
                return false;
        }
    }

    if (!hashTreeCapacityValid(candidate.levels, candidate.levelCount))
        return false;

    *view = candidate;
    return true;
}

bool NXEngineIVFCGetDataLevel(const NXIVFCView *view,
                              const uint8_t **data, size_t *size) {
    if (!view || !view->image || !data || !size ||
        view->levelCount != NX_IVFC_MAX_LEVELS)
        return false;

    const NXIVFCLevel *level = &view->levels[view->levelCount - 1u];
    if (!rangeFits(level->offset, level->size, view->imageSize))
        return false;

    *data = view->image + level->offset;
    *size = level->size;
    return true;
}
