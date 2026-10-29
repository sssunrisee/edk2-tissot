#include <Library/PcdLib.h>
#include <AsmMacroIoLibV8.h>
#include <Chipset/AArch64.h>

.text
.align 3

.set CTRL_SPAN_BIT, (1 << 23)

GCC_ASM_IMPORT (CEntryPoint)
GCC_ASM_IMPORT (SecondaryCEntryPoint)

GCC_ASM_IMPORT (ArmEnableInstructionCache)
GCC_ASM_IMPORT (ArmEnableDataCache)
GCC_ASM_IMPORT (ArmInvalidateTlb)

GCC_ASM_EXPORT (_ModuleEntryPoint)
GCC_ASM_EXPORT (SecondaryCpuEntry)

.global _StackBase
.global _StackSize

_StackBase:
  .quad FixedPcdGet32(PcdPrePiStackBase)

_StackSize:
  .quad FixedPcdGet32(PcdPrePiStackSize)

_ModuleEntryPoint:
  mov x10, FixedPcdGet64 (PcdDeviceTreeStore)
  str x0, [x10] // oh my fdt

  /* Checkpoint 3: PrePi Entry (Paint 40 rows YELLOW at row 80) */
  movz  x7, #(2880 * 80) & 0xFFFF
  movk  x7, #(0x90000000 + (2880 * 80)) >> 16, lsl #16
  movz  x8, #0xFFFF
  orr   x8, x8, x8, lsl #32
  movz  x9, #(720 * 40 / 2)
.Lpaint_yellow:
  str   x8, [x7], #8
  subs  x9, x9, #1
  b.ne  .Lpaint_yellow

  movz  x7, #(0x1000 + (2880 * 80)) & 0xFFFF
  movk  x7, #(0x90000000 + 0x1000 + (2880 * 80)) >> 16, lsl #16
  movz  x9, #(720 * 40 / 2)
.Lpaint_yellow2:
  str   x8, [x7], #8
  subs  x9, x9, #1
  b.ne  .Lpaint_yellow2

  mov x0, #0
  mov x1, #0

  /* First ensure all interrupts are disabled */
  bl ASM_PFX(ArmDisableInterrupts)

  /* Ensure that the MMU and caches are off */
  bl ASM_PFX(ArmDisableCachesAndMmu)

  /* Invalide I-Cache */
  bl ASM_PFX(ArmInvalidateInstructionCache)

  /* Invalidate TLB */
  bl ASM_PFX(ArmInvalidateTlb)

  mrs x0, sctlr_el1
  orr x0, x0, #CTRL_SPAN_BIT
  msr sctlr_el1, x0

_SetupExceptionVector:
  ldr     x0, =VectorTable
  bl      ArmWriteVBar

  /* Also set VBAR_EL2 if running at EL2 */
  mrs     x0, CurrentEL
  cmp     x0, #(2 << 2)
  b.ne    1f
  ldr     x0, =VectorTable
  msr     vbar_el2, x0
1:
  mrs     x4, mpidr_el1
  and     x4, x4, #0xFFFF
  lsr     x4, x4, #8
  cbnz    x4, SecondaryCpuEntry

_DonNotTrap_VFP_SIMD:
  mrs x0, CPACR_EL1
  /* Bit 20 and 21 */
  orr x0, x0, #0x300000
  msr CPACR_EL1, x0
  isb sy

_SetupPrimaryCoreStack:
  ldr x0, _StackBase     /* Stack base arg0 */
  ldr x1, _StackSize     /* Stack size arg1 */

  /* Zero Init stack safely with 64-bit integer stores */
  add x2, x0, x1         /* End of Stack */
  mov x3, x0             /* Stack Base */

  mov x4, xzr
  mov x5, xzr

_ClearStack:
  /* Assumes StackBase is 128-bit aligned, StackSize is a multiple of 16B */
  stp   x4, x5, [x3], #16                        /* Fill every 16 bytes */
  cmp   x3, x2                                   /* Compare Size */
  b.lt  _ClearStack

  add sp, x2, xzr                                /* Initalize SP */

_EnableCache:
  bl ArmInvalidateDataCache
  bl ASM_PFX(ArmEnableInstructionCache)
  bl ASM_PFX(ArmEnableDataCache)

_PrepareArguments:
  /* Checkpoint 4: Primary Stack Ready & Caches Enabled (Paint 40 rows MAGENTA at row 120) */
  movz  x7, #(2880 * 120) & 0xFFFF
  movk  x7, #(0x90000000 + (2880 * 120)) >> 16, lsl #16
  movz  x8, #0x00FF
  movk  x8, #0x00FF, lsl #16
  orr   x8, x8, x8, lsl #32
  movz  x9, #(720 * 40 / 2)
.Lpaint_magenta:
  str   x8, [x7], #8
  subs  x9, x9, #1
  b.ne  .Lpaint_magenta

  movz  x7, #(0x1000 + (2880 * 120)) & 0xFFFF
  movk  x7, #(0x90000000 + 0x1000 + (2880 * 120)) >> 16, lsl #16
  movz  x9, #(720 * 40 / 2)
.Lpaint_magenta2:
  str   x8, [x7], #8
  subs  x9, x9, #1
  b.ne  .Lpaint_magenta2

  /* x0 = _StackBase and x1 = _StackSize */
  ldr x0, _StackBase     /* Stack base arg0 */
  ldr x1, _StackSize     /* Stack size arg1 */

  bl CEntryPoint

.align 3
dead:
  b dead                      /* We should never get here */

.section .text.vectors, "ax"
.balign 2048
VectorTable:
  /* ARM64 Vector Table: 16 vectors, each 0x80 (128) bytes */
  .rept 16
    b _ExceptionHandler
    .space 0x80 - 4
  .endr

.text
.global _ExceptionHandler
.align 3
_ExceptionHandler:
  /* Save exception registers before anything touches them */
  mrs   x0, esr_el1
  mrs   x1, far_el1
  mrs   x2, elr_el1

  /* 1. Paint screen RED (rows 120-220 across all 1080 columns) */
  /* FbBase = 0x90000000, Row stride = 1080 * 4 = 4320 (0x10E0) */
  /* Start = 0x90000000 + 120 * 4320 = 0x9007E900 */
  movz  x7, #0xE900
  movk  x7, #0x9007, lsl #16
  /* Color: Red = 0x00FF0000 (BGRA/RGBA) -> 64-bit pair: 0x00FF000000FF0000 */
  movz  x8, #0x0000
  movk  x8, #0x00FF, lsl #16
  orr   x8, x8, x8, lsl #32
  /* Count: 100 rows * 1080 pixels / 2 pixels per store = 54000 = 0xD2F0 */
  movz  x9, #0xD2F0
.Lpaint_red_loop:
  str   x8, [x7], #8
  subs  x9, x9, #1
  b.ne  .Lpaint_red_loop

  /* 2. Flush data cache range (dc civac) so RED is immediately displayed */
  movz  x7, #0xE900
  movk  x7, #0x9007, lsl #16
  /* 100 rows * 4320 bytes = 432000 bytes / 64 bytes per line = 6750 lines = 0x1A5E */
  movz  x9, #0x1A5E
.Lflush_red_loop:
  dc    civac, x7
  add   x7, x7, #64
  subs  x9, x9, #1
  b.ne  .Lflush_red_loop
  dsb   sy
  isb   sy

  /* 3. Set SP to emergency exception stack (0x9FF8CFE0) and call C handler */
  movz  x16, #0xCFE0
  movk  x16, #0x9FF8, lsl #16
  mov   sp, x16
  bl    PeiExceptionHandler

.Lfreeze_loop:
  /* 4. Continuously pet watchdog (0x0B017004) and keep disabled (0x0B017008) */
  movz  x7, #0x7000
  movk  x7, #0x0B01, lsl #16           /* 0x0B017000 */
  movz  x8, #1
  str   w8, [x7, #4]                   /* WDT0_RST = 1 (pet) */
  str   wzr, [x7, #8]                  /* WDT0_EN = 0 (disable) */
  b     .Lfreeze_loop

SecondaryCpuEntry:
  /* Keep the incoming index */
  mov x19, x4

  mov x5, #0x8
  sub x4, x5, x4

  ubfiz x2, x4, #15, #8

_SetupSecondaryCoreStack:
  ldr x3, _StackBase     /* Stack base arg0 */

  add x2, x3, x2
  sub x3, x2, #0x8000    /* Stack size arg1 */

  mov x4, xzr
  mov x5, xzr

_ClearStackSecondary:
  /* Assumes StackBase is 128-bit aligned, StackSize is a multiple of 16B */
  stp     x4, x5, [x3], #16
  cmp     x3, x2                                   /* Compare Size */
  b.lt    _ClearStackSecondary

  add sp, x2, xzr                                  /* Initalize SP */

  bl ArmInvalidateDataCache

  /* Enter MpPark spin */
  mov x0, x19
  bl SecondaryCEntryPoint

secondarydead:
  wfe
  b secondarydead                      /* We should never get here */
