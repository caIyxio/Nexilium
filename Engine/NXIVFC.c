#include "NXIVFC.h"

#include <string.h>

#define NX_IVFC_MAGIC UINT32_C(0x43465649) /* "IVFC" */
#define NX_IVFC_ID UINT32_C(0x00020000)
#define NX_IVFC_MASTER_HASH_SIZE 0x20u
#define NX_IVFC_LEVEL_ENTRY_SIZE 0x18u
#define NX_IVFC_LEVELS_OFFSET 0x10u
#define NX_IVFC_MASTER_HASH_OFFSET 0xC0u

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

static bool rangeFits(uint64_t offset, uint64_t size, uint64_t total) {
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

bool NXEngineOpenIVFC(const uint8_t *superblock, size_t superblockSize,
                      uint64_t sectionSize, NXIVFCView *view) {
    if (!superblock || !view ||
        superblockSize < NX_IVFC_HEADER_SIZE ||
        sectionSize == 0)
        return false;

    const uint32_t id = readU32LE(superblock + 4);
    const uint32_t masterHashSize = readU32LE(superblock + 8);
    const uint32_t levelCount = readU32LE(superblock + 12);

    if (readU32LE(superblock) != NX_IVFC_MAGIC ||
        id != NX_IVFC_ID ||
        masterHashSize != NX_IVFC_MASTER_HASH_SIZE ||
        levelCount < 2u || levelCount > NX_IVFC_MAX_LEVELS)
        return false;

    NXIVFCView candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.superblock = superblock;
    candidate.superblockSize = superblockSize;
    candidate.sectionSize = sectionSize;
    candidate.id = id;
    candidate.masterHashSize = masterHashSize;
    candidate.levelCount = levelCount;
    candidate.masterHash = superblock + NX_IVFC_MASTER_HASH_OFFSET;

    for (uint32_t i = 0; i < levelCount; ++i) {
        const uint8_t *raw = superblock + NX_IVFC_LEVELS_OFFSET +
                             (size_t)i * NX_IVFC_LEVEL_ENTRY_SIZE;
        const uint64_t rawOffset = readU64LE(raw);
        const uint64_t rawSize = readU64LE(raw + 8);
        const uint32_t blockSizeLog2 = readU32LE(raw + 16);
        size_t offset = 0;
        size_t size = 0;

        if (!rawSize || blockSizeLog2 > 31u ||
            !toSize(rawOffset, &offset) ||
            !toSize(rawSize, &size) ||
            !rangeFits(rawOffset, rawSize, sectionSize))
            return false;

        candidate.levels[i].offset = offset;
        candidate.levels[i].size = size;
        candidate.levels[i].blockSizeLog2 = blockSizeLog2;
        candidate.levels[i].blockSize = UINT32_C(1) << blockSizeLog2;
    }

    for (uint32_t i = 0; i < levelCount; ++i) {
        for (uint32_t j = i + 1; j < levelCount; ++j) {
            if (overlaps(&candidate.levels[i], &candidate.levels[j]))
                return false;
        }
    }

    if (!hashTreeCapacityValid(candidate.levels, levelCount))
        return false;

    *view = candidate;
    return true;
}

bool NXEngineIVFCGetDataLevel(const NXIVFCView *view,
                              size_t *offset, size_t *size) {
    if (!view || !view->superblock || !offset || !size ||
        view->levelCount < 2u || view->levelCount > NX_IVFC_MAX_LEVELS)
        return false;

    const NXIVFCLevel *level = &view->levels[view->levelCount - 1u];
    if (!rangeFits(level->offset, level->size, view->sectionSize))
        return false;

    *offset = level->offset;
    *size = level->size;
    return true;
}
