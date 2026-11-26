#ifndef NEXILIUM_NXCPU_H
#define NEXILIUM_NXCPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NX_CPU_OK = 0,
    NX_CPU_HALTED = 1,
    NX_CPU_WAITING = 2,
    NX_CPU_EXCEPTION = 3,
    NX_CPU_INVALID_ARGUMENT = -1,
    NX_CPU_STEP_LIMIT = -4
} NXCPUResult;

const char *NXCPUBackendName(void);
int NXCPUSelfTest(void);
int NXEngineCanBootSystemMenu(void);

#ifdef __cplusplus
}
#endif

#endif
