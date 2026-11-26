#include "NXCPU.h"
#include "NXMachine.h"
#include "Arm64/mmu.h"
#include <stdint.h>
#include <string.h>

const char *NXCPUBackendName(void) {
    return "AArch64 interpreter (no JIT)";
}

int NXCPUSelfTest(void) {
    NXEngine engine;
    if (!NXEngineInit(&engine)) return 0;

    const uint64_t codeBase = 0x1000;
    const uint64_t dataBase = 0x2000;
    if (!NXEngineMapMemory(&engine, codeBase, 0x1000,
                           NX_MEMORY_READ | NX_MEMORY_WRITE | NX_MEMORY_EXECUTE) ||
        !NXEngineMapMemory(&engine, dataBase, 0x1000,
                           NX_MEMORY_READ | NX_MEMORY_WRITE)) {
        NXEngineDestroy(&engine);
        return 0;
    }

    /* MOVZ X0,#42; ADD X1,X0,#8; SUBS X2,X1,#50.
       Store X2 to data memory and load it back to exercise both bus paths. */
    const uint32_t program[] = {
        UINT32_C(0xD2800540),
        UINT32_C(0x91002001),
        UINT32_C(0xF100C422),
        UINT32_C(0xF9000002),
        UINT32_C(0xF9400003)
    };
    if (!NXEngineLoadBytes(&engine, codeBase, program, sizeof(program))) {
        NXEngineDestroy(&engine);
        return 0;
    }
    engine.cpu.x[4] = dataBase;
    engine.cpu.pc = codeBase;

    NXCPUResult runResult = NXEngineRun(&engine, 5);
    uint64_t stored = UINT64_MAX;
    bool ok = runResult == NX_CPU_STEP_LIMIT &&
              engine.cpu.x[0] == 42 &&
              engine.cpu.x[1] == 50 &&
              engine.cpu.x[2] == 0 &&
              engine.cpu.x[3] == 0 &&
              (engine.cpu.nzcv & PS_Z) != 0 &&
              mem_read(&engine.cpu, dataBase, 8, &stored) &&
              stored == 0;
    NXEngineDestroy(&engine);
    return ok ? 1 : 0;
}
