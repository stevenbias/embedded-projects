# Linker Script and Startup Code

This directory contains two critical files for bare-metal ARM Cortex-M firmware:
`linker.ld` (the linker script) and `startup.s` (the startup code).

## Linker Script (linker.ld)

The linker script defines how the program's sections are mapped into the target
device's memory.

### Memory Regions

The STM32F405 has two main memory regions:

- **FLASH**: Origin `0x08000000`, 1024KB (1 MB). This is non-volatile storage
  where the firmware is stored. It is readable and executable (`rx`).

- **RAM**: Origin `0x20000000`, 128KB. This is volatile read-write memory
  (`rwx`) used for working data, stack, and heap.

### Stack

The stack grows downward from the top of RAM:

```
_stack_top = ORIGIN(RAM) + LENGTH(RAM);  // 0x20020000
```

### Sections

- `.vector_table`: The ARM vector table must be placed at the very start of
  FLASH (address `0x08000000`). The first entry is the initial stack pointer
  value (`_stack_top`), and the second is the address of `Reset_Handler`.

  **Exception handler addresses must have LSB=1** to indicate Thumb instruction
  set (ARMv7-M Architecture Reference Manual DDI 0403 §2.3.4). Use
  `LONG(Reset_Handler | 1)` in the linker script — `LONG(Reset_Handler)` alone
  writes the raw (even) address, which causes a HardFault on reset.

- `.text`: Code instructions, placed in FLASH.

- `.rodata`: Read-only constants (e.g., string literals, const arrays), placed
  in FLASH.

- `.data`: Initialized global and static variables. These must reside in RAM
  at runtime, but their initial values are stored in FLASH. The linker uses
  the `AT>` syntax to specify the load address (FLASH) separately from the
  runtime address (RAM).

- `.bss`: Uninitialized global and static variables. Placed in RAM. Must be
  zeroed at startup.

The linker exports symbols that the startup code uses:
- `_data_start`, `_data_end`, `_data_loadaddr`
- `_bss_start`, `_bss_end`

## Startup Code (startup.s)

The startup code executes immediately after reset, before `main()` is called.

### CPU Configuration

```asm
.syntax unified
.cpu cortex-m4
.thumb
```

This configures the assembler for the Cortex-M4F (ARMv7E-M) architecture.
The `.thumb` directive ensures Thumb instruction set is used (required for
Cortex-M).

### Reset_Handler

The `Reset_Handler` function performs three essential tasks:

1. **Copy .data section**: Initialized globals in RAM need their initial values
   copied from FLASH. A loop copies words from `_data_loadaddr` to `_data_start`
   until `_data_end` is reached.

2. **Zero .bss section**: Uninitialized globals must be cleared to zero.
   A loop writes zeros from `_bss_start` to `_bss_end`.

3. **Call main()**: After initialization, `main()` is invoked.

If `main()` returns, the processor enters an infinite loop (`hang`).

### Default_Handler

A catch-all handler for any unhandled exceptions. It loops forever, which is
standard for bare-metal systems without an OS.

## How They Work Together

1. On power-on/reset, the CPU loads the stack pointer from address `0x08000000`
   and jumps to the reset handler at `0x08000004`. **The LSB of this address
   must be 1 to indicate Thumb state** (Cortex-M4 TRM DDI 0439); if LSB=0,
   the processor faults immediately.

2. `Reset_Handler` copies `.data` from FLASH to RAM, then zeros `.bss`.

3. `Reset_Handler` calls `main()`.

4. The linker script ensures the vector table, code, and data land in the
   correct memory regions, and provides the symbols needed for the startup
   code to function.