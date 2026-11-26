#include "NXNCA.h"

#include <string.h>

#define NX_NCA_SECTION_TABLE_OFFSET 0x240u
#define NX_NCA_FS_HEADER_OFFSET 0x400u
#define NX_NCA_MEDIA_UNIT_SIZE UINT64_C(0x200)
#define NX_NCA_FIRST_DATA_BLOCK 6u

static uint16_t readU16LE(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t readU32LE(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static bool rangesOverlap(uint64_t firstOffset, uint64_t firstSize,
                          uint64_t secondOffset, uint64_t secondSize) {
    if (firstSize == 0 || secondSize == 0) return false;
    return firstOffset < secondOffset + secondSize &&
           secondOffset < firstOffset + firstSize;
}

bool NXNCAParseSectionTable(const uint8_t *decryptedHeader,
                            size_t headerSize, uint64_t ncaFileSize,
                            NXNCASectionLayout *layout) {
    if (!decryptedHeader || !layout || headerSize < NX_NCA_HEADER_SIZE ||
        ncaFileSize < NX_NCA_HEADER_SIZE)
        return false;

    if (memcmp(decryptedHeader + 0x200, "NCA2", 4) != 0 &&
        memcmp(decryptedHeader + 0x200, "NCA3", 4) != 0)
        return false;

    NXNCASectionLayout candidate;
    memset(&candidate, 0, sizeof(candidate));

    for (uint32_t i = 0; i < NX_NCA_MAX_SECTIONS; ++i) {
        const uint8_t *entry = decryptedHeader +
                               NX_NCA_SECTION_TABLE_OFFSET + (size_t)i * 0x10u;
        const uint32_t startBlock = readU32LE(entry);
        const uint32_t endBlock = readU32LE(entry + 4);

        NXNCASectionInfo *section = &candidate.sections[i];
        section->index = i;

        if (startBlock == 0 && endBlock == 0) continue;
        if (startBlock == 0 || endBlock == 0 ||
            startBlock < NX_NCA_FIRST_DATA_BLOCK || endBlock <= startBlock)
            return false;

        const uint64_t byteOffset = (uint64_t)startBlock * NX_NCA_MEDIA_UNIT_SIZE;
        const uint64_t endOffset = (uint64_t)endBlock * NX_NCA_MEDIA_UNIT_SIZE;
        if (endOffset > ncaFileSize) return false;

        const uint8_t *fsHeader = decryptedHeader +
            NX_NCA_FS_HEADER_OFFSET + (size_t)i * 0x200u;
        const uint16_t fsHeaderVersion = readU16LE(fsHeader);
        if (fsHeaderVersion != 2) return false;

        section->present = true;
        section->startBlock = startBlock;
        section->endBlock = endBlock;
        section->fsHeaderVersion = fsHeaderVersion;
        section->fsType = fsHeader[2];
        section->hashType = fsHeader[3];
        section->encryptionType = fsHeader[4];
        section->metadataHashType = fsHeader[5];
        section->byteOffset = byteOffset;
        section->byteSize = endOffset - byteOffset;

        /* Only known NCA filesystem/hash/encryption identifiers are accepted. */
        if (section->fsType > 1 || section->hashType > 6 ||
            section->encryptionType > 6 || section->metadataHashType > 1)
            return false;

        for (uint32_t previous = 0; previous < i; ++previous) {
            const NXNCASectionInfo *other = &candidate.sections[previous];
            if (other->present &&
                rangesOverlap(section->byteOffset, section->byteSize,
                              other->byteOffset, other->byteSize))
                return false;
        }

        ++candidate.sectionCount;
    }

    *layout = candidate;
    return true;
}

bool NXNCAGetSectionInfo(const NXNCASectionLayout *layout,
                         uint32_t sectionIndex, NXNCASectionInfo *info) {
    if (!layout || !info || sectionIndex >= NX_NCA_MAX_SECTIONS)
        return false;
    *info = layout->sections[sectionIndex];
    return true;
}
