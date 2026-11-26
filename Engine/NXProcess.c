#include "NXProcess.h"

#include <string.h>

#define NX_PROCESS_PAGE_SIZE UINT64_C(0x1000)
#define NX_PROCESS_STACK_TOP UINT64_C(0x7FFF0000)

static bool alignUp(uint64_t value, uint64_t alignment, uint64_t *result) {
    if (!result || alignment == 0 ||
        value > UINT64_MAX - (alignment - 1u))
        return false;
    *result = (value + alignment - 1u) & ~(alignment - 1u);
    return true;
}

bool NXEnginePrepareMainThreadWithRegistry(
    NXEngine *engine,
    const uint8_t *npdmBytes, size_t npdmSize,
    const uint8_t *nsoBytes, size_t nsoSize,
    uint64_t loadBase, NXNSORegistry *registry,
    NXPreparedProcess *prepared) {
    if (!engine || !engine->initialized || !npdmBytes || !nsoBytes ||
        !prepared)
        return false;

    NXNPDMView npdm;
    if (!NXEngineOpenNPDM(npdmBytes, npdmSize, &npdm))
        return false;

    /* This initial runtime only supports 64-bit processes on guest CPU 0. */
    if ((npdm.mmuFlags & 1u) == 0 || npdm.defaultCPU != 0)
        return false;

    uint64_t stackSize = 0;
    if (!alignUp(npdm.mainStackSize, NX_PROCESS_PAGE_SIZE, &stackSize) ||
        stackSize == 0 || stackSize > NX_PROCESS_STACK_TOP)
        return false;

    const uint64_t stackBase = NX_PROCESS_STACK_TOP - stackSize;
    NXLoadedNSO mainImage;
    if (!NXEngineLoadNSOWithRegistry(
            engine, nsoBytes, nsoSize, loadBase, registry, &mainImage))
        return false;

    if (!NXEngineMapMemory(engine, stackBase, (size_t)stackSize,
                           NX_MEMORY_READ | NX_MEMORY_WRITE))
        return false;

    /* Enter the main image at EL0 with an empty, 16-byte-aligned stack. */
    engine->cpu.el = 0;
    engine->cpu.sp_sel = 0;
    engine->cpu.sp_el[0] = NX_PROCESS_STACK_TOP;
    engine->cpu.pc = mainImage.entryPoint;
    engine->cpu.stop = false;
    engine->cpu.halted = false;
    engine->machine.processExited = false;

    memset(prepared, 0, sizeof(*prepared));
    prepared->titleID = npdm.titleID;
    prepared->entryPoint = mainImage.entryPoint;
    prepared->stackBase = stackBase;
    prepared->stackSize = stackSize;
    prepared->mainThreadPriority = npdm.mainThreadPriority;
    prepared->defaultCPU = npdm.defaultCPU;
    prepared->mainImage = mainImage;
    return true;
}

bool NXEnginePrepareMainThread(NXEngine *engine,
                               const uint8_t *npdmBytes, size_t npdmSize,
                               const uint8_t *nsoBytes, size_t nsoSize,
                               uint64_t loadBase,
                               NXPreparedProcess *prepared) {
    return NXEnginePrepareMainThreadWithRegistry(
        engine, npdmBytes, npdmSize, nsoBytes, nsoSize,
        loadBase, NULL, prepared
    );
}
