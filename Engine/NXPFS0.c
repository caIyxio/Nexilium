#include "NXPFS0.h"
#include <string.h>
#include <stdint.h>

#define NX_PFS0_HEADER_SIZE 0x10u
#define NX_PFS0_ENTRY_SIZE 0x18u
#define NX_PFS0_MAGIC UINT32_C(0x30534650) /* "PFS0" */

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

static bool rangeFits(size_t offset, size_t length, size_t total) {
    return offset <= total && length <= total - offset;
}

bool NXEngineOpenPFS0(const uint8_t *image, size_t imageSize,
                      NXPFS0View *view) {
    if (!image || !view || imageSize < NX_PFS0_HEADER_SIZE) return false;
    if (readU32LE(image) != NX_PFS0_MAGIC) return false;

    const uint32_t fileCount = readU32LE(image + 4);
    const uint32_t stringTableSize = readU32LE(image + 8);
    if (fileCount > NX_PFS0_MAX_FILES ||
        stringTableSize > NX_PFS0_MAX_STRING_TABLE) return false;

    const size_t entriesOffset = NX_PFS0_HEADER_SIZE;
    if (fileCount > (SIZE_MAX - entriesOffset) / NX_PFS0_ENTRY_SIZE)
        return false;
    const size_t entriesBytes = (size_t)fileCount * NX_PFS0_ENTRY_SIZE;
    const size_t stringTableOffset = entriesOffset + entriesBytes;

    if (!rangeFits(stringTableOffset, stringTableSize, imageSize))
        return false;
    const size_t dataOffset = stringTableOffset + stringTableSize;

    /* Validate every entry before publishing the view. */
    for (uint32_t i = 0; i < fileCount; ++i) {
        const uint8_t *entry = image + entriesOffset + (size_t)i * NX_PFS0_ENTRY_SIZE;
        const uint64_t dataOffsetInPFS = readU64LE(entry);
        const uint64_t dataSize = readU64LE(entry + 8);
        const uint32_t nameOffset = readU32LE(entry + 16);

        if (nameOffset >= stringTableSize) return false;
        if (dataOffsetInPFS > SIZE_MAX || dataSize > SIZE_MAX) return false;
        if ((size_t)dataOffsetInPFS > imageSize - dataOffset) return false;
        if ((size_t)dataSize > imageSize - dataOffset - (size_t)dataOffsetInPFS)
            return false;

        const char *name = (const char *)(image + stringTableOffset + nameOffset);
        const size_t remainingNames = (size_t)stringTableSize - nameOffset;
        if (!memchr(name, '\0', remainingNames)) return false;
    }

    view->image = image;
    view->imageSize = imageSize;
    view->fileCount = fileCount;
    view->entriesOffset = entriesOffset;
    view->stringTableOffset = stringTableOffset;
    view->stringTableSize = stringTableSize;
    view->dataOffset = dataOffset;
    return true;
}

bool NXEnginePFS0GetFile(const NXPFS0View *view, const char *name,
                         NXPFS0File *file) {
    if (!view || !view->image || !name || !file ||
        view->imageSize < view->dataOffset) return false;

    for (uint32_t i = 0; i < view->fileCount; ++i) {
        const uint8_t *entry = view->image + view->entriesOffset +
                               (size_t)i * NX_PFS0_ENTRY_SIZE;
        const uint64_t offset = readU64LE(entry);
        const uint64_t size = readU64LE(entry + 8);
        const uint32_t nameOffset = readU32LE(entry + 16);
        const char *entryName = (const char *)(view->image +
                                view->stringTableOffset + nameOffset);

        if (strcmp(entryName, name) != 0) continue;
        if (offset > SIZE_MAX || size > SIZE_MAX) return false;
        if ((size_t)offset > view->imageSize - view->dataOffset ||
            (size_t)size > view->imageSize - view->dataOffset - (size_t)offset)
            return false;

        file->name = entryName;
        file->bytes = view->image + view->dataOffset + (size_t)offset;
        file->size = (size_t)size;
        return true;
    }
    return false;
}
