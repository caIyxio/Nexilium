#ifndef NX_ARM64_MMU_H
#define NX_ARM64_MMU_H

#include "cpu.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum { ACC_READ = 1, ACC_WRITE = 2, ACC_EXEC = 4 } AccType;

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

bool mem_read(CPU *cpu, u64 address, unsigned size, u64 *out);
bool mem_write(CPU *cpu, u64 address, unsigned size, u64 value);
bool mem_peek(CPU *cpu, u64 address, unsigned size, u64 *out);
bool mem_read128(CPU *cpu, u64 address, V128 *out);
bool mem_write128(CPU *cpu, u64 address, const V128 *value);
bool mem_ifetch(CPU *cpu, u64 address, u32 *out);
void *mem_host_ptr(CPU *cpu, u64 address, unsigned size, AccType access);
void tlb_flush_all(void);

#endif
