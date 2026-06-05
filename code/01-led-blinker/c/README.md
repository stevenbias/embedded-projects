# LED Blinker in C

This is the C implementation of the LED blinker project for the STM32F446RE microcontroller (NUCLEO-F446RE board).

## Comparison with Rust Version

| Metric | C | Rust |
|--------|------|------|
| **BIN size** | 244 bytes | 256 bytes |
| **ELF size** | 5,732 bytes | 5,832 bytes |
| **Text section** | 244 bytes | 88 bytes |
| **Dependencies** | None | None (bare-metal) |

C produces a slightly smaller binary but larger text section due to inline functions.

## Project Structure

```
c/
├── README.md           # This file
├── Makefile            # Build system
├── compile_commands.json
├── inc/
│   └── main.h          # Register definitions (~153 lines)
├── src/
│   └── main.c          # Application logic (~193 lines)
├── build/              # Object files
├── led-blinker.elf     # ELF binary
├── led-blinker.bin     # Binary for flashing/simulation
└── led-blinker.map     # Linker map file
```

## Build Prerequisites

- **arm-none-eabi-gcc** - ARM cross-compiler
- **make** - Build automation
- **st-flash** - For flashing to hardware (optional)
- **renode** - For simulation (optional)

## Building

```bash
# Release build (optimized)
make

# Debug build
make debug

# Clean build artifacts
make clean

# Check binary size
make size
```

## Flashing & Simulation

```bash
# Flash to hardware
make flash

# Erase flash
make erase

# Run in Renode simulator
make renode
```

## Code Overview

```c
int main(void) {
  led_init();
  button_init();

  bool blink_fast = 0;
  bool prev_button = 0;

  while (1) {
    bool curr_button = button_is_pressed();

    /* Rising edge detection */
    if (curr_button && !prev_button) {
      blink_fast = !blink_fast;
      delay_ms(200); /* Debounce */
    }
    prev_button = curr_button;

    led_toggle();
    delay_ms(blink_fast ? 100 : 500);
  }
}
```

## Key Differences from Rust

| Aspect | C | Rust |
|--------|------|------|
| Build system | Make + GCC | Cargo + rustc |
| Startup | Shared `common/crt0.s` | Shared `common/crt0.s` |
| Linker script | Shared `common/linker.ld` | Shared `common/linker.ld` |
| Register access | `volatile` pointers via macros | `volatile` raw pointers |
| Binary size | 244 bytes | 256 bytes |

## Features

- **Zero dependencies** - No HAL, no CMSIS, no standard library
- **Shared linker script** - Uses `common/linker.ld` (same as Rust)
- **Shared startup code** - Uses `common/crt0.s` (same as Rust)
- **Extensive documentation** - Every register access is explained
- **Direct hardware access** - Using volatile memory-mapped I/O

## File Details

### `inc/main.h`

Register definitions for:
- RCC (clock control)
- GPIOA/GPIOC (LED and button)
- SysTick (timing)

Each register includes reference manual citations and bit-field documentation.

### `src/main.c`

Application code implementing:
- `led_init()` / `led_toggle()` - LED control on PA5
- `button_init()` / `button_is_pressed()` - Button input on PC13
- `delay_ms()` - SysTick-based blocking delay
- `main()` - Main loop with edge detection and speed toggle

### `Makefile`

Build configuration:
- Compiler flags: `-mcpu=cortex-m4 -mthumb -mfloat-abi=hard`
- Optimization: `-Os -flto` (release), `-Og -g3` (debug)
- Warnings: `-Wall -Wextra -Werror`
