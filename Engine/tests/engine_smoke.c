#include "NXCPU.h"
#include <stdio.h>

int main(void) {
    if (NXCPUSelfTest() != 1) {
        fputs("AArch64 interpreter self-test failed\n", stderr);
        return 1;
    }
    printf("AArch64 interpreter self-test passed: %s\n", NXCPUBackendName());
    return 0;
}
