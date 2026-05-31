---
title: "Project 1: LED Blinker — Your First Bare Metal Program"
phase: 1
project: 1
---

# Project 1: LED Blinker — Your First Bare Metal Program

## Introduction

The LED blinker is the "Hello, World" of embedded development — but on bare metal, there is no standard library, no operating system, and no `main` function that just works. Everything from the moment the processor comes out of reset is your responsibility.

> **Resource:** For complementary bare-metal programming exercises (French), see the [4SE03 TP site](https://4se03.telecom-paris.fr/tp).

This project teaches you the foundational mechanics of bare-metal programming:

- How the processor boots and finds your code
- How memory is laid out via linker scripts
- How to talk to hardware through memory-mapped registers
- Why `volatile` is non-negotiable
- How to configure the system clock
- How to read a button (GPIO input) and control an LED (GPIO output)

You will implement the same LED blinker with **button speed control** in **C** and **Rust** (Ada and Zig placeholders), each targeting the STM32F405 (Cortex-M4F) running under QEMU's `netduinoplus2` machine. The same code also runs on the NUCLEO-F446RE (STM32F446) with no changes.

> **Reference Implementation:** The complete, tested code is in [`code/01-led-blinker/`](../code/01-led-blinker/).
> This page explains each technique in detail with annotated snippets; the source files contain clean, focused implementations.

> **Tip:** If you already know one language, skim its section and focus on the others. The real value is in comparing approaches.

## Target Hardware

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

## Key Concepts

### Startup Code

When a Cortex-M processor resets, it reads two 32-bit values from address `0x00000000`:

1. **Initial Main Stack Pointer (MSP)** — loaded directly into the stack pointer register
2. **Reset Handler Address** — the address of the first code to execute

This pair is the first entry in the **vector table**. After the reset handler runs, it typically:

- Zeroes the `.bss` section (uninitialized global variables)
- Copies `.data` from flash to RAM (initialized global variables)
- Calls `main()` (or the language equivalent)

> **Deep dive:** See [Linker Scripts & crt0.s Guide](00d-linker-crt0-guide.md) for a complete walkthrough of startup code, ELF sections, and the LMA vs VMA split.

### Vector Table & Thumb State

The vector table is an array of function pointers at a known address. For this project we only need the first two entries (initial MSP and Reset Handler).

**Critical:** All exception handler addresses in the vector table must have bit 0 (LSB) set to 1. Cortex-M4 only supports Thumb instructions; if LSB=0, the processor faults immediately (Cortex-M4 Technical Reference Manual §2.3.4). The `.thumb_func` directive in `crt0.s` tells the assembler to set this bit automatically — the linker script is not involved.

### Linker Script

The linker script tells the linker where to place each section in the target's memory map. A minimal script defines:

- **FLASH** region at `0x08000000`, length `512K`
- **RAM** region at `0x20000000`, length `128K`
- Sections: `.isr_vector`, `.text`, `.rodata`, `.data`, `.bss`

Symbols exported by the linker script (like `_sdata`, `_sbss`, `_stack_top`) form a **contract** with the startup code.

> **Deep dive:** See [Linker Scripts & crt0.s Guide](00d-linker-crt0-guide.md) for detailed section breakdowns and LMA/VMA semantics.

### Memory-Mapped I/O

On ARM Cortex-M, all peripherals are accessed through memory-mapped registers. Writing to a specific address triggers hardware behavior — there is no `ioctl` or syscall. The `volatile` qualifier is critical: the compiler must not optimize away reads or writes to these addresses.

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

> **Details:** Bit-level layouts and register flag definitions are documented in the source header [`code/01-led-blinker/c/inc/main.h`](../code/01-led-blinker/c/inc/main.h).

## Language Notes: C

> **Full source:** [`code/01-led-blinker/c/`](../code/01-led-blinker/c/)

```
code/01-led-blinker/c/
├── Makefile
├── inc/main.h          # Register definitions
└── src/main.c          # Application logic
```

Shared startup from [`common/`](../code/01-led-blinker/common/):
- `linker.ld` — memory layout
- `crt0.s` — C runtime startup (copies `.data`, zeroes `.bss`, calls `main()`)

### Register Access (main.h)

Each peripheral register is a `volatile` pointer to a fixed address. The `volatile` qualifier is critical: it prevents the compiler from caching reads or optimizing away writes — without it, your hardware may never see the writes.

```c
#define RCC_AHB1ENR (*(volatile uint32_t *)(0x40023800U + 0x30U))
#define GPIOA_MODER (*(volatile uint32_t *)(0x40020000U + 0x00U))
#define GPIOA_ODR   (*(volatile uint32_t *)(0x40020000U + 0x14U))
#define GPIOC_MODER (*(volatile uint32_t *)(0x40020800U + 0x00U))
#define GPIOC_IDR   (*(volatile uint32_t *)(0x40020800U + 0x10U))
```

The header also defines bit-position constants (`RCC_GPIOA_CLK_EN`, `GPIO_MODE_OUTPUT`, `SYSTICK_ENABLE`) — always prefer named constants over magic numbers, even in bare-metal code.

### GPIO Initialization

Before touching any GPIO register, enable the peripheral clock. The STM32F4 starts with all GPIO clocks disabled. Writes to a disabled peripheral are silently ignored.

```c
static void led_init(void) {
  RCC_AHB1ENR |= RCC_GPIOA_CLK_EN;                     /* Enable GPIOA clock */
  GPIOA_MODER &= ~(GPIO_MODE_MASK << (LED_PIN * 2));   /* Clear mode bits   */
  GPIOA_MODER |= (GPIO_MODE_OUTPUT << (LED_PIN * 2));   /* Set to output     */
}

static void button_init(void) {
  RCC_AHB1ENR |= RCC_GPIOC_CLK_EN;                     /* Enable GPIOC clock   */
  GPIOC_MODER &= ~(GPIO_MODE_MASK << (BUTTON_PIN * 2)); /* Input mode (default) */
}
```

**Why clear then set?** Each pin's mode uses 2 bits in MODER. Writing `|=` without clearing first would corrupt adjacent bits if the previous value was non-zero. The clear-then-set idiom is safe: it only touches the target bits.

### Button Reading with Active-LOW Invert

The NUCLEO button connects PC13 to ground when pressed (active LOW). An external pull-up holds it HIGH when released.

```c
static bool button_is_pressed(void) {
  return !((GPIOC_IDR >> BUTTON_PIN) & 1U);
}
```

**What this does:** shift the IDR value right by 13 (moving bit 13 to position 0), mask with `1U` to isolate it, then invert so pressed = `true`. Without the `& 1U` mask, higher bits in the shift result could produce false positives on some hardware configurations.

### Precise Delays with SysTick

SysTick is a 24-bit down-counter built into every Cortex-M core. It decrements on each processor cycle, providing predictable timing.

```c
void delay_ms(uint32_t ms) {
  uint32_t cycles = (HSI_CLOCK_HZ / 1000U) * ms;       /* 16K cycles/ms */
  SYSTICK_LOAD = cycles & SYSTICK_RELOAD_MASK;          /* 24-bit reload  */
  SYSTICK_VAL = 0;                                      /* Clear counter  */
  SYSTICK_CTRL = SYSTICK_ENABLE | SYSTICK_CLKSRC_CPU;   /* Start: cpu clk */

  while ((SYSTICK_CTRL & SYSTICK_COUNTFLAG) == 0) {}    /* Wait for zero  */

  SYSTICK_CTRL = 0;                                     /* Stop timer     */
}
```

At 16 MHz, the 24-bit counter allows single-shot delays up to ~1,048 ms. Longer delays require looping.

### Edge Detection in the Main Loop

Without edge detection, holding the button would toggle the blink speed dozens of times per iteration. The solution: track the previous button state and trigger only on the rising edge.

```c
int main(void) {
  led_init();
  button_init();

  bool blink_fast = 0;
  bool prev_button = 0;

  while (1) {
    bool curr_button = button_is_pressed();

    /* Rising edge: was released (0), now pressed (1) */
    if (curr_button && !prev_button) {
      blink_fast = !blink_fast;
      delay_ms(200);                        /* Skip contact bounce */
    }
    prev_button = curr_button;

    led_toggle();
    delay_ms(blink_fast ? BLINK_FAST_MS : BLINK_SLOW_MS);
  }
}
```

**On debouncing:** Mechanical switches bounce for 5–50 ms. The 200 ms delay after each press is a crude approach — see [Project 3: Button Interrupts & Debouncing](03-button-interrupts.md) for proper state-machine debouncing.

### Build

```bash
cd code/01-led-blinker/c
make clean all
```

Output: `led-blinker.elf` — **240 bytes** (`.text`).

## Language Notes: Rust

> **Full source:** [`code/01-led-blinker/rust/`](../code/01-led-blinker/rust/)

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

### Bare Metal Without Crates

Unlike most Rust embedded tutorials (which use `cortex-m` and `cortex-m-rt` crates), this implementation uses **zero external crates** — it shares the same startup code (`crt0.s`) and linker script (`linker.ld`) as C.

| Aspect | Typical `cortex-m-rt` | This Implementation |
|--------|-----------------------|---------------------|
| Entry point | `#[entry]` macro | `#[unsafe(no_mangle)] pub fn main()` |
| Startup code | Provided by crate | Shared `crt0.s` (same as C) |
| Linker script | `memory.x` + `link.x` | Shared `linker.ld` |
| Binary size | ~300+ bytes | **256 bytes** |

### Register Definitions (src/target.rs)

Registers are raw pointers — `*mut u32` for read-write, `*const u32` for read-only. The type system catches accidental writes to read-only registers:

```rust
pub const RCC_AHB1ENR: *mut u32   = 0x4002_3830 as *mut u32;
pub const GPIOA_MODER: *mut u32   = 0x4002_0000 as *mut u32;
pub const GPIOA_ODR: *mut u32     = 0x4002_0014 as *mut u32;
pub const GPIOC_MODER: *mut u32   = 0x4002_0800 as *mut u32;
pub const GPIOC_IDR: *const u32   = 0x4002_0810 as *const u32; // read-only
```

Note the `_` separators in hex literals — Rust allows them for readability, and the compiler ignores them.

### GPIO Initialization (src/main.rs)

Volatile access uses `read_volatile()` / `write_volatile()` on raw pointers. Unlike C's transparent `volatile` macros, every access in Rust is explicitly marked:

```rust
fn led_init() {
    unsafe {
        RCC_AHB1ENR.write_volatile(
            RCC_AHB1ENR.read_volatile() | (1 << 0)
        );
        let moder = GPIOA_MODER.read_volatile();
        GPIOA_MODER.write_volatile(
            (moder & !(0x3 << (LED_PIN * 2)))
                | (0x1 << (LED_PIN * 2)),
        );
    }
}

fn button_init() {
    unsafe {
        RCC_AHB1ENR.write_volatile(
            RCC_AHB1ENR.read_volatile() | (1 << 2)
        );
        GPIOC_MODER.write_volatile(
            GPIOC_MODER.read_volatile() & !(0x3 << (BUTTON_PIN * 2)),
        );
    }
}
```

The `unsafe` blocks are a **contract**: you are promising the pointer is valid and properly aligned. In bare-metal code, the burden is on the programmer — just like C.

### Button Reading

```rust
fn button_is_pressed() -> bool {
    unsafe {
        (GPIOC_IDR.read_volatile() >> BUTTON_PIN) & 1 == 0
    }
}
```

The same active-LOW logic as C, but expressed as a `bool` instead of an `int` — Rust's type system communicates intent more clearly.

### Entry Point and Main Loop (src/main.rs)

```rust
#![no_std]
#![no_main]

#[unsafe(no_mangle)]
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
    loop {}
}
```

**Key points:**
- `#![no_std]` — no heap, no I/O, no runtime. The `#[panic_handler]` is mandatory; without it the linker fails.
- `#[unsafe(no_mangle)]` — edition 2024 syntax (NOT the old `#[no_mangle]`). Prevents name mangling so `crt0.s` can call `main()` by symbol.
- The function never returns (`loop {}` is type `!`), so there is no return type.

### Build

```bash
cd code/01-led-blinker/rust
make clean all
```

Output: `led-blinker.elf` — **256 bytes** (20 bytes larger than C due to the mandatory panic handler stub).

## Language Notes: Ada

> **Status:** Placeholder — implementation coming soon.
> Project scaffold: [`code/01-led-blinker/ada/`](../code/01-led-blinker/ada/)

Ada uses `pragma Volatile` for hardware registers, modular types (`mod 2**32`) for unsigned wraparound arithmetic, and `System.Address` for memory-mapped I/O. The Light runtime handles `.data`/`.bss` initialization.

## Language Notes: Zig

> **Status:** Placeholder — implementation coming soon.
> Project scaffold: [`code/01-led-blinker/zig/`](../code/01-led-blinker/zig/)

Zig offers `comptime` address conversion (`@ptrFromInt`), explicit `*volatile` pointer types, and no hidden control flow. Uses inline assembly for startup code (`startup.zig`).

## Running in QEMU

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

## Running in Renode

[Renode](https://renode.io/) provides faster execution and better debugging than QEMU for this platform. The C and Rust Makefiles include a `renode` target:

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

### Testing the Button

The button on PC13 is active LOW. To test speed toggling:

```
(Nucleo-F446RE) logLevel -1 UserLED
(Nucleo-F446RE) UserButton Press
(Nucleo-F446RE) # wait ~200ms for debounce delay
(Nucleo-F446RE) UserButton Release
```

You should see the LED toggling at a faster rate (100ms period vs 500ms slow). Press again to return to slow blink.

> **Note:** `UserButton` is registered on `sysbus` (not as a child of `gpioPortC`), so it can be accessed directly by short name.

### GDB Debugging with Renode

```bash
# Terminal 1: Start Renode with GDB stub
renode -e '$$bin=@led-blinker.elf; $$repl=@../renode/nucleo-f446re.repl; include @../renode/led-blinker.resc; listen GDB 3333'

# Terminal 2: Connect
arm-none-eabi-gdb -ex "target remote :3333" -ex "break main" -ex "continue" led-blinker.elf
```

> **Renode setup:** See [Emulator Setup & Usage Guide](00b-emulator-setup.md) for installation and general usage.

## Deliverables

- [ ] LED blinks at default slow speed (500ms period)
- [ ] Button press toggles to fast blink (100ms period)
- [ ] Another press returns to slow blink
- [ ] Verify using Renode: `logLevel -1 UserLED` + `UserButton Press`/`Release`
- [ ] C binary: 240 bytes, Rust binary: 256 bytes
- [ ] Understand why peripheral clocks must be enabled before register access
- [ ] Understand output (MODER, ODR) vs input (MODER, IDR) GPIO configuration

## What You Learned

| Concept                  | C                              | Rust                                  |
|--------------------------|--------------------------------|---------------------------------------|
| **Entry point**          | `Reset_Handler` in assembly    | `#[unsafe(no_mangle)] pub fn main()`  |
| **Volatile access**      | `volatile` type qualifier      | `read_volatile` / `write_volatile`    |
| **Linker script**        | Shared `linker.ld`             | Shared `linker.ld`                    |
| **Startup code**         | Shared `crt0.s`                | Shared `crt0.s`                       |
| **No-std declaration**   | `-ffreestanding -nostdlib`     | `#![no_std] #![no_main]`              |
| **Infinite loop**        | `while (1) {}`                 | `loop {}` (type `!`)                  |
| **Binary size**          | 240 bytes                      | 256 bytes                             |

## Next Steps

You now understand the boot process, memory layout, and register access for bare-metal ARM. In [Project 2: UART Echo Server](02-uart-echo.md), you will add serial communication — learning baud rate calculation, polling vs. interrupt-driven I/O, and how each language handles peripheral configuration with more complexity.

> **Tip:** Before moving on, try modifying the blink rate, adding a second LED (if your target supports it), or replacing the busy-wait delay with the SysTick timer for more precise timing.

---

## References

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
