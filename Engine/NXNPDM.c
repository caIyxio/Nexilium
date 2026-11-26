#include "NXNPDM.h"

#include <string.h>

#define NX_NPDM_MAGIC UINT32_C(0x4154454D) /* META */
#define NX_ACID_MAGIC UINT32_C(0x44494341) /* ACID */
#define NX_ACI0_MAGIC UINT32_C(0x30494341) /* ACI0 */

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

static bool rangeFits(size_t offset, size_t size, size_t total) {
    return offset <= total && size <= total - offset;
}

static bool subrangeFits(uint32_t offset, uint32_t size,
                         uint32_t parentSize, uint32_t minimumOffset) {
    return offset >= minimumOffset && offset <= parentSize &&
           size <= parentSize - offset;
}

bool NXEngineOpenNPDM(const uint8_t *image, size_t imageSize,
                      NXNPDMView *view) {
    if (!image || !view || imageSize < NX_NPDM_HEADER_SIZE ||
        readU32LE(image) != NX_NPDM_MAGIC)
        return false;

    const uint32_t aci0Offset = readU32LE(image + 0x70);
    const uint32_t aci0Size = readU32LE(image + 0x74);
    const uint32_t acidOffset = readU32LE(image + 0x78);
    const uint32_t acidSize = readU32LE(image + 0x7C);

    if (aci0Size < NX_NPDM_ACI0_HEADER_SIZE ||
        acidSize < NX_NPDM_ACID_HEADER_SIZE ||
        !rangeFits(aci0Offset, aci0Size, imageSize) ||
        !rangeFits(acidOffset, acidSize, imageSize) ||
        aci0Offset < NX_NPDM_HEADER_SIZE ||
        acidOffset < NX_NPDM_HEADER_SIZE ||
        ((uint64_t)aci0Offset < (uint64_t)acidOffset + acidSize &&
         (uint64_t)acidOffset < (uint64_t)aci0Offset + aci0Size))
        return false;

    const uint8_t *aci0 = image + aci0Offset;
    const uint8_t *acid = image + acidOffset;
    if (readU32LE(aci0) != NX_ACI0_MAGIC ||
        readU32LE(acid + 0x200) != NX_ACID_MAGIC)
        return false;

    const uint64_t titleID = readU64LE(aci0 + 0x10);
    const uint32_t acidDeclaredSize = readU32LE(acid + 0x204);
    const uint64_t titleIDMin = readU64LE(acid + 0x210);
    const uint64_t titleIDMax = readU64LE(acid + 0x218);
    const uint32_t aci0SACOffset = readU32LE(aci0 + 0x28);
    const uint32_t aci0SACSize = readU32LE(aci0 + 0x2C);
    const uint32_t aci0KACOffset = readU32LE(aci0 + 0x30);
    const uint32_t aci0KACSize = readU32LE(aci0 + 0x34);

    /* ACID's declared size excludes its fixed 0x200-byte signature/key prefix. */
    if (acidDeclaredSize > acidSize - 0x200u ||
        acidDeclaredSize < 0x40u ||
        titleIDMin > titleIDMax ||
        titleID < titleIDMin || titleID > titleIDMax ||
        !subrangeFits(aci0SACOffset, aci0SACSize, aci0Size,
                      NX_NPDM_ACI0_HEADER_SIZE) ||
        !subrangeFits(aci0KACOffset, aci0KACSize, aci0Size,
                      NX_NPDM_ACI0_HEADER_SIZE) ||
        (aci0KACSize % sizeof(uint32_t)) != 0)
        return false;

    NXNPDMView candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.image = image;
    candidate.imageSize = imageSize;
    candidate.titleID = titleID;
    candidate.titleIDRangeMin = titleIDMin;
    candidate.titleIDRangeMax = titleIDMax;
    candidate.mainStackSize = readU32LE(image + 0x1C);
    candidate.version = readU32LE(image + 0x18);
    candidate.mmuFlags = image[0x0C];
    candidate.mainThreadPriority = image[0x0E];
    candidate.defaultCPU = image[0x0F];
    candidate.isRetail = (uint8_t)(readU32LE(acid + 0x20C) & 1u);
    candidate.acidOffset = acidOffset;
    candidate.acidSize = acidSize;
    candidate.aci0Offset = aci0Offset;
    candidate.aci0Size = aci0Size;
    candidate.aci0KACOffset = aci0KACOffset;
    candidate.aci0KACSize = aci0KACSize;
    candidate.aci0SACOffset = aci0SACOffset;
    candidate.aci0SACSize = aci0SACSize;

    if (candidate.mainStackSize == 0 ||
        candidate.mainStackSize > 16u * 1024u * 1024u ||
        candidate.mainThreadPriority > 0x3Fu ||
        candidate.defaultCPU > 3u)
        return false;

    *view = candidate;
    return true;
}
