#include "NXMachine.h"
#include "NXPFS0.h"
#include "Arm64/mmu.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static void testPFS0(void) {
    uint8_t archive[48] = {0};
    put32(archive, UINT32_C(0x30534650)); /* PFS0 */
    put32(archive + 4, 1);
    put32(archive + 8, 5);
    put64(archive + 16, 0); /* file data offset */
    put64(archive + 24, 3); /* file size */
    put32(archive + 32, 0); /* name offset */
    memcpy(archive + 40, "main", 5);
    memcpy(archive + 45, "abc", 3);

    NXPFS0View view;
    NXPFS0File file;
    CHECK(NXEngineOpenPFS0(archive, sizeof(archive), &view));
    CHECK(view.fileCount == 1);
    CHECK(NXEnginePFS0GetFile(&view, "main", &file));
    CHECK(file.size == 3);
    CHECK(memcmp(file.bytes, "abc", 3) == 0);
    CHECK(!NXEnginePFS0GetFile(&view, "missing", &file));

    uint8_t badMagic[16] = {0};
    CHECK(!NXEngineOpenPFS0(badMagic, sizeof(badMagic), &view));

    uint8_t badOffset[48];
    memcpy(badOffset, archive, sizeof(archive));
    put64(badOffset + 16, UINT64_MAX);
    CHECK(!NXEngineOpenPFS0(badOffset, sizeof(badOffset), &view));

    uint8_t unterminated[48];
    memcpy(unterminated, archive, sizeof(archive));
    memset(unterminated + 40, 'x', 5);
    CHECK(!NXEngineOpenPFS0(unterminated, sizeof(unterminated), &view));

    uint8_t emptyArchive[16] = {0};
    put32(emptyArchive, UINT32_C(0x30534650));
    CHECK(NXEngineOpenPFS0(emptyArchive, sizeof(emptyArchive), &view));
    CHECK(view.fileCount == 0);
}

static void testGuestMemory(void) {
    NXEngine engine;
    CHECK(NXEngineInit(&engine));
    if (!engine.initialized) return;

    CHECK(NXEngineMapMemory(&engine, UINT64_C(0x100000), 0x1000,
                            NX_MEMORY_READ | NX_MEMORY_WRITE | NX_MEMORY_EXECUTE));
    CHECK(!NXEngineMapMemory(&engine, UINT64_C(0x100800), 0x1000,
                             NX_MEMORY_READ | NX_MEMORY_WRITE));
    CHECK(NXEngineMapMemory(&engine, UINT64_C(0x101000), 0x1000,
                            NX_MEMORY_READ | NX_MEMORY_WRITE));

    const uint32_t instruction = UINT32_C(0xD503201F); /* NOP */
    CHECK(NXEngineLoadBytes(&engine, UINT64_C(0x100000),
                            &instruction, sizeof(instruction)));
    uint64_t value = 0;
    CHECK(mem_read(&engine.cpu, UINT64_C(0x100000), 4, &value));
    CHECK((uint32_t)value == instruction);

    uint32_t fetched = 0;
    CHECK(mem_ifetch(&engine.cpu, UINT64_C(0x100000), &fetched));
    CHECK(fetched == instruction);

    CHECK(!mem_ifetch(&engine.cpu, UINT64_C(0x101000), &fetched));
    CHECK(engine.machine.pendingException.valid);
    NXEngineClearException(&engine);
    CHECK(!engine.machine.pendingException.valid);

    NXEngineDestroy(&engine);
}

int main(void) {
    testPFS0();
    testGuestMemory();
    if (failures) {
        fprintf(stderr, "%d engine format/memory test(s) failed\\n", failures);
        return 1;
    }
    puts("PFS0 parser and guest-memory tests passed");
    return 0;
}
