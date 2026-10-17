// q2_force.h -- force-included (-include) into every engine source file.

#ifndef Q2_FORCE_H
#define Q2_FORCE_H

// the engine's heap calls go to the PSRAM heap (q_mem.c)
#include "q2_alloc_redirect.h"

// The Cortex-M33 FPU is single precision. With tgmath.h, sin(), sqrt(), fabs(),
// atan2(), pow() and friends pick the float versions for float arguments, and the
// build adds -fsingle-precision-constant, so float code stays in float.
#include <tgmath.h>

// Placement on the device (cmake/psram_linker.cmake): the engine's data is in PSRAM
// unless marked Q2_SRAM_BSS (zero-initialised SRAM), and its code is in flash unless
// marked Q2_HOT (copied to SRAM at boot). Both are empty on the host.
#if defined(Q2_DEVICE)
#define Q2_SRAM_BSS		__attribute__((section(".sram_bss")))
#define Q2_HOT			__attribute__((section(".time_critical.q2"), noinline))
#define Q2_SRAM_DATA	__attribute__((section(".time_critical.q2data")))	// const tables, copied to SRAM
// Keeps a large array in PSRAM in an engine object whose other state is placed in
// SRAM as a whole (cmake/psram_linker.cmake). Zero-initialised.
#define Q2_PSRAM_BSS	__attribute__((section(".psram_bss.q2")))
// The uncached, non-allocating XIP alias of a flash or PSRAM address. Memory that is
// only ever accessed through it never takes XIP cache lines from anything else.
#define Q2_UNCACHED(p)	((void *)((unsigned long)(p) + 0x04000000ul))
#else
#define Q2_SRAM_BSS
#define Q2_HOT
#define Q2_SRAM_DATA
#define Q2_PSRAM_BSS
#define Q2_UNCACHED(p)	((void *)(p))
#endif

#endif
