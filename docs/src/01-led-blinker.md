---
title: "Project 1: LED Blinker — Your First Bare Metal Program"
phase: 1
project: 1
---

# Project 1: LED Blinker — Your First Bare Metal Program

## Introduction

The LED blinker is the "Hello, World" of embedded development — but on bare metal, there is no standard library, no operating system, and no `main` function that just works. Everything from the moment the processor comes out of reset is your responsibility.

> **Resource:** For complementary bare-metal programming exercises (French), see the [4SE03 TP site](https://4se03.telecom-paris.fr/tp).

This project teaches you the foundational mechanics of bare-metal programming that apply to every embedded system you will ever write:

- How the processor boots and finds your code
- How memory is laid out via linker scripts
- How to talk to hardware through memory-mapped registers
- Why `volatile` is non-negotiable
- How to configure the system clock
- How to read a button (GPIO input) and control an LED (GPIO output)

You will implement the same LED blinker with **button speed control** in **C**, **Rust**, **Ada**, and **Zig** — each targeting the STM32F405 (Cortex-M4F) running under QEMU's `netduinoplus2` machine. The same code also runs on the NUCLEO-F446RE (STM32F446) with no changes. By the end, you will understand not only how to blink an LED, but how each language approaches the bare-metal problem space.

> **Reference Implementation:** The complete, tested code is in [`code/01-led-blinker/`](../code/01-led-blinker/).
> This tutorial explains the concepts; the actual code files contain detailed inline comments.

> **Tip:** If you already know one of these languages, skim that section and focus on the others. The real value is in comparing approaches.

## Target Hardware

| Property        | Value                              |
|-----------------|------------------------------------|
| Board           | NUCLEO-F446RE (also runs under QEMU's netduinoplus2) |
| MCU             | STM32F446RE              |
| Core            | ARM Cortex-M4F                     |
| Flash           | 1 MiB (QEMU) / 512 KiB (NUCLEO) @ `0x08000000` |
| SRAM            | 128 KiB @ `0x20000000`             |
| LED (User)      | PA5 (GPIO Port A, Pin 5)           |
| Button (User)   | PC13 (GPIO Port C, Pin 13, active LOW) |
| QEMU Machine    | `netduinoplus2`                    |

The user LED is wired to **PA5** and the button to **PC13**. To implement button-controlled blinking:

1. Enable clocks for GPIOA and GPIOC via the RCC peripheral
2. Configure PA5 as push-pull output via `GPIOA_MODER`
3. Configure PC13 as input via `GPIOC_MODER`
4. Read `GPIOC_IDR` to detect button presses, toggle `GPIOA_ODR` with variable delay

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

> **Important — Cortex-M Thumb State:** All exception handler addresses in the vector table must have bit 0 (LSB) set to 1. This tells the processor the handler uses Thumb instructions. Cortex-M4 only supports Thumb; if LSB=0, the processor faults immediately (ARMv7-M Architecture Reference Manual DDI 0403, §2.3.4).
>
> The linker script achieves this with `| 1` on the handler address. Note: `LONG(Reset_Handler)` alone does NOT set LSB — the assembler/compiler sets it automatically for symbols declared with `.thumb_func` or `.type ..., %function`, but the linker script must do it explicitly for vector table entries.

### Linker Script

The linker script tells the linker where to place each section in the target's memory map. A minimal script for STM32F446RE defines:

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
| `RCC_AHB1ENR`    | `0x40023830` | AHB1 peripheral clock enable. Bit 0 = GPIOA, Bit 2 = GPIOC |
| `GPIOA_MODER`    | `0x40020000` | GPIOA mode register. 2 bits per pin. `01` = output |
| `GPIOA_ODR`      | `0x40020014` | GPIOA output data register. Write 1 to set pin high |
| `GPIOC_MODER`    | `0x40020800` | GPIOC mode register. 2 bits per pin. `00` = input |
| `GPIOC_IDR`      | `0x40020810` | GPIOC input data register. Read pin state |

### Register Bit Layouts

**RCC_AHB1ENR (0x40023830):**
```
Bit 0: GPIOAEN — Set to 1 to enable GPIOA clock
Bit 2: GPIOCEN — Set to 1 to enable GPIOC clock
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

**GPIOC_IDR (0x40020810):**
```
Bit 13 — IDR13 (Input data for Pin 13)
  0 = Low (button pressed on NUCLEO — active LOW)
  1 = High (button released)
```

## Implementation: C

> **Full source:** [`code/01-led-blinker/c/`](../code/01-led-blinker/c/)

### Project Structure

```
code/01-led-blinker/c/
├── Makefile
├── inc/main.h          # Register definitions with documentation
└── src/main.c          # Application logic (246 lines, heavily commented)
```

Shared startup code from [`common/`](../code/01-led-blinker/common/):
- `linker.ld` — Memory layout (Flash @ 0x08000000, RAM @ 0x20000000)
- `crt0.s` — C runtime startup: copies .data, zeroes .bss, calls `main()`

### Enabling Peripheral Clocks

Before accessing any GPIO register, you **must** enable its clock. Writes to a disabled peripheral are silently ignored.

```c
/* Enable GPIOA clock (bit 0) and GPIOC clock (bit 2) */
RCC_AHB1ENR |= (1U << 0);  /* GPIOAEN — for LED on PA5 */
RCC_AHB1ENR |= (1U << 2);  /* GPIOCEN — for button on PC13 */
```

Both GPIO ports are on the AHB1 bus, controlled by a single register (`RCC_AHB1ENR`). Each bit gates a different port's clock.

### Configuring GPIO Modes

Each pin uses 2 bits in the MODER register:

| Mode | Bits | Description |
|------|------|-------------|
| Input | `00` | High-impedance (default after reset) |
| Output | `01` | Push-pull output |
| Alternate | `10` | Peripheral function (UART, SPI, etc.) |
| Analog | `11` | ADC/DAC |

```c
/* Configure PA5 as output — bits [11:10] = 0b01 */
GPIOA_MODER &= ~(0x3U << (LED_PIN * 2));   /* Clear first */
GPIOA_MODER |=  (0x1U << (LED_PIN * 2));   /* Set output */

/* Configure PC13 as input — bits [27:26] = 0b00 */
/* Input is the default, but explicit is clearer */
GPIOC_MODER &= ~(0x3U << (BUTTON_PIN * 2));
```

**Why clear-then-set?** Writing `|` directly would fail if another pin's mode bits collide. The clear-then-set idiom is safe: it only touches the target bits.

### Reading and Writing GPIO

**Writing an output** — the ODR register controls pin voltage:

```c
/* Toggle LED: XOR flips bit 5 */
GPIOA_ODR ^= (1U << LED_PIN);

/* Or set/clear explicitly */
GPIOA_ODR |=  (1U << LED_PIN);  /* LED on */
GPIOA_ODR &= ~(1U << LED_PIN);  /* LED off */
```

**Reading an input** — the IDR register reflects actual pin voltage:

```c
/* Read PC13. Active LOW: pressed = 0, released = 1 */
int pressed = !((GPIOC_IDR >> BUTTON_PIN) & 1U);
```

The active-LOW nature is a hardware choice: on the NUCLEO board, the button connects PC13 to ground when pressed, and an external pull-up resistor holds it HIGH when released.

### Precise Delays with SysTick

Instead of an imprecise busy-wait loop, use the Cortex-M SysTick timer. SysTick is a 24-bit down-counter that decrements each processor cycle (ARMv7-M §B3.3).

```c
void delay_ms(uint32_t ms) {
    /* 16 MHz HSI: 16,000 cycles per millisecond */
    uint32_t cycles = (HSI_CLOCK_HZ / 1000U) * ms;

    /* Program the 24-bit reload value */
    SYSTICK_LOAD = cycles & 0xFFFFFFU;
    SYSTICK_VAL = 0;                       /* Clear counter */

    /* Enable: processor clock, no interrupt */
    SYSTICK_CTRL = 0b101;                  /* Bit 2=1, Bit 1=0, Bit 0=1 */

    /* Wait for COUNTFLAG (bit 16) */
    while ((SYSTICK_CTRL & (1U << 16)) == 0) {}
    SYSTICK_CTRL = 0;                      /* Stop timer */
}
```

**Why SysTick over busy-wait?** SysTick is:
- **Predictable**: timing depends only on the processor clock, not compiler optimizations
- **Portable**: available on every Cortex-M, same register layout
- **Offloadable**: can be configured to generate interrupts (used in Projects 3+)

The 24-bit counter allows single-shot delays up to ~1,048ms at 16 MHz. Longer delays require looping.

### Edge Detection for Button Presses

Without edge detection, holding the button would toggle the speed dozens of times per second. The solution: track the previous state and only trigger on the rising edge (released → pressed).

```c
int prev_button = 0;   /* Previous state */

while (1) {
    int curr_button = button_is_pressed();

    /* Rising edge: was released (0), now pressed (1) */
    if (curr_button && !prev_button) {
        blink_fast = !blink_fast;   /* Toggle speed */
        delay_ms(200);               /* Skip contact bounce */
    }
    prev_button = curr_button;

    led_toggle();
    delay_ms(blink_fast ? BLINK_FAST_MS : BLINK_SLOW_MS);
}
```

> **Note on debouncing:** Mechanical switches bounce for 5-50ms. The 200ms delay after each press is a crude approach. For production code, use counter-based or state-machine debouncing (see [Project 3](03-button-interrupts.md)).

### Build

```bash
cd code/01-led-blinker/c
make clean all
```

Output: `led-blinker.elf` — **236 bytes** (smaller than the Rust version due to no panic handler overhead).

## Implementation: Rust

> **Full source:** [`code/01-led-blinker/rust/`](../code/01-led-blinker/rust/)

### True Bare-Metal Rust (No Crates)

Most Rust embedded tutorials use the `cortex-m` and `cortex-m-rt` crates. This implementation uses **zero external crates** — it shares the same startup code (`crt0.s`) and linker script (`linker.ld`) as the C implementation.

| Aspect | With `cortex-m-rt` crate | This Implementation |
|--------|--------------------------|---------------------|
| Entry point | `#[entry]` macro | `#[unsafe(no_mangle)] pub fn main()` |
| Startup code | Provided by crate | Shared `crt0.s` (same as C) |
| Linker script | `memory.x` + crate's `link.x` | Shared `linker.ld` (same as C) |
| Vector table | Generated by macro | Defined in `crt0.s` |
| Binary size | ~300+ bytes | **256 bytes** |

The trade-off: we lose crate conveniences (interrupt macros, exception handling) but gain full control and a smaller binary.

### Project Structure

```
code/01-led-blinker/rust/
├── Cargo.toml
├── Makefile
├── build.rs            # Compiles crt0.s via arm-none-eabi-gcc
├── .cargo/config.toml  # Target: thumbv7em-none-eabihf
└── src/
    ├── main.rs         # Application logic
    ├── target.rs       # Register addresses as raw pointers
    └── time.rs         # SysTick delay function
```

### Volatile Register Access


In Rust, raw pointer dereferencing requires `unsafe`. The `write_volatile` and `read_volatile` methods prevent the compiler from reordering or optimizing away hardware accesses.

```rust
// From src/target.rs — registers as raw mutable pointers
pub const RCC_AHB1ENR: *mut u32 = 0x4002_3830 as *mut u32;
pub const GPIOA_MODER: *mut u32 = 0x4002_0000 as *mut u32;
pub const GPIOC_IDR: *const u32 = 0x4002_0810 as *const u32;

// From src/main.rs — clock enable with read-modify-write
unsafe {
    RCC_AHB1ENR.write_volatile(
        RCC_AHB1ENR.read_volatile() | (1 << 0)
    );
}
```

Note `GPIOC_IDR` is `*const u32` (read-only pointer) while registers like `RCC_AHB1ENR` are `*mut u32` — Rust's type system catches accidental writes.

### GPIO Configuration

```rust
fn led_init() {
    unsafe {
        // Enable clock
        RCC_AHB1ENR.write_volatile(RCC_AHB1ENR.read_volatile() | (1 << 0));

        // MODER5 = 0b01 (output): clear bits 11:10, set bit 10
        GPIOA_MODER.write_volatile(
            (GPIOA_MODER.read_volatile() & !(0x3 << (LED_PIN * 2)))
                | (0x1 << (LED_PIN * 2)),
        );
    }
}

fn button_init() {
    unsafe {
        // Enable GPIOC clock
        RCC_AHB1ENR.write_volatile(RCC_AHB1ENR.read_volatile() | (1 << 2));

        // MODER13 = 0b00 (input)
        GPIOC_MODER.write_volatile(
            GPIOC_MODER.read_volatile() & !(0x3 << (BUTTON_PIN * 2)),
        );
    }
}
```

The `unsafe` blocks are a **contract** with the compiler: you are promising the pointer is valid and properly aligned. In bare-metal code, the burden is on the programmer — just like C.

### Button Reading

```rust
// From src/target.rs — registers as raw mutable pointers
pub const RCC_AHB1ENR: *mut u32 = 0x4002_3830 as *mut u32;
pub const GPIOA_MODER: *mut u32 = 0x4002_0000 as *mut u32;
pub const GPIOC_IDR: *const u32 = 0x4002_0810 as *const u32;

// From src/main.rs — clock enable with read-modify-write
unsafe {
    RCC_AHB1ENR.write_volatile(
        RCC_AHB1ENR.read_volatile() | (1 << 0)
    );
}
```

Note `GPIOC_IDR` is `*const u32` (read-only pointer) while registers like `RCC_AHB1ENR` are `*mut u32` — Rust's type system catches accidental writes.

### GPIO Configuration

```rust
fn led_init() {
    unsafe {
        // Enable clock
        RCC_AHB1ENR.write_volatile(RCC_AHB1ENR.read_volatile() | (1 << 0));

        // MODER5 = 0b01 (output): clear bits 11:10, set bit 10
        GPIOA_MODER.write_volatile(
            (GPIOA_MODER.read_volatile() & !(0x3 << (LED_PIN * 2)))
                | (0x1 << (LED_PIN * 2)),
        );
    }
}

fn button_init() {
    unsafe {
        // Enable GPIOC clock
        RCC_AHB1ENR.write_volatile(RCC_AHB1ENR.read_volatile() | (1 << 2));

        // MODER13 = 0b00 (input)
        GPIOC_MODER.write_volatile(
            GPIOC_MODER.read_volatile() & !(0x3 << (BUTTON_PIN * 2)),
        );
    }
}
```

The `unsafe` blocks are a **contract** with the compiler: you are promising the pointer is valid and properly aligned. In bare-metal code, the burden is on the programmer — just like C.

### Button Reading

```rust
fn button_is_pressed() -> bool {
    unsafe {
        // Active LOW: bit 13 = 0 means voltage is low → pressed
        (GPIOC_IDR.read_volatile() >> BUTTON_PIN) & 1 == 0
    }
}
```

Returning `bool` instead of `int` is a small Rust nicety — the type system communicates intent more clearly than `int`.

### Entry Point Without Macros

```rust
#![no_std]       // No standard library (no heap, no threads, no I/O)
#![no_main]      // No runtime entry point

#[unsafe(no_mangle)]  // Prevent name mangling — crt0.s calls this symbol
pub fn main() {
    led_init();
    button_init();

    let mut blink_fast = false;
    let mut prev_button = false;

    loop {
        let curr_button = button_is_pressed();

        if curr_button && !prev_button {
            blink_fast = !blink_fast;
            delay_ms(200);
        }
        prev_button = curr_button;

        led_toggle();
        delay_ms(if blink_fast { BLINK_FAST_MS } else { BLINK_SLOW_MS });
    }
}

#[panic_handler]
fn _panic(_: &core::panic::PanicInfo) -> ! {
    loop {}    // Spin forever on panic
}
```

**Key points:**
- The `panic_handler` is mandatory in `#![no_std]` — without it, the linker fails
- There's no `main` return type → the function never returns (infinite loop)

### Build

```bash
cd code/01-led-blinker/rust
make clean all
```

Output: `led-blinker.elf` — **256 bytes** (only 20 bytes larger than C).

The extra 20 bytes are the panic handler stub — C doesn't have one because `-ffreestanding` doesn't link one.

## Implementation: Ada

> **Status:** Placeholder — implementation coming soon. The code below demonstrates Ada's approach.
> You can find the project scaffold in [`code/01-led-blinker/ada/`](../code/01-led-blinker/ada/).

Ada provides stronger type safety than C through its type system and `pragma Volatile` for hardware registers.

### Key Ada Concepts for Bare-Metal

| Concept | Ada Syntax | Purpose |
|---------|-----------|---------|
| Memory-mapped I/O | `System.Address` | Map variables to hardware addresses |
| Modular types | `type UInt32 is mod 2**32` | Unsigned wraparound arithmetic |
| Bit manipulation | `Shift_Left`, `or`, `and not` | Register bit operations |

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
   for Runtime use "light-stm32f4";

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
-- The Light runtime handles .data/.bss initialization.
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
# Requires GNAT ARM ELF toolchain with Light runtime
# Typically installed via Alire or AdaCore GNAT Studio

gprbuild -P led_blinker.gpr -p

# The resulting ELF is in ./obj/main
arm-eabi-objcopy -O binary obj/main led-blinker.bin
```

> **Warning:** Ada bare-metal tooling requires a GNAT installation configured for ARM with the Light runtime. This is typically available via AdaCore's GNAT Embedded or the `gnat-arm-elf` crate via Alire with a suitable light-profile runtime (e.g. `light_stm32f4xx`).

## Implementation: Zig

> **Status:** Placeholder — implementation coming soon. The code below demonstrates Zig's approach.
> You can find the project scaffold in [`code/01-led-blinker/zig/`](../code/01-led-blinker/zig/).

Zig offers `comptime` evaluation and explicit `volatile` pointer types without runtime overhead.

### Key Zig Concepts for Bare-Metal

| Concept | Zig Syntax | Purpose |
|---------|-----------|---------|
| Volatile pointers | `*volatile u32` | Hardware register access |
| Comptime | `@ptrFromInt(0x40020000)` | Compile-time address conversion |
| No hidden control flow | Explicit error handling | Predictable code generation |
| Freestanding target | `.os_tag = .freestanding` | No OS assumptions |

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
// main.zig — LED blinker for STM32F446RE

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

This launches Renode with the NUCLEO-F446RE platform and loads the compiled binary. The Renode console will appear where you can interact with the emulated hardware.

After starting Renode:

```
(Nucleo-F446RE) logLevel -1 UserLED
```

You should see messages like:
```
[NOISY] UserLED: LED state changed to True
[NOISY] UserLED: LED state changed to False
```

### Testing the Button

The button on PC13 is active LOW (pressed = 0, released = 1).

> **Note on naming:** `UserButton` is registered on `sysbus` (not as a child of `gpioPortC`) so it can be accessed directly by its short name. If placed on `gpioPortC`, the command would be `gpioPortC.UserButton Press` instead.

To test speed toggling:

1. Start the emulation and enable LED logging:

```
(Nucleo-F446RE) logLevel -1 UserLED
```

2. Observe the LED blinking slowly (500ms period).

3. Simulate a button press to switch to fast blink:

```
(Nucleo-F446RE) UserButton Press
(Nucleo-F446RE) # wait ~200ms for debounce delay
(Nucleo-F446RE) UserButton Release
```

You should see the LED toggling at a faster rate (100ms period).

4. Press the button again to return to slow blink.

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

- [ ] LED blinks at default slow speed (500ms period)
- [ ] Button press toggles to fast blink (100ms period)
- [ ] Another press returns to slow blink
- [ ] Verify using Renode: `logLevel -1 UserLED` + `UserButton Press`/`Release`
- [ ] C binary: 236 bytes, Rust binary: 256 bytes
- [ ] Understand why peripheral clocks must be enabled before register access
- [ ] Understand output (MODER, ODR) vs input (MODER, IDR) GPIO configuration

## What You Learned

| Concept                  | C                              | Rust                                  | Ada                              | Zig                              |
|--------------------------|--------------------------------|---------------------------------------|----------------------------------|----------------------------------|
| **Entry point**          | `Reset_Handler` in assembly    | `#[unsafe(no_mangle)] pub fn main()`  | Exported `main` procedure        | `Reset_Handler` with inline asm  |
| **Volatile access**      | `volatile` type qualifier      | `read_volatile` / `write_volatile`    | `pragma Volatile`                | `*volatile` pointer type         |
| **Linker script**        | Shared `linker.ld`             | Shared `linker.ld`                    | Hand-written `.ld`               | Hand-written `.ld`               |
| **Startup code**         | Shared `crt0.s`                | Shared `crt0.s`                       | Runtime handles it (Light)       | Inline asm in `startup.zig`      |
| **No-std declaration**   | `-ffreestanding -nostdlib`     | `#![no_std] #![no_main]`              | `light` runtime                  | `freestanding` target            |
| **Infinite loop**        | `while (1) {}`                 | `loop {}` (type `!`)                  | `loop ... end loop;`             | `while (true) {}`                |
| **Binary size**          | 236 bytes                      | 256 bytes                             | TBD                              | TBD                              |

## Next Steps

You now understand the boot process, memory layout, and register access for bare-metal ARM. In [Project 2: UART Echo Server](02-uart-echo.md), you will add serial communication — learning baud rate calculation, polling vs. interrupt-driven I/O, and how each language handles peripheral configuration with more complexity.

> **Tip:** Before moving on, try modifying the blink rate, adding a second LED (if your target supports it), or replacing the busy-wait delay with the SysTick timer for more precise timing.

---

## References

### STMicroelectronics Documentation
- [STM32F446 Reference Manual (RM0390)](https://www.st.com/resource/en/reference_manual/dm00135183-stm32f446xx-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) — Ch. 7: Reset and clock control (RCC), Ch. 8: General-purpose I/Os (GPIO)
- [STM32F446RE Datasheet](https://www.st.com/resource/en/datasheet/stm32f446re.pdf)

### ARM Documentation
- [Cortex-M4 Technical Reference Manual](https://developer.arm.com/documentation/ddi0439/latest/) — Ch. 3: Programmer's Model (MSP, vector table), Ch. 4: Memory Model
- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/) — B1.4: Exception entry and return, vector table structure
- [4SE03 ARM Architecture (PDF)](https://4se03.telecom-paris.fr/supports/architecture_se.pdf) — French overview
- [4SE03 ARM Assembly Guide](https://4se03.telecom-paris.fr/supports/asm-arm/) — French assembly tutorial
- [4SE03 Bare-Metal TP](https://4se03.telecom-paris.fr/tp) — Practical exercises (French)

### Startup Code & crt0.s References
- [ARM Community: Writing your own startup code for Cortex-M](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/writing-your-own-startup-code-for-cortex-m) — Tutorial for Cortex-M startup assembly
- [ARM Community: Decoding the Startup file for Arm Cortex-M4](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/decoding-the-startup-file-for-arm-cortex-m4) — Detailed startup file walkthrough
- [Rowley crt0.s Documentation](https://www.rowleydownload.co.uk/arm/documentation/arm_crt0.htm) — crt0.s structure, sections, initialization steps
- [Embedded Artistry: Exploring Startup Implementations (Newlib ARM)](https://embeddedartistry.com/blog/2019/04/17/exploring-startup-implementations-newlib-arm/) — Newlib ARM startup analysis
- [Wasil Zafar ARM Assembly Part 14](https://www.wasilzafar.com/pages/series/arm-assembly/arm-assembly-14-cortex-m-embedded.html) — Cortex-M assembly & bare-metal, crt0, linker scripts

### Linker Script References
- [GNU LD MEMORY Command](https://sourceware.org/binutils/docs/ld/MEMORY.html) — Linker script memory region definition
- [GNU LD SECTIONS Command](https://sourceware.org/binutils/docs/ld/SECTIONS.html) — Output section control
- [Embedds: Programming STM32 with GNU Tools (Linker Script)](https://embedds.com/programming-stm32-discovery-using-gnu-tools-linker-script/) — Practical linker script tutorial
- [Understanding the Linker Script (Stack Overflow)](https://stackoverflow.com/questions/40532180/understanding-the-linkerscript-for-an-arm-cortex-m-microcontroller) — Detailed walkthrough with comments

### Tools & Emulation
- [QEMU STM32 Documentation](https://www.qemu.org/docs/master/system/arm/stm32.html)
