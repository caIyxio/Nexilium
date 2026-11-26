#include "NXProcess.h"
#include "NXPFS0.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
    ++failures; \
} } while (0)

static void put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p,(uint32_t)v); put32(p+4,(uint32_t)(v>>32));
}

static void buildNPDM(uint8_t *image, size_t size) {
    memset(image,0,size);
    put32(image,UINT32_C(0x4154454D));
    image[0x0C]=1; /* 64-bit address space. */
    image[0x0E]=0x20;
    image[0x0F]=0;
    put32(image+0x18,1);
    put32(image+0x1C,0x4000);
    put32(image+0x70,0x2C0);
    put32(image+0x74,0x80);
    put32(image+0x78,0x80);
    put32(image+0x7C,0x240);

    put32(image+0x80+0x200,UINT32_C(0x44494341));
    put32(image+0x80+0x204,0x40);
    put64(image+0x80+0x210,0x0100000000001000);
    put64(image+0x80+0x218,0x0100000000001000);

    put32(image+0x2C0,UINT32_C(0x30494341));
    put64(image+0x2C0+0x10,0x0100000000001000);
    put32(image+0x2C0+0x28,0x40);
    put32(image+0x2C0+0x30,0x40);
}

static void buildNSO(uint8_t *nso, size_t size) {
    memset(nso,0,size);
    memcpy(nso,"NSO0",4);
    put32(nso+0x0C,1);       /* Compressed text segment. */
    put32(nso+0x10,0x100);   /* Text file offset. */
    put32(nso+0x18,4);       /* Text memory size. */
    put32(nso+0x60,5);       /* LZ4 compressed size. */
    nso[0x100]=0x40;         /* Four literal bytes. */
    const uint8_t movz[]={0x40,0x05,0x80,0xD2}; /* MOVZ X0,#42. */
    memcpy(nso+0x101,movz,sizeof(movz));
}

static void buildExeFSPFS0(uint8_t *pfs, size_t size,
                            const uint8_t *npdm, size_t npdmSize,
                            const uint8_t *nso, size_t nsoSize) {
    memset(pfs,0,size);
    memcpy(pfs,"PFS0",4);
    put32(pfs+4,2);
    put32(pfs+8,15);
    const size_t dataOffset=0x10+2*0x18+15;
    put64(pfs+0x10,0);
    put64(pfs+0x18,npdmSize);
    put32(pfs+0x20,0);
    put64(pfs+0x28,npdmSize);
    put64(pfs+0x30,nsoSize);
    put32(pfs+0x38,10);
    memcpy(pfs+0x40,"main.npdm",10);
    memcpy(pfs+0x4A,"main",5);
    memcpy(pfs+dataOffset,npdm,npdmSize);
    memcpy(pfs+dataOffset+npdmSize,nso,nsoSize);
}

static bool openProgramFiles(const uint8_t *pfs, size_t pfsSize,
                             NXPFS0File *npdmFile, NXPFS0File *mainFile) {
    NXPFS0View view;
    return NXEngineOpenPFS0(pfs,pfsSize,&view) &&
           NXEnginePFS0GetFile(&view,"main.npdm",npdmFile) &&
           NXEnginePFS0GetFile(&view,"main",mainFile);
}

static void buildRelocatableNSO(uint8_t *nso, size_t size,
                                  uint32_t relocationType) {
    memset(nso,0,size);
    memcpy(nso,"NSO0",4);
    put32(nso+0x10,0x100);
    put32(nso+0x14,0);
    put32(nso+0x18,0x200);
    uint8_t *text=nso+0x100;
    put32(text+4,0x80); /* Relative offset to MOD0. */

    put32(text+0x80,UINT32_C(0x30444F4D));
    put32(text+0x84,0x40); /* .dynamic is MOD0 + 0x40. */

    put64(text+0xC0,7);  put64(text+0xC8,0x120); /* DT_RELA */
    put64(text+0xD0,8);  put64(text+0xD8,24);   /* DT_RELASZ */
    put64(text+0xE0,9);  put64(text+0xE8,24);   /* DT_RELAENT */
    put64(text+0xF0,0);  put64(text+0xF8,0);    /* DT_NULL */

    put64(text+0x120,0x150); /* r_offset */
    put64(text+0x128,relocationType); /* symbol index 0, relocation type */
    put64(text+0x130,0x1234); /* r_addend */
}

static void testMOD0RelativeRelocation(void) {
    uint8_t nso[0x300];
    buildRelocatableNSO(nso,sizeof(nso),1027u);

    NXEngine engine;
    CHECK(NXEngineInit(&engine));
    NXLoadedNSO loaded;
    const uint64_t loadBase=UINT64_C(0x7200000000);
    CHECK(NXEngineLoadNSO(&engine,nso,sizeof(nso),loadBase,&loaded));
    CHECK(loaded.hasModuleHeader);
    CHECK(loaded.moduleHeaderAddress==loadBase+0x80);
    CHECK(loaded.dynamicAddress==loadBase+0xC0);
    CHECK(loaded.relativeRelocationCount==1);
    CHECK(loaded.unsupportedRelocationCount==0);

    uint64_t relocated=0;
    CHECK(NXEngineReadBytes(
        &engine,loadBase+0x150,&relocated,sizeof(relocated)
    ));
    CHECK(relocated==loadBase+0x1234);
    NXEngineDestroy(&engine);
}

static void buildRelrNSO(uint8_t *nso, size_t size) {
    memset(nso,0,size);
    memcpy(nso,"NSO0",4);
    put32(nso+0x10,0x100);
    put32(nso+0x14,0);
    put32(nso+0x18,0x200);
    uint8_t *text=nso+0x100;
    put32(text+4,0x80);
    put32(text+0x80,UINT32_C(0x30444F4D));
    put32(text+0x84,0x40);

    put64(text+0xC0,36); put64(text+0xC8,0x120); /* DT_RELR */
    put64(text+0xD0,35); put64(text+0xD8,8);     /* DT_RELRSZ */
    put64(text+0xE0,37); put64(text+0xE8,8);     /* DT_RELRENT */
    put64(text+0xF0,0);  put64(text+0xF8,0);     /* DT_NULL */

    put64(text+0x120,0x150); /* Direct RELR offset. */
    put64(text+0x150,0x1234); /* Implicit addend. */
}

static void buildSymbolRelocatableNSO(uint8_t *nso, size_t size) {
    memset(nso,0,size);
    memcpy(nso,"NSO0",4);

    put32(nso+0x10,0x100); /* Text file offset. */
    put32(nso+0x14,0);     /* Text memory offset. */
    put32(nso+0x18,0x200); /* Text memory/file size. */
    put32(nso+0x20,0x300); /* Rodata file offset. */
    put32(nso+0x24,0x200); /* Rodata memory offset. */
    put32(nso+0x28,0x100); /* Rodata memory/file size. */

    uint8_t *text=nso+0x100;
    put32(text+4,0x80);
    put32(text+0x80,UINT32_C(0x30444F4D));
    put32(text+0x84,0x40);

    put64(text+0xC0,4);  put64(text+0xC8,0x220); /* DT_HASH */
    put64(text+0xD0,6);  put64(text+0xD8,0x240); /* DT_SYMTAB */
    put64(text+0xE0,11); put64(text+0xE8,24);    /* DT_SYMENT */
    put64(text+0xF0,5);  put64(text+0xF8,0x270); /* DT_STRTAB */
    put64(text+0x100,10); put64(text+0x108,5);   /* DT_STRSZ */
    put64(text+0x110,7); put64(text+0x118,0x160); /* DT_RELA */
    put64(text+0x120,8); put64(text+0x128,24);   /* DT_RELASZ */
    put64(text+0x130,9); put64(text+0x138,24);   /* DT_RELAENT */
    put64(text+0x140,0); put64(text+0x148,0);    /* DT_NULL */

    put64(text+0x160,0x1C0); /* r_offset */
    put64(text+0x168,(UINT64_C(1)<<32)|257u); /* symbol 1, R_AARCH64_ABS64 */
    put64(text+0x170,0x10); /* r_addend */

    uint8_t *rodata=nso+0x300;
    put32(rodata+0x20,1); /* SysV hash bucket count. */
    put32(rodata+0x24,2); /* SysV hash chain count = two symbols. */
    uint8_t *symbol=rodata+0x40+24; /* Symbol index 1. */
    put32(symbol,1); /* st_name -> "foo". */
    symbol[4]=0x10; /* STB_GLOBAL. */
    symbol[6]=1; /* st_shndx: defined in this module. */
    symbol[7]=0;
    put64(symbol+8,0x180); /* st_value relative to module base. */
    memcpy(rodata+0x70,"\0foo\0",5); /* Dynamic string table. */
}

static void testMOD0LocalSymbolRelocation(void) {
    uint8_t nso[0x400];
    buildSymbolRelocatableNSO(nso,sizeof(nso));

    NXEngine engine;
    CHECK(NXEngineInit(&engine));
    NXLoadedNSO loaded;
    const uint64_t loadBase=UINT64_C(0x7500000000);
    CHECK(NXEngineLoadNSO(&engine,nso,sizeof(nso),loadBase,&loaded));
    CHECK(loaded.hasModuleHeader);
    CHECK(loaded.relativeRelocationCount==0);
    CHECK(loaded.symbolRelocationCount==1);
    CHECK(loaded.unsupportedRelocationCount==0);

    uint64_t relocated=0;
    CHECK(NXEngineReadBytes(
        &engine,loadBase+0x1C0,&relocated,sizeof(relocated)
    ));
    CHECK(relocated==loadBase+0x190);
    NXEngineDestroy(&engine);
}

static void testCrossModuleSymbolResolution(void) {
    uint8_t providerNSO[0x400], consumerNSO[0x400];
    buildSymbolRelocatableNSO(providerNSO,sizeof(providerNSO));
    buildSymbolRelocatableNSO(consumerNSO,sizeof(consumerNSO));

    /* The consumer's symbol is undefined; it must resolve from the provider. */
    uint8_t *consumerSymbol = consumerNSO + 0x300 + 0x40 + 24;
    consumerSymbol[6] = 0;
    put64(consumerSymbol + 8,0);

    NXEngine engine;
    CHECK(NXEngineInit(&engine));
    NXNSORegistry registry;
    NXNSORegistryInit(&registry);

    NXLoadedNSO provider, consumer;
    const uint64_t providerBase=UINT64_C(0x7600000000);
    const uint64_t consumerBase=UINT64_C(0x7610000000);
    CHECK(NXEngineLoadNSOWithRegistry(
        &engine,providerNSO,sizeof(providerNSO),providerBase,
        &registry,&provider
    ));
    CHECK(registry.count==1);
    CHECK(NXEngineLoadNSOWithRegistry(
        &engine,consumerNSO,sizeof(consumerNSO),consumerBase,
        &registry,&consumer
    ));
    CHECK(registry.count==2);
    CHECK(consumer.symbolRelocationCount==1);
    CHECK(consumer.unsupportedRelocationCount==0);

    uint64_t relocated=0;
    CHECK(NXEngineReadBytes(
        &engine,consumerBase+0x1C0,&relocated,sizeof(relocated)
    ));
    CHECK(relocated==providerBase+0x190);
    NXEngineDestroy(&engine);
}

static void testMOD0RELRRelativeRelocation(void) {
    uint8_t nso[0x300];
    buildRelrNSO(nso,sizeof(nso));

    NXEngine engine;
    CHECK(NXEngineInit(&engine));
    NXLoadedNSO loaded;
    const uint64_t loadBase=UINT64_C(0x7400000000);
    CHECK(NXEngineLoadNSO(&engine,nso,sizeof(nso),loadBase,&loaded));
    CHECK(loaded.hasModuleHeader);
    CHECK(loaded.relativeRelocationCount==1);
    CHECK(loaded.unsupportedRelocationCount==0);

    uint64_t relocated=0;
    CHECK(NXEngineReadBytes(
        &engine,loadBase+0x150,&relocated,sizeof(relocated)
    ));
    CHECK(relocated==loadBase+0x1234);
    NXEngineDestroy(&engine);
}

static void testUnsupportedMOD0RelocationIsReported(void) {
    uint8_t nso[0x300];
    buildRelocatableNSO(nso,sizeof(nso),257u);
    /* A nonzero symbol index with no DT_SYMTAB is an unresolved import. */
    put64(nso+0x100+0x128,(UINT64_C(1)<<32)|257u);

    NXEngine engine;
    CHECK(NXEngineInit(&engine));
    NXLoadedNSO loaded;
    CHECK(NXEngineLoadNSO(
        &engine,nso,sizeof(nso),UINT64_C(0x7300000000),&loaded
    ));
    CHECK(loaded.hasModuleHeader);
    CHECK(loaded.relativeRelocationCount==0);
    CHECK(loaded.unsupportedRelocationCount==1);
    NXEngineDestroy(&engine);
}

static void testPrepareMainThread(void) {
    uint8_t npdm[0x400];
    uint8_t nso[0x105];
    uint8_t pfs[0x600];
    NXPFS0File npdmFile, mainFile;
    buildNPDM(npdm,sizeof(npdm));
    buildNSO(nso,sizeof(nso));
    buildExeFSPFS0(pfs,sizeof(pfs),npdm,sizeof(npdm),nso,sizeof(nso));
    CHECK(openProgramFiles(pfs,sizeof(pfs),&npdmFile,&mainFile));

    NXEngine engine;
    CHECK(NXEngineInit(&engine));
    NXPreparedProcess prepared;
    CHECK(NXEnginePrepareMainThread(
        &engine,npdmFile.bytes,npdmFile.size,mainFile.bytes,mainFile.size,
        UINT64_C(0x7100000000),&prepared
    ));
    CHECK(prepared.titleID==UINT64_C(0x0100000000001000));
    CHECK(prepared.entryPoint==UINT64_C(0x7100000000));
    CHECK(prepared.stackSize==0x4000);
    CHECK(engine.cpu.pc==prepared.entryPoint);
    CHECK(engine.cpu.sp_el[0]==UINT64_C(0x7FFF0000));
    CHECK(engine.cpu.el==0);

    NXCPUResult run=NXEngineRun(&engine,1);
    CHECK(run==NX_CPU_STEP_LIMIT);
    CHECK(engine.cpu.x[0]==42);
    NXEngineDestroy(&engine);
}

static void testRejectUnsupportedProcess(void) {
    uint8_t npdm[0x400];
    uint8_t nso[0x105];
    uint8_t pfs[0x600];
    NXPFS0File npdmFile, mainFile;
    buildNPDM(npdm,sizeof(npdm));
    buildNSO(nso,sizeof(nso));
    npdm[0x0F]=1; /* Unsupported secondary core. */
    buildExeFSPFS0(pfs,sizeof(pfs),npdm,sizeof(npdm),nso,sizeof(nso));
    CHECK(openProgramFiles(pfs,sizeof(pfs),&npdmFile,&mainFile));

    NXEngine engine;
    CHECK(NXEngineInit(&engine));
    NXPreparedProcess prepared;
    CHECK(!NXEnginePrepareMainThread(
        &engine,npdmFile.bytes,npdmFile.size,mainFile.bytes,mainFile.size,
        UINT64_C(0x7100000000),&prepared
    ));
    NXEngineDestroy(&engine);
}

int main(void) {
    testMOD0RelativeRelocation();
    testMOD0LocalSymbolRelocation();
    testCrossModuleSymbolResolution();
    testMOD0RELRRelativeRelocation();
    testUnsupportedMOD0RelocationIsReported();
    testPrepareMainThread();
    testRejectUnsupportedProcess();
    if (failures) {
        fprintf(stderr,"%d process-loader test(s) failed\n",failures);
        return 1;
    }
    puts("PFS0 ExeFS + NPDM + NSO main-thread preparation tests passed");
    return 0;
}
