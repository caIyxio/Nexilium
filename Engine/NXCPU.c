#include "NXCPU.h"
#include <stdbool.h>
#include <string.h>

/*
 * Nexilium AArch64 translation core.
 *
 * Guest A64 instructions are decoded into a compact internal operation and
 * executed by a portable backend. This deliberately emits no executable
 * memory and performs no JIT compilation.
 */

typedef enum {
    IR_NOP, IR_MOVZ, IR_ADD_IMM, IR_SUB_IMM, IR_LDR, IR_STR,
    IR_BRANCH, IR_BRANCH_COND, IR_COMPARE_BRANCH, IR_RET
} NXIROp;

typedef struct {
    NXIROp op;
    uint8_t rd, rn, width, setFlags, condition, nonzero;
    uint64_t immediate;
    int64_t displacement;
} NXIR;

static uint64_t readReg(const NXCPUState *s, unsigned r) {
    return r == 31 ? 0 : s->x[r];
}

static void writeReg(NXCPUState *s, unsigned r, uint64_t value) {
    if (r == 31) s->sp = value;
    else if (r < NX_CPU_REGISTER_COUNT) s->x[r] = value;
}

static bool conditionPassed(const NXCPUState *s, unsigned c) {
    switch (c) {
        case 0: return s->z;
        case 1: return !s->z;
        case 2: return s->c;
        case 3: return !s->c;
        case 4: return s->n;
        case 5: return !s->n;
        case 6: return s->v;
        case 7: return !s->v;
        case 8: return s->c && !s->z;
        case 9: return !s->c || s->z;
        case 10: return s->n == s->v;
        case 11: return s->n != s->v;
        case 12: return !s->z && s->n == s->v;
        case 13: return s->z || s->n != s->v;
        case 14: return true;
        default: return false;
    }
}

static int64_t signExtend(uint64_t value, unsigned bits) {
    const uint64_t sign = UINT64_C(1) << (bits - 1);
    return (int64_t)((value ^ sign) - sign);
}

/* Decode one fixed-width A64 instruction into the portable IR. */
static bool translate(uint32_t insn, NXIR *ir) {
    memset(ir, 0, sizeof(*ir));
    if (insn == UINT32_C(0xD503201F)) { ir->op = IR_NOP; return true; }

    /* MOVZ Xd, #imm16, LSL #0/#16/#32/#48 */
    if ((insn & UINT32_C(0x7F800000)) == UINT32_C(0x52800000)) {
        ir->op = IR_MOVZ;
        ir->width = (insn >> 31) ? 64 : 32;
        ir->rd = insn & 31;
        ir->immediate = (uint64_t)((insn >> 5) & 0xFFFF) << (((insn >> 21) & 3) * 16);
        return true;
    }

    /* ADD/SUB (immediate), including flag-setting variants. */
    if ((insn & UINT32_C(0x1F000000)) == UINT32_C(0x11000000)) {
        ir->op = (insn & (1u << 30)) ? IR_SUB_IMM : IR_ADD_IMM;
        ir->width = (insn >> 31) ? 64 : 32;
        ir->setFlags = (insn >> 29) & 1;
        ir->rd = insn & 31;
        ir->rn = (insn >> 5) & 31;
        ir->immediate = (insn >> 10) & 0xFFF;
        if (insn & (1u << 22)) ir->immediate <<= 12;
        return true;
    }

    /* Unsigned-offset 64-bit LDR/STR. */
    if ((insn & UINT32_C(0xFFC00000)) == UINT32_C(0xF9400000) ||
        (insn & UINT32_C(0xFFC00000)) == UINT32_C(0xF9000000)) {
        ir->op = ((insn >> 22) & 1) ? IR_LDR : IR_STR;
        ir->rd = insn & 31;
        ir->rn = (insn >> 5) & 31;
        ir->immediate = (uint64_t)((insn >> 10) & 0xFFF) * 8;
        ir->width = 64;
        return true;
    }

    /* B immediate. */
    if ((insn & UINT32_C(0x7C000000)) == UINT32_C(0x14000000)) {
        ir->op = IR_BRANCH;
        ir->displacement = signExtend(insn & 0x03FFFFFF, 26) * 4;
        return true;
    }

    /* B.cond */
    if ((insn & UINT32_C(0xFF000010)) == UINT32_C(0x54000000)) {
        ir->op = IR_BRANCH_COND;
        ir->condition = insn & 15;
        ir->displacement = signExtend((insn >> 5) & 0x7FFFF, 19) * 4;
        return true;
    }

    /* CBZ/CBNZ, W or X register. */
    if ((insn & UINT32_C(0x7E000000)) == UINT32_C(0x34000000)) {
        ir->op = IR_COMPARE_BRANCH;
        ir->width = (insn >> 31) ? 64 : 32;
        ir->nonzero = (insn >> 24) & 1;
        ir->rd = insn & 31;
        ir->displacement = signExtend((insn >> 5) & 0x7FFFF, 19) * 4;
        return true;
    }

    /* RET Xn (default X30). */
    if ((insn & UINT32_C(0xFFFFFC1F)) == UINT32_C(0xD65F0000)) {
        ir->op = IR_RET;
        ir->rn = (insn >> 5) & 31;
        return true;
    }
    return false;
}

void NXCPUReset(NXCPUState *state, uint64_t entryPoint) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->pc = entryPoint;
}

static bool validRange(uint64_t address, size_t length, size_t memorySize) {
    return address <= memorySize && length <= memorySize - (size_t)address;
}

static void updateAddFlags(NXCPUState *s, uint64_t a, uint64_t b, uint64_t r) {
    s->n = (uint8_t)(r >> 63);
    s->z = (uint8_t)(r == 0);
    s->c = (uint8_t)(r < a);
    s->v = (uint8_t)((~(a ^ b) & (a ^ r)) >> 63);
}

static void updateSubFlags(NXCPUState *s, uint64_t a, uint64_t b, uint64_t r) {
    s->n = (uint8_t)(r >> 63);
    s->z = (uint8_t)(r == 0);
    s->c = (uint8_t)(a >= b);
    s->v = (uint8_t)(((a ^ b) & (a ^ r)) >> 63);
}

NXCPUResult NXCPUExecute(NXCPUState *state, uint8_t *memory,
                         size_t memorySize, uint64_t maxInstructions) {
    if (!state || !memory) return NX_CPU_INVALID_ARGUMENT;
    if (state->halted) return NX_CPU_HALTED;

    uint64_t executed = 0;
    while (executed < maxInstructions) {
        if (!validRange(state->pc, 4, memorySize) || (state->pc & 3))
            return NX_CPU_MEMORY_FAULT;

        uint32_t insn;
        memcpy(&insn, memory + state->pc, sizeof(insn));
        NXIR ir;
        if (!translate(insn, &ir)) return NX_CPU_UNSUPPORTED_INSTRUCTION;

        const uint64_t oldPC = state->pc;
        state->pc += 4;
        uint64_t a, b, result;
        switch (ir.op) {
            case IR_NOP: break;
            case IR_MOVZ:
                result = ir.immediate;
                if (ir.width == 32) result &= UINT32_MAX;
                writeReg(state, ir.rd, result);
                break;
            case IR_ADD_IMM:
            case IR_SUB_IMM:
                a = readReg(state, ir.rn);
                b = ir.immediate;
                if (ir.width == 32) { a &= UINT32_MAX; b &= UINT32_MAX; }
                result = ir.op == IR_ADD_IMM ? a + b : a - b;
                if (ir.width == 32) result &= UINT32_MAX;
                writeReg(state, ir.rd, result);
                if (ir.setFlags) {
                    if (ir.op == IR_ADD_IMM) updateAddFlags(state, a, b, result);
                    else updateSubFlags(state, a, b, result);
                    if (ir.width == 32) state->n = (uint8_t)((result >> 31) & 1);
                }
                break;
            case IR_LDR:
            case IR_STR:
                a = readReg(state, ir.rn) + ir.immediate;
                if (!validRange(a, 8, memorySize) || (a & 7))
                    return NX_CPU_MEMORY_FAULT;
                if (ir.op == IR_LDR) {
                    memcpy(&result, memory + a, 8);
                    writeReg(state, ir.rd, result);
                } else {
                    result = readReg(state, ir.rd);
                    memcpy(memory + a, &result, 8);
                }
                break;
            case IR_BRANCH:
                state->pc = (uint64_t)((int64_t)oldPC + ir.displacement);
                break;
            case IR_BRANCH_COND:
                if (conditionPassed(state, ir.condition))
                    state->pc = (uint64_t)((int64_t)oldPC + ir.displacement);
                break;
            case IR_COMPARE_BRANCH:
                a = readReg(state, ir.rd);
                if (ir.width == 32) a &= UINT32_MAX;
                if ((a != 0) == (ir.nonzero != 0))
                    state->pc = (uint64_t)((int64_t)oldPC + ir.displacement);
                break;
            case IR_RET:
                state->pc = readReg(state, ir.rn);
                state->halted = 1;
                break;
        }
        ++executed;
        ++state->instructions_executed;
        if (state->halted) return NX_CPU_HALTED;
    }
    return NX_CPU_STEP_LIMIT;
}

/* Small deterministic smoke test: MOVZ; ADD; SUBS. */
int NXCPUSelfTest(void) {
    uint8_t memory[64] = {0};
    const uint32_t program[] = {
        UINT32_C(0xD2800540), /* MOVZ X0, #42 */
        UINT32_C(0x91002001), /* ADD X1, X0, #8 */
        UINT32_C(0xF100C422)  /* SUBS X2, X1, #50 */
    };
    memcpy(memory, program, sizeof(program));
    NXCPUState cpu;
    NXCPUReset(&cpu, 0);
    NXCPUResult result = NXCPUExecute(&cpu, memory, sizeof(memory), 3);
    return result == NX_CPU_STEP_LIMIT &&
           cpu.x[0] == 42 && cpu.x[1] == 50 && cpu.x[2] == 0 &&
           cpu.z == 1 && cpu.instructions_executed == 3;
}
