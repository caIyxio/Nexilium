#include "NXNCA.h"
#include "NXIVFC.h"
#include "NXRomFS.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NCA_IMAGE_SIZE 0x1000u
#define NCA_SECTION_OFFSET 0xC00u
#define NCA_FS_HEADER_OFFSET 0x400u
#define IVFC_SUPERBLOCK_OFFSET (NCA_FS_HEADER_OFFSET + 0x8u)
#define IVFC_DATA_OFFSET 0xA0u
#define ROMFS_SIZE 0xBBu

static int failures = 0;

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "FAIL %s:%d: %s\\n", __FILE__, __LINE__, #expression); \
        ++failures; \
    } \
} while (0)

static void put16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void put64(uint8_t *bytes, uint64_t value) {
    put32(bytes, (uint32_t)value);
    put32(bytes + 4, (uint32_t)(value >> 32));
}

static void buildRomFS(uint8_t *image) {
    memset(image, 0, ROMFS_SIZE);
    put64(image + 0, 0x50);
    put64(image + 8, 0x50);
    put64(image + 16, 4);
    put64(image + 24, 0x54);
    put64(image + 32, 56);
    put64(image + 40, 0x8C);
    put64(image + 48, 4);
    put64(image + 56, 0x90);
    put64(image + 64, 40);
    put64(image + 72, 0xB8);

    put32(image + 0x50, 24);
    put32(image + 0x8C, 0);

    put32(image + 0x54, UINT32_C(0xFFFFFFFF));
    put32(image + 0x58, UINT32_C(0xFFFFFFFF));
    put32(image + 0x5C, 24);
    put32(image + 0x60, UINT32_C(0xFFFFFFFF));
    put32(image + 0x64, UINT32_C(0xFFFFFFFF));
    put32(image + 0x68, 0);

    put32(image + 0x6C, 0);
    put32(image + 0x70, UINT32_C(0xFFFFFFFF));
    put32(image + 0x74, UINT32_C(0xFFFFFFFF));
    put32(image + 0x78, 0);
    put32(image + 0x7C, UINT32_C(0xFFFFFFFF));
    put32(image + 0x80, 5);
    memcpy(image + 0x84, "romfs", 5);

    put32(image + 0x90, 24);
    put32(image + 0x94, UINT32_C(0xFFFFFFFF));
    put64(image + 0x98, 0);
    put64(image + 0xA0, 3);
    put32(image + 0xA8, UINT32_C(0xFFFFFFFF));
    put32(image + 0xAC, 8);
    memcpy(image + 0xB0, "boot.bin", 8);
    memcpy(image + 0xB8, "abc", 3);
}

static void buildIVFCHeader(uint8_t *header) {
    memset(header, 0, NX_IVFC_HEADER_SIZE);
    put32(header + 0x00, UINT32_C(0x43465649));
    put32(header + 0x04, UINT32_C(0x00020000));
    put32(header + 0x08, 0x20);
    put32(header + 0x0C, NX_IVFC_MAX_LEVELS);

    for (uint32_t i = 0; i < NX_IVFC_MAX_LEVELS; ++i) {
        uint8_t *level = header + 0x10u + (size_t)i * 0x18u;
        put64(level, i == 5 ? IVFC_DATA_OFFSET : (uint64_t)i * 0x20u);
        put64(level + 8, i == 5 ? ROMFS_SIZE : 0x20u);
        put32(level + 16, 12);
    }
}

static void buildDecryptedNCA(uint8_t *nca) {
    memset(nca, 0, NCA_IMAGE_SIZE);
    memcpy(nca + 0x200, "NCA3", 4);
    put32(nca + 0x208, NCA_IMAGE_SIZE);

    /* One plaintext RomFS section spanning media blocks 6 through 8. */
    put32(nca + 0x240, 6);
    put32(nca + 0x244, 8);
    put16(nca + NCA_FS_HEADER_OFFSET, 2);
    nca[NCA_FS_HEADER_OFFSET + 2] = 0; /* RomFS */
    nca[NCA_FS_HEADER_OFFSET + 3] = 3; /* Hierarchical integrity / IVFC */
    nca[NCA_FS_HEADER_OFFSET + 4] = 1; /* No section encryption */

    buildIVFCHeader(nca + IVFC_SUPERBLOCK_OFFSET);
    buildRomFS(nca + NCA_SECTION_OFFSET + IVFC_DATA_OFFSET);
}

static void testDecryptedNCASectionToRomFSFile(void) {
    uint8_t nca[NCA_IMAGE_SIZE];
    buildDecryptedNCA(nca);

    NXNCASectionLayout ncaLayout;
    NXNCASectionInfo section;
    CHECK(NXNCAParseSectionTable(nca, NX_NCA_HEADER_SIZE,
                                 sizeof(nca), &ncaLayout));
    CHECK(ncaLayout.sectionCount == 1);
    CHECK(NXNCAGetSectionInfo(&ncaLayout, 0, &section));
    CHECK(section.present);
    CHECK(section.partitionType == 0);
    CHECK(section.fileSystemType == 3);
    CHECK(section.encryptionType == 1);

    const uint8_t *sectionBytes = nca + (size_t)section.byteOffset;
    NXIVFCView ivfc;
    size_t dataOffset = 0;
    size_t dataSize = 0;
    CHECK(NXEngineOpenIVFC(nca + IVFC_SUPERBLOCK_OFFSET,
                           NX_IVFC_HEADER_SIZE, section.byteSize, &ivfc));
    CHECK(NXEngineIVFCGetDataLevel(&ivfc, &dataOffset, &dataSize));

    NXRomFSView romfs;
    NXRomFSFile file;
    CHECK(NXEngineOpenRomFS(sectionBytes + dataOffset, dataSize, &romfs));
    CHECK(NXEngineRomFSGetFile(&romfs, "romfs/boot.bin", &file));
    CHECK(file.size == 3);
    CHECK(memcmp(file.bytes, "abc", 3) == 0);
}

int main(void) {
    testDecryptedNCASectionToRomFSFile();

    if (failures != 0) {
        fprintf(stderr, "%d NCA-to-RomFS test(s) failed\\n", failures);
        return 1;
    }

    puts("Decrypted NCA FS header -> section -> IVFC -> RomFS pipeline passed");
    return 0;
}
