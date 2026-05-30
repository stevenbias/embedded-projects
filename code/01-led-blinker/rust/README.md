# LED Blinker in Rust

This is the Rust implementation of the LED blinker project for the STM32F446RE microcontroller (NUCLEO-F446RE board).

## Comparison with C Version

| Metric | C | Rust |
|--------|------|------|
| **BIN size** | 236 bytes | 256 bytes |
| **ELF size** | 11,944 bytes | 5,832 bytes |
| **Text section** | ~100 bytes | 88 bytes |
| **Dependencies** | None | None (bare-metal) |

Rust is 20 bytes larger than C due to the panic handler stub.

## Project Structure

```
rust/
├── README.md           # This file
├── Cargo.toml          # No dependencies!
├── Cargo.lock
├── Makefile
├── build.rs            # Assembles crt0.s via arm-none-eabi-gcc
├── .cargo/config.toml  # Target: thumbv7em-none-eabihf
├── src/
│   ├── main.rs         # Application logic (~52 lines)
│   ├── target.rs       # Register addresses as raw pointers
│   └── time.rs         # SysTick delay function
├── led-blinker.elf     # ELF binary
└── led-blinker.bin     # Binary for flashing/simulation
```

## Build Prerequisites

- **rustup** - For managing Rust toolchains
- **arm-none-eabi-gcc** - ARM cross-compiler

### PATH Setup

The system has a conflicting `/usr/gnat/rust/bin` that must not take precedence:

```bash
export PATH="/home/sbias/.rustup/bin:/home/sbias/.cargo/bin:$PATH"
```

## Building

```bash
cd rust/led-blinker
export PATH="/home/sbias/.rustup/bin:/home/sbias/.cargo/bin:$PATH"
cargo build --release
```

## Output Files

- ELF: `target/thumbv7m-none-eabi/release/led-blinker`
- Binary: Create with `arm-none-eabi-objcopy -O binary input.elf output.bin`

## Code Overview

```rust
#![no_std]
#![no_main]

mod target;
use crate::target::*;

mod time;
use crate::time::*;

fn led_init() {
    unsafe {
        // Enable GPIOA clock on AHB1 bus
        RCC_AHB1ENR.write_volatile(RCC_AHB1ENR.read_volatile() | (1 << 0));
        // Configure PA5 as output: MODER5 = 0b01
        GPIOA_MODER.write_volatile(
            (GPIOA_MODER.read_volatile() & !(0x3 << (LED_PIN * 2)))
                | (0x1 << (LED_PIN * 2)),
        );
    }
}

fn button_is_pressed() -> bool {
    unsafe { (GPIOC_IDR.read_volatile() >> BUTTON_PIN) & 1 == 0 }
}

#[unsafe(no_mangle)]
pub fn main() {
    led_init();
    button_init();
    loop {
        led_toggle();
        delay_ms(500);
    }
}
```

## Key Differences from C

| Aspect | C | Rust |
|--------|------|------|
| Build system | Make + GCC | Cargo + rustc |
| Startup | Assembly (`common/crt0.s`) | Shared `common/crt0.s` |
| Linker script | `common/linker.ld` | Shared `common/linker.ld` |
| Dependencies | None | None |
| Binary size | 236 bytes | 256 bytes |

## Features

- **Zero external crates** - No cortex-m, no cortex-m-rt
- **Shared linker script** - Uses `common/linker.ld` (same as C)
- **Shared startup code** - Uses `common/crt0.s` (same as C)
- **Same register access** - Using `volatile` pointers
- **Vector table** - In shared `crt0.s`

## File Details

### `Cargo.toml`

```toml
[package]
name = "led-blinker"
version = "0.1.0"
edition = "2024"

[profile.release]
opt-level = "s"
lto = true
panic = "abort"
```

### `common/linker.ld` (shared with C)

Custom linker script that:
- Places vector table at `0x08000000`
- Defines FLASH (1MB) and RAM (128KB) regions
- Defines `.text`, `.rodata`, `.data`, `.bss` sections
- Provides `_sdata`, `_edata`, `_lma_sdata`, `_sbss`, `_ebss` symbols

### `src/main.rs`

- ~52 lines of Rust
- Direct hardware register access via volatile pointers
- SysTick-based delay using `target.rs` register definitions
- Rising-edge button detection with debounce delay
