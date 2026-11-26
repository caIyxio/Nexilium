#include "NXRomFS.h"

#include <limits.h>
#include <string.h>

#define NX_ROMFS_HEADER_SIZE 0x50u
#define NX_ROMFS_DIR_ENTRY_SIZE 0x18u
#define NX_ROMFS_FILE_ENTRY_SIZE 0x20u
#define NX_ROMFS_NONE UINT32_C(0xFFFFFFFF)

typedef struct {
    uint32_t parent;
    uint32_t sibling;
    uint32_t childDir;
    uint32_t childFile;
    uint32_t nextHash;
    uint32_t nameLength;
    const uint8_t *name;
} NXRomFSDirectoryEntry;

typedef struct {
    uint32_t parent;
    uint32_t sibling;
    uint64_t dataOffset;
    uint64_t dataSize;
    uint32_t nextHash;
    uint32_t nameLength;
    const uint8_t *name;
} NXRomFSFileEntry;

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

static bool rangeFits(size_t offset, size_t length, size_t total) {
    return offset <= total && length <= total - offset;
}

static bool rangesOverlap(size_t aOffset, size_t aSize,
                          size_t bOffset, size_t bSize) {
    if (aSize == 0 || bSize == 0) return false;
    return aOffset < bOffset + bSize && bOffset < aOffset + aSize;
}

static bool metadataRangesValid(size_t imageSize, size_t headerSize,
                                size_t dirHashOffset, size_t dirHashSize,
                                size_t dirOffset, size_t dirSize,
                                size_t fileHashOffset, size_t fileHashSize,
                                size_t fileOffset, size_t fileSize,
                                size_t fileDataOffset) {
    if (headerSize != NX_ROMFS_HEADER_SIZE ||
        dirHashSize == 0 || (dirHashSize & 3u) != 0 ||
        dirSize < NX_ROMFS_DIR_ENTRY_SIZE ||
        (fileHashSize & 3u) != 0 ||
        (fileSize != 0 && fileHashSize == 0)) return false;

    if (dirHashOffset < headerSize || dirOffset < headerSize ||
        (fileHashSize && fileHashOffset < headerSize) ||
        (fileSize && fileOffset < headerSize) ||
        fileDataOffset < headerSize || fileDataOffset > imageSize)
        return false;

    if ((dirHashOffset & 3u) || (dirOffset & 3u) ||
        (fileHashOffset & 3u) || (fileOffset & 3u))
        return false;

    if (!rangeFits(dirHashOffset, dirHashSize, imageSize) ||
        !rangeFits(dirOffset, dirSize, imageSize) ||
        !rangeFits(fileHashOffset, fileHashSize, imageSize) ||
        !rangeFits(fileOffset, fileSize, imageSize))
        return false;

    const size_t dataSize = imageSize - fileDataOffset;
    if (rangesOverlap(0, headerSize, dirHashOffset, dirHashSize) ||
        rangesOverlap(0, headerSize, dirOffset, dirSize) ||
        rangesOverlap(0, headerSize, fileHashOffset, fileHashSize) ||
        rangesOverlap(0, headerSize, fileOffset, fileSize) ||
        rangesOverlap(dirHashOffset, dirHashSize, dirOffset, dirSize) ||
        rangesOverlap(dirHashOffset, dirHashSize, fileHashOffset, fileHashSize) ||
        rangesOverlap(dirHashOffset, dirHashSize, fileOffset, fileSize) ||
        rangesOverlap(dirOffset, dirSize, fileHashOffset, fileHashSize) ||
        rangesOverlap(dirOffset, dirSize, fileOffset, fileSize) ||
        rangesOverlap(fileHashOffset, fileHashSize, fileOffset, fileSize) ||
        rangesOverlap(dirHashOffset, dirHashSize, fileDataOffset, dataSize) ||
        rangesOverlap(dirOffset, dirSize, fileDataOffset, dataSize) ||
        rangesOverlap(fileHashOffset, fileHashSize, fileDataOffset, dataSize) ||
        rangesOverlap(fileOffset, fileSize, fileDataOffset, dataSize))
        return false;

    return true;
}

static bool readDirectory(const NXRomFSView *view, uint32_t offset,
                          NXRomFSDirectoryEntry *entry) {
    if (!view || !entry || offset == NX_ROMFS_NONE ||
        (offset & 3u) ||
        !rangeFits((size_t)offset, NX_ROMFS_DIR_ENTRY_SIZE,
                   view->dirTableSize)) return false;

    const uint8_t *raw = view->image + view->dirTableOffset + offset;
    const uint32_t nameLength = readU32LE(raw + 20);
    if ((size_t)nameLength > SIZE_MAX - NX_ROMFS_DIR_ENTRY_SIZE - 3u)
        return false;
    const size_t rawLength = NX_ROMFS_DIR_ENTRY_SIZE + (size_t)nameLength;
    const size_t paddedLength = (rawLength + 3u) & ~(size_t)3u;
    if (!rangeFits((size_t)offset, paddedLength, view->dirTableSize))
        return false;

    entry->parent = readU32LE(raw);
    entry->sibling = readU32LE(raw + 4);
    entry->childDir = readU32LE(raw + 8);
    entry->childFile = readU32LE(raw + 12);
    entry->nextHash = readU32LE(raw + 16);
    entry->nameLength = nameLength;
    entry->name = raw + NX_ROMFS_DIR_ENTRY_SIZE;
    return true;
}

static bool readFile(const NXRomFSView *view, uint32_t offset,
                     NXRomFSFileEntry *entry) {
    if (!view || !entry || offset == NX_ROMFS_NONE ||
        (offset & 3u) ||
        !rangeFits((size_t)offset, NX_ROMFS_FILE_ENTRY_SIZE,
                   view->fileTableSize)) return false;

    const uint8_t *raw = view->image + view->fileTableOffset + offset;
    const uint32_t nameLength = readU32LE(raw + 28);
    if ((size_t)nameLength > SIZE_MAX - NX_ROMFS_FILE_ENTRY_SIZE - 3u)
        return false;
    const size_t rawLength = NX_ROMFS_FILE_ENTRY_SIZE + (size_t)nameLength;
    const size_t paddedLength = (rawLength + 3u) & ~(size_t)3u;
    if (!rangeFits((size_t)offset, paddedLength, view->fileTableSize))
        return false;

    entry->parent = readU32LE(raw);
    entry->sibling = readU32LE(raw + 4);
    entry->dataOffset = readU64LE(raw + 8);
    entry->dataSize = readU64LE(raw + 16);
    entry->nextHash = readU32LE(raw + 24);
    entry->nameLength = nameLength;
    entry->name = raw + NX_ROMFS_FILE_ENTRY_SIZE;
    return true;
}

static uint32_t rotateRight5(uint32_t value) {
    return (value >> 5) | (value << 27);
}

static uint32_t nameHash(uint32_t parentOffset, const char *name,
                         size_t nameLength) {
    uint32_t hash = parentOffset ^ UINT32_C(123456789);
    for (size_t i = 0; i < nameLength; ++i) {
        hash = rotateRight5(hash);
        hash ^= (uint8_t)name[i];
    }
    return hash;
}

static bool findDirectory(const NXRomFSView *view, uint32_t parentOffset,
                          const char *name, size_t nameLength,
                          uint32_t *outOffset) {
    if (!view || !name || !outOffset || view->dirHashTableSize < 4 ||
        (view->dirHashTableSize & 3u) != 0) return false;

    const size_t bucketCount = view->dirHashTableSize / 4u;
    const size_t bucket = nameHash(parentOffset, name, nameLength) % bucketCount;
    uint32_t offset = readU32LE(view->image + view->dirHashTableOffset + bucket * 4u);
    const size_t maxEntries = view->dirTableSize / NX_ROMFS_DIR_ENTRY_SIZE + 1u;

    for (size_t step = 0; offset != NX_ROMFS_NONE && step < maxEntries; ++step) {
        NXRomFSDirectoryEntry entry;
        if (!readDirectory(view, offset, &entry)) return false;
        if (entry.parent == parentOffset && entry.nameLength == nameLength &&
            memcmp(entry.name, name, nameLength) == 0) {
            *outOffset = offset;
            return true;
        }
        offset = entry.nextHash;
    }
    return false;
}

static bool findFile(const NXRomFSView *view, uint32_t parentOffset,
                     const char *name, size_t nameLength,
                     NXRomFSFileEntry *outEntry) {
    if (!view || !name || !outEntry || view->fileHashTableSize < 4 ||
        (view->fileHashTableSize & 3u) != 0 || view->fileTableSize == 0)
        return false;

    const size_t bucketCount = view->fileHashTableSize / 4u;
    const size_t bucket = nameHash(parentOffset, name, nameLength) % bucketCount;
    uint32_t offset = readU32LE(view->image + view->fileHashTableOffset + bucket * 4u);
    const size_t maxEntries = view->fileTableSize / NX_ROMFS_FILE_ENTRY_SIZE + 1u;

    for (size_t step = 0; offset != NX_ROMFS_NONE && step < maxEntries; ++step) {
        NXRomFSFileEntry entry;
        if (!readFile(view, offset, &entry)) return false;
        if (entry.parent == parentOffset && entry.nameLength == nameLength &&
            memcmp(entry.name, name, nameLength) == 0) {
            if (entry.dataOffset > view->imageSize - view->fileDataOffset ||
                entry.dataSize > view->imageSize - view->fileDataOffset -
                                 (size_t)entry.dataOffset)
                return false;
            *outEntry = entry;
            return true;
        }
        offset = entry.nextHash;
    }
    return false;
}

bool NXEngineOpenRomFS(const uint8_t *image, size_t imageSize,
                       NXRomFSView *view) {
    if (!image || !view || imageSize < NX_ROMFS_HEADER_SIZE) return false;

    size_t fields[10];
    for (size_t i = 0; i < 10; ++i) {
        if (!toSize(readU64LE(image + i * 8u), &fields[i])) return false;
    }

    if (!metadataRangesValid(imageSize, fields[0],
                             fields[1], fields[2], fields[3], fields[4],
                             fields[5], fields[6], fields[7], fields[8],
                             fields[9])) return false;

    NXRomFSView candidate = {
        .image = image,
        .imageSize = imageSize,
        .dirHashTableOffset = fields[1],
        .dirHashTableSize = fields[2],
        .dirTableOffset = fields[3],
        .dirTableSize = fields[4],
        .fileHashTableOffset = fields[5],
        .fileHashTableSize = fields[6],
        .fileTableOffset = fields[7],
        .fileTableSize = fields[8],
        .fileDataOffset = fields[9]
    };

    NXRomFSDirectoryEntry root;
    if (!readDirectory(&candidate, 0, &root) || root.nameLength != 0)
        return false;

    *view = candidate;
    return true;
}

bool NXEngineRomFSGetFile(const NXRomFSView *view, const char *path,
                          NXRomFSFile *file) {
    if (!view || !view->image || !path || !file) return false;

    const char *cursor = path;
    while (*cursor == '/') ++cursor;
    if (*cursor == '\0') return false;

    uint32_t directoryOffset = 0;
    for (;;) {
        const char *component = cursor;
        while (*cursor != '\0' && *cursor != '/') ++cursor;
        const size_t componentLength = (size_t)(cursor - component);
        if (componentLength == 0 || componentLength > UINT32_MAX ||
            (componentLength == 1 && component[0] == '.') ||
            (componentLength == 2 && component[0] == '.' &&
             component[1] == '.')) return false;

        if (*cursor == '\0') {
            NXRomFSFileEntry entry;
            if (!findFile(view, directoryOffset, component,
                          componentLength, &entry)) return false;

            file->name = (const char *)entry.name;
            file->nameLength = entry.nameLength;
            file->bytes = view->image + view->fileDataOffset +
                          (size_t)entry.dataOffset;
            file->size = (size_t)entry.dataSize;
            return true;
        }

        ++cursor;
        if (*cursor == '\0' || *cursor == '/') return false;

        uint32_t childOffset = NX_ROMFS_NONE;
        if (!findDirectory(view, directoryOffset, component,
                           componentLength, &childOffset)) return false;
        directoryOffset = childOffset;
    }
}

static bool findDirectoryPath(const NXRomFSView *view, const char *path,
                              uint32_t *directoryOffset) {
    if (!view || !path || !directoryOffset) return false;
    const char *cursor = path;
    while (*cursor == '/') ++cursor;
    uint32_t current = 0;

    while (*cursor != '\0') {
        const char *component = cursor;
        while (*cursor != '\0' && *cursor != '/') ++cursor;
        const size_t length = (size_t)(cursor - component);
        if (length == 0 || length > UINT32_MAX ||
            (length == 1 && component[0] == '.') ||
            (length == 2 && component[0] == '.' && component[1] == '.'))
            return false;

        uint32_t child = NX_ROMFS_NONE;
        if (!findDirectory(view, current, component, length, &child))
            return false;
        current = child;

        if (*cursor == '/') {
            ++cursor;
            if (*cursor == '/') return false;
        }
    }

    *directoryOffset = current;
    return true;
}

bool NXEngineRomFSGetDirectoryItem(const NXRomFSView *view,
                                   const char *path, size_t index,
                                   NXRomFSDirectoryItem *item) {
    if (!view || !view->image || !path || !item) return false;

    uint32_t directoryOffset = 0;
    if (!findDirectoryPath(view, path, &directoryOffset)) return false;

    NXRomFSDirectoryEntry directory;
    if (!readDirectory(view, directoryOffset, &directory)) return false;

    size_t currentIndex = 0;
    uint32_t offset = directory.childDir;
    const size_t maxDirectories =
        view->dirTableSize / NX_ROMFS_DIR_ENTRY_SIZE + 1u;

    for (size_t step = 0;
         offset != NX_ROMFS_NONE && step < maxDirectories; ++step) {
        NXRomFSDirectoryEntry child;
        if (!readDirectory(view, offset, &child) ||
            child.parent != directoryOffset)
            return false;
        if (currentIndex == index) {
            item->name = (const char *)child.name;
            item->nameLength = child.nameLength;
            item->isDirectory = true;
            item->size = 0;
            return true;
        }
        ++currentIndex;
        offset = child.sibling;
    }
    if (offset != NX_ROMFS_NONE) return false;

    offset = directory.childFile;
    const size_t maxFiles =
        view->fileTableSize / NX_ROMFS_FILE_ENTRY_SIZE + 1u;

    for (size_t step = 0; offset != NX_ROMFS_NONE && step < maxFiles; ++step) {
        NXRomFSFileEntry child;
        if (!readFile(view, offset, &child) ||
            child.parent != directoryOffset)
            return false;
        if (child.dataOffset > view->imageSize - view->fileDataOffset ||
            child.dataSize > view->imageSize - view->fileDataOffset -
                             (size_t)child.dataOffset ||
            child.dataSize > (uint64_t)SIZE_MAX)
            return false;

        if (currentIndex == index) {
            item->name = (const char *)child.name;
            item->nameLength = child.nameLength;
            item->isDirectory = false;
            item->size = (size_t)child.dataSize;
            return true;
        }
        ++currentIndex;
        offset = child.sibling;
    }
    return false;
}
