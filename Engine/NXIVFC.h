#ifndef NEXILIUM_NXIVFC_H
#define NEXILIUM_NXIVFC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NX_IVFC_MAX_LEVELS 6u

typedef struct {
    size_t offset;
    size_t size;
    uint32_t blockSizeLog2;
    uint32_t blockSize;
} NXIVFCLevel;

typedef struct {
    const uint8_t *image;
    size_t imageSize;
    uint32_t version;
    uint32_t masterHashSize;
    uint32_t levelCount;
    const uint8_t *masterHash;
    NXIVFCLevel levels[NX_IVFC_MAX_LEVELS];
} NXIVFCView;

/*
 * Parses an IVFC RomFS section after its NCA section encryption has been
 * removed. This validates the header, level extents, and hash-tree capacity;
 * it does not verify the SHA-256 hashes or decrypt NCA data.
 */
bool NXEngineOpenIVFC(const uint8_t *image, size_t imageSize,
                      NXIVFCView *view);

/* Returns the final IVFC data level, which contains the RomFS image. */
bool NXEngineIVFCGetDataLevel(const NXIVFCView *view,
                              const uint8_t **data, size_t *size);

#endif
