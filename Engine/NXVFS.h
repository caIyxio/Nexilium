#ifndef NEXILIUM_NXVFS_H
#define NEXILIUM_NXVFS_H

#include "NXRomFS.h"
#include "NXPFS0.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NX_VFS_MAX_MOUNTS 16u
#define NX_VFS_MOUNT_NAME_MAX 31u

typedef enum {
    NX_VFS_FILESYSTEM_NONE = 0,
    NX_VFS_FILESYSTEM_ROMFS,
    NX_VFS_FILESYSTEM_PFS0
} NXVFSFilesystemType;

typedef struct {
    bool active;
    char name[NX_VFS_MOUNT_NAME_MAX + 1u];
    NXVFSFilesystemType type;
    const uint8_t *image;
    size_t imageSize;
    union {
        NXRomFSView romfs;
        NXPFS0View pfs0;
    } view;
} NXVFSMount;

typedef struct {
    NXVFSMount mounts[NX_VFS_MAX_MOUNTS];
    size_t mountCount;
} NXVFS;

typedef struct {
    const uint8_t *bytes;
    size_t size;
    uint32_t mountIndex;
} NXVFSFile;

typedef struct {
    const char *name;
    size_t nameLength;
    bool isDirectory;
    size_t size;
} NXVFSDirectoryItem;

/* All mounted images are borrowed; callers must keep them alive while mounted. */
void NXVFSInit(NXVFS *vfs);
bool NXVFSMountRomFS(NXVFS *vfs, const char *name,
                     const uint8_t *image, size_t imageSize);
bool NXVFSMountPFS0(NXVFS *vfs, const char *name,
                    const uint8_t *image, size_t imageSize);
bool NXVFSOpenFile(const NXVFS *vfs, const char *mountName,
                   const char *path, NXVFSFile *file);
bool NXVFSReadFile(const NXVFSFile *file, uint64_t offset,
                   void *destination, size_t requested,
                   size_t *bytesRead);
bool NXVFSReadDirectory(const NXVFS *vfs, const char *mountName,
                        const char *path, size_t index,
                        NXVFSDirectoryItem *item);

#endif
