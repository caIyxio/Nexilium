#ifndef NX_CPU_H
#define NX_CPU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NX_CPU_REGISTER_COUNT 31

typedef struct {
    uint64_t x[NX_CPU_REGISTER_COUNT];
    uint64_t sp;
    uint64_t pc;
    uint8_t n, z, c, v;
    uint8_t halted;
    uint64_t instructions_executed;
} NXCPUState;

typedef enum {
    NX_CPU_OK = 0,
    NX_CPU_HALTED = 1,
    NX_CPU_INVALID_ARGUMENT = -1,
    NX_CPU_UNSUPPORTED_INSTRUCTION = -2,
    NX_CPU_MEMORY_FAULT = -3,
    NX_CPU_STEP_LIMIT = -4
} NXCPUResult;

void NXCPUReset(NXCPUState *state, uint64_t entryPoint);
NXCPUResult NXCPUExecute(NXCPUState *state, uint8_t *memory,
                         size_t memorySize, uint64_t maxInstructions);
int NXCPUSelfTest(void);

#ifdef __cplusplus
}
#endif

#endif
