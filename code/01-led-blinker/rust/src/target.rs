//! # Memory-mapped register addresses for STM32F446RE (Cortex-M4F).
//!
//! On ARM Cortex-M, all peripherals are accessed through memory-mapped
//! registers. Each peripheral has a base address, and its control/status
//! registers are at fixed offsets from that base.
//!
//! Safety: Reading and writing these addresses through raw pointers is
//! inherently `unsafe` in Rust. The compiler cannot prove that the
//! addresses are valid, that the memory is not aliased, or that accesses
//! won't be reordered. The `volatile` access methods (`read_volatile`,
//! `write_volatile`) prevent the compiler from optimizing away reads/writes
//! that have side effects on hardware — exactly like C's `volatile`.
//!
//! Reference: RM0390 STM32F446 Reference Manual
//!   - RCC:   Section 6.3
//!   - GPIO:  Section 7.4
//!   - SysTick: Cortex-M4 TRM Section 4.4

/// Base address for RCC (Reset and Clock Control) peripheral
pub const RCC_BASE: u32 = 0x40023800;

/// AHB1 Peripheral Clock Enable Register (offset 0x30)
///
/// All GPIO peripherals are on the AHB1 bus. Before accessing any GPIO
/// registers, you MUST enable the corresponding bit here. Writes to GPIO
/// registers are silently ignored when the clock is disabled (RM0390 §6.3.10).
///
/// Bit layout:
///   - Bit 0: GPIOAEN — GPIOA clock enable
///   - Bit 2: GPIOCEN — GPIOC clock enable
pub const RCC_AHB1ENR: *mut u32 = (RCC_BASE + 0x30) as *mut u32;

/// Clock enable bit positions for RCC_AHB1ENR (RM0390 §6.3.10)
pub const RCC_GPIOA_CLK_EN: u32 = 1 << 0;
pub const RCC_GPIOC_CLK_EN: u32 = 1 << 2;

// ---- GPIO Port A (LED on PA5) ----
pub const GPIOA_BASE: u32 = 0x40020000;

/// GPIO Port A Mode Register (offset 0x00).
/// 2 bits per pin: 00=Input, 01=Output, 10=AF, 11=Analog.
pub const GPIOA_MODER: *mut u32 = (GPIOA_BASE + 0x00) as *mut u32;

/// GPIO Port A Output Data Register (offset 0x14).
pub const GPIOA_ODR: *mut u32 = (GPIOA_BASE + 0x14) as *mut u32;

// ---- GPIO Port C (Button on PC13) ----
pub const GPIOC_BASE: u32 = 0x40020800;

/// GPIO Port C Mode Register (offset 0x00).
pub const GPIOC_MODER: *mut u32 = (GPIOC_BASE + 0x00) as *mut u32;

/// GPIO Port C Input Data Register (offset 0x10).
/// Reading bit N returns the actual logic level on pin N.
pub const GPIOC_IDR: *const u32 = (GPIOC_BASE + 0x10) as *const u32;

pub const GPIO_MODE_INPUT: u32 = 0b00;
pub const GPIO_MODE_OUTPUT: u32 = 0b01;
pub const GPIO_MODE_MASK: u32 = 0b11;

// ---- SysTick Timer (Cortex-M system timer) ----
pub const SYSTICK_BASE: u32 = 0xE000E010;

/// SysTick Control and Status Register (offset 0x00).
pub const SYSTICK_CTRL: *mut u32 = (SYSTICK_BASE + 0x00) as *mut u32;

/// SysTick Reload Value Register (offset 0x04), 24-bit.
pub const SYSTICK_LOAD: *mut u32 = (SYSTICK_BASE + 0x04) as *mut u32;

/// SysTick Current Value Register (offset 0x08).
pub const SYSTICK_VAL: *mut u32 = (SYSTICK_BASE + 0x08) as *mut u32;

// ---- Pin Assignments ----
pub const LED_PIN: u32 = 5;
pub const BUTTON_PIN: u32 = 13;

// ---- System Clock ----
/// Internal 16 MHz HSI oscillator — no PLL configured.
pub const HSI_CLOCK_HZ: u32 = 16_000_000;

// ---- Timing Constants ----
pub const BLINK_SLOW_MS: u32 = 500;
pub const BLINK_FAST_MS: u32 = 100;
