/* main.c — LED blinker for STM32F405 (Netduino Plus 2 / NUCLEO-F446RE) */

#include "main.h"
#include <stdint.h>

/* Simple busy-wait delay — not precise, but sufficient for blinking */
static void delay(uint32_t count) {
  for (volatile uint32_t i = 0; i < count; i++) {
    /* volatile loop variable prevents optimization */
  }
}

int main(void) {
  /* Step 1: Enable GPIOA clock on AHB1 bus */
  RCC_AHB1ENR |= (1U << 0);

  /* Step 2: Configure PA5 as general-purpose output (MODER5 = 01) */
  GPIOA_MODER &= ~(0x3U << (LED_PIN * 2)); /* Clear bits 11:10 */
  GPIOA_MODER |= (0x1U << (LED_PIN * 2));  /* Set to output */

  /* Step 3: Blink forever */
  while (1) {
    GPIOA_ODR ^= (1U << LED_PIN); /* Toggle PA5 */
    delay(500000);                /* Busy-wait */
  }

  return 0; /* Never reached */
}
