/* =========================================================================
 * Project 01: LED Blinker with Button Speed Control
 * =========================================================================
 * Target: STM32F446RE (NUCLEO-F446RE board, Cortex-M4F)
 *
 * == Board Connections (NUCLEO-F446RE) ==
 *   PA5  → User LED    (active HIGH: write 1 = LED on)
 *   PC13 → User Button (active LOW:  pressed = logic 0)
 *
 * Reference: RM0390 STM32F446 Reference Manual
 * ========================================================================= */
#include "main.h"
#include <stdbool.h>

/* ---- Timing Constants ---- */
/* Button press toggles between these blink periods */
#define BLINK_SLOW_MS 500U
#define BLINK_FAST_MS 100U

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

/*
 * button_init() — Configure PC13 as a digital input
 *
 * The button on the NUCLEO board is connected to PC13 and is active LOW:
 *   - Released: pin is pulled HIGH (reads as 1)
 *   - Pressed:  pin is grounded  (reads as 0)
 */
static void button_init(void) {
  /* Enable GPIOC clock on the AHB1 bus (bit 2 = GPIOCEN) */
  RCC_AHB1ENR |= RCC_GPIOC_CLK_EN;

  /* PC13 is already in input mode after reset (MODER13 = 00),
   * but we explicitly clear the mode bits for clarity */
  GPIOC_MODER &= ~(GPIO_MODE_MASK << (BUTTON_PIN * 2));
}

/* =========================================================================
 * Button Reading
 * =========================================================================
 * GPIOC_IDR (Input Data Register) reflects the actual voltage on each pin.
 * We read bit 13. Note the active-LOW logic:
 *   - Pressed:  bit 13 = 0 → button_is_pressed() returns 1
 *   - Released: bit 13 = 1 → button_is_pressed() returns 0
 *
 * No pull resistor is configured here because the NUCLEO board has an
 * external pull-up on PC13. For a custom board, you would set PUPDR.
 */

static bool button_is_pressed(void) {
  /* Read IDR bit 13; active-LOW, so invert: pressed = 1, released = 0 */
  return !((GPIOC_IDR >> BUTTON_PIN) & 1U);
}

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
   *
   * We multiply first to avoid integer truncation: (HSI_CLOCK_HZ / 1000U)
   * is 16,000, so (16000 * ms) gives the cycles.
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

/* =========================================================================
 * Main Program
 * =========================================================================
 *
 * The control flow:
 *   1. Initialize hardware (LED, button)
 *   2. Loop forever:
 *      a. Check if button is pressed (with edge detection to catch one
 *         press per physical push, not continuous activation)
 *      b. Toggle blink speed if a press was detected
 *      c. Toggle the LED and wait for the current blink period
 *
 * Edge detection: We track the previous button state and only trigger
 * when state changes from "not pressed" (0) to "pressed" (1). Without
 * this, the speed would oscillate rapidly while the button is held.
 *
 * Note: This is NOT debounced. A mechanical button can bounce for 5-50ms,
 * causing multiple edges. For a production system you would add debouncing
 * (see Project 03: Button Interrupts & Debouncing). Here we use a simple
 * 200ms delay after a press to skip the bounce period.
 */
int main(void) {
  led_init();
  button_init();

  bool blink_fast = 0;  /* State: 0 = slow, 1 = fast */
  bool prev_button = 0; /* Previous edge state */

  while (1) {
    bool curr_button = button_is_pressed();

    /*
     * Rising edge detection: button was not pressed (prev_button = 0)
     * and is now pressed (curr_button = 1). This fires exactly once
     * per press regardless of how long the button is held.
     */
    if (curr_button && !prev_button) {
      blink_fast = !blink_fast; /* Toggle speed */

      /*
       * Simple delay to skip the contact bounce period.
       * 200ms is generous — most mechanical switches settle
       * within 5-20ms. This is NOT a proper debounce algorithm
       * but sufficient for this learning exercise.
       */
      delay_ms(200);
    }
    prev_button = curr_button;

    /* Blink LED at the current speed */
    led_toggle();
    delay_ms(blink_fast ? BLINK_FAST_MS : BLINK_SLOW_MS);
  }

  return 0;
}
