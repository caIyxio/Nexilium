#include "NXIVFC.h"
#include "NXRomFS.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ROMFS_SIZE 0xBBu
#define IVFC_DATA_OFFSET 0xA0u
#define IVFC_SECTION_SIZE 0x400u

static int failures = 0;

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "FAIL %s:%d: %s\\n", __FILE__, __LINE__, #expression); \
        ++failures; \
    } \
} while (0)

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
    put32(header + 0x00, UINT32_C(0x43465649)); /* IVFC */
    put32(header + 0x04, UINT32_C(0x00020000));
    put32(header + 0x08, 0x20);
    put32(header + 0x0C, NX_IVFC_MAX_LEVELS);

    for (uint32_t i = 0; i < NX_IVFC_MAX_LEVELS; ++i) {
        uint8_t *level = header + 0x10u + (size_t)i * 0x18u;
        put64(level, i == 5 ? IVFC_DATA_OFFSET : (uint64_t)i * 0x20u);
        put64(level + 8, i == 5 ? ROMFS_SIZE : 0x20u);
        put32(level + 16, 12); /* 4 KiB hash blocks */
    }
}

static void testIVFCRomFSIntegration(void) {
    uint8_t header[NX_IVFC_HEADER_SIZE];
    uint8_t section[IVFC_SECTION_SIZE] = {0};
    buildIVFCHeader(header);
    buildRomFS(section + IVFC_DATA_OFFSET);

    NXIVFCView ivfc;
    size_t dataOffset = 0;
    size_t dataSize = 0;
    CHECK(NXEngineOpenIVFC(header, sizeof(header), sizeof(section), &ivfc));
    CHECK(ivfc.levelCount == NX_IVFC_MAX_LEVELS);
    CHECK(NXEngineIVFCGetDataLevel(&ivfc, &dataOffset, &dataSize));
    CHECK(dataOffset == IVFC_DATA_OFFSET);
    CHECK(dataSize == ROMFS_SIZE);

    NXRomFSView romfs;
    NXRomFSFile file;
    CHECK(NXEngineOpenRomFS(section + dataOffset, dataSize, &romfs));
    CHECK(NXEngineRomFSGetFile(&romfs, "/romfs/boot.bin", &file));
    CHECK(file.size == 3);
    CHECK(memcmp(file.bytes, "abc", 3) == 0);
}

static void testIVFCAllowsVariableLevelCount(void) {
    uint8_t header[NX_IVFC_HEADER_SIZE];
    buildIVFCHeader(header);
    put32(header + 0x0C, 5);

    NXIVFCView view;
    size_t offset = 0;
    size_t size = 0;
    CHECK(NXEngineOpenIVFC(header, sizeof(header), IVFC_SECTION_SIZE, &view));
    CHECK(view.levelCount == 5);
    CHECK(NXEngineIVFCGetDataLevel(&view, &offset, &size));
    CHECK(offset == 0x80u);
    CHECK(size == 0x20u);
}

static void testIVFCRejectsMalformedInput(void) {
    uint8_t header[NX_IVFC_HEADER_SIZE];
    NXIVFCView view;

    buildIVFCHeader(header);
    put32(header + 4, UINT32_C(0x00010000));
    CHECK(!NXEngineOpenIVFC(header, sizeof(header), IVFC_SECTION_SIZE, &view));

    buildIVFCHeader(header);
    put32(header + 12, 1);
    CHECK(!NXEngineOpenIVFC(header, sizeof(header), IVFC_SECTION_SIZE, &view));

    buildIVFCHeader(header);
    put32(header + 12, 7);
    CHECK(!NXEngineOpenIVFC(header, sizeof(header), IVFC_SECTION_SIZE, &view));

    buildIVFCHeader(header);
    put64(header + 0x10 + 5u * 0x18u, UINT64_MAX);
    CHECK(!NXEngineOpenIVFC(header, sizeof(header), IVFC_SECTION_SIZE, &view));

    buildIVFCHeader(header);
    put64(header + 0x10 + 4u * 0x18u + 8u, 0x10);
    CHECK(!NXEngineOpenIVFC(header, sizeof(header), IVFC_SECTION_SIZE, &view));

    buildIVFCHeader(header);
    CHECK(!NXEngineOpenIVFC(header, NX_IVFC_HEADER_SIZE - 1,
                            IVFC_SECTION_SIZE, &view));

    buildIVFCHeader(header);
    CHECK(!NXEngineOpenIVFC(header, sizeof(header), IVFC_DATA_OFFSET,
                            &view));
}

int main(void) {
    testIVFCRomFSIntegration();
    testIVFCAllowsVariableLevelCount();
    testIVFCRejectsMalformedInput();

    if (failures != 0) {
        fprintf(stderr, "%d IVFC/RomFS test(s) failed\\n", failures);
        return 1;
    }

    puts("NCA IVFC superblock -> section data -> RomFS tests passed");
    return 0;
}
