//! # Project 01: LED Blinker with Button Speed Control
//!
//! A bare-metal program for STM32F446RE (NUCLEO-F446RE, Cortex-M4F)
//! demonstrating GPIO output (LED on PA5) and GPIO input (button on PC13).
//!
//! ## Behavior
//! The LED blinks continuously. Each press of the user button toggles
//! the blink speed between slow (500ms period) and fast (100ms period).
//!
//! ## What Makes This "Bare Metal"
//! - `#![no_std]` — No Rust standard library (no heap, no threads, no I/O)
//! - `#![no_main]` — No runtime entry point; `crt0.s` calls `main()` directly
//! - No HAL crates (no `cortex-m`, no `cortex-m-rt`, no `embedded-hal`)
//! - All register access via raw volatile pointers
//! - Startup code shared with C (`common/crt0.s`, `common/linker.ld`)
//!
//! ## Reference
//! - RM0390: STM32F446 Reference Manual
//! - ARMv7-M Architecture Reference Manual (DDI 0403)
//! - Cortex-M4 Technical Reference Manual (DDI 0439)
#![no_std]
#![no_main]

mod target;
use crate::target::*;

mod time;
use crate::time::*;

/// GPIO output: LED on PA5
///
/// Enables the GPIOA clock and configures PA5 as a push-pull output.
/// Wait-states: reading back a just-written GPIO register may return
/// stale data due to the bus fabric; a DSB instruction inserted by the
/// compiler's volatile access rules handles this.
fn led_init() {
    unsafe {
        // Enable GPIOA clock on AHB1 bus
        RCC_AHB1ENR.write_volatile(RCC_AHB1ENR.read_volatile() | RCC_GPIOA_CLK_EN);

        // Configure PA5 as output: clear bits 11:10, set to 01
        GPIOA_MODER
            .write_volatile(GPIOA_MODER.read_volatile() & !(GPIO_MODE_MASK << (LED_PIN * 2)));
        GPIOA_MODER.write_volatile(GPIOA_MODER.read_volatile() | GPIO_MODE_OUTPUT << LED_PIN * 2);
    }
}

fn led_toggle() {
    unsafe { GPIOA_ODR.write_volatile(GPIOA_ODR.read_volatile() ^ (1 << LED_PIN)) }
}

/// GPIO input: button on PC13
///
/// Enables the GPIOC clock. PC13 defaults to input mode after reset
/// (MODER13 = 00), so we only need to ensure the clock is on.
///
/// The button is active LOW:
/// - Pressed:  pin voltage is low  → `is_pressed()` returns `true`
/// - Released: pin is pulled HIGH → `is_pressed()` returns `false`
fn button_init() {
    unsafe {
        // Enable GPIOC clock on AHB1 bus (bit 2 = GPIOCEN)
        RCC_AHB1ENR.write_volatile(RCC_AHB1ENR.read_volatile() | RCC_GPIOC_CLK_EN);

        // Explicitly configure PC13 as input (MODER13 = 00)
        GPIOC_MODER.write_volatile(GPIOC_MODER.read_volatile() & !(0x3 << (BUTTON_PIN * 2)));
        GPIOC_MODER
            .write_volatile(GPIOC_MODER.read_volatile() | (GPIO_MODE_INPUT << (BUTTON_PIN * 2)));
    }
}

fn button_is_pressed() -> bool {
    unsafe {
        // Read GPIOC_IDR bit 13; active-LOW, so invert the result
        ((GPIOC_IDR.read_volatile() >> BUTTON_PIN) & 1) == 0
    }
}

/// Main entry point — called from `crt0.s` after .data/.bss init.
///
/// Control flow:
/// 1. Initialize LED and button GPIO
/// 2. Loop: detect button press (with rising-edge logic), toggle speed, blink
#[unsafe(no_mangle)]
pub fn main() {
    led_init();
    button_init();

    let mut blink_fast = false;
    let mut prev_button = false;

    loop {
        let curr_button = button_is_pressed();

        // Rising edge: button transitions from released to pressed
        if curr_button && !prev_button {
            blink_fast = !blink_fast;

            // Crude delay to skip contact bounce (not a real debounce —
            // see Project 03 for proper debouncing with interrupts)
            delay_ms(200);
        }
        prev_button = curr_button;

        led_toggle();
        delay_ms(if blink_fast {
            BLINK_FAST_MS
        } else {
            BLINK_SLOW_MS
        });
    }
}

/// Panic handler — required by `#![no_std]`.
///
/// If a panic occurs (e.g. from an unwrap or index-out-of-bounds), this
/// function is called. Since there's no way to print or halt gracefully
/// on bare metal, we just spin forever.
#[panic_handler]
fn _panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
