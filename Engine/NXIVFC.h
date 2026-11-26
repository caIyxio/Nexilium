#ifndef NEXILIUM_NXIVFC_H
#define NEXILIUM_NXIVFC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NX_IVFC_MAX_LEVELS 6u
#define NX_IVFC_HEADER_SIZE 0xE0u

typedef struct {
    size_t offset;
    size_t size;
    uint32_t blockSizeLog2;
    uint32_t blockSize;
} NXIVFCLevel;

typedef struct {
    const uint8_t *superblock;
    size_t superblockSize;
    uint64_t sectionSize;
    uint32_t id;
    uint32_t masterHashSize;
    uint32_t levelCount;
    const uint8_t *masterHash;
    NXIVFCLevel levels[NX_IVFC_MAX_LEVELS];
} NXIVFCView;

/*
 * Opens the IVFC metadata stored in the NCA filesystem header.
 * Level offsets and sizes are relative to the separately supplied decrypted
 * NCA section data. The caller retains ownership of both buffers.
 */
bool NXEngineOpenIVFC(const uint8_t *superblock, size_t superblockSize,
                      uint64_t sectionSize, NXIVFCView *view);

/* Returns the last declared level's section-relative offset and size. */
bool NXEngineIVFCGetDataLevel(const NXIVFCView *view,
                              size_t *offset, size_t *size);

#endif
