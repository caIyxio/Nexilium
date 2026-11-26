#include "NXNPDM.h"

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
    image[0x0E]=0x20;
    image[0x0F]=0;
    put32(image+0x18,1);
    put32(image+0x1C,0x4000);
    put32(image+0x70,0x280);
    put32(image+0x74,0x80);
    put32(image+0x78,0x200);
    put32(image+0x7C,0x80);

    put32(image+0x200+0x200,UINT32_C(0x44494341));
    put32(image+0x200+0x204,0x38);
    put32(image+0x200+0x20C,1);
    put64(image+0x200+0x210,0x0100000000001000);
    put64(image+0x200+0x218,0x0100000000001000);

    put32(image+0x280,UINT32_C(0x30494341));
    put64(image+0x280+0x10,0x0100000000001000);
    put32(image+0x280+0x28,0x30);
    put32(image+0x280+0x2C,0);
    put32(image+0x280+0x30,0x30);
    put32(image+0x280+0x34,0);
}

static void testValidNPDM(void) {
    uint8_t image[0x300];
    buildNPDM(image,sizeof(image));
    NXNPDMView view;
    CHECK(NXEngineOpenNPDM(image,sizeof(image),&view));
    CHECK(view.titleID==UINT64_C(0x0100000000001000));
    CHECK(view.mainStackSize==0x4000);
    CHECK(view.mainThreadPriority==0x20);
    CHECK(view.isRetail==1);
}

static void testRejectsMalformedNPDM(void) {
    uint8_t image[0x300];
    NXNPDMView view;
    buildNPDM(image,sizeof(image));
    put32(image,0);
    CHECK(!NXEngineOpenNPDM(image,sizeof(image),&view));

    buildNPDM(image,sizeof(image));
    put32(image+0x70,0xFFFFFFF0);
    CHECK(!NXEngineOpenNPDM(image,sizeof(image),&view));

    buildNPDM(image,sizeof(image));
    put32(image+0x200+0x200,0);
    CHECK(!NXEngineOpenNPDM(image,sizeof(image),&view));

    buildNPDM(image,sizeof(image));
    put64(image+0x200+0x210,UINT64_MAX);
    CHECK(!NXEngineOpenNPDM(image,sizeof(image),&view));

    buildNPDM(image,sizeof(image));
    put32(image+0x1C,0);
    CHECK(!NXEngineOpenNPDM(image,sizeof(image),&view));

    buildNPDM(image,sizeof(image));
    put32(image+0x280+0x30,0xFFFFFFF0);
    CHECK(!NXEngineOpenNPDM(image,sizeof(image),&view));
}

int main(void) {
    testValidNPDM();
    testRejectsMalformedNPDM();
    if (failures) {
        fprintf(stderr,"%d NPDM test(s) failed\n",failures);
        return 1;
    }
    puts("NPDM metadata validation tests passed");
    return 0;
}
