#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

#define RCC_BASE 0x40023800U
#define GPIOA_BASE 0x40020000U

#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30U))
#define GPIOA_MODER (*(volatile uint32_t *)(GPIOA_BASE + 0x00U))
#define GPIOA_ODR (*(volatile uint32_t *)(GPIOA_BASE + 0x14U))

#define LED_PIN 5

#define SYSTICK_BASE 0xE000E010U
#define SYSTICK_CTRL (*(volatile uint32_t *)(SYSTICK_BASE + 0x00U))
#define SYSTICK_LOAD (*(volatile uint32_t *)(SYSTICK_BASE + 0x04U))
#define SYSTICK_VAL (*(volatile uint32_t *)(SYSTICK_BASE + 0x08U))

#define HSI_CLOCK_HZ 16000000U

void delay_ms(uint32_t ms);

#endif
