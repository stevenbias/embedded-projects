/* main.c — LED blinker for STM32F446RE (NUCLEO-F446RE) */
#include "main.h"

static void led_init(void) {
  /* Enable GPIOA clock on the AHB1 bus */
  RCC_AHB1ENR |= RCC_GPIOA_CLK_EN;

  /* Configure PA5 as general-purpose output (MODER5 = 0b01) */
  GPIOA_MODER &= ~(GPIO_MODE_MASK << (LED_PIN * 2));
  GPIOA_MODER |= (GPIO_MODE_OUTPUT << (LED_PIN * 2));
}

static void led_toggle(void) { GPIOA_ODR ^= (1U << LED_PIN); }

void delay_ms(uint32_t ms) {
  uint32_t cycles = (HSI_CLOCK_HZ / 1000U) * ms; // SysTick is a 24-bit timer,
  // so we need to handle delays longer than ~16.7ms
  SYSTICK_LOAD = cycles & SYSTICK_RELOAD_MASK;
  SYSTICK_VAL = 0; /* Write clears the counter */

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
