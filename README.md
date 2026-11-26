# Nexilium

JIT-free Nintendo Switch emulator for iOS. The project is in early engine development.

## Engine status

The native engine currently contains the first AArch64 guest translation backend:
- A64 instruction decoding into a compact internal representation
- Portable execution of translated operations (no JIT and no executable-memory generation)
- Initial support for NOP, MOVZ, ADD/SUB immediate, 64-bit unsigned-offset LDR/STR, B, B.cond, CBZ/CBNZ, and RET
- A deterministic CPU-core smoke test exposed to Swift

This is an engine foundation, **not yet a bootable Switch emulator**. ELF/NSO/NRO loading, virtual memory mappings, exceptions, SIMD/FP, atomics, cache coherence, Horizon OS services, GPU command processing, audio, and controller integration remain to be implemented. The current memory model is a bounded flat buffer and is not the Switch address space.

## Build

The iOS app is generated with XcodeGen and built by GitHub Actions. The CPU core is C and is compiled into the app target.
