/* main.c — LED blinker for STM32F405 (NUCLEO-F446RE) */
#include "main.h"

void delay_ms(uint32_t ms) {
  uint32_t cycles = (HSI_CLOCK_HZ / 1000U) * ms; // SysTick is a 24-bit timer,
  // so we need to handle delays longer than ~16.7ms
  SYSTICK_LOAD = cycles & 0xFFFFFFU;
  SYSTICK_VAL = 0;
  SYSTICK_CTRL = 0b101; // Enable SysTick with HSI clock
  while ((SYSTICK_CTRL & 0x10000U) == 0) {
  }
  SYSTICK_CTRL = 0;
}

int main(void) {
  RCC_AHB1ENR |= (1U << 0);
  GPIOA_MODER &= ~(0x3U << (LED_PIN * 2));
  GPIOA_MODER |= (0x1U << (LED_PIN * 2));

  while (1) {
    GPIOA_ODR ^= (1U << LED_PIN);
    delay_ms(500);
  }

  return 0;
}
