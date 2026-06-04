---
title: "Project 1: LED Blinker — Your First Bare Metal Program"
phase: 1
project: 1
---

# Project 1: LED Blinker — Your First Bare Metal Program

## 1. Introduction

The LED blinker is the "Hello, World" of embedded development — but on bare metal, there is no standard library, no operating system, and no `main` function that just works. Everything from the moment the processor comes out of reset is your responsibility.

> **Resource:** For complementary bare-metal programming exercises (French), see the [4SE03 TP site](https://4se03.telecom-paris.fr/tp).

This project teaches you the foundational mechanics of bare-metal programming:

- How the processor boots and finds your code
- How memory is laid out via linker scripts
- How to talk to hardware through memory-mapped registers
- Why `volatile` is non-negotiable
- How to configure the system clock
- How to read a button (GPIO input) and control an LED (GPIO output)

You will implement the same LED blinker with **button speed control** in **C**, **Rust**, and **Ada** (Zig placeholder), each targeting the STM32F446RE (Cortex-M4F). The code runs on both the physical NUCLEO-F446RE board and QEMU's `netduinoplus2` machine (which emulates the STM32F405, a compatible chip from the same STM32F4 family).

### Why Bare Metal First?

This project deliberately implements all languages (C, Rust, Ada) at the **lowest level possible** — raw pointers, manual volatile access, no abstractions. The goal is to master the fundamentals:

- **C**: The baseline — direct register access via `volatile` pointers
- **Rust**: Raw `*mut u32` pointers with `read_volatile`/`write_volatile` — no PAC crates, no HAL, no `cortex-m-rt`
- **Ada**: Manual `Address` and `Volatile` aspects — no higher-level bindings

This approach means we **do not yet benefit** from language-specific safety features:
- Rust's ownership model and type-safe peripheral access (PAC/HAL crates)
- Ada's SPARK contracts, Ravenscar profile, and protected objects

**Why?** By staying close to C, you understand exactly what happens at the hardware level. There is no magic, no hidden abstractions. You control every bit.

In subsequent projects, we will progressively introduce higher-level features:
- **Project 2**: Type-safe register access patterns
- **Project 3**: Interrupt handlers with language-specific idioms
- **Project 7+**: Concurrency with Rust's `Send`/`Sync` and Ada's Ravenscar

> **Philosophy:** Master the foundation first, then build abstractions you understand.

> **Reference Implementation:** The complete, tested code is in [`code/01-led-blinker/`](../code/01-led-blinker/).
> This page explains key concepts with focused snippets; refer to the source files for complete implementations.

> **Tip:** If you already know one language, skim its section and focus on the others. The real value is in comparing approaches.

---

## 2. Target Hardware

| Property        | Value                              |
|-----------------|------------------------------------|
| Board           | NUCLEO-F446RE (also runs under QEMU's netduinoplus2) |
| MCU             | STM32F446RE              |
| Core            | ARM Cortex-M4F                     |
| Flash           | 512 KiB @ `0x08000000`             |
| SRAM            | 128 KiB @ `0x20000000`             |
| LED (User)      | PA5 (GPIO Port A, Pin 5)           |
| Button (User)   | PC13 (GPIO Port C, Pin 13, active LOW) |
| QEMU Machine    | `netduinoplus2`                    |

To implement button-controlled blinking:

1. Enable clocks for GPIOA and GPIOC via the RCC peripheral
2. Configure PA5 as push-pull output via `GPIOA_MODER`
3. Configure PC13 as input via `GPIOC_MODER`
4. Read `GPIOC_IDR` to detect button presses, toggle `GPIOA_ODR` with variable delay

---

## 3. Key Concepts

### 3.1 Startup Code

When a Cortex-M processor resets, it reads two 32-bit values from address `0x00000000`:

1. **Initial Main Stack Pointer (MSP)** — loaded directly into the stack pointer register
2. **Reset Handler Address** — the address of the first code to execute

This pair is the first entry in the **vector table**. After the reset handler runs, it typically:

- Zeroes the `.bss` section (uninitialized global variables)
- Copies `.data` from flash to RAM (initialized global variables)
- Calls `main()` (or the language equivalent)

> **Deep dive:** See [Linker Scripts & crt0.s Guide](00d-linker-crt0-guide.md) for a complete walkthrough of startup code, ELF sections, and the LMA vs VMA split.

### 3.2 Vector Table & Thumb State

The vector table is an array of function pointers at a known address. For this project we only need the first two entries (initial MSP and Reset Handler).

**Critical:** All exception handler addresses in the vector table must have bit 0 (LSB) set to 1. Cortex-M4 only supports Thumb instructions; if LSB=0, the processor faults immediately (Cortex-M4 Technical Reference Manual §2.3.4). The `.thumb_func` directive in `crt0.s` tells the assembler to set this bit automatically — the linker script is not involved.

### 3.3 Linker Script

The linker script tells the linker where to place each section in the target's memory map. A minimal script defines:

- **FLASH** region at `0x08000000`, length `512K`
- **RAM** region at `0x20000000`, length `128K`
- Sections: `.isr_vector`, `.text`, `.rodata`, `.data`, `.bss`

Symbols exported by the linker script (like `_sdata`, `_sbss`, `_stack_top`) form a **contract** with the startup code.

> **Deep dive:** See [Linker Scripts & crt0.s Guide](00d-linker-crt0-guide.md) for detailed section breakdowns and LMA/VMA semantics.

### 3.4 Memory-Mapped I/O

On ARM Cortex-M, all peripherals are accessed through memory-mapped registers. Writing to a specific address triggers hardware behavior — there is no `ioctl` or syscall. The `volatile` qualifier is critical: the compiler must not optimize away reads or writes to these addresses.

### 3.5 Clock Configuration

The STM32F4 starts up running on its internal 16 MHz HSI oscillator. GPIO peripherals live on the AHB1 bus and are **disabled by default** to save power. Before accessing any GPIO register, you must enable its clock via the RCC (Reset and Clock Control) peripheral.

---

## 4. Key Registers

| Register         | Address      | Description                                    |
|------------------|--------------|------------------------------------------------|
| `RCC_AHB1ENR`    | `0x40023830` | AHB1 peripheral clock enable. Bit 0 = GPIOA, Bit 2 = GPIOC |
| `GPIOA_MODER`    | `0x40020000` | GPIOA mode register. 2 bits per pin. `01` = output |
| `GPIOA_ODR`      | `0x40020014` | GPIOA output data register. Write 1 to set pin high |
| `GPIOC_MODER`    | `0x40020800` | GPIOC mode register. 2 bits per pin. `00` = input |
| `GPIOC_IDR`      | `0x40020810` | GPIOC input data register. Read pin state |

---

## 5. Language Notes: C

```
code/01-led-blinker/c/
├── Makefile
├── inc/main.h          # Register definitions
└── src/main.c          # Application logic
```

Shared startup from [`common/`](../code/01-led-blinker/common/):
- `linker.ld` — memory layout
- `crt0.s` — C runtime startup (copies `.data`, zeroes `.bss`, calls `main()`)

### 5.1 Volatile Register Access

Each peripheral register is defined as a `volatile` pointer to a fixed address. The `volatile` qualifier prevents the compiler from caching reads or optimizing away writes:

```c
#define RCC_AHB1ENR (*(volatile uint32_t *)(0x40023800U + 0x30U))
#define GPIOA_MODER (*(volatile uint32_t *)(0x40020000U + 0x00U))
```

Always use named constants (`RCC_GPIOA_CLK_EN`, `GPIO_MODE_OUTPUT`) instead of magic numbers.

### 5.2 Clear-Then-Set GPIO Pattern

Each pin's mode uses 2 bits in MODER. The clear-then-set idiom ensures only target bits are modified:

```c
GPIOA_MODER &= ~(GPIO_MODE_MASK << (LED_PIN * 2));   /* Clear mode bits */
GPIOA_MODER |= (GPIO_MODE_OUTPUT << (LED_PIN * 2));  /* Set to output   */
```

Writing `|=` without clearing first would corrupt adjacent bits if the previous value was non-zero.

### 5.3 Active-LOW Button Reading

The NUCLEO button connects PC13 to ground when pressed. Shift, mask, and invert:

```c
static bool button_is_pressed(void) {
  return !((GPIOC_IDR >> BUTTON_PIN) & 1U);
}
```

### 5.4 SysTick Timing

SysTick is a 24-bit down-counter (max ~1,048 ms at 16 MHz). Program reload, start, wait for flag, stop:

```c
SYSTICK_LOAD = cycles & SYSTICK_RELOAD_MASK;
SYSTICK_VAL = 0;
SYSTICK_CTRL = SYSTICK_ENABLE | SYSTICK_CLKSRC_CPU;
while ((SYSTICK_CTRL & SYSTICK_COUNTFLAG) == 0) {}
SYSTICK_CTRL = 0;
```

### 5.5 Edge Detection

Track previous button state to trigger only on rising edge (prevents multiple toggles while held):

```c
if (curr_button && !prev_button) {
  blink_fast = !blink_fast;
  delay_ms(200);  /* Crude debounce */
}
prev_button = curr_button;
```

See [Project 3: Button Interrupts & Debouncing](03-button-interrupts.md) for proper state-machine debouncing.

### 5.6 Build

```bash
cd code/01-led-blinker/c
make clean all
```

> **Full source:**
> - [`inc/main.h`](../code/01-led-blinker/c/inc/main.h) — Register definitions and constants
> - [`src/main.c`](../code/01-led-blinker/c/src/main.c) — Complete application logic

---

## 6. Language Notes: Rust

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

### 6.1 Bare Metal Without Crates

This implementation uses **zero external crates** — it shares the same startup code (`crt0.s`) and linker script (`linker.ld`) as C.

| Aspect | Typical `cortex-m-rt` | This Implementation |
|--------|-----------------------|---------------------|
| Entry point | `#[entry]` macro | `#[unsafe(no_mangle)] pub fn main()` |
| Startup code | Provided by crate | Shared `crt0.s` (same as C) |
| Linker script | `memory.x` + `link.x` | Shared `linker.ld` |

### 6.2 Raw Pointer Types

Registers are raw pointers — `*mut u32` for read-write, `*const u32` for read-only. The type system catches accidental writes to read-only registers:

```rust
pub const RCC_AHB1ENR: *mut u32  = 0x4002_3830 as *mut u32;
pub const GPIOC_IDR: *const u32  = 0x4002_0810 as *const u32; // read-only
```

The `_` separators in hex literals improve readability.

### 6.3 Volatile Access with Unsafe

Unlike C's transparent `volatile` macros, Rust requires explicit `read_volatile()` / `write_volatile()` calls inside `unsafe` blocks:

```rust
unsafe {
    RCC_AHB1ENR.write_volatile(RCC_AHB1ENR.read_volatile() | (1 << 0));
}
```

The `unsafe` block is a **contract**: you promise the pointer is valid and properly aligned.

### 6.4 No-Std Entry Point

```rust
#![no_std]
#![no_main]

#[unsafe(no_mangle)]
pub fn main() {
    // Application logic
    loop { /* ... */ }
}

#[panic_handler]
fn _panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
```

- `#![no_std]` — no heap, no I/O, no runtime
- `#![no_main]` — no standard entry point; `crt0.s` calls `main()`
- `#[unsafe(no_mangle)]` — edition 2024 syntax, prevents name mangling
- `#[panic_handler]` — mandatory for `no_std`; loops forever on panic

### 6.5 Build

```bash
cd code/01-led-blinker/rust
make clean all
```

> **Full source:**
> - [`src/target.rs`](../code/01-led-blinker/rust/src/target.rs) — Register definitions
> - [`src/main.rs`](../code/01-led-blinker/rust/src/main.rs) — Application logic
> - [`src/time.rs`](../code/01-led-blinker/rust/src/time.rs) — SysTick delay

---

## 7. Language Notes: Ada

```
code/01-led-blinker/ada/
├── led_blinker.gpr     # GNAT project file
├── led_blinker.adc     # Configuration pragmas
├── Makefile
└── src/
    ├── crt0.S           # Startup code (same logic as common/)
    ├── led_blinker.adb  # Main procedure
    ├── nucleo_f446re.ads # Package specification (registers, types)
    └── nucleo_f446re.adb # Package body (implementations)
```

Ada separates **specification** (`.ads`) from **implementation** (`.adb`). The specification declares types, constants, and subprogram signatures; the body provides implementations.

### 7.1 Modular Types

Ada uses modular types for unsigned integers with wraparound arithmetic (equivalent to C's `uint32_t`):

```ada
type UInt32 is mod 2 ** 32;
for UInt32'Size use 32;
pragma Provide_Shift_Operators (UInt32);
```

The pragma enables `Shift_Left` and `Shift_Right` functions.

### 7.2 Address Conversion

`System.Address` provides type-safe memory address representation:

```ada
RCC_AHB1ENR : constant System.Address := System'To_Address (16#40023830#);
```

### 7.3 Volatile Register Access

Ada overlays variables at hardware addresses using the `Address` and `Volatile` aspects:

```ada
procedure Led_Init is
   Rcc_Ahb1enr_Value : Register
      with Address => RCC_AHB1ENR, Volatile;
begin
   Rcc_Ahb1enr_Value := Rcc_Ahb1enr_Value or Register (RCC_AHB1ENR_GPIOAEN);
end Led_Init;
```

Variables are declared in the declarative region (between `is` and `begin`), not inline.

### 7.4 Bit Manipulation with Shift_Left

Ada provides explicit shift functions rather than operators:

```ada
Gpioa_Odr_Value := Gpioa_Odr_Value
   xor Register (Shift_Left (UInt32'(1), Natural (LED_PIN)));
```

`UInt32'(1)` is a qualified expression specifying the type of the literal.

### 7.5 Loop with Exit When

Ada's `exit when` provides clean loop termination:

```ada
loop
   exit when (Ctrl_Value and Register (SYSTICK_CTRL_COUNTFLAG)) /= 0;
end loop;
```

### 7.6 Main Procedure

```ada
with Nucleo_F446RE; use Nucleo_F446RE;

procedure Led_Blinker is
   Is_Fast : Boolean := False;
begin
   Led_Init;
   Button_Init;
   loop
      -- Edge detection and blink logic
      Delay_Ms (if Is_Fast then 100 else 500);
   end loop;
end Led_Blinker;
```

- `with ... use ...` — imports package and makes declarations visible
- `loop ... end loop` — infinite loop (like C's `while(1)`)
- Conditional expression: `if ... then ... else ...` (Ada 2012+)

### 7.7 Build

```bash
cd code/01-led-blinker/ada
make clean all
```

> **Full source:**
> - [`src/nucleo_f446re.ads`](../code/01-led-blinker/ada/src/nucleo_f446re.ads) — Package specification
> - [`src/nucleo_f446re.adb`](../code/01-led-blinker/ada/src/nucleo_f446re.adb) — Package body
> - [`src/led_blinker.adb`](../code/01-led-blinker/ada/src/led_blinker.adb) — Main procedure

---

## 8. Language Notes: Zig

> **Status:** Placeholder — implementation coming soon.
> Project scaffold: [`code/01-led-blinker/zig/`](../code/01-led-blinker/zig/)

Zig offers `comptime` address conversion (`@ptrFromInt`), explicit `*volatile` pointer types, and no hidden control flow.

---

## 9. Running and Debugging

### 9.1 Running in QEMU

All language implementations produce a binary that runs identically:

```bash
qemu-system-arm -machine netduinoplus2 -kernel led-blinker.bin -nographic
```

The LED state is internal to the emulated GPIO — no visible output appears in `-nographic` mode alone. Use GDB to verify toggling:

```bash
# Terminal 1: Start QEMU with GDB stub
qemu-system-arm -machine netduinoplus2 -kernel led-blinker.bin -nographic -s -S

# Terminal 2: Connect and watch GPIOA_ODR
arm-none-eabi-gdb -ex "target remote :1234" -ex "break main" \
  -ex "continue" -ex "watch *0x40020014" -ex "continue" led-blinker.elf
```

> **Detailed guide:** See [Emulator Setup & Usage Guide](00b-emulator-setup.md) for QEMU installation, GDB workflows, and advanced debugging flags. See [GDB Survival Guide](00c-gdb-survival-guide.md) for the full GDB command reference.

### 9.2 Running in Renode

[Renode](https://renode.io/) provides faster execution and better debugging than QEMU for this platform. The C, Rust, and Ada Makefiles include a `renode` target:

```bash
cd code/01-led-blinker/c
make renode
```

This loads the binary with the NUCLEO-F446RE platform configuration. Inside the Renode console:

```
(Nucleo-F446RE) logLevel -1 UserLED
```

You should see messages like:
```
[NOISY] UserLED: LED state changed to True
[NOISY] UserLED: LED state changed to False
```

### 9.3 Testing the Button

The button on PC13 is active LOW. To test speed toggling:

```
(Nucleo-F446RE) logLevel -1 UserLED
(Nucleo-F446RE) UserButton Press
(Nucleo-F446RE) # wait ~200ms for debounce delay
(Nucleo-F446RE) UserButton Release
```

You should see the LED toggling at a faster rate (100ms period vs 500ms slow). Press again to return to slow blink.

> **Note:** `UserButton` is registered on `sysbus` (not as a child of `gpioPortC`), so it can be accessed directly by short name.

### 9.4 GDB Debugging with Renode

```bash
# Terminal 1: Start Renode with GDB stub
renode -e '$$bin=@led-blinker.elf; $$repl=@../renode/nucleo-f446re.repl; include @../renode/led-blinker.resc; listen GDB 3333'

# Terminal 2: Connect
arm-none-eabi-gdb -ex "target remote :3333" -ex "break main" -ex "continue" led-blinker.elf
```

> **Renode setup:** See [Emulator Setup & Usage Guide](00b-emulator-setup.md) for installation and general usage.

---

## 10. Binary Size Comparison

| Language | .text | .bss | Total | Difference |
|----------|-------|------|-------|------------|
| C        | 244   | 0    | 244   | baseline   |
| Rust     | 280   | 0    | 280   | +36 bytes (+15%) |
| Ada      | 280   | 4    | 284   | +40 bytes (+16%) |

**Why are Rust and Ada larger than C?**

- **Rust** — Mandatory panic handler stub and lang items (+36 bytes)
- **Ada** — Elaboration mechanism (`adainit`), GNAT metadata in `.rodata`, and elaboration flag in `.bss` (+40 bytes)

> **Deep dive:** See [Compiler & Linker Switches](A01-compiler-linker-switches.md) for details on `-flto` and other optimization flags.

---

## 11. Deliverables

- [ ] LED blinks at default slow speed (500ms period)
- [ ] Button press toggles to fast blink (100ms period)
- [ ] Another press returns to slow blink
- [ ] Verify using Renode: `logLevel -1 UserLED` + `UserButton Press`/`Release`
- [ ] Verify binary sizes match [Binary Size Comparison](#10-binary-size-comparison) table
- [ ] Understand why peripheral clocks must be enabled before register access
- [ ] Understand output (MODER, ODR) vs input (MODER, IDR) GPIO configuration

---

## 12. What You Learned

| Concept                  | C                              | Rust                                  | Ada                                   |
|--------------------------|--------------------------------|---------------------------------------|---------------------------------------|
| **Entry point**          | `Reset_Handler` in assembly    | `#[unsafe(no_mangle)] pub fn main()`  | `procedure Led_Blinker`               |
| **Volatile access**      | `volatile` type qualifier      | `read_volatile` / `write_volatile`    | `Volatile` aspect + `Address` aspect  |
| **Linker script**        | Shared `linker.ld`             | Shared `linker.ld`                    | Shared `linker.ld`                    |
| **Startup code**         | Shared `crt0.s`                | Shared `crt0.s`                       | Own `crt0.S` (same logic)             |
| **No-std declaration**   | `-ffreestanding -nostdlib`     | `#![no_std] #![no_main]`              | `-nostdlib -nostartfiles`             |
| **Infinite loop**        | `while (1) {}`                 | `loop {}` (type `!`)                  | `loop ... end loop;`                  |
| **Bit shifts**           | `<<` operator                  | `<<` operator                         | `Shift_Left` function                 |
| **Module system**        | Header files (`.h`)            | Modules (`mod`)                       | Packages (`.ads` / `.adb`)            |
| **Binary size**          | 244 bytes                      | 280 bytes                             | 284 bytes                             |
| **Abstraction level**    | Raw pointers                   | Raw pointers (no PAC/HAL)             | Raw aspects (no high-level bindings)  |

> **Note:** All implementations use the same low-level approach in this project. Higher-level language features (Rust PAC/HAL, Ada SPARK/Ravenscar) will be introduced progressively in later projects.

---

## 13. Exercises

### Exercise 1: Modify Blink Timing

Change the slow blink period to 1000ms and the fast blink period to 50ms. Rebuild and verify in Renode.

**Hint:** Find `BLINK_SLOW_MS` and `BLINK_FAST_MS` constants in each implementation.

### Exercise 2: Three-Speed Mode

Modify the program to cycle through three speeds instead of two:
- Slow (1000ms)
- Medium (500ms)
- Fast (100ms)

**Hint:** Replace the boolean `blink_fast` with an integer counter (0, 1, 2) and use modulo arithmetic.

### Exercise 3: Toggle On/Off Mode

Instead of changing blink speed, make the button toggle the LED completely on or off:
- First press: LED stays on continuously
- Second press: LED stays off continuously
- Third press: LED returns to blinking

**Hint:** Introduce a state machine with three states: `BLINKING`, `ON`, `OFF`.

### Exercise 4: Long Press Detection

Detect a long press (held for >1 second) vs a short press:
- Short press: toggle blink speed
- Long press: toggle LED on/off mode

**Hint:** Record the time when the button is first pressed and compare when released.

---

## 14. Next Steps

You now understand the boot process, memory layout, and register access for bare-metal ARM. In [Project 2: UART Echo Server](02-uart-echo.md), you will add serial communication — learning baud rate calculation, polling vs. interrupt-driven I/O, and how each language handles peripheral configuration with more complexity.

> **Tip:** Before moving on, try modifying the blink rate, adding a second LED (if your target supports it), or replacing the busy-wait delay with the SysTick timer for more precise timing.

---

## 15. References

### STMicroelectronics Documentation
- [STM32F446 Reference Manual (RM0390)](https://www.st.com/resource/en/reference_manual/dm00135183-stm32f446xx-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) — Ch. 6: RCC, Ch. 7: GPIO
- [STM32F446RE Datasheet](https://www.st.com/resource/en/datasheet/stm32f446re.pdf)

### ARM Documentation
- [Cortex-M4 Technical Reference Manual](https://developer.arm.com/documentation/ddi0439/latest/) — Programmer's Model, SysTick
- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/) — B1.5: ARMv7-M exception model (vector table, exception model)
- [4SE03 ARM Architecture (PDF)](https://4se03.telecom-paris.fr/supports/architecture_se.pdf) — French overview
- [4SE03 ARM Assembly Guide](https://4se03.telecom-paris.fr/supports/asm-arm/) — French assembly tutorial

### Guides in This Course
- [Emulator Setup & Usage Guide](00b-emulator-setup.md) — QEMU and Renode installation, GDB workflows
- [GDB Survival Guide](00c-gdb-survival-guide.md) — Debugging reference
- [Linker Scripts & crt0.s Guide](00d-linker-crt0-guide.md) — Startup code, sections, LMA/VMA

### Startup Code & Linker Scripts
- [ARM Community: Writing your own startup code for Cortex-M](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/writing-your-own-startup-code-for-cortex-m)
- [ARM Community: Decoding the Startup file for Arm Cortex-M4](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/decoding-the-startup-file-for-arm-cortex-m4)
- [Wasil Zafar ARM Assembly Part 14](https://www.wasilzafar.com/pages/series/arm-assembly/arm-assembly-14-cortex-m-embedded.html) — Cortex-M assembly, crt0, linker scripts
- [GNU LD MEMORY Command](https://sourceware.org/binutils/docs/ld/MEMORY.html)
- [GNU LD SECTIONS Command](https://sourceware.org/binutils/docs/ld/SECTIONS.html)

### Tools & Emulation
- [QEMU STM32 Documentation](https://www.qemu.org/docs/master/system/arm/stm32.html)

### More
- [4SE03 Bare-Metal TP](https://4se03.telecom-paris.fr/tp) — Practical exercises (French)
- [Rowley crt0.s Documentation](https://www.rowleydownload.co.uk/arm/documentation/arm_crt0.htm) — crt0.s structure, sections, initialization steps
- [Embedded Artistry: Exploring Startup Implementations (Newlib ARM)](https://embeddedartistry.com/blog/2019/04/17/exploring-startup-implementations-newlib-arm/) — Newlib ARM startup analysis
- [Embedds: Programming STM32 with GNU Tools (Linker Script)](https://embedds.com/programming-stm32-discovery-using-gnu-tools-linker-script/) — Practical linker script tutorial
- [Understanding the Linker Script (Stack Overflow)](https://stackoverflow.com/questions/40532180/understanding-the-linkerscript-for-an-arm-cortex-m-microcontroller) — Detailed walkthrough with comments
