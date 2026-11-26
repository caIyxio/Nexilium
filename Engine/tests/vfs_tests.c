#include "NXVFS.h"

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

static size_t buildPFS0(uint8_t *image, size_t capacity) {
    const char names[] = "main.npdm\0main\0";
    const uint8_t npdm[] = {0x4D,0x45,0x54,0x41};
    const uint8_t nso[] = {0x4E,0x53,0x4F,0x30,0xAA};
    const size_t dataOffset = 0x10 + 2 * 0x18 + sizeof(names) - 1;
    const size_t totalSize = dataOffset + sizeof(npdm) + sizeof(nso);
    if (capacity < totalSize) return 0;

    memset(image,0,totalSize);
    memcpy(image,"PFS0",4);
    put32(image+4,2);
    put32(image+8,sizeof(names)-1);
    put64(image+0x10,0);
    put64(image+0x18,sizeof(npdm));
    put32(image+0x20,0);
    put64(image+0x28,sizeof(npdm));
    put64(image+0x30,sizeof(nso));
    put32(image+0x38,10);
    memcpy(image+0x40,names,sizeof(names)-1);
    memcpy(image+dataOffset,npdm,sizeof(npdm));
    memcpy(image+dataOffset+sizeof(npdm),nso,sizeof(nso));
    return totalSize;
}

static void testPFS0MountAndRead(void) {
    uint8_t image[0x100];
    size_t imageSize=buildPFS0(image,sizeof(image));
    NXVFS vfs;
    NXVFSInit(&vfs);
    CHECK(imageSize != 0);
    CHECK(NXVFSMountPFS0(&vfs,"exefs",image,imageSize));
    CHECK(vfs.mountCount==1);
    CHECK(!NXVFSMountPFS0(&vfs,"exefs",image,imageSize));
    CHECK(!NXVFSMountPFS0(&vfs,"bad/name",image,imageSize));

    NXVFSFile file;
    CHECK(NXVFSOpenFile(&vfs,"exefs","main.npdm",&file));
    CHECK(file.size==4);
    CHECK(memcmp(file.bytes,"META",4)==0);

    uint8_t output[8]={0};
    size_t bytesRead=0;
    CHECK(NXVFSReadFile(&file,1,output,8,&bytesRead));
    CHECK(bytesRead==3);
    CHECK(memcmp(output,"ETA",3)==0);
    CHECK(NXVFSReadFile(&file,4,output,4,&bytesRead));
    CHECK(bytesRead==0);
    CHECK(!NXVFSReadFile(&file,5,output,1,&bytesRead));
    CHECK(!NXVFSOpenFile(&vfs,"missing","main",&file));
}

static void testRejectMalformedMount(void) {
    uint8_t bad[16]={0};
    NXVFS vfs;
    NXVFSInit(&vfs);
    CHECK(!NXVFSMountPFS0(&vfs,"bad",bad,sizeof(bad)));
    CHECK(vfs.mountCount==0);
    CHECK(!NXVFSMountRomFS(&vfs,"rom",bad,sizeof(bad)));
    CHECK(vfs.mountCount==0);
}

int main(void) {
    testPFS0MountAndRead();
    testRejectMalformedMount();
    if (failures) {
        fprintf(stderr,"%d VFS test(s) failed\n",failures);
        return 1;
    }
    puts("Read-only VFS mount and file-read tests passed");
    return 0;
}
