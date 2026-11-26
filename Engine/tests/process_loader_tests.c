#include "NXProcess.h"

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
    testPrepareMainThread();
    testRejectUnsupportedProcess();
    if (failures) {
        fprintf(stderr,"%d process-loader test(s) failed\n",failures);
        return 1;
    }
    puts("PFS0 ExeFS + NPDM + NSO main-thread preparation tests passed");
    return 0;
}
