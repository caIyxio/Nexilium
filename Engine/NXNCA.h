#ifndef NEXILIUM_NXNCA_H
#define NEXILIUM_NXNCA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NX_NCA_HEADER_SIZE 0xC00u
#define NX_NCA_MAX_SECTIONS 4u

typedef struct {
    bool present;
    uint32_t index;
    uint32_t startBlock;
    uint32_t endBlock;
    uint16_t fsHeaderVersion;
    uint8_t partitionType;
    uint8_t fileSystemType;
    uint8_t encryptionType;
    uint64_t byteOffset;
    uint64_t byteSize;
} NXNCASectionInfo;

typedef struct {
    uint32_t sectionCount;
    NXNCASectionInfo sections[NX_NCA_MAX_SECTIONS];
} NXNCASectionLayout;

/*
 * Parses the section extent table and its four filesystem headers from an
 * already-decrypted 0xC00-byte NCA header. The input remains caller-owned.
 */
bool NXNCAParseSectionTable(const uint8_t *decryptedHeader,
                            size_t headerSize, uint64_t ncaFileSize,
                            NXNCASectionLayout *layout);

/* Copies one slot (including absent slots) from a parsed section layout. */
bool NXNCAGetSectionInfo(const NXNCASectionLayout *layout,
                         uint32_t sectionIndex, NXNCASectionInfo *info);

#endif
