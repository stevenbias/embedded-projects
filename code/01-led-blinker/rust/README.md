# LED Blinker in Rust

This is the Rust implementation of the LED blinker project for the STM32F405 microcontroller (NUCLEO-F446RE board).

## Comparison with C Version

| Metric | C | Rust |
|--------|------|------|
| **BIN size** | 170 bytes | 168 bytes |
| **ELF size** | 7,112 bytes | 5,468 bytes |
| **Text section** | ~100 bytes | 88 bytes |
| **Dependencies** | None | None (bare-metal) |

Rust is now smaller than C by 2 bytes!

## Project Structure

```
rust/
├── README.md           # This file
├── led-blinker.elf    # ELF binary
├── led-blinker.bin    # Binary for flashing
└── led-blinker/
    ├── Cargo.toml   # No dependencies!
    ├── linker.ld    # Custom linker script
    ├── src/
    │   └── main.rs # ~80 lines of Rust
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
#![no_mangle]

// Hardware register addresses
const RCC_AHB1ENR: *mut u32 = 0x40023830 as *mut u32;
const GPIOA_MODER: *mut u32 = 0x40020000 as *mut u32;
const GPIOA_ODR: *mut u32 = 0x40020014 as *mut u32;

// Vector table at flash start
#[link_section = ".vector_table"]
static VectorTable: [u32; 2] = [0x20000400, 0x08000500];

// Reset handler: zero .bss, copy .data, call main
unsafe extern "C" fn Reset() -> ! { ... }

fn main() {
    // Enable GPIOA clock
    // Configure PA5 as output
    loop {
        // Toggle LED
        delay_ms(500);
    }
}
```

## Key Differences from C

| Aspect | C | Rust |
|--------|------|------|
| Build system | Make + GCC | Cargo + rustc |
| Startup | Assembly (`startup.s`) | Rust (`Reset()`) |
| Linker script | `common/linker.ld` | `linker.ld` |
| Dependencies | None | None |
| Binary size | 170 bytes | 168 bytes |

## Features

- **Zero external crates** - No cortex-m, no cortex-m-rt
- **Custom linker script** - Matches C's approach
- **Manual startup** - .bss zeroing and .data copy in Rust
- **Same register access** - Using `volatile` pointers
- **Vector table** - In Rust source

## File Details

### `Cargo.toml`

```toml
[package]
name = "led-blinker"
version = "0.1.0"
edition = "2021"

[profile.release]
opt-level = "z"
lto = true
```

### `linker.ld`

Custom linker script that:
- Places vector table at `0x08000000`
- Defines `.text`, `.data`, `.bss` sections
- Provides `_sbss`, `_ebss`, `_sdata`, `_edata` symbols

### `src/main.rs`

- ~80 lines of Rust
- Manual .bss zeroing
- Manual .data copy
- Direct hardware register access
- SysTick-based delay