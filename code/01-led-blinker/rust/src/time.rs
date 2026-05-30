//! Millisecond delay using the SysTick timer.
//!
//! SysTick is a 24-bit down-counter built into every Cortex-M processor
//! (ARMv7-M Architecture Reference Manual §B3.3). It decrements by 1
//! each processor cycle. When it reaches 0, the COUNTFLAG bit in
//! SYSTICK_CTRL is set.
//!
//! At 16 MHz, one millisecond = 16,000 clock cycles. For a 500ms delay
//! we program the reload value as 8,000,000 (16_000_000 / 1000 * 500).
//!
//! The counter is 24 bits wide (max 16,777,215). At 16 MHz this gives a
//! maximum single-shot delay of ~1.048 seconds. Longer delays must be
//! composed of multiple shorter waits.
//!
//! Reference: ARMv7-M Architecture Reference Manual, §B3.3
//!            Cortex-M4 TRM, §4.4 (SysTick)

use crate::target::*;

/// Busy-wait for `ms` milliseconds using SysTick.
///
/// This function programs the SysTick counter, waits for it to reach
/// zero, then stops the timer. It blocks the CPU completely — no
/// interrupts are serviced unless previously enabled (we disable the
/// SysTick interrupt here for simplicity).
///
/// # Limitations
///
/// Maximum delay is ~1,048ms at 16 MHz due to the 24-bit counter.
/// Delays > 1,048ms will be truncated and produce shorter waits.
pub fn delay_ms(ms: u32) {
    // Total cycles for the requested delay: 16,000 cycles/ms × ms
    let cycles = (HSI_CLOCK_HZ / 1000) * ms;

    unsafe {
        // Program the 24-bit reload value
        SYSTICK_LOAD.write_volatile(cycles & 0xFFFFFF);

        // Writing any value to VAL clears the counter and COUNTFLAG
        SYSTICK_VAL.write_volatile(0);

        // Enable SysTick with processor clock, no interrupt:
        //   Bit 0 = 1 → ENABLE
        //   Bit 1 = 0 → TICKINT disabled
        //   Bit 2 = 1 → CLKSOURCE = processor clock
        SYSTICK_CTRL.write_volatile(0b101);

        // Wait for COUNTFLAG (bit 16) to be set
        while (SYSTICK_CTRL.read_volatile() & (1 << 16)) == 0 {}

        // Stop the timer
        SYSTICK_CTRL.write_volatile(0);
    }
}
