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

## Understanding ELF Sections for Bare-Metal

### Why Sections Matter
When you compile C code, the compiler organizes the output into **sections** (also called segments) in ELF object files. Each section has a specific purpose: code goes to one section, initialized data to another, etc. The linker script's job is to take these sections and place them into your MCU's physical memory (Flash/RAM) at the correct addresses. The startup code (crt0.s) then performs runtime initialization for sections that need it (copying `.data`, zeroing `.bss`).

### Key Sections Summary
Here are the critical sections you'll encounter in bare-metal Cortex-M development:

| Section | Content | LMA (Storage) | VMA (Runtime) | crt0.s Action |
|---------|---------|----------------|----------------|---------------|
| `.isr_vector` | Exception/interrupt vector table (hardware-readable) | Flash | Flash | None |
| `.text` | Executable code (all functions) | Flash | Flash | None |
| `.rodata` | Read-only data (constants, string literals) | Flash | Flash | None |
| `.data` | Initialized global/static variables | Flash | RAM | Copy LMA → VMA |
| `.bss` | Uninitialized global/static variables | (None, zeroed at runtime) | RAM | Zero fill |

### Detailed Section Breakdown

#### `.isr_vector` (Vector Table)
- **Purpose:** A table of 32-bit handler addresses that the Cortex-M NVIC reads to determine where to jump for exceptions/interrupts.
- **Hardware requirement:** Must be placed at the start of Flash (address `0x08000000` for STM32) so the processor can read the initial SP and reset handler on boot.
- **Content example:** The first entry is the initial Main Stack Pointer (MSP) value; the second is the address of `Reset_Handler`; subsequent entries are NMI, HardFault, and peripheral interrupt handlers.
- **Linker script:** Placed in Flash with `KEEP()` to prevent garbage collection:
  ```ld
.isr_vector : ALIGN(4) {
    KEEP(*(.isr_vector))
    . = ALIGN(4);
} > FLASH

.text : ALIGN(4) {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;        /* End of .text in Flash */
} > FLASH
  ```

#### `.text` (Executable Code)
- **Purpose:** Contains all compiled function code (your `main()`, helper functions, library code).
- **Storage:** Stored in Flash (non-volatile) and runs directly from Flash (no copy needed for basic use).
- **Content example:** Any function definition:
  ```c
  void led_on(void) { /* implementation */ }  // Goes to .text
  ```
- **Linker script:** Placed in Flash:
  ```ld
.text : ALIGN(4) {
    *(.text*)       /* All code sections */
    *(.rodata*)     /* Often included here for locality */
    . = ALIGN(4);
    _etext = .;     /* Symbol for end of .text in Flash */
} > FLASH
  ```

#### `.rodata` (Read-Only Data)
- **Purpose:** Constants, string literals, and `const` global/static variables that must not be modified.
- **Storage:** Stored in Flash alongside `.text`.
- **Content example:**
  ```c
  const char greeting[] = "Hello World";  // Goes to .rodata
  #define PI 3.1415                      // Often stored in .rodata
  ```
- **Linker script:** Typically merged with `.text` or placed immediately after it in Flash.

#### `.data` (Initialized Data)
- **Purpose:** Global and static variables that have an initial value (e.g., `int x = 42;`).
- **Key quirk:** Initial values must be stored in Flash (non-volatile), but the variables must live in RAM (read/write) at runtime. This creates the **LMA vs VMA** split (covered in detail later).
- **Content example:**
  ```c
  int global_counter = 0;          // Goes to .data (has initial value)
  static int s_count = 10;        // Goes to .data (static + initial value)
  ```
- **Linker script:** `> RAM AT > FLASH` (VMA in RAM, LMA in Flash)
- **crt0.s action:** Copy initial values from Flash (LMA) to RAM (VMA) before calling `main()`.

#### `.bss` (Zero-Initialized Data)
- **Purpose:** Uninitialized global/static variables. Per the C standard, these must be zeroed before `main()` starts.
- **Key quirk:** No initial values are stored in Flash (saves space in the binary). The startup code just fills this section with zeros.
- **Content example:**
  ```c
  int global_flag;                 // Goes to .bss (no initial value)
  static int s_buffer[256];       // Goes to .bss (static, no initial value)
  ```
- **Linker script:** Marked `(NOLOAD)` to exclude from the binary, placed in RAM:
  ```ld
.bss (NOLOAD) : ALIGN(4) {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
} > RAM
  ```
- **crt0.s action:** Fill the entire section with zeros before calling `main()`.

### Cross-Reference

The critical LMA vs VMA concept for `.data` is explained in detail in the [LMA vs VMA (Critical Concept)](#lma-vs-vma-critical-concept) section later in this guide.

### ELF Section Flags (Critical for Thumb Functions)

When defining custom sections in assembly with `.section`, you must specify flags and types. These flags tell the assembler how to categorize the section in the ELF binary, which affects whether the linker and processor can correctly execute code and access data.

#### Section Flag Syntax

```armasm
.section name, "flags", %type
```

| Flag | Meaning | Use Case |
|------|---------|----------|
| `"a"` | Allocatable — section occupies memory | All sections (required) |
| `"x"` | Executable — contains runnable code | Functions |
| `"w"` | Writable — can be modified at runtime | Variables |

The common flags are:
- `"aw"` = Allocatable + Writable (`.data`, `.bss`)
- `"ax"` = Allocatable + Executable (`.text`)
- `"a"` = Allocatable only (vector table)

#### Section Types

| Type | Meaning | Use Case |
|------|---------|----------|
| `%progbits` | Contains actual data/code | Vector table, code sections |
| `%nobits` | No binary storage (zero-filled) | `.bss` |

#### Why `"ax"` Matters for Thumb Functions

On Cortex-M, all code runs in Thumb state, where function addresses must have bit 0 set (the LSB, or Least Significant Bit). For example, `Reset_Handler` at address `0x08000068` is stored as `0x08000069` in the vector table.

The assembler only applies Thumb function attributes to symbols defined in **executable** sections. If you omit the `"x"` flag, the assembler cannot mark the symbol correctly, and `.thumb_func` has no effect:

```armasm
/* INCORRECT — no "x" flag means no Thumb attribute */
.section .text.Reset_Handler

/* CORRECT — "x" flag enables assemblerThumb function handling */
.section .text.Reset_Handler, "ax", %progbits
```

This is why `.text.*` sections must use `"ax"` flags.

#### Vector Table Sections

The vector table uses `"a"` only because it contains **data** (addresses), not executable instructions:

```armasm
.section .isr_vector, "a", %progbits
    .word _stack_top
    .word Reset_Handler
```

The executable flag is not needed here because the CPU doesn't "run" the vector table — it reads 32-bit values from it and branches to those addresses.

#### Recommended Flags by Section Type

| Section | Flags | Type | Reason |
|---------|-------|------|--------|
| `.isr_vector` | `"a"` | `%progbits` | Vector table (addresses) |
| `.text.*` | `"ax"` | `%progbits` | Executable code |
| `.rodata` | `"a"` | `%progbits` | Read-only constants |
| `.data` | `"aw"` | `%progbits` | Initialized variables in RAM |
| `.bss` | `"aw"` | `%nobits` | Uninitialized variables |

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

#### Basic Structure

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

The `AT > FLASH` sets the LMA to Flash, while `> RAM` sets the VMA to RAM.

### Best Practices for Linker Scripts

#### 1. Use `ALIGN()` for Section Boundaries
ARM AAPCS requires 8-byte stack alignment; sections should be 4-byte aligned:

```ld
.text : ALIGN(4) {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
} > FLASH
```

**Why two ALIGN calls?** The `: ALIGN(4)` on the output section (or equivalently `. = ALIGN(4)` before the content) ensures the section *starts* at a 4-byte aligned address — required for ARM vector tables and AAPCS compliance. The second `. = ALIGN(4)` *inside* the section (after the content) ensures the location counter is aligned for the *next* section, so whatever follows (e.g., `.text`) also starts on a 4-byte boundary. This defensive pattern guarantees proper alignment at both section boundaries.

#### 2. Use `KEEP()` for Critical Sections
Without `KEEP()`, the linker might remove the vector table as "unused" when garbage collection (`--gc-sections`) is enabled:

```ld
.isr_vector : ALIGN(4) {
    KEEP(*(.isr_vector))   /* Don't garbage-collect the vector table */
    . = ALIGN(4);
} > FLASH
```

#### 3. Use `(NOLOAD)` for `.bss`
The `.bss` section is zeroed at runtime and doesn't need to occupy space in the binary:

```ld
.bss (NOLOAD) : ALIGN(4) {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
} > RAM
```

#### 4. Use `PROVIDE()` for Overridable Symbols
`PROVIDE()` defines a symbol only if it's not already defined elsewhere (e.g., by the user in C code):

```ld
PROVIDE(_stack_size = 0x400);   /* Default 1KB stack, overridable */
PROVIDE(_heap_size = 0);        /* Default no heap */
```

#### 5. Discard Unwanted Sections
Remove sections that aren't needed in bare-metal (C++ exception frames, debug info):

```ld
/DISCARD/ : {
    *(.ARM.exidx*)
    *(.eh_frame*)
    *(.comment)
}
```

### Linker Script Symbols for crt0.s

The startup code needs to know:
- Where `.data` lives in RAM (`_sdata`, `_edata`)
- Where `.data` initial values are in Flash (`_sidata`)
- Where `.bss` starts/ends (`_sbss`, `_ebss`)
- Where the stack top is (`_stack_top`)

These are defined in the linker script:

```ld
/* In SECTIONS: */
.isr_vector : ALIGN(4) {
    KEEP(*(.isr_vector))
    . = ALIGN(4);
} > FLASH

.text : ALIGN(4) {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;        /* End of .text in Flash */
} > FLASH

.data : ALIGN(4) {
    _sdata = .;        /* VMA start (RAM) */
    *(.data*)
    . = ALIGN(4);
    _edata = .;        /* VMA end (RAM) */
} > RAM AT > FLASH

/* .data LMA in Flash — used by crt0.s to copy initial values */
_sidata = LOADADDR(.data);

.bss (NOLOAD) : ALIGN(4) {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
} > RAM

/* After SECTIONS: */
_stack_top = ORIGIN(RAM) + LENGTH(RAM);   /* 8-byte aligned automatically */
```

### Complete Example: Production-Ready Linker Script

Here's a complete, copy-paste linker script that follows best practices:

```ld
/* linker.ld — Generic Cortex-M linker script */
ENTRY(Reset_Handler)

/* Memory regions — adjust to match your MCU */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

/* Default stack/heap sizes — override with PROVIDE() in your code if needed */
PROVIDE(_stack_size = 0x400);   /* 1KB default stack */
PROVIDE(_heap_size = 0);        /* No heap by default */

/* Top of stack — 8-byte aligned for AAPCS compliance */
_stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
/* Vector table — must be at start of Flash */
.isr_vector : ALIGN(4) {
    KEEP(*(.isr_vector))
    . = ALIGN(4);
} > FLASH

/* Code and read-only data */
.text : ALIGN(4) {
    *(.text*)           /* Program code */
    *(.rodata*)         /* Read-only data */
    *(.glue_7)          /* ARM/Thumb interworking */
    *(.glue_7t)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
        _etext = .;         /* End of .text in Flash */
    } > FLASH

/* Initialized data — VMA in RAM, LMA in Flash */
.data : ALIGN(4) {
    _sdata = .;         /* Start of .data in RAM */
    *(.data*)
    . = ALIGN(4);
    _edata = .;         /* End of .data in RAM */
} > RAM AT > FLASH

    /* Load address of .data in Flash */
    _sidata = LOADADDR(.data);

/* Zero-initialized data — NOLOAD (not stored in binary) */
.bss (NOLOAD) : ALIGN(4) {
    _sbss = .;          /* Start of .bss in RAM */
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;          /* End of .bss in RAM */
} > RAM

    /* Heap (optional, if using dynamic memory) */
    . = ALIGN(8);
    PROVIDE(_heap_start = .);
    . = . + _heap_size;
    PROVIDE(_heap_end = .);

    /* Discard unwanted sections */
    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.eh_frame*)
        *(.comment)
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

### Using the crt0.s

Assemble the startup code:
```bash
arm-none-eabi-as -mthumb -mcpu=cortex-m4 crt0.s -o crt0.o
```

Link with the linker script:
```bash
arm-none-eabi-gcc -T linker.ld -nostdlib -o firmware.elf main.c crt0.s
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

### Complete Build Example

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

## Quick Reference

### Minimal Linker Script (Copy-Paste)

```ld
/* Minimal linker script for Cortex-M */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
.isr_vector : ALIGN(4) {
    KEEP(*(.isr_vector))
    . = ALIGN(4);
} > FLASH

.text : ALIGN(4) {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;
} > FLASH

_sidata = LOADADDR(.data);
.data : ALIGN(4) {
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
    } > RAM AT > FLASH

.bss (NOLOAD) : ALIGN(4) {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
} > RAM
}
```

### Minimal crt0.s (Copy-Paste)

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
.extern _lma_sdata
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
    ldr r2, =_lma_sdata             /* Source: start of .data in Flash */

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

---

## References

### Linker Script Resources
- [GNU LD MEMORY Command](https://sourceware.org/binutils/docs/ld/MEMORY.html) — Linker script memory region definition
- [GNU LD SECTIONS Command](https://sourceware.org/binutils/docs/ld/SECTIONS.html) — Output section control
- [cortex-m-rt/link.x.in](https://github.com/rust-embedded/cortex-m/blob/master/cortex-m-rt/link.x.in) — Production-ready Cortex-M linker script from Rust embedded
- [libopencm3 cortex-m-generic.ld](https://github.com/libopencm3/libopencm3/blob/master/lib/cortex-m-generic.ld) — Generic Cortex-M linker script for libopencm3
- [Linker Script Reference (liminfo)](https://www.liminfo.com/tools/linkerref) — MEMORY, SECTIONS, Symbols & ALIGN cheat sheet
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
