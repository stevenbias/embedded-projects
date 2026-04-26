---
title: "Project 1: LED Blinker — Your First Bare Metal Program"
phase: 1
project: 1
---

# Project 1: LED Blinker — Your First Bare Metal Program

## Introduction

The LED blinker is the "Hello, World" of embedded development — but on bare metal, there is no standard library, no operating system, and no `main` function that just works. Everything from the moment the processor comes out of reset is your responsibility.

This project teaches you the foundational mechanics of bare-metal programming that apply to every embedded system you will ever write:

- How the processor boots and finds your code
- How memory is laid out via linker scripts
- How to talk to hardware through memory-mapped registers
- Why `volatile` is non-negotiable
- How to configure the system clock

You will implement the same LED blinker in **C**, **Rust**, **Ada**, and **Zig** — each targeting the STM32F405 (Cortex-M4F) running under QEMU's `netduinoplus2` machine. The same code also runs on the NUCLEO-F446RE (STM32F446) with no changes. By the end, you will understand not only how to blink an LED, but how each language approaches the bare-metal problem space.

> **Tip:** If you already know one of these languages, skim that section and focus on the others. The real value is in comparing approaches.

## Target Hardware

| Property        | Value                              |
|-----------------|------------------------------------|
| Board           | Netduino Plus 2 (QEMU) / NUCLEO-F446RE (HW) |
| MCU             | STM32F405 / STM32F446              |
| Core            | ARM Cortex-M4F                     |
| Flash           | 1 MiB (QEMU) / 512 KiB (NUCLEO) @ `0x08000000` |
| SRAM            | 128 KiB @ `0x20000000`             |
| LED (User)      | PA5 (GPIO Port A, Pin 5)           |
| QEMU Machine    | `netduinoplus2`                    |

The user LED is wired to **PA5**. To blink it, we need to:

1. Enable the clock for GPIOA via the RCC peripheral
2. Configure PA5 as a push-pull output via `GPIOA_MODER`
3. Toggle `GPIOA_ODR` bit 5 with a delay loop

## Key Concepts

### Startup Code

When a Cortex-M processor resets, it reads two 32-bit values from address `0x00000000`:

1. **Initial Main Stack Pointer (MSP)** — loaded directly into the stack pointer register
2. **Reset Handler Address** — the address of the first code to execute

This pair is the first entry in the **vector table**. After the reset handler runs, it typically:

- Zeroes the `.bss` section (uninitialized global variables)
- Copies `.data` from flash to RAM (initialized global variables)
- Calls `main()` (or the language equivalent)

### Vector Table

The vector table is an array of function pointers at a known address. On Cortex-M, it contains the initial stack pointer followed by exception/interrupt handlers in a fixed order defined by ARM. For this project we only need the first two entries:

```
Index 0: Initial MSP
Index 1: Reset Handler
```

### Linker Script

The linker script tells the linker where to place each section in the target's memory map. A minimal script for STM32F405 defines:

- **FLASH** region at `0x08000000`, length `1024K`
- **RAM** region at `0x20000000`, length `128K`
- Sections: `.vector_table`, `.text`, `.rodata`, `.data`, `.bss`

### Memory-Mapped I/O

On ARM Cortex-M, all peripherals are accessed through memory-mapped registers. Writing to a specific address triggers hardware behavior — there is no `ioctl` or syscall. This is why `volatile` is critical: the compiler must not optimize away reads or writes to these addresses.

### Volatile Semantics

Every language provides a way to tell the compiler "this memory location can change without the program's knowledge." In bare-metal code, **all peripheral register accesses must be volatile**. Without it, the compiler will cache values in registers and your hardware will never see the writes.

### Clock Configuration

The STM32F4 starts up running on its internal 16 MHz HSI oscillator. GPIO peripherals live on the AHB1 bus and are **disabled by default** to save power. Before accessing any GPIO register, you must enable its clock via the RCC (Reset and Clock Control) peripheral.

## Key Registers

| Register         | Address      | Description                                    |
|------------------|--------------|------------------------------------------------|
| `RCC_AHB1ENR`    | `0x40023830` | AHB1 peripheral clock enable. Bit 0 = GPIOA    |
| `GPIOA_MODER`    | `0x40020000` | GPIOA mode register. 2 bits per pin. `01` = output |
| `GPIOA_ODR`      | `0x40020014` | GPIOA output data register. Write 1 to set pin high |

### Register Bit Layouts

**RCC_AHB1ENR (0x40023830):**
```
Bit 0: GPIOAEN — Set to 1 to enable GPIOA clock
```

**GPIOA_MODER (0x40020000):**
```
Bits 11:10 — MODER5 (Pin 5 mode)
  00 = Input (reset state)
  01 = General purpose output
  10 = Alternate function
  11 = Analog
```

**GPIOA_ODR (0x40020014):**
```
Bit 5 — ODR5 (Output data for Pin 5)
  0 = Low
  1 = High
```

## Implementation: C

### File Structure

```
led-blinker-c/
├── linker.ld
├── startup.s
├── main.c
└── Makefile
```

### Linker Script (`linker.ld`)

```ld
/* linker.ld — STM32F405 memory layout */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

/* Top of stack — grows downward from end of RAM */
_stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    /* Vector table must be at the very start of flash */
    .vector_table :
    {
        LONG(_stack_top)          /* Initial MSP */
        LONG(Reset_Handler)       /* Reset handler address */
    } > FLASH

    .text :
    {
        *(.text*)
    } > FLASH

    .rodata :
    {
        *(.rodata*)
    } > FLASH

    /* .data section — lives in RAM, initialized from flash */
    _data_start = .;
    .data :
    {
        *(.data*)
    } > RAM AT > FLASH
    _data_end = .;
    _data_loadaddr = LOADADDR(.data);

    /* .bss section — zeroed at startup */
    .bss :
    {
        *(.bss*)
        *(COMMON)
    } > RAM
    _bss_start = .;
    _bss_end = .;

    /DISCARD/ : { *(.eh_frame*) }
}
```

### Startup Assembly (`startup.s`)

```armasm
/* startup.s — Cortex-M4F startup for STM32F405 */

    .syntax unified
    .cpu cortex-m4
    .thumb

/* External symbols defined by the linker */
    .extern _data_start
    .extern _data_end
    .extern _data_loadaddr
    .extern _bss_start
    .extern _bss_end

    .global Reset_Handler
    .global Default_Handler

    .section .text.Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    /* Copy .data from flash to RAM */
    ldr  r0, =_data_start
    ldr  r1, =_data_end
    ldr  r2, =_data_loadaddr
    movs r3, #0
copy_data:
    cmp  r0, r1
    beq  zero_bss
    ldr  r4, [r2, r3]
    str  r4, [r0, r3]
    adds r3, r3, #4
    b    copy_data

    /* Zero .bss */
zero_bss:
    ldr  r0, =_bss_start
    ldr  r1, =_bss_end
    movs r2, #0
zero_loop:
    cmp  r0, r1
    beq  call_main
    str  r2, [r0]
    adds r0, r0, #4
    b    zero_loop

    /* Call main() */
call_main:
    bl   main

    /* If main returns, hang */
hang:
    b    hang

    .size Reset_Handler, . - Reset_Handler

/* Catch-all handler for unused exceptions */
    .section .text.Default_Handler
    .type Default_Handler, %function
Default_Handler:
    b    .
    .size Default_Handler, . - Default_Handler
```

### Main Code (`main.c`)

```c
/* main.c — LED blinker for STM32F405 (Netduino Plus 2 / NUCLEO-F446RE) */

#include <stdint.h>

/* Peripheral base addresses */
#define RCC_BASE        0x40023800U
#define GPIOA_BASE      0x40020000U

/* Register offsets */
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30U))
#define GPIOA_MODER     (*(volatile uint32_t *)(GPIOA_BASE + 0x00U))
#define GPIOA_ODR       (*(volatile uint32_t *)(GPIOA_BASE + 0x14U))

/* LED pin */
#define LED_PIN         5

/* Simple busy-wait delay — not precise, but sufficient for blinking */
static void delay(uint32_t count)
{
    for (volatile uint32_t i = 0; i < count; i++) {
        /* volatile loop variable prevents optimization */
    }
}

int main(void)
{
    /* Step 1: Enable GPIOA clock on AHB1 bus */
    RCC_AHB1ENR |= (1U << 0);

    /* Step 2: Configure PA5 as general-purpose output (MODER5 = 01) */
    GPIOA_MODER &= ~(0x3U << (LED_PIN * 2));  /* Clear bits 11:10 */
    GPIOA_MODER |=  (0x1U << (LED_PIN * 2));  /* Set to output */

    /* Step 3: Blink forever */
    while (1) {
        GPIOA_ODR ^= (1U << LED_PIN);  /* Toggle PA5 */
        delay(500000);                  /* Busy-wait */
    }

    return 0;  /* Never reached */
}
```

### Makefile

```makefile
# Makefile — LED Blinker (C / STM32F405)

CC      = arm-none-eabi-gcc
AS      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

CFLAGS  = -g -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os -Wall -Wextra -ffreestanding -nostdlib
ASFLAGS = -mcpu=cortex-m4 -mthumb

TARGET  = led-blinker
SRCS    = main.c startup.s
OBJS    = $(SRCS:.c=.o)
OBJS    := $(OBJS:.s=.o)

all: $(TARGET).bin size

$(TARGET).elf: $(OBJS) linker.ld
	$(CC) $(CFLAGS) -T linker.ld -o $@ $(OBJS) -Wl,-Map=$(TARGET).map

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.s
	$(AS) $(ASFLAGS) -c -o $@ $<

size: $(TARGET).elf
	$(SIZE) $<

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin $(TARGET).map

.PHONY: all clean size
```

### Build (C)

```bash
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os -Wall -Wextra \
  -g -ffreestanding -nostdlib -T linker.ld -o led-blinker.elf main.c startup.s
arm-none-eabi-objcopy -O binary led-blinker.elf led-blinker.bin
```

## Implementation: Rust

### File Structure

```
led-blinker-rust/
├── Cargo.toml
├── .cargo/
│   └── config.toml
├── build.rs
├── memory.x
└── src/
    └── main.rs
```

### Cargo.toml

```toml
[package]
name = "led-blinker"
version = "0.1.0"
edition = "2021"

[dependencies]
cortex-m = "0.7"
cortex-m-rt = "0.7"
panic-halt = "0.2"

[profile.release]
opt-level = "s"
lto = true
codegen-units = 1
debug = true
```

### `.cargo/config.toml`

```toml
[build]
target = "thumbv7em-none-eabihf"  # Cortex-M4F

[target.thumbv7em-none-eabihf]
runner = "qemu-system-arm -machine netduinoplus2 -nographic -kernel"
rustflags = [
  "-C", "link-arg=-Tlink.x",
]
```

### `memory.x` (Linker Script)

```ld
/* memory.x — Memory layout for STM32F405 */

MEMORY
{
    FLASH : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   : ORIGIN = 0x20000000, LENGTH = 128K
}

_stack_start = ORIGIN(RAM) + LENGTH(RAM);
```

### `build.rs`

```rust
use std::env;
use std::fs::File;
use std::io::Write;
use std::path::PathBuf;

fn main() {
    let out = &PathBuf::from(env::var_os("OUT_DIR").unwrap());
    File::create(out.join("memory.x"))
        .unwrap()
        .write_all(include_bytes!("memory.x"))
        .unwrap();
    println!("cargo:rustc-link-search={}", out.display());
    println!("cargo:rerun-if-changed=memory.x");
}
```

### `src/main.rs`

```rust
#![no_std]
#![no_main]

use core::ptr::{read_volatile, write_volatile};
use cortex_m_rt::{entry, exception, ExceptionFrame};
use panic_halt as _;

/* Peripheral register addresses */
const RCC_AHB1ENR: *mut u32 = 0x4002_3830_u32 as *mut u32;
const GPIOA_MODER: *mut u32 = 0x4002_0000_u32 as *mut u32;
const GPIOA_ODR:   *mut u32 = 0x4002_0014_u32 as *mut u32;

const LED_PIN: u32 = 5;

/// Busy-wait delay loop
fn delay(count: u32) {
    for _ in 0..count {
        core::hint::spin_loop();
    }
}

#[entry]
fn main() -> ! {
    // Step 1: Enable GPIOA clock
    unsafe {
        let rcc = read_volatile(RCC_AHB1ENR);
        write_volatile(RCC_AHB1ENR, rcc | (1 << 0));
    }

    // Step 2: Configure PA5 as output (MODER5 = 01)
    unsafe {
        let moder = read_volatile(GPIOA_MODER);
        let cleared = moder & !(0x3 << (LED_PIN * 2));
        let set = cleared | (0x1 << (LED_PIN * 2));
        write_volatile(GPIOA_MODER, set);
    }

    // Step 3: Blink forever
    loop {
        unsafe {
            let odr = read_volatile(GPIOA_ODR);
            write_volatile(GPIOA_ODR, odr ^ (1 << LED_PIN));
        }
        delay(500_000);
    }
}

#[exception]
fn DefaultHandler(_irqn: i16) {
    loop {}
}

#[exception]
fn HardFault(_frame: &ExceptionFrame) -> ! {
    loop {}
}
```

### Build (Rust)

```bash
# Install the Cortex-M4F target
rustup target add thumbv7em-none-eabihf

# Build in release mode
cargo build --release

# The binary is at target/thumbv7em-none-eabihf/release/led-blinker
```

## Implementation: Ada

### File Structure

```
led-blinker-ada/
├── led_blinker.gpr
├── memmap.ld
├── startup.adb
├── main.adb
├── main.ads
└── s-stm32f4.ads
```

### Project File (`led_blinker.gpr`)

```ada
project Led_Blinker is

   for Target use "arm-eabi";
   for Runtime use "ravenscar-sfp-stm32f4";

   for Source_Dirs use (".");
   for Object_Dir use "obj";
   for Main use ("main.adb");

   package Compiler is
      for Default_Switches ("Ada") use (
         "-O2",
         "-g",
         "-fstack-check",
          "-mcpu=cortex-m4",
          "-mthumb",
          "-mfloat-abi=hard",
          "-mfpu=fpv4-sp-d16"
      );
   end Compiler;

   package Binder is
      for Default_Switches ("Ada") use ("-L");
   end Binder;

   package Linker is
      for Default_Switches ("Ada") use (
         "-Tmemmap.ld",
         "-nostartfiles"
      );
   end Linker;

end Led_Blinker;
```

### Register Definitions (`s-stm32f2.ads`)

```ada
with System; use System;

package S.STM32F4 is
   pragma Preelaborate;

   type UInt32 is mod 2 ** 32;
   for UInt32'Size use 32;

   type UInt32_Access is access all UInt32;

   -- RCC Registers
   RCC_AHB1ENR_Addr : constant := 16#4002_3830#;
   RCC_AHB1ENR      : UInt32_Access :=
      UInt32_Access (RCC_AHB1ENR_Addr'Address);
   pragma Import (Ada, RCC_AHB1ENR);
   pragma Volatile (RCC_AHB1ENR);

   -- GPIOA Registers
   GPIOA_BASE       : constant := 16#4002_0000#;
   GPIOA_MODER_Addr : constant := GPIOA_BASE + 16#00#;
   GPIOA_ODR_Addr   : constant := GPIOA_BASE + 16#14#;

   GPIOA_MODER      : UInt32_Access :=
      UInt32_Access (GPIOA_MODER_Addr'Address);
   pragma Import (Ada, GPIOA_MODER);
   pragma Volatile (GPIOA_MODER);

   GPIOA_ODR        : UInt32_Access :=
      UInt32_Access (GPIOA_ODR_Addr'Address);
   pragma Import (Ada, GPIOA_ODR);
   pragma Volatile (GPIOA_ODR);

   LED_PIN          : constant := 5;

end S.STM32F4;
```

### Startup (`startup.adb`)

```ada
-- startup.adb — Minimal startup for Ada on Cortex-M3
-- The Ravenscar runtime handles .data/.bss initialization.
-- This package provides the reset handler entry point.

pragma Warnings (Off);

with Interfaces; use Interfaces;
with Main;

package body Startup is

   pragma Linker_Section (Item => Reset_Handler,
                          Section => ".text.Reset_Handler");
   pragma Export (C, Reset_Handler, "Reset_Handler");

   procedure Reset_Handler is
   begin
      Main.Main;
   end Reset_Handler;

end Startup;
```

### Main (`main.ads`)

```ada
package Main is
   pragma Preelaborate;
   procedure Main;
   pragma Export (C, Main, "main");
end Main;
```

### Main Body (`main.adb`)

```ada
with S.STM32F4; use S.STM32F4;
with Interfaces; use Interfaces;

package body Main is

   procedure Delay (Count : UInt32) is
      I : UInt32 := 0;
   begin
      while I < Count loop
         I := I + 1;
      end loop;
   end Delay;

   procedure Main is
   begin
      -- Step 1: Enable GPIOA clock
      RCC_AHB1ENR.all := RCC_AHB1ENR.all or 16#0000_0001#;

      -- Step 2: Configure PA5 as output (MODER5 = 01)
      declare
         Moder : UInt32 := GPIOA_MODER.all;
         Shift : constant UInt32 := UInt32 (LED_PIN * 2);
      begin
         Moder := Moder and not (16#3# shift_left Shift);
         Moder := Moder or (16#1# shift_left Shift);
         GPIOA_MODER.all := Moder;
      end;

      -- Step 3: Blink forever
      loop
         GPIOA_ODR.all := GPIOA_ODR.all xor (16#1# shift_left LED_PIN);
         Delay (500_000);
      end loop;
   end Main;

end Main;
```

### Build (Ada)

```bash
# Requires GNAT ARM ELF toolchain with Ravenscar runtime
# Typically installed via Alire or AdaCore GNAT Studio

gprbuild -P led_blinker.gpr -p

# The resulting ELF is in ./obj/main
arm-eabi-objcopy -O binary obj/main led-blinker.bin
```

> **Warning:** Ada bare-metal tooling requires a GNAT installation configured for ARM with the Ravenscar-SFP runtime. This is typically available via AdaCore's GNAT Embedded or the open-source `gnat-arm-elf` package with runtime support.

## Implementation: Zig

### File Structure

```
led-blinker-zig/
├── build.zig
├── linker.ld
├── startup.zig
└── src/
    └── main.zig
```

### `build.zig`

```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .thumb,
        .cpu_model = .{ .explicit = &std.Target.arm.cpu.cortex_m4 },
        .os_tag = .freestanding,
        .abi = .eabihf,
    });

    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "led-blinker",
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
        .strip = false,
        .single_threaded = true,
    });

    exe.setLinkerScript(b.path("linker.ld"));
    exe.entry = .{ .symbol_name = "Reset_Handler" };

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());

    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);
}
```

### Linker Script (`linker.ld`)

```ld
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .vector_table :
    {
        LONG(_stack_top)
        LONG(Reset_Handler)
    } > FLASH

    .text :
    {
        *(.text*)
    } > FLASH

    .rodata :
    {
        *(.rodata*)
    } > FLASH

    .data :
    {
        _data_start = .;
        *(.data*)
        _data_end = .;
    } > RAM AT > FLASH
    _data_loadaddr = LOADADDR(.data);

    .bss :
    {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    } > RAM
}
```

### Startup (`startup.zig`)

```zig
// startup.zig — Vector table and reset handler for Cortex-M3

const main = @import("main.zig");

comptime {
    // Place the vector table at the start of flash
    asm (".section .vector_table");
    asm (".global __vector_table");
    asm ("__vector_table:");
    asm (".word _stack_top");
    asm (".word Reset_Handler");
}

export const _stack_top: u32 = 0x20020000; // Top of 128K RAM

export fn Reset_Handler() callconv(.Naked) noreturn {
    // Copy .data from flash to RAM
    asm volatile (
        \\ ldr r0, =_data_start
        \\ ldr r1, =_data_end
        \\ ldr r2, =_data_loadaddr
        \\ movs r3, #0
        \\ 1:
        \\ cmp r0, r1
        \\ beq 2f
        \\ ldr r4, [r2, r3]
        \\ str r4, [r0, r3]
        \\ adds r3, r3, #4
        \\ b 1b
        \\ 2:
        \\ // Zero .bss
        \\ ldr r0, =_bss_start
        \\ ldr r1, =_bss_end
        \\ movs r2, #0
        \\ 3:
        \\ cmp r0, r1
        \\ beq 4f
        \\ str r2, [r0]
        \\ adds r0, r0, #4
        \\ b 3b
        \\ 4:
        \\ bl main_entry
        \\ 5:
        \\ b 5b
    );
    unreachable;
}

export fn main_entry() callconv(.C) noreturn {
    main.main();
}
```

### `src/main.zig`

```zig
// main.zig — LED blinker for STM32F405

const std = @import("std");

// Memory-mapped peripheral registers using comptime
const RCC_AHB1ENR = @as(*volatile u32, @ptrFromInt(0x40023830));
const GPIOA_MODER = @as(*volatile u32, @ptrFromInt(0x40020000));
const GPIOA_ODR   = @as(*volatile u32, @ptrFromInt(0x40020014));

const LED_PIN: u5 = 5;

fn delay(count: u32) void {
    var i: u32 = 0;
    while (i < count) : (i += 1) {
        // Busy wait — compiler cannot optimize this away
        // because the loop variable is used
    }
}

pub fn main() noreturn {
    // Step 1: Enable GPIOA clock
    RCC_AHB1ENR.* |= 1 << 0;

    // Step 2: Configure PA5 as output (MODER5 = 01)
    const shift: u32 = LED_PIN * 2;
    const moder = GPIOA_MODER.*;
    GPIOA_MODER.* = (moder & ~(@as(u32, 0x3) << shift)) | (@as(u32, 0x1) << shift);

    // Step 3: Blink forever
    while (true) {
        GPIOA_ODR.* ^= @as(u32, 1) << LED_PIN;
        delay(500_000);
    }
}
```

### Build (Zig)

```bash
# Build in ReleaseSmall mode
zig build -Doptimize=ReleaseSmall

# The ELF is in zig-out/bin/led-blinker
# Convert to binary if needed
arm-none-eabi-objcopy -O binary zig-out/bin/led-blinker led-blinker.bin
```

## Running in QEMU

### Start QEMU

All languages produce a binary that runs the same way:

```bash
qemu-system-arm -machine netduinoplus2 -kernel led-blinker.bin -nographic
```

You will not see visible output in `-nographic` mode for a simple LED blinker — the LED state is internal to the emulated GPIO. To verify it works, use GDB.

### GDB Verification

```bash
# Terminal 1: Start QEMU with GDB stub
qemu-system-arm -machine netduinoplus2 -kernel led-blinker.bin \
  -nographic -s -S

# Terminal 2: Connect with GDB
arm-none-eabi-gdb led-blinker.elf
```

```gdb
(gdb) target remote :1234
(gdb) break main          # or Reset_Handler for C
(gdb) continue
(gdb) display/i $pc
(gdb) stepi               # Step through initialization

# Watch the GPIOA_ODR register toggle
(gdb) watch *0x40020014
(gdb) continue
# You should see the watch trigger repeatedly as the LED toggles

# Or manually inspect the register
(gdb) x/x 0x40020014
```

> **Tip:** In QEMU, you can also use the `info registers` command and inspect `GPIOA_ODR` to see the pin state change. Add `-d guest_errors,int` to QEMU for interrupt trace output.

## Running in Renode

[Renode](https://renode.io/) is an open-source emulation framework that provides faster execution and better debugging capabilities than QEMU for many ARM platforms. It supports the NUCLEO-F446RE board out of the box.

### Prerequisites

```bash
# Install Renode (Ubuntu/Debian)
sudo apt install renode

# Or via pip
pip3 install renode
```

### Running

The C implementation includes a Makefile target for running in Renode:

```bash
cd code/01-led-blinker/c
make renode
```
This launches Renode with the NUCLEO-F446RE platform and loads the compiled binary. TheRenode console will appear where you can interact with the emulated hardware.

After starting Renode:

```
(Nucleo-F446RE) logLevel -1 UserLED
```

You should see messages like:
```
[NOISY] UserLED: LED state changed to True
[NOISY] UserLED: LED state changed to False
```

### GDB Debugging with Renode

Renode also provides a GDB server for debugging:

```bash
# Terminal 1: Start Renode with GDB stub enabled
renode -e '$$bin=@led-blinker.elf; $$repl=@../renode/nucleo-f446re.repl; include @../renode/led-blinker.resc; listen GDB 3333'
```

```bash
# Terminal 2: Connect with GDB
arm-none-eabi-gdb led-blinker.elf
```

```gdb
(gdb) target remote :3333
(gdb) break main
(gdb) continue

# Watch GPIOA_ODR toggle
(gdb) watch *0x40020014
(gdb) continue
```

### Renode Scripts

The project includes a Renode script (`renode/led-blinker.resc`) that:
- Creates the NUCLEO-F446RE board
- Maps the LED to a visual indicator in Renode's monitor
- Sets up sysbus.elfLoader to load the binary

> **Tip:** In the Renode console, use `led0` to see the LED state: `led0 Toggle` or `led0 State`. Use `qemu` to access QEMU-specific commands when running in Renode's QEMU backend.

## Deliverables

- [ ] Blinking LED binary for all four languages
- [ ] GDB session showing GPIOA_ODR toggling
- [ ] Verified vector table at `0x08000000` with correct initial stack pointer
- [ ] `.bss` zeroed and `.data` copied (verify with `arm-none-eabi-objdump -t`)
- [ ] Binary size comparison across languages

## What You Learned

| Concept                  | C                              | Rust                                  | Ada                              | Zig                              |
|--------------------------|--------------------------------|---------------------------------------|----------------------------------|----------------------------------|
| **Entry point**          | `Reset_Handler` in assembly    | `#[entry]` macro from `cortex-m-rt`   | Exported `main` procedure        | `Reset_Handler` with inline asm  |
| **Volatile access**      | `volatile` type qualifier      | `read_volatile` / `write_volatile`    | `pragma Volatile`                | `*volatile` pointer type         |
| **Linker script**        | Hand-written `.ld`             | `memory.x` + `link.x` from crate      | Hand-written `.ld`               | Hand-written `.ld`               |
| **Startup code**         | Assembly `.s` file             | Provided by `cortex-m-rt`             | Runtime handles it (Ravenscar)   | Inline asm in `startup.zig`      |
| **No-std declaration**   | `-ffreestanding -nostdlib`     | `#![no_std] #![no_main]`              | `ravenscar-sfp` runtime          | `freestanding` target            |
| **Infinite loop**        | `while (1) {}`                 | `loop {}` (type `!`)                  | `loop ... end loop;`             | `while (true) {}`                |

## Next Steps

You now understand the boot process, memory layout, and register access for bare-metal ARM. In [Project 2: UART Echo Server](02-uart-echo.md), you will add serial communication — learning baud rate calculation, polling vs. interrupt-driven I/O, and how each language handles peripheral configuration with more complexity.

> **Tip:** Before moving on, try modifying the blink rate, adding a second LED (if your target supports it), or replacing the busy-wait delay with the SysTick timer for more precise timing.

---

## References

### STMicroelectronics Documentation
- [STM32F4 Reference Manual (RM0090)](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-415-stm32f407-417-stm32f427-437-and-stm32f429-439-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) — Ch. 7: Reset and clock control (RCC), Ch. 8: General-purpose I/Os (GPIO)
- [STM32F405/407 Datasheet](https://www.st.com/resource/en/datasheet/stm32f405rg.pdf)

### ARM Documentation
- [Cortex-M4 Technical Reference Manual](https://developer.arm.com/documentation/ddi0439/latest/) — Ch. 3: Programmer's Model (MSP, vector table), Ch. 4: Memory Model
- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/) — B1.4: Exception entry and return, vector table structure

### Tools & Emulation
- [QEMU STM32 Documentation](https://www.qemu.org/docs/master/system/arm/stm32.html)
