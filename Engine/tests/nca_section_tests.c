#include "NXNCA.h"

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

static void buildHeader(uint8_t *header) {
    memset(header, 0, NX_NCA_HEADER_SIZE);
    memcpy(header + 0x200, "NCA3", 4);
    put32(header + 0x208, 12u * 0x200u);

    /* Section 0: RomFS, integrity hash tree, AES-CTR. */
    put32(header + 0x240, 6);
    put32(header + 0x244, 10);
    put16(header + 0x400, 2);
    header[0x402] = 0;
    header[0x403] = 3;
    header[0x404] = 3;
    header[0x405] = 0;

    /* Section 1: PartitionFS, SHA-256 hierarchy, plaintext. */
    put32(header + 0x250, 10);
    put32(header + 0x254, 12);
    put16(header + 0x600, 2);
    header[0x602] = 1;
    header[0x603] = 2;
    header[0x604] = 1;
    header[0x605] = 0;
}

static void testValidLayout(void) {
    uint8_t header[NX_NCA_HEADER_SIZE];
    buildHeader(header);

    NXNCASectionLayout layout;
    NXNCASectionInfo section;
    CHECK(NXNCAParseSectionTable(header, sizeof(header), 12u * 0x200u,
                                 &layout));
    CHECK(layout.sectionCount == 2);

    CHECK(NXNCAGetSectionInfo(&layout, 0, &section));
    CHECK(section.present);
    CHECK(section.index == 0);
    CHECK(section.byteOffset == 6u * 0x200u);
    CHECK(section.byteSize == 4u * 0x200u);
    CHECK(section.partitionType == 0);
    CHECK(section.fileSystemType == 3);
    CHECK(section.encryptionType == 3);

    CHECK(NXNCAGetSectionInfo(&layout, 1, &section));
    CHECK(section.present);
    CHECK(section.byteOffset == 10u * 0x200u);
    CHECK(section.byteSize == 2u * 0x200u);
    CHECK(section.partitionType == 1);
    CHECK(section.fileSystemType == 2);
    CHECK(section.encryptionType == 1);

    CHECK(NXNCAGetSectionInfo(&layout, 2, &section));
    CHECK(!section.present);
    CHECK(!NXNCAGetSectionInfo(&layout, 4, &section));
}

static void testMalformedLayouts(void) {
    uint8_t header[NX_NCA_HEADER_SIZE];
    NXNCASectionLayout layout;

    buildHeader(header);
    memcpy(header + 0x200, "BAD!", 4);
    CHECK(!NXNCAParseSectionTable(header, sizeof(header), 12u * 0x200u,
                                  &layout));

    buildHeader(header);
    put32(header + 0x208, 11u * 0x200u);
    CHECK(!NXNCAParseSectionTable(header, sizeof(header), 12u * 0x200u,
                                  &layout));

    buildHeader(header);
    put32(header + 0x244, 0); /* Only one half of this extent is set. */
    CHECK(!NXNCAParseSectionTable(header, sizeof(header), 12u * 0x200u,
                                  &layout));

    buildHeader(header);
    put32(header + 0x244, 13); /* Extends beyond the NCA file. */
    CHECK(!NXNCAParseSectionTable(header, sizeof(header), 12u * 0x200u,
                                  &layout));

    buildHeader(header);
    put32(header + 0x250, 9); /* Overlaps section zero. */
    CHECK(!NXNCAParseSectionTable(header, sizeof(header), 12u * 0x200u,
                                  &layout));

    buildHeader(header);
    put16(header + 0x600, 3); /* Unsupported filesystem-header version. */
    CHECK(!NXNCAParseSectionTable(header, sizeof(header), 12u * 0x200u,
                                  &layout));

    buildHeader(header);
    header[0x402] = 7; /* Unknown filesystem type. */
    CHECK(!NXNCAParseSectionTable(header, sizeof(header), 12u * 0x200u,
                                  &layout));

    buildHeader(header);
    CHECK(!NXNCAParseSectionTable(header, NX_NCA_HEADER_SIZE - 1,
                                  12u * 0x200u, &layout));
}

int main(void) {
    testValidLayout();
    testMalformedLayouts();

    if (failures != 0) {
        fprintf(stderr, "%d NCA section test(s) failed\\n", failures);
        return 1;
    }

    puts("NCA section extents and filesystem-header tests passed");
    return 0;
}
