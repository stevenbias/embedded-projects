---
title: "Project 2: UART Echo Server — Talking to the Host"
phase: 1
project: 2
---

# Project 2: UART Echo Server — Talking to the Host

## Introduction

In [Project 1](01-led-blinker.md), you blinked an LED — but you had no way to know if your code was actually running beyond watching a register in GDB. UART (Universal Asynchronous Receiver-Transmitter) changes that. It gives your bare-metal program a bidirectional text channel to your host machine.

This project teaches you:

- How to configure a UART peripheral (baud rate, frame format, enable)
- The difference between polling and interrupt-driven I/O
- How to calculate baud rate register values from clock frequency
- How to build a minimal echo server: read a byte, write it back
- How each language handles blocking I/O, error types, and interrupt handlers

You will implement a UART echo server that receives characters and immediately transmits them back. When you type `hello` in your terminal, you should see `hello` echoed back.

> **Tip:** An echo server seems trivial, but it exercises every fundamental UART concept: clock gating, pin multiplexing, baud rate configuration, status register polling, and data register access.

## Target Hardware

| Property        | Value                              |
|-----------------|------------------------------------|
| Board           | Netduino Plus 2 (QEMU) / NUCLEO-F446RE (HW) |
| MCU             | STM32F405 / STM32F446              |
| Core            | ARM Cortex-M4F                     |
| UART Peripheral | USART2                             |
| TX Pin          | PA2 (Alternate Function 7)         |
| RX Pin          | PA3 (Alternate Function 7)         |
| QEMU Machine    | `netduinoplus2`                    |

QEMU's `netduinoplus2` machine connects USART2 to the host's stdin/stdout when using `-nographic` or `-serial mon:stdio`.

## UART Fundamentals

### Frame Format

UART is asynchronous — there is no clock line. Both sides must agree on the baud rate. A standard frame looks like:

```
Idle ─┐   ┌─ Start ─┬─ D0 ─┬─ D1 ─┬─ ... ─┬─ D7 ─┬─ Stop ─┐ Idle
      └───┴─────────┴──────┴──────┴────────┴──────┴────────┘
          1 bit     8 data bits                    1 bit
```

For this project, we use the most common configuration: **8 data bits, no parity, 1 stop bit** (8N1).

### Baud Rate Calculation

The STM32F4 USART baud rate is determined by:

```
USARTDIV = f_ck / (8 * (2 - OVER8) * baud)
```

Where `f_ck` is the USART clock frequency and `OVER8` is the oversampling mode (0 = 16x oversampling, the default).

With the default HSI clock of 16 MHz and a target baud rate of 115200:

```
USARTDIV = 16,000,000 / (16 * 115,200) = 8.68055...
```

The USART_BRR register splits this into mantissa and fraction:

```
Mantissa = 8
Fraction = 0.68055 * 16 = 10.89 ≈ 11 (0xB)

USART_BRR = (8 << 4) | 0xB = 0x8B
```

> **Warning:** Baud rate errors above 2% will cause framing errors. Always verify your calculated USARTDIV produces an acceptable error rate.

### Polling vs. Interrupt-Driven I/O

| Approach      | How it works                                       | Pros                        | Cons                          |
|---------------|----------------------------------------------------|-----------------------------|-------------------------------|
| **Polling**   | CPU repeatedly checks status register until ready  | Simple, no interrupt config | Wastes CPU cycles, blocks     |
| **Interrupt** | Peripheral raises IRQ when data ready / TX empty   | CPU can do other work       | More complex, needs ISR       |

This project implements **polling** in C and Zig (for simplicity) and **interrupt-driven** in Rust and Ada (to demonstrate each language's interrupt model).

## Key Registers for USART2

| Register         | Address      | Description                                         |
|------------------|--------------|-----------------------------------------------------|
| `RCC_APB1ENR`    | `0x40023840` | APB1 clock enable. Bit 17 = USART2EN               |
| `RCC_AHB1ENR`    | `0x40023830` | AHB1 clock enable. Bit 0 = GPIOAEN                 |
| `GPIOA_MODER`    | `0x40020000` | Mode register. Bits 5:4 and 7:6 = AF for PA2/PA3   |
| `GPIOA_AFRL`     | `0x40020020` | Alternate function low. Bits 11:8 and 15:12 = AF7  |
| `USART2_SR`      | `0x40004400` | Status register. Bit 7 = TXE, Bit 5 = RXNE         |
| `USART2_DR`      | `0x40004404` | Data register (read to receive, write to transmit) |
| `USART2_BRR`     | `0x40004408` | Baud rate register                                  |
| `USART2_CR1`     | `0x4000440C` | Control register 1. Bits 13/3/2 = USART/TE/RE enable |

### USART_SR Status Bits

| Bit | Name | Description                              |
|-----|------|------------------------------------------|
| 7   | TXE  | Transmit data register empty (1 = ready) |
| 6   | TC   | Transmission complete                    |
| 5   | RXNE | Read data register not empty (1 = data)  |
| 4   | IDLE | Idle line detected                       |
| 3   | ORE  | Overrun error                            |
| 0   | PE   | Parity error                             |

### USART_CR1 Enable Bits

| Bit | Name | Description                    |
|-----|------|--------------------------------|
| 13  | UE   | USART enable                   |
| 3   | TE   | Transmitter enable             |
| 2   | RE   | Receiver enable                |
| 5   | RXNEIE | RXNE interrupt enable        |

## Implementation: C (Polling)

### File Structure

```
uart-echo-c/
├── linker.ld
├── startup.s
├── main.c
└── Makefile
```

The linker script and startup assembly are identical to Project 1. Only `main.c` changes.

### `main.c`

```c
/* main.c — UART echo server for STM32F405 (polling) */

#include <stdint.h>

/* RCC Registers */
#define RCC_AHB1ENR     (*(volatile uint32_t *)0x40023830U)
#define RCC_APB1ENR     (*(volatile uint32_t *)0x40023840U)

/* GPIOA Registers */
#define GPIOA_BASE      0x40020000U
#define GPIOA_MODER     (*(volatile uint32_t *)(GPIOA_BASE + 0x00U))
#define GPIOA_AFRL      (*(volatile uint32_t *)(GPIOA_BASE + 0x20U))

/* USART2 Registers */
#define USART2_BASE     0x40004400U
#define USART2_SR       (*(volatile uint32_t *)(USART2_BASE + 0x00U))
#define USART2_DR       (*(volatile uint32_t *)(USART2_BASE + 0x04U))
#define USART2_BRR      (*(volatile uint32_t *)(USART2_BASE + 0x08U))
#define USART2_CR1      (*(volatile uint32_t *)(USART2_BASE + 0x0CU))

/* USART status flags */
#define USART_SR_TXE    (1U << 7)
#define USART_SR_RXNE   (1U << 5)

/* USART control bits */
#define USART_CR1_UE    (1U << 13)
#define USART_CR1_TE    (1U << 3)
#define USART_CR1_RE    (1U << 2)

/* Pin definitions */
#define TX_PIN          2
#define RX_PIN          3

/* Baud rate: 115200 @ 16 MHz => USARTDIV = 8.68 => 0x8B */
#define USART_BRR_VALUE 0x8BU

static void uart_init(void)
{
    /* Enable GPIOA clock */
    RCC_AHB1ENR |= (1U << 0);

    /* Enable USART2 clock on APB1 */
    RCC_APB1ENR |= (1U << 17);

    /* Configure PA2 (TX) and PA3 (RX) as alternate function (mode 10) */
    /* Clear existing mode bits for pins 2 and 3 */
    GPIOA_MODER &= ~((0x3U << (TX_PIN * 2)) | (0x3U << (RX_PIN * 2)));
    /* Set to alternate function */
    GPIOA_MODER |=  ((0x2U << (TX_PIN * 2)) | (0x2U << (RX_PIN * 2)));

    /* Set alternate function 7 (USART2) for PA2 and PA3 */
    GPIOA_AFRL &= ~((0xFU << (TX_PIN * 4)) | (0xFU << (RX_PIN * 4)));
    GPIOA_AFRL |=  ((0x7U << (TX_PIN * 4)) | (0x7U << (RX_PIN * 4)));

    /* Set baud rate */
    USART2_BRR = USART_BRR_VALUE;

    /* Enable USART2, transmitter, and receiver */
    USART2_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

static void uart_putc(char c)
{
    /* Wait until TXE is set */
    while (!(USART2_SR & USART_SR_TXE))
        ;
    USART2_DR = (uint32_t)c;
}

static char uart_getc(void)
{
    /* Wait until RXNE is set */
    while (!(USART2_SR & USART_SR_RXNE))
        ;
    return (char)(USART2_DR & 0xFFU);
}

static void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

int main(void)
{
    uart_init();

    uart_puts("\r\n=== UART Echo Server ===\r\n");
    uart_puts("Type characters to see them echoed back.\r\n\r\n");

    while (1) {
        char c = uart_getc();
        uart_putc(c);

        /* Echo newline as CRLF */
        if (c == '\r' || c == '\n') {
            uart_putc('\r');
            uart_putc('\n');
        }
    }

    return 0;
}
```

### `Makefile`

```makefile
# Makefile — UART Echo Server (C / STM32F405)

CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

CFLAGS  = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os -Wall -Wextra -ffreestanding -nostdlib

TARGET  = uart-echo
SRCS    = main.c startup.s
OBJS    = $(SRCS:.c=.o)
OBJS    := $(OBJS:.s=.o)

all: $(TARGET).bin size

$(TARGET).elf: $(OBJS) linker.ld
	$(CC) $(CFLAGS) -T linker.ld -o $@ $(OBJS)

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.s
	$(CC) $(CFLAGS) -c -o $@ $<

size: $(TARGET).elf
	$(SIZE) $<

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin

.PHONY: all clean size
```

### Build (C)

```bash
make
```

## Implementation: Rust (Interrupt-Driven)

### File Structure

```
uart-echo-rust/
├── Cargo.toml
├── .cargo/
│   └── config.toml
├── build.rs
├── memory.x
└── src/
    └── main.rs
```

### `Cargo.toml`

```toml
[package]
name = "uart-echo"
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
runner = "qemu-system-arm -machine netduinoplus2 -serial mon:stdio -nographic -kernel"
rustflags = [
  "-C", "link-arg=-Tlink.x",
]
```

### `memory.x`

```ld
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

use core::cell::RefCell;
use core::ptr::{read_volatile, write_volatile};

use cortex_m::interrupt::{free, Mutex};
use cortex_m::peripheral::NVIC;
use cortex_m_rt::{entry, exception, ExceptionFrame};
use panic_halt as _;

/* Peripheral addresses */
const RCC_AHB1ENR: *mut u32 = 0x4002_3830_u32 as *mut u32;
const RCC_APB1ENR: *mut u32 = 0x4002_3840_u32 as *mut u32;

const GPIOA_BASE: u32 = 0x4002_0000;
const GPIOA_MODER: *mut u32 = (GPIOA_BASE + 0x00) as *mut u32;
const GPIOA_AFRL:  *mut u32 = (GPIOA_BASE + 0x20) as *mut u32;

const USART2_BASE: u32 = 0x4000_4400;
const USART2_SR:   *mut u32 = (USART2_BASE + 0x00) as *mut u32;
const USART2_DR:   *mut u32 = (USART2_BASE + 0x04) as *mut u32;
const USART2_BRR:  *mut u32 = (USART2_BASE + 0x08) as *mut u32;
const USART2_CR1:  *mut u32 = (USART2_BASE + 0x0C) as *mut u32;

/* Bit definitions */
const USART_SR_TXE: u32   = 1 << 7;
const USART_SR_RXNE: u32  = 1 << 5;
const USART_CR1_UE: u32   = 1 << 13;
const USART_CR1_TE: u32   = 1 << 3;
const USART_CR1_RE: u32   = 1 << 2;
const USART_CR1_RXNEIE: u32 = 1 << 5;

const USART2_IRQ: u32 = 38; // IRQ number for USART2 in STM32F4

/* Shared receive buffer protected by a critical section mutex */
static RX_BYTE: Mutex<RefCell<Option<u8>>> = Mutex::new(RefCell::new(None));

fn uart_init() {
    unsafe {
        // Enable clocks
        write_volatile(RCC_AHB1ENR, read_volatile(RCC_AHB1ENR) | (1 << 0));
        write_volatile(RCC_APB1ENR, read_volatile(RCC_APB1ENR) | (1 << 17));

        // Configure PA2/PA3 as alternate function (mode 10)
        let moder = read_volatile(GPIOA_MODER);
        let moder = moder & !((0x3 << 4) | (0x3 << 6)); // Clear pins 2,3
        let moder = moder | ((0x2 << 4) | (0x2 << 6));  // Set AF
        write_volatile(GPIOA_MODER, moder);

        // Set AF7 for PA2/PA3
        let afrl = read_volatile(GPIOA_AFRL);
        let afrl = afrl & !((0xF << 8) | (0xF << 12));
        let afrl = afrl | ((0x7 << 8) | (0x7 << 12));
        write_volatile(GPIOA_AFRL, afrl);

        // Baud rate: 115200 @ 16 MHz
        write_volatile(USART2_BRR, 0x8B);

        // Enable USART, TX, RX, and RXNE interrupt
        write_volatile(
            USART2_CR1,
            USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE,
        );
    }
}

fn uart_putc(c: u8) {
    unsafe {
        while (read_volatile(USART2_SR) & USART_SR_TXE) == 0 {}
        write_volatile(USART2_DR, c as u32);
    }
}

fn uart_puts(s: &str) {
    for b in s.bytes() {
        uart_putc(b);
    }
}

#[entry]
fn main() -> ! {
    uart_init();

    uart_puts("\r\n=== UART Echo Server (Interrupt-Driven) ===\r\n");
    uart_puts("Type characters to see them echoed back.\r\n\r\n");

    // Enable USART2 interrupt in NVIC
    unsafe {
        NVIC::unmask(cortex_m::interrupt::InterruptNumber::new(USART2_IRQ as u16));
    }

    loop {
        // Check if we have a received byte and echo it
        free(|cs| {
            if let Some(byte) = RX_BYTE.borrow(cs).borrow_mut().take() {
                uart_putc(byte);
                if byte == b'\r' || byte == b'\n' {
                    uart_putc(b'\r');
                    uart_putc(b'\n');
                }
            }
        });

        // In a real application, the main loop would do other work here
        cortex_m::asm::wfi(); // Wait for interrupt (low power)
    }
}

#[interrupt]
fn USART2() {
    // Read the received byte and store it in the shared buffer
    unsafe {
        let sr = read_volatile(USART2_SR);
        if (sr & USART_SR_RXNE) != 0 {
            let byte = (read_volatile(USART2_DR) & 0xFF) as u8;
            free(|cs| {
                *RX_BYTE.borrow(cs).borrow_mut() = Some(byte);
            });
        }
    }
}

#[exception]
fn HardFault(_frame: &ExceptionFrame) -> ! {
    loop {}
}
```

### Build (Rust)

```bash
rustup target add thumbv7em-none-eabihf
cargo build --release
```

## Implementation: Ada

### File Structure

```
uart-echo-ada/
├── uart_echo.gpr
├── memmap.ld
├── startup.adb
├── main.adb
├── main.ads
├── stm32f4_uart.ads
└── stm32f4_uart.adb
```

### Project File (`uart_echo.gpr`)

```ada
project Uart_Echo is

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

   package Linker is
      for Default_Switches ("Ada") use (
         "-Tmemmap.ld",
         "-nostartfiles"
      );
   end Linker;

end Uart_Echo;
```

### UART Package Spec (`stm32f4_uart.ads`)

```ada
with Interfaces; use Interfaces;

package STM32F4_UART is
   pragma Preelaborate;

   type UInt32 is mod 2 ** 32;
   for UInt32'Size use 32;

   -- Register addresses
   RCC_AHB1ENR_Addr : constant := 16#4002_3830#;
   RCC_APB1ENR_Addr : constant := 16#4002_3840#;
   GPIOA_MODER_Addr : constant := 16#4002_0000#;
   GPIOA_AFRL_Addr  : constant := 16#4002_0020#;
   USART2_SR_Addr   : constant := 16#4000_4400#;
   USART2_DR_Addr   : constant := 16#4000_4404#;
   USART2_BRR_Addr  : constant := 16#4000_4408#;
   USART2_CR1_Addr  : constant := 16#4000_440C#;

   -- Volatile register access
   type Reg_Ptr is access all UInt32;
   pragma Volatile_Access (Reg_Ptr);

   function To_Reg_Ptr (Addr : System.Address) return Reg_Ptr;
   pragma Inline (To_Reg_Ptr);

   -- Initialization
   procedure UART_Init;

   -- Send a single character
   procedure UART_Put_C (C : Character);

   -- Send a string
   procedure UART_Puts (S : String);

   -- Receive a single character (blocking)
   function UART_Get_C return Character;

   -- Check if a character is available (non-blocking)
   function UART_Data_Ready return Boolean;

private

   TXE  : constant UInt32 := 2 ** 7;
   RXNE : constant UInt32 := 2 ** 5;
   UE   : constant UInt32 := 2 ** 13;
   TE   : constant UInt32 := 2 ** 3;
   RE   : constant UInt32 := 2 ** 2;

   BRR_115200 : constant UInt32 := 16#8B#;

end STM32F4_UART;
```

### UART Package Body (`stm32f4_uart.adb`)

```ada
with System; use System;

package body STM32F4_UART is

   function To_Reg_Ptr (Addr : System.Address) return Reg_Ptr is
      Result : Reg_Ptr;
      pragma Import (Ada, Result);
      for Result'Address use Addr;
      pragma Volatile (Result.all);
   begin
      return Result;
   end To_Reg_Ptr;

   procedure UART_Init is
      RCC_AHB1ENR : Reg_Ptr := To_Reg_Ptr (RCC_AHB1ENR_Addr'Address);
      RCC_APB1ENR : Reg_Ptr := To_Reg_Ptr (RCC_APB1ENR_Addr'Address);
      GPIOA_MODER : Reg_Ptr := To_Reg_Ptr (GPIOA_MODER_Addr'Address);
      GPIOA_AFRL  : Reg_Ptr := To_Reg_Ptr (GPIOA_AFRL_Addr'Address);
      USART2_BRR  : Reg_Ptr := To_Reg_Ptr (USART2_BRR_Addr'Address);
      USART2_CR1  : Reg_Ptr := To_Reg_Ptr (USART2_CR1_Addr'Address);
   begin
      -- Enable clocks
      RCC_AHB1ENR.all := RCC_AHB1ENR.all or 16#0000_0001#;
      RCC_APB1ENR.all := RCC_APB1ENR.all or (16#1# shift_left 17);

      -- Configure PA2 (TX) and PA3 (RX) as alternate function
      declare
         Moder : UInt32 := GPIOA_MODER.all;
      begin
         Moder := Moder and not ((16#3# shift_left 4) or (16#3# shift_left 6));
         Moder := Moder or ((16#2# shift_left 4) or (16#2# shift_left 6));
         GPIOA_MODER.all := Moder;
      end;

      -- Set AF7 for PA2 and PA3
      declare
         Afrl : UInt32 := GPIOA_AFRL.all;
      begin
         Afrl := Afrl and not ((16#F# shift_left 8) or (16#F# shift_left 12));
         Afrl := Afrl or ((16#7# shift_left 8) or (16#7# shift_left 12));
         GPIOA_AFRL.all := Afrl;
      end;

      -- Set baud rate and enable USART
      USART2_BRR.all := BRR_115200;
      USART2_CR1.all := UE or TE or RE;
   end UART_Init;

   procedure UART_Put_C (C : Character) is
      USART2_SR : Reg_Ptr := To_Reg_Ptr (USART2_SR_Addr'Address);
      USART2_DR : Reg_Ptr := To_Reg_Ptr (USART2_DR_Addr'Address);
   begin
      while (USART2_SR.all and TXE) = 0 loop
         null;
      end loop;
      USART2_DR.all := Character'Pos (C);
   end UART_Put_C;

   procedure UART_Puts (S : String) is
   begin
      for I in S'Range loop
         UART_Put_C (S (I));
      end loop;
   end UART_Puts;

   function UART_Get_C return Character is
      USART2_SR : Reg_Ptr := To_Reg_Ptr (USART2_SR_Addr'Address);
      USART2_DR : Reg_Ptr := To_Reg_Ptr (USART2_DR_Addr'Address);
   begin
      while (USART2_SR.all and RXNE) = 0 loop
         null;
      end loop;
      return Character'Val (UInt32 (USART2_DR.all and 16#FF#));
   end UART_Get_C;

   function UART_Data_Ready return Boolean is
      USART2_SR : Reg_Ptr := To_Reg_Ptr (USART2_SR_Addr'Address);
   begin
   return (USART2_SR.all and RXNE) /= 0;
end STM32F4_UART;
```

### `main.adb`

```ada
with STM32F4_UART; use STM32F4_UART;

procedure Main is
   C : Character;
begin
   UART_Init;

   UART_Puts (ASCII.CR & ASCII.LF);
   UART_Puts ("=== UART Echo Server (Ada) ===");
   UART_Puts (ASCII.CR & ASCII.LF);
   UART_Puts ("Type characters to see them echoed back.");
   UART_Puts (ASCII.CR & ASCII.LF & ASCII.CR & ASCII.LF);

   loop
      C := UART_Get_C;
      UART_Put_C (C);

      -- Echo newline as CRLF
      if C = ASCII.CR or else C = ASCII.LF then
         UART_Put_C (ASCII.CR);
         UART_Put_C (ASCII.LF);
      end if;
   end loop;
end Main;
```

### Build (Ada)

```bash
gprbuild -P uart_echo.gpr -p
arm-eabi-objcopy -O binary obj/main uart-echo.bin
```

## Implementation: Zig

### File Structure

```
uart-echo-zig/
├── build.zig
├── linker.ld
├── startup.zig
└── src/
    └── main.zig
```

The linker script and startup are the same as Project 1.

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
        .name = "uart-echo",
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
        .strip = false,
        .single_threaded = true,
    });

    exe.setLinkerScript(b.path("linker.ld"));
    exe.entry = .{ .symbol_name = "Reset_Handler" };

    b.installArtifact(exe);
}
```

### `src/main.zig`

```zig
// main.zig — UART echo server for STM32F405 (polling with error unions)

const std = @import("std");

// Error types for UART operations
const UartError = error{
    Timeout,
    FramingError,
    OverrunError,
    ParityError,
};

// Memory-mapped registers
const RCC_AHB1ENR = @as(*volatile u32, @ptrFromInt(0x40023830));
const RCC_APB1ENR = @as(*volatile u32, @ptrFromInt(0x40023840));
const GPIOA_MODER = @as(*volatile u32, @ptrFromInt(0x40020000));
const GPIOA_AFRL  = @as(*volatile u32, @ptrFromInt(0x40020020));
const USART2_SR   = @as(*volatile u32, @ptrFromInt(0x40004400));
const USART2_DR   = @as(*volatile u32, @ptrFromInt(0x40004404));
const USART2_BRR  = @as(*volatile u32, @ptrFromInt(0x40004408));
const USART2_CR1  = @as(*volatile u32, @ptrFromInt(0x4000440C));

// Bit definitions
const USART_SR_TXE: u32   = 1 << 7;
const USART_SR_RXNE: u32  = 1 << 5;
const USART_SR_ORE: u32   = 1 << 3;
const USART_SR_FE: u32    = 1 << 1;
const USART_SR_PE: u32    = 1 << 0;
const USART_CR1_UE: u32   = 1 << 13;
const USART_CR1_TE: u32   = 1 << 3;
const USART_CR1_RE: u32   = 1 << 2;

const TX_PIN: u32 = 2;
const RX_PIN: u32 = 3;

const Uart = struct {
    fn init() void {
        // Enable clocks
        RCC_AHB1ENR.* |= 1 << 0;
        RCC_APB1ENR.* |= 1 << 17;

        // Configure PA2/PA3 as alternate function (mode 10)
        const moder_mask = (0x3 << (TX_PIN * 2)) | (0x3 << (RX_PIN * 2));
        const moder_af = (0x2 << (TX_PIN * 2)) | (0x2 << (RX_PIN * 2));
        GPIOA_MODER.* = (GPIOA_MODER.* & ~moder_mask) | moder_af;

        // Set AF7 for PA2 and PA3
        const afrl_mask = (0xF << (TX_PIN * 4)) | (0xF << (RX_PIN * 4));
        const afrl_val = (0x7 << (TX_PIN * 4)) | (0x7 << (RX_PIN * 4));
        GPIOA_AFRL.* = (GPIOA_AFRL.* & ~afrl_mask) | afrl_val;

        // Set baud rate: 115200 @ 16 MHz
        USART2_BRR.* = 0x8B;

        // Enable USART, TX, RX
        USART2_CR1.* = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
    }

    fn putc(c: u8) UartError!void {
        var timeout: u32 = 0;
        while ((USART2_SR.* & USART_SR_TXE) == 0) : (timeout += 1) {
            if (timeout > 1_000_000) return UartError.Timeout;
        }
        USART2_DR.* = c;
    }

    fn getc() UartError!u8 {
        var timeout: u32 = 0;
        while ((USART2_SR.* & USART_SR_RXNE) == 0) : (timeout += 1) {
            if (timeout > 1_000_000) return UartError.Timeout;
        }

        const sr = USART2_SR.*;
        if ((sr & USART_SR_PE) != 0) return UartError.ParityError;
        if ((sr & USART_SR_FE) != 0) return UartError.FramingError;
        if ((sr & USART_SR_ORE) != 0) return UartError.OverrunError;

        return @truncate(USART2_DR.*);
    }

    fn puts(s: []const u8) void {
        for (s) |c| {
            putc(c) catch return;
        }
    }
};

pub fn main() noreturn {
    Uart.init();

    Uart.puts("\r\n=== UART Echo Server (Zig) ===\r\n");
    Uart.puts("Type characters to see them echoed back.\r\n\r\n");

    while (true) {
        const c = Uart.getc() catch continue;
        Uart.putc(c) catch continue;

        // Echo newline as CRLF
        if (c == '\r' or c == '\n') {
            Uart.putc('\r') catch continue;
            Uart.putc('\n') catch continue;
        }
    }
}
```

### Build (Zig)

```bash
zig build -Doptimize=ReleaseSmall
```

## Running in QEMU

### Start QEMU with Serial Console

```bash
qemu-system-arm -machine netduinoplus2 -serial mon:stdio -nographic \
  -kernel uart-echo.bin
```

> **Warning:** The order of `-serial mon:stdio` and `-nographic` matters. Place `-serial mon:stdio` **before** `-nographic` to ensure QEMU connects USART2 to your terminal.

### Expected Output

```
=== UART Echo Server ===
Type characters to see them echoed back.

hello
hello
world
world
```

Every character you type should be immediately echoed back. Pressing Enter should produce a clean newline.

### Exit QEMU

Press `Ctrl-A` then `x` to exit QEMU.

### GDB Verification

```bash
# Terminal 1
qemu-system-arm -machine netduinoplus2 -serial mon:stdio -nographic \
  -kernel uart-echo.bin -s -S

# Terminal 2
arm-none-eabi-gdb uart-echo.elf
(gdb) target remote :1234
(gdb) break main
(gdb) continue

# Watch USART2_DR to see data flowing
(gdb) watch *0x40004404
(gdb) continue
# Type a character in the QEMU terminal — the watch should trigger
```

## Deliverables

- [ ] UART echo server binary for all four languages
- [ ] Verified echo in QEMU `-nographic` mode (type characters, see them back)
- [ ] Baud rate calculation documented (show the math for 115200 @ 16 MHz)
- [ ] GPIO alternate function configuration verified (PA2/PA3 as AF7)
- [ ] For Rust: interrupt handler fires and RXNE flag is cleared by reading DR
- [ ] For Zig: error union types used for `putc` and `getc`
- [ ] Binary size comparison with Project 1 (UART code adds ~200-500 bytes)

## What You Learned

| Concept                  | C                              | Rust                                  | Ada                              | Zig                              |
|--------------------------|--------------------------------|---------------------------------------|----------------------------------|----------------------------------|
| **I/O model**            | Blocking polling               | Interrupt-driven with `Mutex<RefCell>`| Blocking polling                 | Blocking polling with error unions |
| **Error handling**       | None (assumes success)         | `Option<u8>` in shared state          | Return values                    | `error{...}!T` union types       |
| **Register access**      | `volatile` pointer macros      | `read_volatile` / `write_volatile`    | `pragma Volatile` access         | `*volatile` pointers             |
| **Shared state**         | Global variables               | `cortex_m::interrupt::Mutex`          | Package-level state              | Struct methods                   |
| **String output**        | Manual `while (*s)` loop       | `for b in s.bytes()`                  | `for I in S'Range`               | `for (s) |c|`                  |
| **Newline handling**     | Manual CRLF conversion         | Manual CRLF conversion                | `ASCII.CR` / `ASCII.LF`          | Manual CRLF conversion           |

## Next Steps

Your MCU can now talk to the outside world. In [Project 3: Button Interrupts & Debouncing](03-button-interrupts.md), you will add user input via a hardware button — learning EXTI configuration, NVIC interrupt priorities, software debouncing, and how each language handles atomic state and critical sections.

> **Tip:** Before moving on, try extending the echo server: add a simple command parser that recognizes commands like `help`, `status`, or `led on`/`led off` to control the LED from Project 1 over UART.

---

## References

### STMicroelectronics Documentation
- [STM32F4 Reference Manual (RM0090)](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-415-stm32f407-417-stm32f427-437-and-stm32f429-439-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) — Ch. 30: USART (BRR, SR, CR1, DR), Ch. 8: GPIO (AFRL, alternate function AF7)
- [STM32F405/407 Datasheet](https://www.st.com/resource/en/datasheet/stm32f405rg.pdf)

### ARM Documentation
- [Cortex-M4 Technical Reference Manual](https://developer.arm.com/documentation/ddi0439/latest/) — NVIC interrupt enable for USART2 (IRQ 38), exception priorities
- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/)

### Tools & Emulation
- [QEMU STM32 Documentation](https://www.qemu.org/docs/master/system/arm/stm32.html)
