#ifndef NEXILIUM_NXROMFS_H
#define NEXILIUM_NXROMFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *image;
    size_t imageSize;
    size_t dirHashTableOffset;
    size_t dirHashTableSize;
    size_t dirTableOffset;
    size_t dirTableSize;
    size_t fileHashTableOffset;
    size_t fileHashTableSize;
    size_t fileTableOffset;
    size_t fileTableSize;
    size_t fileDataOffset;
} NXRomFSView;

typedef struct {
    const char *name;
    size_t nameLength;
    const uint8_t *bytes;
    size_t size;
} NXRomFSFile;

/*
 * Opens the RomFS data-level image (not an NCA IVFC wrapper).
 * The view borrows the supplied image; the caller owns its lifetime.
 */
bool NXEngineOpenRomFS(const uint8_t *image, size_t imageSize,
                       NXRomFSView *view);

/*
 * Finds a file by slash-separated path, e.g. "romfs/boot.bin".
 * A leading slash is accepted. The returned name and bytes borrow the image.
 */
bool NXEngineRomFSGetFile(const NXRomFSView *view, const char *path,
                          NXRomFSFile *file);

#endif
