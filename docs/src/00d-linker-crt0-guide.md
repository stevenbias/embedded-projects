---
title: "Linker Scripts & crt0.s: From Reset to main()"
phase: 0
project: 0
---

# Linker Scripts & crt0.s: From Reset to main()

> **Prerequisite:** Read this before writing bare-metal code for Cortex-M. You need to understand both the memory layout (linker script) and the runtime initialization (crt0.s) to write bare-metal code.

## Introduction

When a Cortex-M processor boots, two pieces of code work together to get you from reset to `main()`:

1. **Linker Script (`linker.ld`)** — Defines the memory layout: where Flash and RAM live, and how sections (`.text`, `.data`, `.bss`) map to those memories. It also exports symbols that the startup code uses.

2. **crt0.s (C Runtime Startup)** — The first code that runs after reset. It copies initialized data from Flash to RAM, zeros the `.bss` section, and calls `main()`.

These two files have a **contract**: the linker script defines symbols (like `_sdata`, `_edata`, `_sbss`, `_ebss`), and the startup code declares them as `.extern` and uses them.

This guide walks through both, providing generic, production-ready examples that follow embedded best practices.

## ELF Sections Reference

When you compile C code, the compiler organizes the output into **sections** in ELF object files. Each section has a specific purpose: code goes to one section, initialized data to another, etc. The linker script places these sections into your MCU's physical memory. The startup code then performs runtime initialization (copying `.data`, zeroing `.bss`).

### Key Sections Summary

| Section | Content | LMA (Storage) | VMA (Runtime) | crt0.s Action |
|---------|---------|----------------|----------------|---------------|
| `.isr_vector` | Exception/interrupt vector table (hardware-readable) | Flash | Flash | None |
| `.text` | Executable code (all functions) | Flash | Flash | None |
| `.rodata` | Read-only data (constants, string literals) | Flash | Flash | None |
| `.data` | Initialized global/static variables | Flash | RAM | Copy LMA → VMA |
| `.bss` | Uninitialized global/static variables | (None, zeroed at runtime) | RAM | Zero fill |

### Per-Section Details

**`.isr_vector`** — A table of 32-bit handler addresses that the Cortex-M NVIC reads on reset and on exceptions/interrupts. The first entry is the initial Main Stack Pointer (MSP); the second is the address of `Reset_Handler`; subsequent entries are NMI, HardFault, and peripheral interrupt handlers. Must be placed at the start of Flash (address `0x08000000` for STM32). On reset, the Cortex-M reads from address `0x00000000`, which STM32 aliases to the selected boot device — when booting from main Flash, `0x00000000` maps to `0x08000000`. Use `KEEP()` in the linker script to prevent garbage collection from removing the vector table.

**`.text`** — All compiled function code. Stored in Flash and runs directly from Flash (no copy needed for basic use). The `_etext` symbol marks the end of `.text` in Flash, which also serves as the load address for `.data` initial values.

**`.rodata`** — Read-only data: constants, string literals, and `const` global/static variables. Merged with `.text` in the linker script for locality.

**`.data`** — Global and static variables with initial values (e.g., `int x = 42;`). The initial values are stored in Flash (non-volatile), but the variables must live in RAM (read/write) at runtime. This creates the **LMA vs VMA** split. The linker script expresses this with `> RAM AT > FLASH`. The startup code copies the initial values from Flash to RAM before `main()` runs.

**`.bss`** — Uninitialized global and static variables (e.g., `int flag;`). Per the C standard, these must be zeroed before `main()` starts. No initial values are stored in the binary — the section is marked `(NOLOAD)` to save space. The startup code fills it with zeros.

### Section Flags

When defining custom sections in assembly with `.section`, you specify flags and a type:

```armasm
.section name, "flags", %type
```

| Flag | Meaning | Use Case |
|------|---------|----------|
| `"a"` | Allocatable — section occupies memory | All sections (required) |
| `"x"` | Executable — contains runnable code | Functions |
| `"w"` | Writable — can be modified at runtime | Variables |

| Type | Meaning | Use Case |
|------|---------|----------|
| `%progbits` | Contains actual data/code | Vector table, code, data |
| `%nobits` | No binary storage (zero-filled) | `.bss` |

On Cortex-M, all code runs in Thumb state: function addresses must have bit 0 set (the LSB). The `.thumb_func` directive marks the symbol as a Thumb function entry point, causing the assembler to set bit 0 on the symbol's value.

The `"x"` (executable) flag tells the linker the section contains runnable code. While `.thumb_func` correctly marks the symbol regardless of section flags, always use `"ax"` for code sections — without `"x"`, the linker may not correctly handle the section for branch targets.

**Recommended flags by section:**

| Section | Flags | Type | Reason |
|---------|-------|------|--------|
| `.isr_vector` | `"a"` | `%progbits` | Vector table contains addresses, not code |
| `.text.*` | `"ax"` | `%progbits` | Executable code |
| `.rodata` | `"a"` | `%progbits` | Read-only constants |
| `.data` | `"aw"` | `%progbits` | Initialized variables in RAM |
| `.bss` | `"aw"` | `%nobits` | Uninitialized variables (zero-filled) |

---

## Part 1: GNU LD Linker Scripts

### What Is a Linker Script?

A linker script tells `ld` (the GNU linker) how to arrange sections from input object files into the output binary. Without a custom script, the linker uses a default layout that won't match your MCU's memory map.

> **Tip:** You can see the default linker script with `arm-none-eabi-ld --verbose`. For bare-metal work, you always need a custom script.

### The MEMORY Command

The `MEMORY` command defines the physical memory regions of your target. For a typical STM32F4 series MCU:

```ld
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}
```

| Field | Meaning |
|-------|---------|
| `FLASH`, `RAM` | Region names (used later in SECTIONS) |
| `(rx)`, `(rwx)` | Attributes: r=read, w=write, x=execute |
| `ORIGIN` | Start address of the region |
| `LENGTH` | Size of the region |

> **Note:** Adjust ORIGIN and LENGTH to match your specific MCU's memory map.

### The SECTIONS Command

The `SECTIONS` command maps input sections (from `.o` files) to output sections, and places them into memory regions.

```ld
SECTIONS
{
    .text : {
        *(.text*)
    } > FLASH

    .data : {
        *(.data*)
    } > RAM AT > FLASH
}
```

| Symbol | Meaning |
|--------|---------|
| `.text` | Output section name |
| `*(.text*)` | Wildcard: all input `.text` sections |
| `> FLASH` | Place this section in FLASH region |
| `AT > FLASH` | Load address (LMA) in FLASH, runtime address (VMA) in RAM |

### LMA vs VMA (Critical Concept)

- **VMA (Virtual Memory Address)** — Where the section lives at runtime (e.g., `.data` runs from RAM)
- **LMA (Load Memory Address)** — Where the section is stored in the binary (e.g., `.data` initial values stored in Flash)

For `.data` (initialized global variables):
- The initial values must be stored in Flash (non-volatile)
- At runtime, the variables live in RAM (read/write)

The linker script expresses this as:
```ld
.data : {
    _sdata = .;
    *(.data*)
    _edata = .;
} > RAM AT > FLASH
```

The `AT > FLASH` sets the LMA to Flash, while `> RAM` sets the VMA to RAM. The symbols `_sdata` and `_edata` mark the section boundaries in RAM; the linker computes the Flash load address via `LOADADDR(.data)`.

### Best Practices Summary

**`ALIGN(4)`** — ARM AAPCS requires 8-byte stack alignment; sections should be 4-byte aligned. Use `: ALIGN(4)` on the output section to ensure it starts aligned. Add `. = ALIGN(4)` inside the section after its content so the next section also starts aligned. Two ALIGN calls are defensive: one for the start of this section, one for the start of the next.

**`KEEP()`** — Without `KEEP()`, the linker may remove the vector table as "unused" when garbage collection (`--gc-sections`) is enabled.

**`(NOLOAD)`** — Marks a section that should not be loaded into the binary. Used for `.bss` since its content (zeros) is generated at runtime.

**`PROVIDE()`** — Defines a symbol with a default value that can be overridden by the user's code. Useful for configurable parameters like heap size.

**`/DISCARD/`** — Removes unwanted sections from the output. Commonly used to discard exception frame info (`.ARM.exidx`), C++ exception tables (`.eh_frame`), and assembler comments (`.comment`).

### Complete Linker Script

Here's a complete, copy-paste linker script that follows all best practices:

```ld
/* linker.ld — Generic Cortex-M linker script */
ENTRY(Reset_Handler)

/* Memory regions — adjust to match your MCU */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

/* Default heap size — override with PROVIDE() in your code if needed */
PROVIDE(_heap_size = 0);        /* No heap by default */

/* Top of stack — 8-byte aligned for AAPCS compliance */
_stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
/* Vector table — must be at start of Flash */
.isr_vector : ALIGN(4) {
    KEEP(*(.isr_vector))    /* KEEP prevents garbage collection */
    . = ALIGN(4);           /* Align end for next section */
} > FLASH

/* Code and read-only data */
.text : ALIGN(4) {
    *(.text*)               /* Program code */
    *(.rodata*)             /* Read-only data */
    KEEP(*(.init))          /* C++ static constructors (optional) */
    KEEP(*(.fini))          /* C++ static destructors (optional) */
    . = ALIGN(4);           /* Align end for next section */
    _etext = .;             /* End of .text in Flash (also LMA for .data) */
} > FLASH

/* Initialized data — VMA in RAM, LMA in Flash */
.data : ALIGN(4) {
    _sdata = .;             /* Start of .data in RAM */
    *(.data*)
    . = ALIGN(4);
    _edata = .;             /* End of .data in RAM */
} > RAM AT > FLASH

/* Load address of .data in Flash — used by crt0.s copy loop */
_sidata = LOADADDR(.data);

/* Zero-initialized data — NOLOAD (not stored in binary) */
.bss (NOLOAD) : ALIGN(4) {
    _sbss = .;              /* Start of .bss in RAM */
    *(.bss*)
    *(COMMON)               /* Uninitialized C statics */
    . = ALIGN(4);
    _ebss = .;              /* End of .bss in RAM */
} > RAM

    /* Heap (optional, if using dynamic memory) */
    . = ALIGN(8);
    PROVIDE(_heap_start = .);
    . = . + _heap_size;
    PROVIDE(_heap_end = .);

    /* Discard unwanted sections */
    /DISCARD/ : {
        *(.ARM.exidx*)      /* Exception frame info (unused) */
        *(.eh_frame*)       /* C++ exception tables (unused) */
        *(.comment)         /* Assembler comments */
    }
}
```

---

## Part 2: crt0.s (C Runtime Startup)

### What Is crt0.s?

`crt0.s` (C Runtime, file #0) is the assembly code that runs immediately after reset. It's responsible for:

1. **Setting up the stack pointer** (if not done by the vector table)
2. **Copying `.data` from Flash to RAM** (initialized globals)
3. **Zeroing `.bss`** (uninitialized globals)
4. **Calling `main()`**
5. **Handling `main()` returning** (infinite loop)

### Cortex-M Boot Process

When a Cortex-M processor resets:

1. It reads the initial MSP (Main Stack Pointer) from address `0x00000000` (first vector table entry)
2. It reads the reset handler address from address `0x00000004` (second entry)
3. It jumps to the reset handler

> **Important:** The reset handler address must have bit 0 set (LSB=1) to indicate Thumb state. Cortex-M only supports Thumb instructions. This is handled automatically by the `.thumb_func` directive.

### crt0.s Code

```armasm
.syntax unified
.thumb

/* Vector table — placed in .isr_vector section */
.section .isr_vector, "a", %progbits
    .word _stack_top
    .word Reset_Handler

/* External symbols from linker script */
.extern _sdata
.extern _edata
.extern _sidata
.extern _sbss
.extern _ebss

.global Reset_Handler

/* Reset Handler — placed in .text section */
.section .text.Reset_Handler, "ax", %progbits
.thumb_func
Reset_Handler:
    /* Copy .data from Flash (LMA) to RAM (VMA) */
    ldr r0, =_sdata              /* Destination: start of .data in RAM */
    ldr r1, =_edata              /* End of .data in RAM */
    ldr r2, =_sidata             /* Source: start of .data in Flash */

    /* If _sdata == _edata, skip copy */
    cmp r0, r1
    beq zero_bss

copy_data:
    ldr r3, [r2], #4            /* Load from Flash, post-increment */
    str r3, [r0], #4            /* Store to RAM, post-increment */
    cmp r0, r1                  /* Check if done */
    bne copy_data

zero_bss:
    /* Zero .bss section */
    ldr r0, =_sbss              /* Start of .bss in RAM */
    ldr r1, =_ebss              /* End of .bss in RAM */
    movs r2, #0                 /* Zero value */

zero_loop:
    str r2, [r0], #4            /* Store zero, post-increment */
    cmp r0, r1                  /* Check if done */
    bne zero_loop

call_main:
    bl main

    /* If main returns, hang */
hang:
    b hang
```

### Building

Here's how to build a complete firmware from scratch:

```bash
# Assemble the startup code
arm-none-eabi-as -mthumb -mcpu=cortex-m4 crt0.s -o crt0.o

# Compile main.c (freestanding = no standard library)
arm-none-eabi-gcc -c -mthumb -mcpu=cortex-m4 -ffreestanding -nostdlib main.c -o main.o

# Link everything with the custom linker script
arm-none-eabi-gcc -T linker.ld -nostdlib -o firmware.elf crt0.o main.o

# Convert to raw binary (for flashing)
arm-none-eabi-objcopy -O binary firmware.elf firmware.bin

# Verify section addresses
arm-none-eabi-objdump -h firmware.elf

# Check memory usage
arm-none-eabi-size firmware.elf
```

### Debugging crt0.s with GDB

Set a breakpoint at the reset handler:
```gdb
target remote localhost:3333
break Reset_Handler
continue
```

Step through the `.data` copy loop:
```gdb
stepi        # Execute one instruction
info registers r0 r1 r2 r3   # Check register values
x/4x 0x20000000   # Examine RAM where .data is being copied
```

Verify `.bss` zeroing:
```gdb
x/16x 0x20000000 + sizeof(.data)   # Check that .bss is zeroed
```

---

## Part 3: How They Work Together

### The Contract

The linker script and crt0.s have a strict contract:

| Linker Script Defines | crt0.s Declares As | Purpose |
|----------------------|-------------------|---------|
| `_sdata = .` | `.extern _sdata` | Start of `.data` in RAM |
| `_edata = .` | `.extern _edata` | End of `.data` in RAM |
| `_sidata = LOADADDR(.data)` | `.extern _sidata` | `.data` initial values in Flash |
| `_sbss = .` | `.extern _sbss` | Start of `.bss` in RAM |
| `_ebss = .` | `.extern _ebss` | End of `.bss` in RAM |
| `_stack_top` | Used in vector table | Initial stack pointer |

If these don't match exactly, you'll get linker errors (undefined symbols) or runtime bugs (wrong addresses).

> **Reference implementation:** See [`../src/linker.ld`](../src/linker.ld) and [`../src/crt0.s`](../src/crt0.s) for the actual files in this project.

### Language-Specific Notes

| Language | Startup Code | Notes |
|----------|--------------|-------|
| **C** | Custom crt0.s as shown | Requires `.data` copy and `.bss` zero |
| **Rust** | Built-in `cortex-m-rt` crate | Provides its own reset handler, no crt0 needed |
| **Ada** | GNAT provides default startup | Uses `.ld` script, can customize |
| **Zig** | Built-in start code | `zig build` handles linking automatically |

### Common Bugs

1. **Wrong LMA/VMA** — `.data` not copied because addresses are wrong
   - Fix: Check `LOADADDR()` in linker script, verify with `objdump -h`

2. **Stack pointer not set** — May crash before reaching `main()`
   - Fix: Ensure first vector table entry has valid `_stack_top` value

3. **`.bss` not zeroed** — Uninitialized globals have random values
   - Fix: Check `_sbss` and `_ebss` symbols, verify zero loop runs

4. **Symbol mismatch** — Linker error: `undefined reference to _sdata`
   - Fix: Ensure linker script defines `_sdata` and crt0.s declares `.extern _sdata`

5. **Missing `KEEP()`** — Vector table removed by garbage collection
   - Fix: Add `KEEP(*(.isr_vector))` in linker script

6. **Alignment issues** — Hard faults with 64-bit data or FPU
   - Fix: Use `ALIGN(8)` for stack, `ALIGN(4)` for sections

---

## References

### Linker Script Resources
- [GNU LD MEMORY Command](https://sourceware.org/binutils/docs/ld/MEMORY.html) — Linker script memory region definition
- [GNU LD SECTIONS Command](https://sourceware.org/binutils/docs/ld/SECTIONS.html) — Output section control
- [GNU LD Builtin Functions](https://sourceware.org/binutils/docs/ld/Builtin-Functions.html) — LOADADDR, ALIGN, ADDR, SIZEOF and other linker script functions
- [cortex-m-rt/link.x.in](https://github.com/rust-embedded/cortex-m/blob/master/cortex-m-rt/link.x.in) — Production-ready Cortex-M linker script from Rust embedded
- [libopencm3 cortex-m-generic.ld](https://github.com/libopencm3/libopencm3/blob/master/lib/cortex-m-generic.ld) — Generic Cortex-M linker script for libopencm3
- [SEGGER Linker Script Files](https://kb.segger.com/SEGGER_Linker_Script_Files) — Generic Cortex-M linker script from SEGGER

### crt0.s / Startup Code Resources
- [ARM Developer: Writing your own startup code for Cortex-M](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/writing-your-own-startup-code-for-cortex-m) — Official ARM tutorial
- [ARM Developer: Decoding the Startup file for Arm Cortex-M4](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/decoding-the-startup-file-for-arm-cortex-m4) — Detailed startup file walkthrough
- [Wasil Zafar: Startup Code, Linker Scripts & Vector Table](https://www.wasilzafar.com/pages/series/cmsis/cmsis-part03-startup-linker-vector-table.html) — CMSIS Mastery Series Part 3
- [SEGGER: Startup code (thumb_crt0.s)](https://studio.segger.com/arm_crt0.htm) — C runtime-startup code documentation

### Tutorials
- [STM32 bare metal: writing a linker script and startup code from scratch](https://magdaref.com/blog/stm32-linker-script-and-startup-code) — Step-by-step bare-metal tutorial
- [ARM Assembly Part 14: Cortex-M Embedded](https://www.wasilzafar.com/pages/series/arm-assembly/arm-assembly-14-cortex-m-embedded.html) — Cortex-M assembly, crt0, linker scripts
- [Bare Metal Embedded Systems Linker Script File](https://microcontrollerslab.com/bare-metal-embedded-systems-linker-script-file/) — Linker script basics for ARM Cortex M4
