---
title: "Project 3: Button Interrupts & Debouncing"
phase: 1
project: 3
---

# Project 3: Button Interrupts & Debouncing

## Introduction

In [Project 1](01-led-blinker.md), you blinked an LED. In [Project 2](02-uart-echo.md), you added serial communication. Now you will add **user input** — a hardware button that triggers an interrupt, toggles the LED, and requires software debouncing to work reliably.

This is the first project where timing matters beyond a simple delay loop. Mechanical buttons do not produce clean digital signals — they bounce. A single press can generate dozens of spurious transitions over 5-50ms. Without debouncing, one press looks like many.

This project teaches you:

- How to configure EXTI (External Interrupt) lines on STM32
- How NVIC interrupt priorities work and why they matter
- Software debouncing algorithms (counter-based and state-machine)
- Atomic operations and critical sections for shared state
- How each language guarantees safe concurrent access to shared variables

> **Tip:** Debouncing is one of those problems that seems trivial until you try it without a debounce algorithm. The difference between "works on my desk" and "works in production" is often a few lines of debounce code.

## Target Hardware

| Property        | Value                              |
|-----------------|------------------------------------|
| Board           | Netduino Plus 2 (QEMU) / NUCLEO-F446RE (HW) |
| MCU             | STM32F405 / STM32F446              |
| Core            | ARM Cortex-M4F                     |
| LED             | PA5 (output)                       |
| Button          | PA0 (input, EXTI Line 0)           |
| QEMU Machine    | `netduinoplus2`                    |

In QEMU, you can simulate a button press by writing to the GPIO input register via the QEMU monitor or GDB.

## EXTI Configuration

### What is EXTI?

The EXTI (External Interrupt/Event Controller) connects GPIO pins to the NVIC. Each EXTI line (0-15) can be triggered by one GPIO pin with the same number across all ports. EXTI Line 0 can be connected to PA0, PB0, PC0, etc. — but only one at a time, selected via the SYSCFG (or AFIO on older chips) register.

On the STM32F4, the SYSCFG peripheral selects which port drives each EXTI line:

```
SYSCFG_EXTICR1[3:0] selects EXTI0 source:
  0000 = PA0
  0001 = PB0
  0010 = PC0
  ...
```

### Configuration Steps

1. **Enable SYSCFG clock** — `RCC_APB2ENR` bit 14
2. **Enable GPIOA clock** — `RCC_AHB1ENR` bit 0
3. **Configure PA0 as input** — `GPIOA_MODER` bits 1:0 = `00`
4. **Select PA0 as EXTI0 source** — `SYSCFG_EXTICR1` bits 3:0 = `0x0`
5. **Configure EXTI0 trigger** — `EXTI_RTSR` for rising edge, `EXTI_FTSR` for falling edge
6. **Unmask EXTI0** — `EXTI_IMR` bit 0 = 1
7. **Enable EXTI0 in NVIC** — NVIC ISER register, bit 6 (IRQ 6)
8. **Set interrupt priority** — NVIC IP register for EXTI0

### Key Registers

| Register            | Address      | Description                                          |
|---------------------|--------------|------------------------------------------------------|
| `RCC_APB2ENR`       | `0x40023844` | APB2 clock enable. Bit 14 = SYSCFGEN                |
| `RCC_AHB1ENR`       | `0x40023830` | AHB1 clock enable. Bit 0 = GPIOAEN                  |
| `SYSCFG_EXTICR1`    | `0x40013808` | EXTI0-3 configuration. Bits 3:0 = EXTI0 source      |
| `EXTI_IMR`          | `0x40013C00` | Interrupt mask register. Bit 0 = EXTI0 unmask       |
| `EXTI_RTSR`         | `0x40013C08` | Rising trigger selection. Bit 0 = EXTI0 rising edge |
| `EXTI_FTSR`         | `0x40013C0C` | Falling trigger selection. Bit 0 = EXTI0 falling edge |
| `EXTI_PR`           | `0x40013C14` | Pending register. Write 1 to clear pending interrupt |
| `GPIOA_MODER`       | `0x40020000` | Mode register. Bits 1:0 = `00` for input            |
| `GPIOA_ODR`         | `0x40020014` | Output data register. Bit 5 = LED                   |

### NVIC Interrupt Priorities

The Cortex-M4 NVIC supports 8 priority levels (3 bits implemented, values 0-7). **Lower number = higher priority**.

| Priority | Meaning                        |
|----------|--------------------------------|
| 0        | Highest priority               |
| 7        | Lowest priority                |

For this project, we set EXTI0 to priority 2 — high enough to respond quickly, but leaving room for higher-priority interrupts (like a fault handler or real-time timer).

## Software Debouncing

### The Problem

When a mechanical button is pressed or released, the contacts bounce:

```
Ideal:    ────┐           ┌────
              └───────────┘

Real:     ────┐ ┌─┐ ┌───┐ ┌────
              └─┘ └─┘ └───┘ └────
              ←── 5-50ms ──→
```

### Counter-Based Debouncing

The most common approach: sample the button at a fixed interval (e.g., every 1ms via SysTick), and require N consecutive identical readings before accepting a state change.

```
Debounce counter:
  - If current reading == previous stable state: reset counter
  - If current reading != previous stable state: increment counter
  - If counter >= DEBOUNCE_THRESHOLD: accept new state
```

A typical threshold is 10-20 samples at 1ms intervals = 10-20ms debounce time.

### State Machine Debouncing

A more structured approach using explicit states:

```
States: IDLE → MAYBE_PRESSED → PRESSED → MAYBE_RELEASED → IDLE

Transitions:
  IDLE:              button down → MAYBE_PRESSED
  MAYBE_PRESSED:     button still down after N ms → PRESSED (trigger action)
                     button up → IDLE
  PRESSED:           button up → MAYBE_RELEASED
  MAYBE_RELEASED:    button still up after N ms → IDLE
                     button down → PRESSED
```

This project uses the **counter-based approach** in the SysTick handler for simplicity, but the state machine is shown in the Zig implementation.

## Atomic Operations and Critical Sections

When an interrupt handler and the main loop share state, you must prevent data races. On Cortex-M4:

- **Critical sections** disable interrupts temporarily (via `cpsid i` / `cpsie i`)
- **Atomic operations** use LDREX/STREX instructions for lock-free access
- **Volatile** ensures the compiler does not cache the variable

Each language provides different abstractions for this:

| Language | Mechanism                                  |
|----------|--------------------------------------------|
| C        | `volatile` + `__disable_irq()` / `__enable_irq()` |
| Rust     | `AtomicBool` with `Ordering::SeqCst`       |
| Ada      | Protected objects (built-in mutual exclusion) |
| Zig      | `std.atomic.Atomic(u32)` with `.SeqCst`    |

## Implementation: C

### File Structure

```
button-interrupts-c/
├── linker.ld
├── startup.s
├── main.c
└── Makefile
```

### `startup.s` (Updated with EXTI0 Handler)

```armasm
/* startup.s — Cortex-M4 startup with EXTI0 handler */

    .syntax unified
    .cpu cortex-m4
    .thumb

    .extern _data_start
    .extern _data_end
    .extern _data_loadaddr
    .extern _bss_start
    .extern _bss_end

    .global Reset_Handler
    .global EXTI0_IRQHandler
    .global SysTick_Handler

    .section .text.Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
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

call_main:
    bl   main
hang:
    b    hang
    .size Reset_Handler, . - Reset_Handler

/* EXTI0 interrupt handler — declared weak so main.c can override */
    .weak EXTI0_IRQHandler
    .type EXTI0_IRQHandler, %function
EXTI0_IRQHandler:
    b    .
    .size EXTI0_IRQHandler, . - EXTI0_IRQHandler

/* SysTick handler — declared weak */
    .weak SysTick_Handler
    .type SysTick_Handler, %function
SysTick_Handler:
    b    .
    .size SysTick_Handler, . - SysTick_Handler

/* Default handler for all other exceptions */
    .section .text.Default_Handler
    .type Default_Handler, %function
Default_Handler:
    b    .
    .size Default_Handler, . - Default_Handler
```

### Updated Vector Table

The vector table in `linker.ld` needs to include the SysTick and EXTI0 handlers. Update the `.vector_table` section:

```ld
    .vector_table :
    {
        LONG(_stack_top)              /* 0: Initial MSP */
        LONG(Reset_Handler)           /* 1: Reset */
        LONG(Default_Handler)         /* 2: NMI */
        LONG(Default_Handler)         /* 3: HardFault */
        LONG(Default_Handler)         /* 4: MemManage */
        LONG(Default_Handler)         /* 5: BusFault */
        LONG(Default_Handler)         /* 6: UsageFault */
        . = . + 0x28;                 /* 7-10: Reserved */
        LONG(Default_Handler)         /* 11: SVCall */
        LONG(Default_Handler)         /* 12: DebugMon */
        . = . + 0x04;                 /* 13: Reserved */
        LONG(Default_Handler)         /* 14: PendSV */
        LONG(SysTick_Handler)         /* 15: SysTick */
        . = . + 0x20;                 /* 16-23: External 0-7 reserved */
        LONG(EXTI0_IRQHandler)        /* 24: EXTI Line 0 (IRQ 6 = index 22, but STM32 maps EXTI0 to IRQ 6) */
    } > FLASH
```

> **Warning:** The vector table index for EXTI0 is **22** (IRQ 6 + 16). The STM32F4 maps EXTI Line 0 to interrupt number 6 in the NVIC, which is index 22 in the vector table (16 system + 6).

### `main.c`

```c
/* main.c — Button interrupts with software debouncing (STM32F405) */

#include <stdint.h>

/* RCC Registers */
#define RCC_AHB1ENR     (*(volatile uint32_t *)0x40023830U)
#define RCC_APB2ENR     (*(volatile uint32_t *)0x40023844U)

/* SYSCFG Registers */
#define SYSCFG_BASE     0x40013800U
#define SYSCFG_EXTICR1  (*(volatile uint32_t *)(SYSCFG_BASE + 0x08U))

/* EXTI Registers */
#define EXTI_BASE       0x40013C00U
#define EXTI_IMR        (*(volatile uint32_t *)(EXTI_BASE + 0x00U))
#define EXTI_RTSR       (*(volatile uint32_t *)(EXTI_BASE + 0x08U))
#define EXTI_FTSR       (*(volatile uint32_t *)(EXTI_BASE + 0x0CU))
#define EXTI_PR         (*(volatile uint32_t *)(EXTI_BASE + 0x14U))

/* GPIOA Registers */
#define GPIOA_BASE      0x40020000U
#define GPIOA_MODER     (*(volatile uint32_t *)(GPIOA_BASE + 0x00U))
#define GPIOA_IDR       (*(volatile uint32_t *)(GPIOA_BASE + 0x10U))
#define GPIOA_ODR       (*(volatile uint32_t *)(GPIOA_BASE + 0x14U))

/* SysTick Registers */
#define SYSTICK_CSR     (*(volatile uint32_t *)0xE000E010U)
#define SYSTICK_RVR     (*(volatile uint32_t *)0xE000E014U)
#define SYSTICK_CVR     (*(volatile uint32_t *)0xE000E018U)

/* NVIC Registers */
#define NVIC_ISER0      (*(volatile uint32_t *)0xE000E100U)
#define NVIC_IPR1       (*(volatile uint32_t *)0xE000E404U)  /* Priority for IRQ 4-7 */

/* Bit definitions */
#define LED_PIN         5
#define BUTTON_PIN      0

#define EXTI_IMR_BIT    (1U << 0)
#define EXTI_RTSR_BIT   (1U << 0)
#define EXTI_PR_BIT     (1U << 0)

#define DEBOUNCE_THRESHOLD  10  /* 10ms at 1ms tick rate */

/* Shared state — volatile because accessed from ISR and main */
static volatile uint32_t debounce_counter = 0;
static volatile int button_pressed = 0;
static volatile int last_stable_state = 0;  /* 0 = released, 1 = pressed */

/* Cortex-M4 intrinsic functions (provided by compiler) */
extern void __disable_irq(void);
extern void __enable_irq(void);
extern uint32_t __get_PRIMASK(void);

static void systick_init(void)
{
    /* 1ms tick at 16 MHz: reload = 16000 - 1 */
    SYSTICK_RVR = 15999;
    SYSTICK_CVR = 0;
    /* Enable SysTick, enable interrupt, use processor clock */
    SYSTICK_CSR = (1U << 0) | (1U << 1) | (1U << 2);
}

static void exti_init(void)
{
    /* Enable SYSCFG clock */
    RCC_APB2ENR |= (1U << 14);

    /* Enable GPIOA clock */
    RCC_AHB1ENR |= (1U << 0);

    /* Configure PA0 as input (MODER0 = 00, already default) */
    GPIOA_MODER &= ~0x3U;

    /* Select PA0 as EXTI0 source */
    SYSCFG_EXTICR1 &= ~0xFU;  /* Bits 3:0 = 0 = PA0 */

    /* Configure EXTI0 for rising edge trigger */
    EXTI_RTSR |= EXTI_RTSR_BIT;

    /* Unmask EXTI0 interrupt */
    EXTI_IMR |= EXTI_IMR_BIT;

    /* Enable EXTI0 in NVIC (IRQ 6) */
    NVIC_ISER0 |= (1U << 6);

    /* Set EXTI0 priority to 2 (in NVIC_IPR1, bits 23:16 for IRQ 6) */
    /* Clear existing priority, then set to 2 */
    NVIC_IPR1 &= ~(0xFFU << 24);
    NVIC_IPR1 |= (2U << 24);
}

static void led_init(void)
{
    /* Enable GPIOA clock */
    RCC_AHB1ENR |= (1U << 0);

    /* Configure PA5 as output */
    GPIOA_MODER &= ~(0x3U << (LED_PIN * 2));
    GPIOA_MODER |=  (0x1U << (LED_PIN * 2));
}

static void led_toggle(void)
{
    GPIOA_ODR ^= (1U << LED_PIN);
}

/* SysTick interrupt handler — runs every 1ms */
void SysTick_Handler(void)
{
    /* Read current button state */
    int current_state = (GPIOA_IDR & (1U << BUTTON_PIN)) ? 1 : 0;

    if (current_state != last_stable_state) {
        debounce_counter++;
        if (debounce_counter >= DEBOUNCE_THRESHOLD) {
            /* State has been stable long enough — accept it */
            last_stable_state = current_state;
            debounce_counter = 0;

            if (current_state == 1) {
                button_pressed = 1;
            }
        }
    } else {
        debounce_counter = 0;
    }
}

/* EXTI0 interrupt handler — triggered by button edge */
void EXTI0_IRQHandler(void)
{
    /* Clear pending interrupt */
    EXTI_PR = EXTI_PR_BIT;

    /* The actual debouncing and action happens in SysTick.
     * This handler just acknowledges the interrupt.
     * Alternatively, you could start a timer here. */
}

int main(void)
{
    led_init();
    systick_init();
    exti_init();

    /* Enable global interrupts */
    __enable_irq();

    while (1) {
        if (button_pressed) {
            /* Critical section — clear flag and toggle LED atomically */
            __disable_irq();
            button_pressed = 0;
            __enable_irq();

            led_toggle();
        }

        /* Low-power: wait for next interrupt */
        __asm volatile ("wfi");
    }

    return 0;
}
```

### `Makefile`

```makefile
CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy

CFLAGS  = -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -mthumb -Os -Wall -Wextra -ffreestanding -nostdlib

TARGET  = button-interrupts
SRCS    = main.c startup.s
OBJS    = $(SRCS:.c=.o)
OBJS    := $(OBJS:.s=.o)

all: $(TARGET).bin

$(TARGET).elf: $(OBJS) linker.ld
	$(CC) $(CFLAGS) -T linker.ld -o $@ $(OBJS)

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.s
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin

.PHONY: all clean
```

### Build (C)

```bash
make
```

## Implementation: Rust

### `Cargo.toml`

```toml
[package]
name = "button-interrupts"
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

### `src/main.rs`

```rust
#![no_std]
#![no_main]

use core::ptr::{read_volatile, write_volatile};
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};

use cortex_m::peripheral::NVIC;
use cortex_m_rt::{entry, exception, ExceptionFrame};
use panic_halt as _;

/* Peripheral addresses */
const RCC_AHB1ENR: *mut u32 = 0x4002_3830 as *mut u32;
const RCC_APB2ENR: *mut u32 = 0x4002_3844 as *mut u32;

const SYSCFG_BASE: u32 = 0x4001_3800;
const SYSCFG_EXTICR1: *mut u32 = (SYSCFG_BASE + 0x08) as *mut u32;

const EXTI_BASE: u32 = 0x4001_3C00;
const EXTI_IMR: *mut u32    = (EXTI_BASE + 0x00) as *mut u32;
const EXTI_RTSR: *mut u32   = (EXTI_BASE + 0x08) as *mut u32;
const EXTI_PR: *mut u32     = (EXTI_BASE + 0x14) as *mut u32;

const GPIOA_BASE: u32 = 0x4002_0000;
const GPIOA_MODER: *mut u32 = (GPIOA_BASE + 0x00) as *mut u32;
const GPIOA_IDR:   *mut u32 = (GPIOA_BASE + 0x10) as *mut u32;
const GPIOA_ODR:   *mut u32 = (GPIOA_BASE + 0x14) as *mut u32;

const SYSTICK_CSR: *mut u32 = 0xE000_E010 as *mut u32;
const SYSTICK_RVR: *mut u32 = 0xE000_E014 as *mut u32;
const SYSTICK_CVR: *mut u32 = 0xE000_E018 as *mut u32;

const NVIC_ISER0: *mut u32 = 0xE000_E100 as *mut u32;
const NVIC_IPR1:  *mut u32 = 0xE000_E404 as *mut u32;

const EXTI0_IRQ: u16 = 6;
const DEBOUNCE_THRESHOLD: u32 = 10;

/* Atomic shared state — safe to access from ISR and main */
static BUTTON_PRESSED: AtomicBool = AtomicBool::new(false);
static DEBOUNCE_COUNTER: AtomicU32 = AtomicU32::new(0);
static LAST_STABLE_STATE: AtomicBool = AtomicBool::new(false);

fn led_init() {
    unsafe {
        write_volatile(RCC_AHB1ENR, read_volatile(RCC_AHB1ENR) | (1 << 0));

        let moder = read_volatile(GPIOA_MODER);
        let moder = moder & !(0x3 << 10); // Clear bits 11:10
        let moder = moder | (0x1 << 10);  // Set PA5 to output
        write_volatile(GPIOA_MODER, moder);
    }
}

fn systick_init() {
    unsafe {
        write_volatile(SYSTICK_RVR, 15999);
        write_volatile(SYSTICK_CVR, 0);
        // Enable SysTick, enable interrupt, processor clock
        write_volatile(SYSTICK_CSR, 0x7);
    }
}

fn exti_init() {
    unsafe {
        // Enable SYSCFG clock
        write_volatile(RCC_APB2ENR, read_volatile(RCC_APB2ENR) | (1 << 14));

        // Enable GPIOA clock
        write_volatile(RCC_AHB1ENR, read_volatile(RCC_AHB1ENR) | (1 << 0));

        // PA0 as input (MODER0 = 00, already default)

        // Select PA0 as EXTI0 source
        let exticr1 = read_volatile(SYSCFG_EXTICR1);
        write_volatile(SYSCFG_EXTICR1, exticr1 & !0xF);

        // Rising edge trigger
        write_volatile(EXTI_RTSR, read_volatile(EXTI_RTSR) | 0x1);

        // Unmask EXTI0
        write_volatile(EXTI_IMR, read_volatile(EXTI_IMR) | 0x1);

        // Enable EXTI0 in NVIC
        write_volatile(NVIC_ISER0, read_volatile(NVIC_ISER0) | (1 << 6));

        // Set priority to 2 (bits 23:16 in IPR1 for IRQ 6)
        let ipr1 = read_volatile(NVIC_IPR1);
        write_volatile(NVIC_IPR1, (ipr1 & !(0xFF << 24)) | (2 << 24));
    }
}

fn led_toggle() {
    unsafe {
        let odr = read_volatile(GPIOA_ODR);
        write_volatile(GPIOA_ODR, odr ^ (1 << 5));
    }
}

#[entry]
fn main() -> ! {
    led_init();
    systick_init();
    exti_init();

    // Enable global interrupts
    unsafe {
        cortex_m::interrupt::enable();
    }

    loop {
        if BUTTON_PRESSED.load(Ordering::SeqCst) {
            BUTTON_PRESSED.store(false, Ordering::SeqCst);
            led_toggle();
        }

        cortex_m::asm::wfi();
    }
}

#[exception]
fn SysTick() {
    let idr = unsafe { read_volatile(GPIOA_IDR) };
    let current_state = (idr & (1 << 0)) != 0;
    let last_stable = LAST_STABLE_STATE.load(Ordering::SeqCst);

    if current_state != last_stable {
        let counter = DEBOUNCE_COUNTER.load(Ordering::SeqCst);
        if counter + 1 >= DEBOUNCE_THRESHOLD {
            LAST_STABLE_STATE.store(current_state, Ordering::SeqCst);
            DEBOUNCE_COUNTER.store(0, Ordering::SeqCst);

            if current_state {
                BUTTON_PRESSED.store(true, Ordering::SeqCst);
            }
        } else {
            DEBOUNCE_COUNTER.store(counter + 1, Ordering::SeqCst);
        }
    } else {
        DEBOUNCE_COUNTER.store(0, Ordering::SeqCst);
    }
}

#[exception]
fn EXTI0() {
    // Clear pending interrupt
    unsafe {
        write_volatile(EXTI_PR, 0x1);
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
button-interrupts-ada/
├── button_interrupts.gpr
├── memmap.ld
├── startup.adb
├── main.adb
├── main.ads
├── hardware.ads
├── hardware.adb
└── button_handler.ads
```

### `hardware.ads`

```ada
with System; use System;
with Interfaces; use Interfaces;

package Hardware is
   pragma Preelaborate;

   type UInt32 is mod 2 ** 32;
   for UInt32'Size use 32;

   type UInt32_Access is access all UInt32;

   -- RCC
   RCC_AHB1ENR_Addr : constant := 16#4002_3830#;
   RCC_APB2ENR_Addr : constant := 16#4002_3844#;

   -- SYSCFG
   SYSCFG_EXTICR1_Addr : constant := 16#4001_3808#;

   -- EXTI
   EXTI_IMR_Addr  : constant := 16#4001_3C00#;
   EXTI_RTSR_Addr : constant := 16#4001_3C08#;
   EXTI_PR_Addr   : constant := 16#4001_3C14#;

   -- GPIOA
   GPIOA_MODER_Addr : constant := 16#4002_0000#;
   GPIOA_IDR_Addr   : constant := 16#4002_0010#;
   GPIOA_ODR_Addr   : constant := 16#4002_0014#;

   -- SysTick
   SYSTICK_CSR_Addr : constant := 16#E000_E010#;
   SYSTICK_RVR_Addr : constant := 16#E000_E014#;
   SYSTICK_CVR_Addr : constant := 16#E000_E018#;

   -- NVIC
   NVIC_ISER0_Addr : constant := 16#E000_E100#;
   NVIC_IPR1_Addr  : constant := 16#E000_E404#;

   -- Constants
   LED_PIN         : constant := 5;
   BUTTON_PIN      : constant := 0;
   DEBOUNCE_LIMIT  : constant := 10;

   -- Volatile register access
   function Reg (Addr : System.Address) return UInt32_Access;
   pragma Inline (Reg);

   -- Initialization procedures
   procedure LED_Init;
   procedure SysTick_Init;
   procedure EXTI_Init;

   -- LED toggle
   procedure LED_Toggle;

   -- Read button state
   function Button_State return Boolean;

   -- Clear EXTI pending bit
   procedure EXTI_Clear_Pending;

end Hardware;
```

### `hardware.adb`

```ada
package body Hardware is

   function Reg (Addr : System.Address) return UInt32_Access is
      Result : UInt32_Access;
      for Result'Address use Addr;
      pragma Import (Ada, Result);
      pragma Volatile (Result.all);
   begin
      return Result;
   end Reg;

   procedure LED_Init is
      RCC_AHB1ENR : constant UInt32_Access := Reg (RCC_AHB1ENR_Addr'Address);
      GPIOA_MODER : constant UInt32_Access := Reg (GPIOA_MODER_Addr'Address);
   begin
      RCC_AHB1ENR.all := RCC_AHB1ENR.all or 16#0000_0001#;

      declare
         Moder : UInt32 := GPIOA_MODER.all;
         Shift : constant UInt32 := UInt32 (LED_PIN * 2);
      begin
         Moder := Moder and not (16#3# shift_left Shift);
         Moder := Moder or (16#1# shift_left Shift);
         GPIOA_MODER.all := Moder;
      end;
   end LED_Init;

   procedure SysTick_Init is
      SYSTICK_CSR : constant UInt32_Access := Reg (SYSTICK_CSR_Addr'Address);
      SYSTICK_RVR : constant UInt32_Access := Reg (SYSTICK_RVR_Addr'Address);
      SYSTICK_CVR : constant UInt32_Access := Reg (SYSTICK_CVR_Addr'Address);
   begin
      SYSTICK_RVR.all := 15999;
      SYSTICK_CVR.all := 0;
      SYSTICK_CSR.all := 16#7#;  -- Enable, interrupt, processor clock
   end SysTick_Init;

   procedure EXTI_Init is
      RCC_AHB1ENR : constant UInt32_Access := Reg (RCC_AHB1ENR_Addr'Address);
      RCC_APB2ENR : constant UInt32_Access := Reg (RCC_APB2ENR_Addr'Address);
      SYSCFG_EXTICR1 : constant UInt32_Access := Reg (SYSCFG_EXTICR1_Addr'Address);
      EXTI_IMR  : constant UInt32_Access := Reg (EXTI_IMR_Addr'Address);
      EXTI_RTSR : constant UInt32_Access := Reg (EXTI_RTSR_Addr'Address);
      NVIC_ISER0 : constant UInt32_Access := Reg (NVIC_ISER0_Addr'Address);
      NVIC_IPR1  : constant UInt32_Access := Reg (NVIC_IPR1_Addr'Address);
   begin
      -- Enable clocks
      RCC_APB2ENR.all := RCC_APB2ENR.all or (16#1# shift_left 14);
      RCC_AHB1ENR.all := RCC_AHB1ENR.all or 16#0000_0001#;

      -- PA0 as input (default, but clear explicitly)
      -- GPIOA_MODER bits 1:0 = 00 (already set)

      -- Select PA0 as EXTI0 source
      SYSCFG_EXTICR1.all := SYSCFG_EXTICR1.all and not 16#F#;

      -- Rising edge trigger
      EXTI_RTSR.all := EXTI_RTSR.all or 16#1#;

      -- Unmask EXTI0
      EXTI_IMR.all := EXTI_IMR.all or 16#1#;

      -- Enable EXTI0 in NVIC (IRQ 6)
      NVIC_ISER0.all := NVIC_ISER0.all or (16#1# shift_left 6);

      -- Set priority to 2
      declare
         IPR1 : UInt32 := NVIC_IPR1.all;
      begin
         IPR1 := IPR1 and not (16#FF# shift_left 24);
         IPR1 := IPR1 or (16#2# shift_left 24);
         NVIC_IPR1.all := IPR1;
      end;
   end EXTI_Init;

   procedure LED_Toggle is
      GPIOA_ODR : constant UInt32_Access := Reg (GPIOA_ODR_Addr'Address);
   begin
      GPIOA_ODR.all := GPIOA_ODR.all xor (16#1# shift_left LED_PIN);
   end LED_Toggle;

   function Button_State return Boolean is
      GPIOA_IDR : constant UInt32_Access := Reg (GPIOA_IDR_Addr'Address);
   begin
      return (GPIOA_IDR.all and (16#1# shift_left BUTTON_PIN)) /= 0;
   end Button_State;

   procedure EXTI_Clear_Pending is
      EXTI_PR : constant UInt32_Access := Reg (EXTI_PR_Addr'Address);
   begin
      EXTI_PR.all := 16#1#;  -- Write 1 to clear
   end EXTI_Clear_Pending;

end Hardware;
```

### `button_handler.ads` — Protected Object

```ada
with Hardware; use Hardware;

package Button_Handler is
   pragma Preelaborate;

   -- Protected object provides mutual exclusion for shared state
   protected Debouncer is
      -- Called from SysTick ISR every 1ms
      procedure Tick;

      -- Called from main loop to check and consume button press
      function Get_Press return Boolean;
   private
      Counter          : UInt32 := 0;
      Last_Stable      : Boolean := False;
      Press_Pending    : Boolean := False;
   end Debouncer;

end Button_Handler;
```

### `button_handler.adb`

```ada
package body Button_Handler is

   protected body Debouncer is

      procedure Tick is
         Current : constant Boolean := Button_State;
      begin
         if Current /= Last_Stable then
            Counter := Counter + 1;
            if Counter >= DEBOUNCE_LIMIT then
               Last_Stable := Current;
               Counter := 0;

               if Current then
                  Press_Pending := True;
               end if;
            end if;
         else
            Counter := 0;
         end if;
      end Tick;

      function Get_Press return Boolean is
         Result : Boolean := Press_Pending;
      begin
         if Press_Pending then
            Press_Pending := False;
         end if;
         return Result;
      end Get_Press;

   end Debouncer;

end Button_Handler;
```

### `main.adb`

```ada
with Hardware; use Hardware;
with Button_Handler;

procedure Main is
begin
   LED_Init;
   SysTick_Init;
   EXTI_Init;

   -- Main loop
   loop
      if Button_Handler.Debouncer.Get_Press then
         LED_Toggle;
      end if;

      -- In a Ravenscar runtime, we can use delay_until or similar
      -- For bare metal, just loop — the ISR does the real work
      null;
   end loop;
end Main;
```

### Build (Ada)

```bash
gprbuild -P button_interrupts.gpr -p
arm-eabi-objcopy -O binary obj/main button-interrupts.bin
```

> **Note:** The Ravenscar runtime automatically handles interrupt handler registration and critical sections within protected objects. The `protected` type in Ada provides built-in mutual exclusion — no manual interrupt disable/enable needed.

## Implementation: Zig

### `src/main.zig`

```zig
// main.zig — Button interrupts with atomic state machine (STM32F405)

const std = @import("std");
const atomic = std.atomic;

// Register addresses
const RCC_AHB1ENR = @as(*volatile u32, @ptrFromInt(0x40023830));
const RCC_APB2ENR = @as(*volatile u32, @ptrFromInt(0x40023844));
const SYSCFG_EXTICR1 = @as(*volatile u32, @ptrFromInt(0x40013808));
const EXTI_IMR = @as(*volatile u32, @ptrFromInt(0x40013C00));
const EXTI_RTSR = @as(*volatile u32, @ptrFromInt(0x40013C08));
const EXTI_PR = @as(*volatile u32, @ptrFromInt(0x40013C14));
const GPIOA_MODER = @as(*volatile u32, @ptrFromInt(0x40020000));
const GPIOA_IDR = @as(*volatile u32, @ptrFromInt(0x40020010));
const GPIOA_ODR = @as(*volatile u32, @ptrFromInt(0x40020014));
const SYSTICK_CSR = @as(*volatile u32, @ptrFromInt(0xE000E010));
const SYSTICK_RVR = @as(*volatile u32, @ptrFromInt(0xE000E014));
const SYSTICK_CVR = @as(*volatile u32, @ptrFromInt(0xE000E018));
const NVIC_ISER0 = @as(*volatile u32, @ptrFromInt(0xE000E100));
const NVIC_IPR1 = @as(*volatile u32, @ptrFromInt(0xE000E404));

const LED_PIN: u5 = 5;
const DEBOUNCE_THRESHOLD: u32 = 10;

// Debounce states
const DebounceState = enum(u8) {
    Idle,
    MaybePressed,
    Pressed,
    MaybeReleased,
};

// Atomic shared state
var button_pressed = atomic.Atomic(bool).init(false);
var debounce_state = atomic.Atomic(DebounceState).init(DebounceState.Idle);
var debounce_counter = atomic.Atomic(u32).init(0);

fn ledInit() void {
    RCC_AHB1ENR.* |= 1 << 0;
    const moder = GPIOA_MODER.*;
    const shift: u32 = LED_PIN * 2;
    GPIOA_MODER.* = (moder & ~(@as(u32, 0x3) << shift)) | (@as(u32, 0x1) << shift);
}

fn systickInit() void {
    SYSTICK_RVR.* = 15999;
    SYSTICK_CVR.* = 0;
    SYSTICK_CSR.* = 0x7; // Enable + interrupt + processor clock
}

fn extiInit() void {
    // Enable SYSCFG clock
    RCC_APB2ENR.* |= 1 << 14;
    // Enable GPIOA clock
    RCC_AHB1ENR.* |= 1 << 0;

    // PA0 as EXTI0 source
    SYSCFG_EXTICR1.* &= ~@as(u32, 0xF);

    // Rising edge trigger
    EXTI_RTSR.* |= 0x1;

    // Unmask EXTI0
    EXTI_IMR.* |= 0x1;

    // Enable EXTI0 in NVIC (IRQ 6)
    NVIC_ISER0.* |= @as(u32, 1) << 6;

    // Set priority to 2
    const ipr1 = NVIC_IPR1.*;
    NVIC_IPR1.* = (ipr1 & ~(@as(u32, 0xFF) << 24)) | (@as(u32, 2) << 24);
}

fn ledToggle() void {
    GPIOA_ODR.* ^= @as(u32, 1) << LED_PIN;
}

fn readButton() bool {
    return (GPIOA_IDR.* & 0x1) != 0;
}

fn extiClearPending() void {
    EXTI_PR.* = 0x1; // Write 1 to clear
}

export fn SysTick_Handler() void {
    const current = readButton();
    const state = debounce_state.load(.SeqCst);
    const counter = debounce_counter.load(.SeqCst);

    const new_state: DebounceState = switch (state) {
        .Idle => if (current) .MaybePressed else .Idle,
        .MaybePressed => if (current) blk: {
            if (counter + 1 >= DEBOUNCE_THRESHOLD) {
                debounce_counter.store(0, .SeqCst);
                button_pressed.store(true, .SeqCst);
                break :blk .Pressed;
            } else {
                debounce_counter.store(counter + 1, .SeqCst);
                break :blk .MaybePressed;
            }
        } else .Idle,
        .Pressed => if (!current) .MaybeReleased else .Pressed,
        .MaybeReleased => if (!current) blk: {
            if (counter + 1 >= DEBOUNCE_THRESHOLD) {
                debounce_counter.store(0, .SeqCst);
                break :blk .Idle;
            } else {
                debounce_counter.store(counter + 1, .SeqCst);
                break :blk .MaybeReleased;
            }
        } else .Pressed,
    };

    debounce_state.store(new_state, .SeqCst);
}

export fn EXTI0_IRQHandler() void {
    extiClearPending();
}

pub fn main() noreturn {
    ledInit();
    systickInit();
    extiInit();

    while (true) {
        if (button_pressed.load(.SeqCst)) {
            button_pressed.store(false, .SeqCst);
            ledToggle();
        }

        // Inline WFI
        asm volatile ("wfi");
    }
}
```

### Build (Zig)

```bash
zig build -Doptimize=ReleaseSmall
```

## Running in QEMU

### Start QEMU

```bash
qemu-system-arm -machine netduinoplus2 -kernel button-interrupts.bin -nographic -s -S
```

### Simulating Button Press via GDB

Since QEMU's `netduinoplus2` doesn't have a physical button, you simulate presses by writing to the GPIO input register:

```bash
# Terminal 2: Connect with GDB
arm-none-eabi-gdb button-interrupts.elf
(gdb) target remote :1234
(gdb) break main
(gdb) continue

# Simulate button press: set bit 0 of GPIOA_IDR
(gdb) set {int}0x40020010 = 0x1

# Wait for debounce (10ms = 10 SysTick interrupts)
# You can speed this up by manually triggering SysTick:
(gdb) set $pc = SysTick_Handler
(gdb) continue
# Repeat 10+ times to trigger the debounce threshold

# Check LED state
(gdb) x/x 0x40020014
# Bit 5 should have toggled

# Simulate button release
(gdb) set {int}0x40020010 = 0x0
```

### Simulating via QEMU Monitor

```bash
# In the QEMU monitor (Ctrl-A c):
(qemu) qom-set /machine/netduino/gpio-a[0] 0x1
(qemu) qom-get /machine/netduino/gpio-a[0]
```

> **Tip:** For faster testing, temporarily reduce `DEBOUNCE_THRESHOLD` to 2 or 3 so you don't need to trigger SysTick as many times in GDB.

## Deliverables

- [ ] Button interrupt binary for all four languages
- [ ] Verified EXTI0 interrupt fires when GPIOA_IDR bit 0 is set
- [ ] Debounce counter reaches threshold before LED toggles
- [ ] No double-toggles from a single simulated button press
- [ ] Verified NVIC priority is set correctly (check NVIC_IPR1 in GDB)
- [ ] For Rust: `AtomicBool` used for `BUTTON_PRESSED`
- [ ] For Ada: Protected object provides mutual exclusion
- [ ] For Zig: Atomic state machine with `DebounceState` enum
- [ ] Binary size comparison (interrupts + debounce adds ~300-600 bytes)

## What You Learned

| Concept                  | C                              | Rust                                  | Ada                              | Zig                              |
|--------------------------|--------------------------------|---------------------------------------|----------------------------------|----------------------------------|
| **Shared state**         | `volatile` globals             | `AtomicBool` / `AtomicU32`            | Protected object                 | `std.atomic.Atomic(T)`           |
| **Critical section**     | `__disable_irq()` / `__enable_irq()` | `Ordering::SeqCst` on atomics    | Built into protected objects     | `.SeqCst` ordering               |
| **ISR registration**     | Named handler in vector table  | `#[exception]` attribute              | Runtime handles it (Ravenscar)   | `export fn` with matching name   |
| **Debouncing**           | Counter in SysTick handler     | Atomic counter in `SysTick` exception | Protected `Tick` procedure       | Atomic state machine enum        |
| **Interrupt clear**      | Write 1 to `EXTI_PR`           | Same via `write_volatile`             | Same via volatile access         | Same via volatile pointer        |
| **Pending flag**         | `volatile int`                 | `AtomicBool`                          | Protected object private field   | `Atomic(bool)`                   |
| **Low-power wait**       | `__asm volatile ("wfi")`       | `cortex_m::asm::wfi()`                | Implicit (Ravenscar idle task)   | `asm volatile ("wfi")`           |

## Next Steps

You now have the complete foundation: LED output, serial I/O, and interrupt-driven user input with debouncing. These three projects cover the essential building blocks of any bare-metal embedded system.

From here, you can:

- Add a SysTick-based scheduler for cooperative multitasking
- Implement a simple command-line interface over UART (combining Projects 2 and 3)
- Add PWM output for LED brightness control
- Explore DMA for zero-CPU UART transfers
- Move to Phase 2 projects with RTOS integration, SPI/I2C peripherals, and more complex interrupt architectures

> **Tip:** The patterns you've learned here — volatile register access, interrupt handlers, debouncing, atomic shared state — translate directly to any ARM Cortex-M chip, and with minor adjustments, to other architectures. The language changes, but the hardware fundamentals remain the same.

---

## References

### STMicroelectronics Documentation
- [STM32F4 Reference Manual (RM0090)](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-415-stm32f407-417-stm32f427-437-and-stm32f429-439-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) — Ch. 12: SYSCFG (EXTICR1), Ch. 13: EXTI (IMR, RTSR, FTSR, PR), Ch. 14: SysTick timer
- [STM32F405/407 Datasheet](https://www.st.com/resource/en/datasheet/stm32f405rg.pdf)

### ARM Documentation
- [Cortex-M4 Technical Reference Manual](https://developer.arm.com/documentation/ddi0439/latest/) — Ch. 8: NVIC (ISER, IPR, priority levels), WFI instruction, DMB/DSB memory barriers
- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/) — B1.5: Interrupts and exceptions, EXTI to NVIC mapping (IRQ 6 for EXTI0)

### Tools & Emulation
- [QEMU STM32 Documentation](https://www.qemu.org/docs/master/system/arm/stm32.html)
