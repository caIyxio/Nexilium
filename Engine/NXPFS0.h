#ifndef NEXILIUM_NXPFS0_H
#define NEXILIUM_NXPFS0_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NX_PFS0_MAX_FILES 4096u
#define NX_PFS0_MAX_STRING_TABLE (16u * 1024u * 1024u)

typedef struct {
    const uint8_t *image;
    size_t imageSize;
    uint32_t fileCount;
    size_t entriesOffset;
    size_t stringTableOffset;
    size_t stringTableSize;
    size_t dataOffset;
} NXPFS0View;

typedef struct {
    const char *name;
    const uint8_t *bytes;
    size_t size;
} NXPFS0File;

/* Opens a PFS0 image without copying its contents. */
bool NXEngineOpenPFS0(const uint8_t *image, size_t imageSize,
                      NXPFS0View *view);

/* Returns false when the name is absent or the PFS0 image is malformed. */
bool NXEnginePFS0GetFile(const NXPFS0View *view, const char *name,
                         NXPFS0File *file);

#endif
