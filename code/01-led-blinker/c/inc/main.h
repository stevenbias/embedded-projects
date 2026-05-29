#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

#define RCC_BASE 0x40023800U
#define GPIOA_BASE 0x40020000U

#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30U))

/* Clock enable bit positions for RCC_AHB1ENR (RM0390 §6.3.10) */
#define RCC_GPIOA_CLK_EN (1U << 0)

#define GPIO_MODE_OUTPUT 0x1U
#define GPIO_MODE_MASK 0x3U

/*
 * GPIOA_MODER — GPIO Port A Mode Register (offset 0x00)
 *   00 = Input (reset state, high-impedance)
 */
#define GPIOA_MODER (*(volatile uint32_t *)(GPIOA_BASE + 0x00U))

/* GPIOA_ODR — GPIO Port A Output Data Register (offset 0x14) */
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
#define SYSTICK_CTRL (*(volatile uint32_t *)(SYSTICK_BASE + 0x00U))
#define SYSTICK_LOAD (*(volatile uint32_t *)(SYSTICK_BASE + 0x04U))
#define SYSTICK_VAL (*(volatile uint32_t *)(SYSTICK_BASE + 0x08U))

/* SysTick control register bit flags (ARMv7-M §B3.3) */
#define SYSTICK_ENABLE (1U << 0)
#define SYSTICK_TICKINT (1U << 1)
#define SYSTICK_CLKSRC_CPU (1U << 2)
#define SYSTICK_COUNTFLAG (1U << 16)

/* 24-bit reload register mask */
#define SYSTICK_RELOAD_MASK 0xFFFFFFU

/*
 * NUCLEO-F446RE board connections:
 *   PA5: User LED (green, directly mapped to the GPIO pin)
 * Reference: NUCLEO-F446RE User Manual UM1724 §7.6
 */
#define LED_PIN 5

/* ---- System Clock ---- */
/*
 * After reset, the STM32F4 runs from the internal 16 MHz HSI oscillator
 * (HSI = High-Speed Internal).
 *
 * Reference: RM0390 §6.2 (Clock tree overview), §6.2.2 (HSI clock)
 */
#define HSI_CLOCK_HZ 16000000U

/* ---- Function Prototypes ---- */
void delay_ms(uint32_t ms);

#endif /* MAIN_H */
