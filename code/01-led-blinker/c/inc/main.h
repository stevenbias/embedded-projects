#ifndef MAIN_H
#define MAIN_H

/* Peripheral base addresses */
#define RCC_BASE 0x40023800U
#define GPIOA_BASE 0x40020000U

/* Register offsets */
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30U))
#define GPIOA_MODER (*(volatile uint32_t *)(GPIOA_BASE + 0x00U))
#define GPIOA_ODR (*(volatile uint32_t *)(GPIOA_BASE + 0x14U))

/* LED pin */
#define LED_PIN 5

#endif /* MAIN_H */
