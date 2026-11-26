#include "NXVFS.h"

#include <string.h>

static bool validMountName(const char *name, size_t *length) {
    if (!name || !length) return false;
    size_t count = 0;
    while (name[count] != '\0') {
        if (count >= NX_VFS_MOUNT_NAME_MAX) return false;
        const unsigned char ch = (unsigned char)name[count];
        if (!(ch >= 'a' && ch <= 'z') &&
            !(ch >= 'A' && ch <= 'Z') &&
            !(ch >= '0' && ch <= '9') &&
            ch != '_' && ch != '-' && ch != ':')
            return false;
        ++count;
    }
    if (count == 0) return false;
    *length = count;
    return true;
}

static NXVFSMount *findMount(NXVFS *vfs, const char *name) {
    if (!vfs || !name) return NULL;
    for (size_t i = 0; i < NX_VFS_MAX_MOUNTS; ++i) {
        NXVFSMount *mount = &vfs->mounts[i];
        if (mount->active && strcmp(mount->name, name) == 0)
            return mount;
    }
    return NULL;
}

static bool reserveMount(NXVFS *vfs, const char *name,
                         const uint8_t *image, size_t imageSize,
                         NXVFSFilesystemType type, NXVFSMount **outMount) {
    size_t nameLength = 0;
    if (!vfs || !image || imageSize == 0 || !outMount ||
        !validMountName(name, &nameLength) ||
        vfs->mountCount >= NX_VFS_MAX_MOUNTS ||
        findMount(vfs, name))
        return false;

    for (size_t i = 0; i < NX_VFS_MAX_MOUNTS; ++i) {
        NXVFSMount *mount = &vfs->mounts[i];
        if (mount->active) continue;
        memset(mount, 0, sizeof(*mount));
        memcpy(mount->name, name, nameLength);
        mount->name[nameLength] = '\0';
        mount->type = type;
        mount->image = image;
        mount->imageSize = imageSize;
        mount->active = true;
        ++vfs->mountCount;
        *outMount = mount;
        return true;
    }
    return false;
}

void NXVFSInit(NXVFS *vfs) {
    if (vfs) memset(vfs, 0, sizeof(*vfs));
}

bool NXVFSMountRomFS(NXVFS *vfs, const char *name,
                     const uint8_t *image, size_t imageSize) {
    NXVFSMount *mount = NULL;
    if (!reserveMount(vfs, name, image, imageSize,
                      NX_VFS_FILESYSTEM_ROMFS, &mount))
        return false;
    if (!NXEngineOpenRomFS(image, imageSize, &mount->view.romfs)) {
        memset(mount, 0, sizeof(*mount));
        --vfs->mountCount;
        return false;
    }
    return true;
}

bool NXVFSMountPFS0(NXVFS *vfs, const char *name,
                    const uint8_t *image, size_t imageSize) {
    NXVFSMount *mount = NULL;
    if (!reserveMount(vfs, name, image, imageSize,
                      NX_VFS_FILESYSTEM_PFS0, &mount))
        return false;
    if (!NXEngineOpenPFS0(image, imageSize, &mount->view.pfs0)) {
        memset(mount, 0, sizeof(*mount));
        --vfs->mountCount;
        return false;
    }
    return true;
}

bool NXVFSOpenFile(const NXVFS *vfs, const char *mountName,
                   const char *path, NXVFSFile *file) {
    if (!vfs || !mountName || !path || !file) return false;

    for (size_t i = 0; i < NX_VFS_MAX_MOUNTS; ++i) {
        const NXVFSMount *mount = &vfs->mounts[i];
        if (!mount->active || strcmp(mount->name, mountName) != 0)
            continue;

        if (mount->type == NX_VFS_FILESYSTEM_ROMFS) {
            NXRomFSFile source;
            if (!NXEngineRomFSGetFile(&mount->view.romfs, path, &source))
                return false;
            file->bytes = source.bytes;
            file->size = source.size;
            file->mountIndex = (uint32_t)i;
            return true;
        }
        if (mount->type == NX_VFS_FILESYSTEM_PFS0) {
            NXPFS0File source;
            if (!NXEnginePFS0GetFile(&mount->view.pfs0, path, &source))
                return false;
            file->bytes = source.bytes;
            file->size = source.size;
            file->mountIndex = (uint32_t)i;
            return true;
        }
        return false;
    }
    return false;
}

bool NXVFSReadFile(const NXVFSFile *file, uint64_t offset,
                   void *destination, size_t requested,
                   size_t *bytesRead) {
    if (!file || !bytesRead || (!destination && requested) ||
        offset > file->size)
        return false;

    *bytesRead = 0;
    const size_t remaining = file->size - (size_t)offset;
    const size_t amount = requested < remaining ? requested : remaining;
    if (amount != 0)
        memcpy(destination, file->bytes + (size_t)offset, amount);
    *bytesRead = amount;
    return true;
}

static uint32_t readU32LE(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

bool NXVFSReadDirectory(const NXVFS *vfs, const char *mountName,
                        const char *path, size_t index,
                        NXVFSDirectoryItem *item) {
    if (!vfs || !mountName || !path || !item) return false;

    const NXVFSMount *mount = NULL;
    for (size_t i = 0; i < NX_VFS_MAX_MOUNTS; ++i) {
        const NXVFSMount *candidate = &vfs->mounts[i];
        if (candidate->active && strcmp(candidate->name, mountName) == 0) {
            mount = candidate;
            break;
        }
    }
    if (!mount) return false;

    if (mount->type == NX_VFS_FILESYSTEM_ROMFS) {
        NXRomFSDirectoryItem source;
        if (!NXEngineRomFSGetDirectoryItem(
                &mount->view.romfs, path, index, &source))
            return false;
        item->name = source.name;
        item->nameLength = source.nameLength;
        item->isDirectory = source.isDirectory;
        item->size = source.size;
        return true;
    }

    if (mount->type == NX_VFS_FILESYSTEM_PFS0) {
        if (path[0] != '\0' &&
            !(path[0] == '/' && path[1] == '\0'))
            return false;
        if (index >= mount->view.pfs0.fileCount) return false;

        const NXPFS0View *view = &mount->view.pfs0;
        const size_t entryOffset = view->entriesOffset + index * 0x18u;
        if (entryOffset > view->imageSize ||
            0x18u > view->imageSize - entryOffset)
            return false;
        const uint8_t *entry = view->image + entryOffset;
        const uint32_t nameOffset = readU32LE(entry + 16);
        if (nameOffset >= view->stringTableSize) return false;
        const char *name = (const char *)(
            view->image + view->stringTableOffset + nameOffset
        );
        NXPFS0File file;
        if (!NXEnginePFS0GetFile(view, name, &file)) return false;

        item->name = file.name;
        item->nameLength = strlen(file.name);
        item->isDirectory = false;
        item->size = file.size;
        return true;
    }

    return false;
}

bool NXVFSIsDirectory(const NXVFS *vfs, const char *mountName,
                      const char *path) {
    if (!vfs || !mountName || !path) return false;
    for (size_t i = 0; i < NX_VFS_MAX_MOUNTS; ++i) {
        const NXVFSMount *mount = &vfs->mounts[i];
        if (!mount->active || strcmp(mount->name, mountName) != 0)
            continue;
        if (mount->type == NX_VFS_FILESYSTEM_ROMFS)
            return NXEngineRomFSIsDirectory(&mount->view.romfs, path);
        if (mount->type == NX_VFS_FILESYSTEM_PFS0)
            return path[0] == '\0' ||
                   (path[0] == '/' && path[1] == '\0');
        return false;
    }
    return false;
}
