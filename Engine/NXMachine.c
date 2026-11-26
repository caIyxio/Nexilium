#include "NXMachine.h"
#include "Arm64/esr.h"
#include "Arm64/mmu.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static Machine *machineFor(CPU *cpu) {
    return cpu ? (Machine *)cpu->m : NULL;
}

static NXMemoryRegion *findRegion(Machine *machine, uint64_t address,
                                  size_t length, unsigned access) {
    if (!machine || length == 0 || address > UINT64_MAX - length) return NULL;
    for (size_t i = 0; i < machine->regionCount; ++i) {
        NXMemoryRegion *region = &machine->regions[i];
        if (address < region->guestBase) continue;
        uint64_t offset64 = address - region->guestBase;
        if (offset64 > region->size) continue;
        size_t offset = (size_t)offset64;
        if (length > region->size - offset) continue;
        if ((region->permissions & access) != access) continue;
        return region;
    }
    return NULL;
}

static bool hasMapping(Machine *machine, uint64_t address, size_t length) {
    if (!machine || length == 0 || address > UINT64_MAX - length) return false;
    for (size_t i = 0; i < machine->regionCount; ++i) {
        NXMemoryRegion *region = &machine->regions[i];
        if (address < region->guestBase) continue;
        uint64_t offset64 = address - region->guestBase;
        if (offset64 <= region->size &&
            length <= region->size - (size_t)offset64) return true;
    }
    return false;
}

void *mem_host_ptr(CPU *cpu, u64 address, unsigned size, AccType access) {
    Machine *machine = machineFor(cpu);
    NXMemoryRegion *region = findRegion(machine, address, size, (unsigned)access);
    if (!region) return NULL;
    return region->host + (size_t)(address - region->guestBase);
}

static void raiseMemoryFault(CPU *cpu, uint64_t address, bool write,
                             bool execute, bool permissionFailure) {
    unsigned ec = execute ? EC_IABORT_LOWER : EC_DABORT_LOWER;
    unsigned fsc = permissionFailure ? FSC_PERM_L3 : FSC_TRANS_L3;
    u32 iss = execute ? fsc : iss_dabort(write, fsc);
    cpu_raise_sync(cpu, esr_make(ec, iss), address);
}

bool mem_read(CPU *cpu, u64 address, unsigned size, u64 *out) {
    if (!out || (size != 1 && size != 2 && size != 4 && size != 8)) return false;
    void *source = mem_host_ptr(cpu, address, size, ACC_READ);
    if (!source) {
        raiseMemoryFault(cpu, address, false, false,
                         hasMapping(machineFor(cpu), address, size));
        return false;
    }
    u64 value = 0;
    memcpy(&value, source, size);
    *out = value;
    return true;
}

bool mem_write(CPU *cpu, u64 address, unsigned size, u64 value) {
    if (size != 1 && size != 2 && size != 4 && size != 8) return false;
    void *destination = mem_host_ptr(cpu, address, size, ACC_WRITE);
    if (!destination) {
        raiseMemoryFault(cpu, address, true, false,
                         hasMapping(machineFor(cpu), address, size));
        return false;
    }
    memcpy(destination, &value, size);
    return true;
}

bool mem_peek(CPU *cpu, u64 address, unsigned size, u64 *out) {
    if (!out || (size != 1 && size != 2 && size != 4 && size != 8)) return false;
    void *source = mem_host_ptr(cpu, address, size, ACC_READ);
    if (!source) return false;
    u64 value = 0;
    memcpy(&value, source, size);
    *out = value;
    return true;
}

bool mem_read128(CPU *cpu, u64 address, V128 *out) {
    if (!out) return false;
    void *source = mem_host_ptr(cpu, address, sizeof(*out), ACC_READ);
    if (!source) {
        raiseMemoryFault(cpu, address, false, false,
                         hasMapping(machineFor(cpu), address, sizeof(*out)));
        return false;
    }
    memcpy(out, source, sizeof(*out));
    return true;
}

bool mem_write128(CPU *cpu, u64 address, const V128 *value) {
    if (!value) return false;
    void *destination = mem_host_ptr(cpu, address, sizeof(*value), ACC_WRITE);
    if (!destination) {
        raiseMemoryFault(cpu, address, true, false,
                         hasMapping(machineFor(cpu), address, sizeof(*value)));
        return false;
    }
    memcpy(destination, value, sizeof(*value));
    return true;
}

bool mem_ifetch(CPU *cpu, u64 address, u32 *out) {
    if (!out || (address & 3)) {
        raiseMemoryFault(cpu, address, false, true, false);
        return false;
    }
    void *source = mem_host_ptr(cpu, address, sizeof(*out), ACC_EXEC);
    if (!source) {
        raiseMemoryFault(cpu, address, false, true,
                         hasMapping(machineFor(cpu), address, sizeof(*out)));
        return false;
    }
    memcpy(out, source, sizeof(*out));
    return true;
}

void tlb_flush_all(void) {
    /* Address translation is currently a direct region lookup, not a cached TLB. */
}

void exception_take(CPU *cpu, ExcKind kind, u64 syndrome,
                    u64 faultAddress, u64 returnAddress) {
    Machine *machine = machineFor(cpu);
    if (machine) {
        machine->pendingException.valid = true;
        machine->pendingException.kind = kind;
        machine->pendingException.syndrome = syndrome;
        machine->pendingException.faultAddress = faultAddress;
        machine->pendingException.returnAddress = returnAddress;
    }
    cpu->pc = returnAddress;
    cpu->excl_valid = false;
}

void cpu_raise_sync(CPU *cpu, u64 syndrome, u64 faultAddress) {
    if (!cpu) return;
    exception_take(cpu, EXC_SYNC, syndrome, faultAddress, cpu->cur_insn_pc);
}

u64 gt_count(CPU *cpu, bool virt) {
    (void)virt;
    return cpu ? cpu->icount : 0;
}

void timer_update(struct Machine *machine) {
    (void)machine;
}

bool NXEngineInit(NXEngine *engine) {
    if (!engine) return false;
    memset(engine, 0, sizeof(*engine));
    engine->cpu.m = &engine->machine;
    cpu_reset(&engine->cpu, 0, 0);
    engine->initialized = true;
    return true;
}

void NXEngineDestroy(NXEngine *engine) {
    if (!engine) return;
    for (size_t i = 0; i < engine->machine.regionCount; ++i) {
        NXMemoryRegion *region = &engine->machine.regions[i];
        if (region->ownsStorage) free(region->host);
    }
    memset(engine, 0, sizeof(*engine));
}

bool NXEngineMapMemory(NXEngine *engine, uint64_t guestBase, size_t size,
                       unsigned permissions) {
    if (!engine || !engine->initialized || !size ||
        guestBase > UINT64_MAX - size ||
        engine->machine.regionCount >= NX_MAX_MEMORY_REGIONS) return false;
    Machine *machine = &engine->machine;
    uint64_t end = guestBase + size;
    for (size_t i = 0; i < machine->regionCount; ++i) {
        NXMemoryRegion *r = &machine->regions[i];
        uint64_t rEnd = r->guestBase + r->size;
        if (guestBase < rEnd && r->guestBase < end) return false;
    }
    uint8_t *bytes = (uint8_t *)calloc(1, size);
    if (!bytes) return false;
    NXMemoryRegion *region = &machine->regions[machine->regionCount++];
    region->guestBase = guestBase;
    region->size = size;
    region->host = bytes;
    region->permissions = permissions;
    region->ownsStorage = true;
    return true;
}

bool NXEngineLoadBytes(NXEngine *engine, uint64_t guestAddress,
                       const void *bytes, size_t length) {
    if (!engine || !engine->initialized || (!bytes && length)) return false;
    void *destination = mem_host_ptr(&engine->cpu, guestAddress,
                                     (unsigned)length, ACC_WRITE);
    if (!destination) return false;
    memcpy(destination, bytes, length);
    return true;
}

NXCPUResult NXEngineRun(NXEngine *engine, uint64_t instructionBudget) {
    if (!engine || !engine->initialized) return NX_CPU_INVALID_ARGUMENT;
    engine->machine.pendingException.valid = false;
    uint64_t executed = 0;
    while (executed < instructionBudget) {
        if (engine->machine.pendingException.valid) return NX_CPU_EXCEPTION;
        if (engine->cpu.stop) return NX_CPU_HALTED;
        StepResult result = cpu_step(&engine->cpu);
        ++executed;
        engine->machine.timerTicks = engine->cpu.icount;
        if (engine->machine.pendingException.valid) return NX_CPU_EXCEPTION;
        if (result == STEP_HALT || engine->cpu.stop) return NX_CPU_HALTED;
        if (engine->cpu.halted) return NX_CPU_WAITING;
    }
    return NX_CPU_STEP_LIMIT;
}

void NXEngineClearException(NXEngine *engine) {
    if (engine) memset(&engine->machine.pendingException, 0,
                       sizeof(engine->machine.pendingException));
}
