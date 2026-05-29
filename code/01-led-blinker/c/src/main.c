/* =========================================================================
 * Project 01: LED Blinker
 * =========================================================================
 * Target: STM32F446RE (NUCLEO-F446RE board, Cortex-M4F)
 *
 * == Board Connections (NUCLEO-F446RE) ==
 *   PA5  → User LED    (active HIGH: write 1 = LED on)
 *
 * Reference: RM0390 STM32F446 Reference Manual
 * ========================================================================= */
#include "main.h"

/* =========================================================================
 * GPIO Initialization
 * =========================================================================
 * Each GPIO port has its own clock gate in RCC_AHB1ENR. Before touching
 * any register in a GPIO block, the clock must be enabled — otherwise
 * writes are silently discarded and reads return garbage.
 */

/*
 * led_init() — Configure PA5 as a push-pull output
 *
 * Two steps are required to make a GPIO pin behave as an output:
 *
 *   Step 1 — Enable the peripheral clock
 *     RCC_AHB1ENR bit 0 (GPIOAEN) must be set. This powers up the GPIOA
 *     block and connects it to the AHB1 bus.
 *
 *   Step 2 — Set the pin mode to output
 *     GPIOA_MODER uses 2 bits per pin:
 *       01 = Output   ← we need this
 *     For pin 5, the mode bits are at [11:10] = (5 * 2).
 *     The idiom: clear both bits first, then set only the output bit.
 *
 * Other registers (OTYPER for push-pull vs open-drain, OSPEEDR for slew
 * rate, PUPDR for pull-up/down) are left at their reset defaults, which
 * are appropriate for driving an LED (push-pull, low speed, no pull).
 */
static void led_init(void) {
  /* Enable GPIOA clock on the AHB1 bus */
  RCC_AHB1ENR |= RCC_GPIOA_CLK_EN;

  /* Configure PA5 as general-purpose output (MODER5 = 0b01) */
  GPIOA_MODER &= ~(GPIO_MODE_MASK << (LED_PIN * 2));
  GPIOA_MODER |= (GPIO_MODE_OUTPUT << (LED_PIN * 2));
}

/*
 * let_toggle() — Toggle the state of the LED on PA5
 * The ODR (Output Data Register) controls the output state of the pin:
 *   0 = LOW (LED off)
 *   1 = HIGH (LED on)
 * Toggling can be done with a simple XOR operation.
 */
static void led_toggle(void) { GPIOA_ODR ^= (1U << LED_PIN); }

/* =========================================================================
 * Timing with SysTick
 * =========================================================================
 *
 * SysTick is a 24-bit down-counter built into every Cortex-M processor
 * (ARMv7-M Architecture Reference Manual §B3.3). It counts down from
 * a reload value on each system clock cycle.
 *
 * Limitation: 24-bit counter × (1/16 MHz) = ~1.048 seconds max per reload.
 * Longer delays must be composed of multiple shorter waits.
 */
void delay_ms(uint32_t ms) {
  /*
   * Calculate the number of CPU cycles for the requested delay.
   * The 16 MHz HSI clock gives us 16,000 cycles per millisecond.
   */
  uint32_t cycles = (HSI_CLOCK_HZ / 1000U) * ms;

  /*
   * For delays > 1,048ms, you would need to loop. This implementation
   * masks to 24 bits, so delays > 1,048ms will be SHORTER than expected.
   * Our usage (100ms, 200ms, 500ms) is well within the limit.
   */
  SYSTICK_LOAD = cycles & SYSTICK_RELOAD_MASK;
  SYSTICK_VAL = 0; /* Write clears the counter */

  /* Enable with processor clock source; no interrupt */
  SYSTICK_CTRL = SYSTICK_ENABLE | SYSTICK_CLKSRC_CPU;

  /* Wait for COUNTFLAG: counter decrements and sets flag when reaching 0 */
  while ((SYSTICK_CTRL & SYSTICK_COUNTFLAG) == 0) {
  }

  /* Stop the timer to save power until the next call */
  SYSTICK_CTRL = 0;
}

int main(void) {

  led_init();

  while (1) {
    led_toggle();
    delay_ms(500);
  }

  return 0;
}
