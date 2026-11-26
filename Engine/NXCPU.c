#include "NXCPU.h"
#include "NXMachine.h"
#include "Arm64/mmu.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

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
        UINT32_C(0xF100C822),
        UINT32_C(0xF9000082),
        UINT32_C(0xF9400083)
    };
    if (!NXEngineLoadBytes(&engine, codeBase, program, sizeof(program))) {
        NXEngineDestroy(&engine);
        return 0;
    }
    engine.cpu.x[4] = dataBase;
    engine.cpu.pc = codeBase;

    NXCPUResult runResult = NXEngineRun(&engine, 5);
    uint64_t stored = UINT64_MAX;
    bool memoryReadOK = mem_read(&engine.cpu, dataBase, 8, &stored);
    bool ok = runResult == NX_CPU_STEP_LIMIT &&
              engine.cpu.x[0] == 42 &&
              engine.cpu.x[1] == 50 &&
              engine.cpu.x[2] == 0 &&
              engine.cpu.x[3] == 0 &&
              (engine.cpu.nzcv & PS_Z) != 0 &&
              memoryReadOK && stored == 0;
    if (!ok) {
        fprintf(stderr,
                "CPU self-test detail: result=%d pc=%llx x0=%llx x1=%llx x2=%llx x3=%llx x4=%llx NZCV=%08x memOK=%d stored=%llx exception=%d\\n",
                (int)runResult, (unsigned long long)engine.cpu.pc,
                (unsigned long long)engine.cpu.x[0],
                (unsigned long long)engine.cpu.x[1],
                (unsigned long long)engine.cpu.x[2],
                (unsigned long long)engine.cpu.x[3],
                (unsigned long long)engine.cpu.x[4],
                engine.cpu.nzcv, memoryReadOK,
                (unsigned long long)stored,
                engine.machine.pendingException.valid);
    }
    NXEngineDestroy(&engine);
    return ok ? 1 : 0;
}
