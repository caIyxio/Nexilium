#include "NXCPU.h"
#include "NXMachine.h"
#include "NXNSO.h"
#include "NXPFS0.h"
#include "NXHorizon.h"
#include "Arm64/mmu.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

const char *NXCPUBackendName(void) {
    return "AArch64 interpreter (no JIT)";
}

int NXCPUSelfTest(void) {
    NXEngine engine;
    if (!NXEngineInit(&engine)) {
        fprintf(stderr, "CPU self-test: initial engine allocation failed\\n");
        return 0;
    }

    const uint64_t codeBase = 0x1000;
    const uint64_t dataBase = 0x2000;
    if (!NXEngineMapMemory(&engine, codeBase, 0x1000,
                           NX_MEMORY_READ | NX_MEMORY_WRITE | NX_MEMORY_EXECUTE) ||
        !NXEngineMapMemory(&engine, dataBase, 0x1000,
                           NX_MEMORY_READ | NX_MEMORY_WRITE)) {
        fprintf(stderr, "CPU self-test: base memory map failed\\n");
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
        fprintf(stderr, "CPU self-test: program copy failed\\n");
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
    if (!ok) return 0;

    /* Exercise the real Horizon dispatch path: SetHeapSize then GetSystemTick. */
    NXEngine svcEngine;
    if (!NXEngineInit(&svcEngine)) {
        fprintf(stderr, "Horizon self-test: engine allocation failed\\n");
        return 0;
    }
    const uint64_t svcCodeBase = 0x4000;
    if (!NXEngineMapMemory(&svcEngine, svcCodeBase, 0x1000,
                           NX_MEMORY_READ | NX_MEMORY_WRITE | NX_MEMORY_EXECUTE)) {
        fprintf(stderr, "Horizon self-test: code memory map failed\\n");
        NXEngineDestroy(&svcEngine);
        return 0;
    }
    const uint32_t svcProgram[] = {
        UINT32_C(0xD4000021), /* SVC #0x01: SetHeapSize */
        UINT32_C(0xD40003C1)  /* SVC #0x1E: GetSystemTick */
    };
    if (!NXEngineLoadBytes(&svcEngine, svcCodeBase, svcProgram, sizeof(svcProgram))) {
        fprintf(stderr, "Horizon self-test: program copy failed\\n");
        NXEngineDestroy(&svcEngine);
        return 0;
    }
    svcEngine.cpu.x[1] = UINT64_C(0x200000);
    svcEngine.cpu.pc = svcCodeBase;
    NXCPUResult svcResult = NXEngineRun(&svcEngine, 2);
    bool svcOK = svcResult == NX_CPU_STEP_LIMIT &&
                 svcEngine.cpu.x[0] > 0 &&
                 svcEngine.cpu.x[1] == UINT64_C(0x80000000) &&
                 svcEngine.machine.heapSize == UINT64_C(0x200000) &&
                 !svcEngine.machine.pendingException.valid;

    /* Validate SVC port connection + a CMIF RegisterClient round-trip on sm:. */
    const char smPortName[] = "sm:";
    if (!NXEngineLoadBytes(&svcEngine, svcCodeBase + 0x100, smPortName, sizeof(smPortName))) {
        NXEngineDestroy(&svcEngine);
        return 0;
    }
    uint32_t smHandle = 0;
    uint32_t connectResult = NXHorizonConnectToNamedPort(
        &svcEngine, svcCodeBase + 0x100, &smHandle
    );
    uint8_t *hipc = (uint8_t *)mem_host_ptr(
        &svcEngine.cpu, NX_TLS_BASE + NX_HIPC_TLS_OFFSET, 0x100,
        ACC_READ | ACC_WRITE
    );
    bool ipcOK = hipc != NULL && connectResult == 0 && smHandle != 0;
    if (ipcOK) {
        memset(hipc, 0, 0x100);
        uint32_t header0 = 4;
        uint32_t header1 = 4;
        memcpy(hipc, &header0, sizeof(header0));
        memcpy(hipc + 4, &header1, sizeof(header1));
        const uint32_t cmifRequest[] = {
            UINT32_C(0x49434653), 0, 0, 0 /* SFCI RegisterClient */
        };
        memcpy(hipc + 0x10, cmifRequest, sizeof(cmifRequest));
        uint32_t ipcResult = NXHorizonSendSyncRequest(&svcEngine, smHandle);
        uint32_t responseMagic = 0, responseResult = UINT32_MAX;
        memcpy(&responseMagic, hipc + 0x10, sizeof(responseMagic));
        memcpy(&responseResult, hipc + 0x18, sizeof(responseResult));
        ipcOK = ipcResult == 0 &&
                responseMagic == UINT32_C(0x4F434653) &&
                responseResult == 0;
    }

    /* sm:GetServiceHandle("set:sys") must return a moved service handle. */
    bool serviceOK = hipc != NULL;
    uint32_t settingsHandle = 0;
    if (serviceOK) {
        memset(hipc, 0, 0x100);
        uint32_t header0 = 4;
        uint32_t header1 = 6;
        memcpy(hipc, &header0, sizeof(header0));
        memcpy(hipc + 4, &header1, sizeof(header1));
        const uint32_t getServiceRequest[] = {
            UINT32_C(0x49434653), 0, 1, 0
        };
        memcpy(hipc + 0x10, getServiceRequest, sizeof(getServiceRequest));
        memcpy(hipc + 0x20, "set:sys", 7);

        uint32_t getServiceResult = NXHorizonSendSyncRequest(&svcEngine, smHandle);
        uint32_t responseMagic = 0, responseResult = UINT32_MAX;
        memcpy(&responseMagic, hipc + 0x10, sizeof(responseMagic));
        memcpy(&responseResult, hipc + 0x18, sizeof(responseResult));
        memcpy(&settingsHandle, hipc + 0x0C, sizeof(settingsHandle));
        serviceOK = getServiceResult == 0 &&
                    responseMagic == UINT32_C(0x4F434653) &&
                    responseResult == 0 && settingsHandle != 0;
    }

    /* Unknown service names must be rejected without creating a handle. */
    if (serviceOK) {
        memset(hipc, 0, 0x100);
        uint32_t header0 = 4;
        uint32_t header1 = 6;
        memcpy(hipc, &header0, sizeof(header0));
        memcpy(hipc + 4, &header1, sizeof(header1));
        const uint32_t getServiceRequest[] = {
            UINT32_C(0x49434653), 0, 1, 0
        };
        memcpy(hipc + 0x10, getServiceRequest, sizeof(getServiceRequest));
        memcpy(hipc + 0x20, "unknown", 7);
        uint32_t unknownResult = NXHorizonSendSyncRequest(&svcEngine, smHandle);
        uint32_t responseResult = UINT32_MAX;
        memcpy(&responseResult, hipc + 0x18, sizeof(responseResult));
        serviceOK = unknownResult == UINT32_C(0x0000F601) &&
                    responseResult == UINT32_C(0x0000F601);
    }

    NXHorizonCloseHandle(&svcEngine, settingsHandle);
    NXHorizonCloseHandle(&svcEngine, smHandle);
    if (!svcOK || !ipcOK || !serviceOK) {
        fprintf(stderr, "Horizon self-test failed: svc=%d ipc=%d service=%d result=%d handle=%u\\n",
                svcOK, ipcOK, serviceOK, (int)svcResult, smHandle);
    }
    NXEngineDestroy(&svcEngine);
    if (!svcOK || !ipcOK || !serviceOK) return 0;

    /* Validate PFS0 directory bounds, names, and file data views. */
    uint8_t pfs[0x36] = {0};
    pfs[0] = 'P'; pfs[1] = 'F'; pfs[2] = 'S'; pfs[3] = '0';
    pfs[4] = 1; /* One file. */
    pfs[8] = 9; /* "test.bin" plus NUL. */
    pfs[0x10 + 8] = 5; /* File size = 5 bytes. */
    memcpy(pfs + 0x28, "test.bin", 9);
    memcpy(pfs + 0x31, "hello", 5);
    NXPFS0View pfsView;
    NXPFS0File pfsFile;
    bool pfsOpened = NXEngineOpenPFS0(pfs, sizeof(pfs), &pfsView);
    bool pfsFound = pfsOpened &&
                    NXEnginePFS0GetFile(&pfsView, "test.bin", &pfsFile);
    bool pfsOK = pfsFound && pfsFile.size == 5 &&
                 memcmp(pfsFile.bytes, "hello", 5) == 0 &&
                 !NXEnginePFS0GetFile(&pfsView, "missing.nso", &pfsFile);
    if (!pfsOK) {
        fprintf(stderr, "PFS0 self-test failed: opened=%d found=%d\\n",
                pfsOpened, pfsFound);
        return 0;
    }

    /* Load a compressed NSO text segment (one MOVZ instruction) and execute it. */
    NXEngine nsoEngine;
    if (!NXEngineInit(&nsoEngine)) {
        fprintf(stderr, "NSO self-test: engine allocation failed\\n");
        return 0;
    }
    uint8_t nso[0x105] = {0};
    nso[0] = 'N'; nso[1] = 'S'; nso[2] = 'O'; nso[3] = '0';
    nso[0x0C] = 1; /* .text is LZ4-compressed. */
    nso[0x10] = 0x00; nso[0x11] = 0x01; /* TextFileOffset = 0x100. */
    nso[0x18] = 0x04;                   /* TextMemorySize = 4. */
    nso[0x60] = 0x05;                   /* Compressed text size = 5. */
    nso[0x100] = 0x40;                  /* LZ4 token: four literal bytes. */
    const uint8_t movz[] = {0x40, 0x05, 0x80, 0xD2}; /* MOVZ X0,#42 (LE). */
    memcpy(nso + 0x101, movz, sizeof(movz));
    NXLoadedNSO loaded;
    bool nsoLoaded = NXEngineLoadNSO(&nsoEngine, nso, sizeof(nso),
                                     UINT64_C(0x7100000000), &loaded);
    NXCPUResult nsoRun = nsoLoaded ? NXEngineRun(&nsoEngine, 1) : NX_CPU_INVALID_ARGUMENT;
    bool nsoOK = nsoLoaded &&
                 nsoRun == NX_CPU_STEP_LIMIT &&
                 loaded.entryPoint == UINT64_C(0x7100000000) &&
                 nsoEngine.cpu.x[0] == 42;
    if (!nsoOK) {
        fprintf(stderr,
                "NSO self-test failed: loaded=%d result=%d pc=%llx x0=%llx entry=%llx imageSize=%llu\\n",
                nsoLoaded, (int)nsoRun,
                (unsigned long long)nsoEngine.cpu.pc,
                (unsigned long long)nsoEngine.cpu.x[0],
                (unsigned long long)(nsoLoaded ? loaded.entryPoint : 0),
                (unsigned long long)(nsoLoaded ? loaded.imageSize : 0));
    }
    NXEngineDestroy(&nsoEngine);
    return nsoOK ? 1 : 0;
}
