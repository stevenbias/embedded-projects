#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

/* =========================================================================
 * Memory-Mapped Register Definitions for STM32F446RE (Cortex-M4F)
 * =========================================================================
 * On ARM Cortex-M, all peripherals are accessed through memory-mapped
 * registers. Each peripheral has a base address, and its control/status
 * registers are at fixed offsets from that base.
 *
 * The `volatile` qualifier is critical: it tells the compiler that every
 * read or write to these addresses must be preserved and not optimized
 * away. Without volatile, the compiler may cache register values in CPU
 * registers, and your hardware will never see the writes (or may miss
 * external state changes on reads).
 *
 * Reference: RM0390 STM32F446 Reference Manual
 *   - RCC:   Section 6.3
 *   - GPIO:  Section 7.4
 *   - SysTick: Cortex-M4 TRM Section 4.4
 * ========================================================================= */

/* ---- RCC (Reset and Clock Control) ---- */
/* Base address for the RCC peripheral */
#define RCC_BASE 0x40023800U

/*
 * RCC_AHB1ENR — AHB1 Peripheral Clock Enable Register (offset 0x30)
 *
 * All GPIO peripherals are on the AHB1 bus. Before accessing any GPIO
 * registers, you MUST enable the corresponding bit here. Writes to GPIO
 * registers are silently ignored when the clock is disabled (RM0390 §6.3.10).
 *
 * Bit layout:
 *   Bit 0: GPIOAEN — GPIOA clock enable
 *   ...
 */
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30U))

/* Clock enable bit positions for RCC_AHB1ENR (RM0390 §6.3.10) */
#define RCC_GPIOA_CLK_EN (1U << 0)

/* ---- GPIO Port A (LED on PA5) ---- */
#define GPIOA_BASE 0x40020000U

/*
 * GPIOA_MODER — GPIO Port A Mode Register (offset 0x00)
 *
 * Each GPIO pin uses 2 bits to select the operating mode:
 *   00 = Input (reset state, high-impedance)
 *   01 = General purpose output
 *   10 = Alternate function (peripheral-specific)
 *   11 = Analog
 *
 * For pin N, the mode bits are at positions (N*2 + 1):(N*2).
 * Example: PA5 uses bits [11:10].
 */
#define GPIOA_MODER (*(volatile uint32_t *)(GPIOA_BASE + 0x00U))

/* 2-bit-per-pin mode values for MODER registers */
#define GPIO_MODE_INPUT 0x0U
#define GPIO_MODE_OUTPUT 0x1U
#define GPIO_MODE_AF 0x2U
#define GPIO_MODE_ANALOG 0x3U
#define GPIO_MODE_MASK 0x3U

/*
 * GPIOA_ODR — GPIO Port A Output Data Register (offset 0x14)
 *
 * Writing 1 to bit N sets pin N high; writing 0 drives it low.
 */
#define GPIOA_ODR (*(volatile uint32_t *)(GPIOA_BASE + 0x14U))

/* ---- SysTick Timer (System Timer, part of the Cortex-M4 core) ---- */
#define SYSTICK_BASE 0xE000E010U

/*
 * SysTick is a 24-bit decrementing counter built into every Cortex-M
 * processor. It counts down from a reload value and sets COUNTFLAG when
 * it reaches zero.
 *
 * References:
 * - ARMv7-M Architecture Reference Manual §B3.3
 * - STM32 Cortex-M4 MCU and MPU porgramming manual (PM0214 §4.5)
 */

#define SYSTICK_CTRL                                                           \
  (*(volatile uint32_t *)(SYSTICK_BASE +                                       \
                          0x00U)) // Control and status (see SYSTICK_* flags)
#define SYSTICK_LOAD                                                           \
  (*(volatile uint32_t *)(SYSTICK_BASE +                                       \
                          0x04U)) // Reload value (24-bit, max 16,777,215)
#define SYSTICK_VAL                                                            \
  (*(volatile uint32_t *)(SYSTICK_BASE +                                       \
                          0x08U)) // Current counter value (write clears it)

/* SysTick control register bit flags */
#define SYSTICK_ENABLE (1U << 0)
#define SYSTICK_TICKINT (1U << 1)
#define SYSTICK_CLKSRC_CPU (1U << 2)
#define SYSTICK_COUNTFLAG (1U << 16)

/* 24-bit reload register mask */
#define SYSTICK_RELOAD_MASK 0xFFFFFFU

/* ---- Pin Assignments ---- */
/*
 * NUCLEO-F446RE board connections:
 *   PA5: User LED (green, directly mapped to the GPIO pin)
 *
 * Reference: NUCLEO-F446RE User Manual UM1724 §7.6
 */
#define LED_PIN 5
#define BUTTON_PIN 13

/* ---- System Clock ---- */
/*
 * After reset, the STM32F4 runs from the internal 16 MHz HSI oscillator
 * (HSI = High-Speed Internal). No PLL configuration is needed for this
 * project — we use HSI directly as the system clock.
 *
 * Reference: RM0390 §6.2 (Clock tree overview), §6.2.2 (HSI clock)
 */
#define HSI_CLOCK_HZ 16000000U

/* ---- Function Prototypes ---- */
void delay_ms(uint32_t ms);

#endif /* MAIN_H */
