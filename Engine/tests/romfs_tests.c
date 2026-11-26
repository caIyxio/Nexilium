#include "NXRomFS.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static void buildRomFS(uint8_t *image, size_t imageSize) {
    memset(image, 0, imageSize);

    /* Ten-u64 RomFS header followed by two one-bucket hash tables. */
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

    put32(image + 0x50, 24); /* root's "romfs" directory entry */
    put32(image + 0x8C, 0);  /* "boot.bin" file entry */

    /* Root directory, located at directory-table offset zero. */
    put32(image + 0x54, UINT32_C(0xFFFFFFFF));
    put32(image + 0x58, UINT32_C(0xFFFFFFFF));
    put32(image + 0x5C, 24);
    put32(image + 0x60, UINT32_C(0xFFFFFFFF));
    put32(image + 0x64, UINT32_C(0xFFFFFFFF));
    put32(image + 0x68, 0);

    /* Directory "romfs" at directory-table offset 24. */
    put32(image + 0x6C, 0);
    put32(image + 0x70, UINT32_C(0xFFFFFFFF));
    put32(image + 0x74, UINT32_C(0xFFFFFFFF));
    put32(image + 0x78, 0);
    put32(image + 0x7C, UINT32_C(0xFFFFFFFF));
    put32(image + 0x80, 5);
    memcpy(image + 0x84, "romfs", 5);

    /* File "boot.bin" in directory-table parent offset 24. */
    put32(image + 0x90, 24);
    put32(image + 0x94, UINT32_C(0xFFFFFFFF));
    put64(image + 0x98, 0);
    put64(image + 0xA0, 3);
    put32(image + 0xA8, UINT32_C(0xFFFFFFFF));
    put32(image + 0xAC, 8);
    memcpy(image + 0xB0, "boot.bin", 8);
    memcpy(image + 0xB8, "abc", 3);
}

static void testRomFSLookup(void) {
    uint8_t image[0xBB];
    buildRomFS(image, sizeof(image));

    NXRomFSView view;
    NXRomFSFile file;
    CHECK(NXEngineOpenRomFS(image, sizeof(image), &view));
    CHECK(NXEngineRomFSGetFile(&view, "romfs/boot.bin", &file));
    CHECK(file.nameLength == 8);
    CHECK(memcmp(file.name, "boot.bin", 8) == 0);
    CHECK(file.size == 3);
    CHECK(memcmp(file.bytes, "abc", 3) == 0);

    CHECK(NXEngineRomFSGetFile(&view, "/romfs/boot.bin", &file));
    CHECK(!NXEngineRomFSGetFile(&view, "romfs/missing.bin", &file));
    CHECK(!NXEngineRomFSGetFile(&view, "romfs/", &file));
    CHECK(!NXEngineRomFSGetFile(&view, "../boot.bin", &file));
    CHECK(!NXEngineRomFSGetFile(&view, "romfs/../boot.bin", &file));
}

static void testRomFSValidation(void) {
    uint8_t image[0xBB];
    NXRomFSView view;

    buildRomFS(image, sizeof(image));
    put64(image + 24, 0x50); /* directory table overlaps directory hash table */
    CHECK(!NXEngineOpenRomFS(image, sizeof(image), &view));

    buildRomFS(image, sizeof(image));
    CHECK(!NXEngineOpenRomFS(image, 0x4F, &view));

    buildRomFS(image, sizeof(image));
    put64(image + 32, UINT64_MAX); /* impossible metadata-table extent */
    CHECK(!NXEngineOpenRomFS(image, sizeof(image), &view));

    buildRomFS(image, sizeof(image));
    CHECK(NXEngineOpenRomFS(image, sizeof(image), &view));
    put32(image + 0x8C, UINT32_C(0xFFFFFFFC)); /* invalid hash-chain pointer */
    CHECK(!NXEngineRomFSGetFile(&view, "romfs/boot.bin", &(NXRomFSFile){0}));

    buildRomFS(image, sizeof(image));
    CHECK(NXEngineOpenRomFS(image, sizeof(image), &view));
    put32(image + 0xA8, 0); /* self-loop in the file hash chain */
    CHECK(!NXEngineRomFSGetFile(&view, "romfs/not-present", &(NXRomFSFile){0}));
}

int main(void) {
    testRomFSLookup();
    testRomFSValidation();

    if (failures != 0) {
        fprintf(stderr, "%d RomFS test(s) failed\\n", failures);
        return 1;
    }

    puts("RomFS header, path lookup, file bounds, and malformed-input tests passed");
    return 0;
}
