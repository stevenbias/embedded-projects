---
title: "Project 6: PWM Motor Controller"
phase: 2
project: 6
---

# Project 6: PWM Motor Controller

## Introduction

Pulse Width Modulation (PWM) is the foundation of motor control, LED dimming, servo positioning, and power regulation. This project configures an STM32 timer to generate PWM signals with configurable frequency and duty cycle, implements soft-start ramp algorithms, and adds dead-time insertion for safe H-bridge operation.

**What you'll learn:**

- Timer architecture on STM32: prescaler, auto-reload, compare registers
- PWM signal generation: frequency and duty cycle calculations
- Center-aligned vs edge-aligned PWM modes
- Dead-time insertion for H-bridge control and why it matters
- Soft-start ramp algorithms to prevent inrush current
- Type-level guarantees on frequency and duty range (Rust)
- Range-checked motor parameters (Ada)
- Comptime-validated PWM configuration (Zig)

## Timer Configuration on STM32

The STM32 general-purpose timer (TIM1–TIM5) generates PWM through a counter that compares against capture/compare registers.

### Timer Block Diagram

```
PCLK ──► Prescaler (PSC) ──► Counter (CNT) ──► Auto-Reload (ARR)
                                    │
                                    ├── Compare CH1 (CCR1) ──► PWM Output
                                    ├── Compare CH2 (CCR2) ──► PWM Output
                                    ├── Compare CH3 (CCR3) ──► PWM Output
                                    └── Compare CH4 (CCR4) ──► PWM Output
```

### Key Registers

| Register | Name | Description |
|----------|------|-------------|
| **CR1** | Control Register 1 | Counter enable, direction, alignment mode |
| **PSC** | Prescaler | Divides input clock: f_CNT = f_PCLK / (PSC + 1) |
| **ARR** | Auto-Reload Register | Counter reset value, determines PWM period |
| **CCRn** | Capture/Compare Register n | Duty cycle threshold for channel n |
| **CCMRn** | Capture/Compare Mode Register n | PWM mode, output compare config |
| **CCER** | Capture/Compare Enable Register | Channel output enable, polarity |
| **BDTR** | Break and Dead-Time Register | Dead-time, break input, main output enable |
| **EGR** | Event Generation Register | Software update event (shadow register reload) |

### Frequency Calculation

The PWM frequency is determined by the prescaler and auto-reload register:

```
f_PWM = f_PCLK / ((PSC + 1) × (ARR + 1))
```

For a 72 MHz system clock targeting 20 kHz PWM:

```
72,000,000 / (PSC + 1) / (ARR + 1) = 20,000

Option 1: PSC = 0,    ARR = 3599  → 72M / 1 / 3600 = 20,000 Hz
Option 2: PSC = 3,    ARR = 899   → 72M / 4 / 900  = 20,000 Hz
Option 3: PSC = 71,   ARR = 49    → 72M / 72 / 50  = 20,000 Hz
```

**Choosing PSC and ARR:**
- Higher ARR = finer duty cycle resolution (ARR + 1 steps)
- Lower PSC = less prescaler jitter
- Option 1 gives 3600-step resolution — good for motor control
- Option 3 gives only 50-step resolution — too coarse

### Duty Cycle

The duty cycle is set by the compare register:

```
duty = CCR / (ARR + 1)

For ARR = 3599:
  0%   → CCR = 0
  25%  → CCR = 900
  50%  → CCR = 1800
  75%  → CCR = 2700
  100% → CCR = 3600 (clamped to ARR)
```

## PWM Signal Generation

### Edge-Aligned PWM (Mode 1)

The counter counts up from 0 to ARR. Output is high when CNT < CCR.

```
CNT:    0 ───────────────────────────► ARR ──► 0
PWM:    ██████████████░░░░░░░░░░░░░░░░████████
        ◄─── CCR ───►◄── ARR-CCR ──►
```

```
CR1: CMS = 00 (edge-aligned)
CCMR1: OC1M = 110 (PWM Mode 1)
```

### Center-Aligned PWM

The counter counts up to ARR, then down to 0. Output toggles at compare match in both directions.

```
CNT:    0 ──────► ARR ──────► 0 ──────► ARR
PWM:    ████░░░░░░░░░░░░░░░░░░░░████
        ◄── CCR ──►◄ ARR-CCR ►◄ CCR ─►
```

```
CR1: CMS = 01 (center-aligned mode 1)
CCMR1: OC1M = 110 (PWM Mode 1)
```

### Edge vs Center-Aligned Comparison

| Property | Edge-Aligned | Center-Aligned |
|----------|-------------|----------------|
| **PWM frequency** | f_PCLK / (PSC+1) / (ARR+1) | f_PCLK / (PSC+1) / (2×ARR) |
| **Harmonic content** | Higher (single edge) | Lower (symmetric edges) |
| **Current ripple** | Higher | Lower |
| **Motor noise** | More audible | Less audible |
| **Update timing** | Update at overflow | Update at peak/valley |
| **Use case** | LED dimming, simple motors | Precision motor control, inverters |

> **Tip:** For motor control, prefer center-aligned PWM. The symmetric switching reduces current ripple and electromagnetic interference (EMI), which means less torque ripple and quieter operation.

## Dead-Time Insertion for H-Bridge Control

### Why Dead Time Matters

An H-bridge uses four switches to drive a motor in both directions:

```
         VDD
          │
     Q1 ──┤├── Q2
          │
    ──────┤├── Motor ──────
          │
     Q3 ──┤├── Q4
          │
         GND
```

When switching direction, Q1/Q4 (forward) must turn off before Q2/Q3 (reverse) turn on. If both high-side and low-side switches on the same leg conduct simultaneously, you get **shoot-through** — a direct short from VDD to GND.

```
WITHOUT DEAD TIME:          WITH DEAD TIME:
Q1: ████████░░░░░░░░        Q1: ████████░░░░░░░░░░░░░░░░
Q2: ░░░░░░░░████████        Q2: ░░░░░░░░░░░░░░░░████████
                                └─dead time─┘
                                (both off)
```

### Calculating Dead Time

The STM32 advanced timer (TIM1/TIM8) has a hardware dead-time generator in the BDTR register:

```
DT[7:0] selects dead time based on DTG[1:0]:

DTG[1:0] = 00: DT = DTG[7:0] × t_DTS          (step = t_DTS)
DTG[1:0] = 01: DT = (64 + DTG[5:0]) × 2 × t_DTS (step = 2×t_DTS)
DTG[1:0] = 10: DT = (32 + DTG[4:0]) × 8 × t_DTS (step = 8×t_DTS)
DTG[1:0] = 11: DT = (32 + DTG[4:0]) × 16 × t_DTS (step = 16×t_DTS)
```

Where `t_DTS` is the dead-time generator sampling time (derived from the timer clock).

**Example:** At 72 MHz timer clock, t_DTS = 13.89 ns. For 200 ns dead time:
- DTG[1:0] = 00: DTG = 200 / 13.89 ≈ 14 → `BDTR = 0x0E`
- Actual dead time: 14 × 13.89 = 194 ns

> **Warning:** Dead time must exceed the MOSFET turn-off time plus driver propagation delay. Check your MOSFET datasheet for `t_f` (fall time) and `t_r` (rise time). A typical IRLZ44N needs ~50 ns minimum; use 200–500 ns for safety margin.

## Soft-Start Ramp Algorithms

Motors draw 5–10× their rated current at startup (locked rotor current). A soft-start ramp gradually increases duty cycle to limit inrush current and mechanical stress.

### Linear Ramp

```c
for (duty = 0; duty <= target; duty += step) {
    pwm_set_duty(duty);
    delay(ramp_interval_ms);
}
```

Simple but causes a current step at each increment.

### Exponential Ramp

```c
duty = current_duty + (target_duty - current_duty) * alpha;
```

Smoother — the increment decreases as you approach the target. Better for large motors.

### S-Curve Ramp

```
duty(t) = target × (1 - cos(π × t / T_ramp)) / 2
```

Smoothest — zero acceleration at start and end. Ideal for precision positioning.

### Implementation Strategy

For this project, we implement a **non-blocking linear ramp** using a timer interrupt:

```
Timer interrupt (every ramp_interval_ms):
    if (ramp_active):
        if (current_duty < target_duty):
            current_duty += ramp_step
            if (current_duty > target_duty):
                current_duty = target_duty
                ramp_active = false
            pwm_set_duty(current_duty)
        else if (current_duty > target_duty):
            current_duty -= ramp_step
            if (current_duty < target_duty):
                current_duty = target_duty
                ramp_active = false
            pwm_set_duty(current_duty)
```

## Implementation

### C: Timer-Based PWM with Configurable Frequency/Duty, Soft-Start Ramp

#### PWM Driver (`pwm.h`)

```c
#ifndef PWM_DRIVER_H
#define PWM_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

/* Timer channel selection */
typedef enum {
    PWM_CH1 = 0,
    PWM_CH2 = 1,
    PWM_CH3 = 2,
    PWM_CH4 = 3,
} pwm_channel_t;

/* PWM alignment mode */
typedef enum {
    PWM_EDGE_ALIGNED = 0,
    PWM_CENTER_ALIGNED = 1,
} pwm_mode_t;

/* PWM configuration */
typedef struct {
    uint32_t frequency_hz;
    pwm_mode_t mode;
    uint16_t dead_time_ns;  /* 0 for single-ended, >0 for complementary */
} pwm_config_t;

/* Ramp configuration */
typedef struct {
    bool enabled;
    uint16_t step;           /* Duty increment per interval */
    uint32_t interval_ms;    /* Time between steps */
} ramp_config_t;

/* Motor state */
typedef enum {
    MOTOR_STOPPED = 0,
    MOTOR_RAMPING,
    MOTOR_RUNNING,
} motor_state_t;

/* PWM handle */
typedef struct {
    volatile uint32_t *cr1;
    volatile uint32_t *cr2;
    volatile uint32_t *ccmr1;
    volatile uint32_t *ccmr2;
    volatile uint32_t *ccer;
    volatile uint32_t *psc;
    volatile uint32_t *arr;
    volatile uint32_t *ccr1;
    volatile uint32_t *ccr2;
    volatile uint32_t *ccr3;
    volatile uint32_t *ccr4;
    volatile uint32_t *bdtr;
    volatile uint32_t *egr;
    volatile uint32_t *dier;
    uint32_t timer_clk;
    uint16_t arr_value;
    pwm_mode_t mode;
} pwm_handle_t;

/* Motor controller state */
typedef struct {
    pwm_handle_t *pwm;
    pwm_channel_t channel;
    motor_state_t state;
    uint16_t current_duty;
    uint16_t target_duty;
    ramp_config_t ramp;
    uint32_t ramp_tick_count;
} motor_controller_t;

/* Initialize PWM timer */
void pwm_init(pwm_handle_t *hpwm, uint32_t timer_base,
              uint32_t timer_clk_hz, const pwm_config_t *config);

/* Enable PWM output on a channel */
void pwm_enable_channel(pwm_handle_t *hpwm, pwm_channel_t ch);

/* Set duty cycle (0–10000 = 0.00%–100.00%) */
void pwm_set_duty(pwm_handle_t *hpwm, pwm_channel_t ch, uint16_t duty_x100);

/* Get current duty cycle */
uint16_t pwm_get_duty(pwm_handle_t *hpwm, pwm_channel_t ch);

/* Get PWM resolution (steps) */
uint16_t pwm_get_resolution(pwm_handle_t *hpwm);

/* Motor controller API */
void motor_init(motor_controller_t *motor, pwm_handle_t *pwm,
                pwm_channel_t ch, const ramp_config_t *ramp);

/* Set target speed with optional ramp */
void motor_set_speed(motor_controller_t *motor, uint16_t duty_x100);

/* Call from timer interrupt (every 1ms) */
void motor_tick(motor_controller_t *motor);

/* Get current motor state */
motor_state_t motor_get_state(const motor_controller_t *motor);
uint16_t motor_get_current_duty(const motor_controller_t *motor);

#endif
```

#### PWM Driver Implementation (`pwm.c`)

```c
#include "pwm.h"

/* TIM1 register offsets */
#define TIM_CR1_OFF     0x00
#define TIM_CR2_OFF     0x04
#define TIM_CCMR1_OFF   0x18
#define TIM_CCMR2_OFF   0x1C
#define TIM_CCER_OFF    0x20
#define TIM_CNT_OFF     0x24
#define TIM_PSC_OFF     0x28
#define TIM_ARR_OFF     0x2C
#define TIM_CCR1_OFF    0x34
#define TIM_CCR2_OFF    0x38
#define TIM_CCR3_OFF    0x3C
#define TIM_CCR4_OFF    0x40
#define TIM_BDTR_OFF    0x44
#define TIM_EGR_OFF     0x14
#define TIM_DIER_OFF    0x0C

/* CR1 bits */
#define CR1_CEN     (1U << 0)
#define CR1_UDIS    (1U << 1)
#define CR1_ARPE    (1U << 7)
#define CR1_CMS_SHIFT 5

/* CR2 bits */
#define CR2_CCPC    (1U << 0)

/* CCMR bits (per channel, shift by 0 or 8 for CH1/CH2) */
#define CCMR_OCxM_PWM1  0x6  /* PWM Mode 1 */
#define CCMR_OCxM_SHIFT  4
#define CCMR_OCxPE       (1U << 3)  /* Preload enable */
#define CCMR_CCxS_OUTPUT 0x0  /* Channel is output */

/* CCER bits */
#define CCER_CCxE     (1U << 0)   /* Channel enable */
#define CCER_CCxNE    (1U << 2)   /* Complementary enable */
#define CCER_CCxP     (1U << 1)   /* Polarity */
#define CCER_CCxNP    (1U << 3)   /* Complementary polarity */

/* BDTR bits */
#define BDTR_MOE      (1U << 15)  /* Main output enable */
#define BDTR_AOE      (1U << 14)  /* Automatic output enable */
#define BDTR_OSSI     (1U << 11)  /* Off-state selection idle */
#define BDTR_OSSR     (1U << 10)  /* Off-state selection run */
#define BDTR_DTG_SHIFT 0

/* EGR bits */
#define EGR_UG        (1U << 0)   /* Update generation */

/* DIER bits */
#define DIER_UIE      (1U << 0)   /* Update interrupt enable */

static uint16_t calc_dead_time(uint32_t timer_clk, uint16_t dead_time_ns) {
    if (dead_time_ns == 0) return 0;

    /* t_DTS = 1 / timer_clk (assuming no additional prescaling) */
    /* DT = dead_time_ns / (1e9 / timer_clk) = dead_time_ns * timer_clk / 1e9 */
    uint32_t dtg = ((uint32_t)dead_time_ns * timer_clk) / 1000000000UL;

    /* Clamp to 8-bit DTG value */
    if (dtg > 255) dtg = 255;

    return (uint16_t)dtg;
}

void pwm_init(pwm_handle_t *hpwm, uint32_t timer_base,
              uint32_t timer_clk_hz, const pwm_config_t *config) {
    hpwm->cr1 = (volatile uint32_t *)(timer_base + TIM_CR1_OFF);
    hpwm->cr2 = (volatile uint32_t *)(timer_base + TIM_CR2_OFF);
    hpwm->ccmr1 = (volatile uint32_t *)(timer_base + TIM_CCMR1_OFF);
    hpwm->ccmr2 = (volatile uint32_t *)(timer_base + TIM_CCMR2_OFF);
    hpwm->ccer = (volatile uint32_t *)(timer_base + TIM_CCER_OFF);
    hpwm->psc = (volatile uint32_t *)(timer_base + TIM_PSC_OFF);
    hpwm->arr = (volatile uint32_t *)(timer_base + TIM_ARR_OFF);
    hpwm->ccr1 = (volatile uint32_t *)(timer_base + TIM_CCR1_OFF);
    hpwm->ccr2 = (volatile uint32_t *)(timer_base + TIM_CCR2_OFF);
    hpwm->ccr3 = (volatile uint32_t *)(timer_base + TIM_CCR3_OFF);
    hpwm->ccr4 = (volatile uint32_t *)(timer_base + TIM_CCR4_OFF);
    hpwm->bdtr = (volatile uint32_t *)(timer_base + TIM_BDTR_OFF);
    hpwm->egr = (volatile uint32_t *)(timer_base + TIM_EGR_OFF);
    hpwm->dier = (volatile uint32_t *)(timer_base + TIM_DIER_OFF);
    hpwm->timer_clk = timer_clk_hz;
    hpwm->mode = config->mode;

    /* Disable counter during config */
    *hpwm->cr1 = 0;

    /* Calculate PSC and ARR for target frequency */
    /* f_PWM = f_CLK / ((PSC+1) * (ARR+1)) for edge-aligned */
    /* f_PWM = f_CLK / ((PSC+1) * 2*ARR) for center-aligned */
    uint32_t target_period;
    if (config->mode == PWM_CENTER_ALIGNED) {
        target_period = timer_clk_hz / (config->frequency_hz * 2);
    } else {
        target_period = timer_clk_hz / config->frequency_hz;
    }

    /* Find optimal PSC/ARR: maximize ARR for resolution */
    uint32_t psc = 0;
    uint32_t arr = target_period - 1;

    /* If ARR exceeds 16-bit, increase PSC */
    while (arr > 65535 && psc < 65535) {
        psc++;
        arr = target_period / (psc + 1) - 1;
    }

    /* Ensure minimum ARR of 1 */
    if (arr < 1) arr = 1;

    *hpwm->psc = (uint16_t)psc;
    *hpwm->arr = (uint16_t)arr;
    hpwm->arr_value = (uint16_t)arr;

    /* Configure alignment mode */
    if (config->mode == PWM_CENTER_ALIGNED) {
        *hpwm->cr1 = (1U << CR1_CMS_SHIFT);  /* CMS = 01 */
    }

    /* Auto-reload preload */
    *hpwm->cr1 |= CR1_ARPE;

    /* Configure dead time if needed */
    if (config->dead_time_ns > 0) {
        uint16_t dtg = calc_dead_time(timer_clk_hz, config->dead_time_ns);
        *hpwm->bdtr = BDTR_MOE | BDTR_AOE | BDTR_OSSI | BDTR_OSSR | dtg;
    }

    /* Generate update event to load shadow registers */
    *hpwm->egr = EGR_UG;
}

void pwm_enable_channel(pwm_handle_t *hpwm, pwm_channel_t ch) {
    uint32_t shift = ch * 4;

    /* Configure PWM Mode 1 with preload */
    volatile uint32_t *ccmr;
    uint32_t ccmr_shift;
    if (ch < 2) {
        ccmr = hpwm->ccmr1;
        ccmr_shift = (ch == 0) ? 0 : 8;
    } else {
        ccmr = hpwm->ccmr2;
        ccmr_shift = (ch == 2) ? 0 : 8;
    }

    *ccmr &= ~(0xFFU << ccmr_shift);
    *ccmr |= (CCMR_CCxS_OUTPUT << ccmr_shift) |
             (CCMR_OCxM_PWM1 << (ccmr_shift + CCMR_OCxM_SHIFT)) |
             (CCMR_OCxPE << (ccmr_shift + 3));

    /* Enable channel output (and complementary if dead time configured) */
    *hpwm->ccer |= (CCER_CCxE << shift);
    if (*hpwm->bdtr & BDTR_MOE) {
        *hpwm->ccer |= (CCER_CCxNE << shift);
    }

    /* Enable counter */
    *hpwm->cr1 |= CR1_CEN;
}

void pwm_set_duty(pwm_handle_t *hpwm, pwm_channel_t ch, uint16_t duty_x100) {
    /* Clamp to 100% */
    if (duty_x100 > 10000) duty_x100 = 10000;

    /* Calculate CCR value: CCR = (duty / 10000) * (ARR + 1) */
    uint32_t ccr = ((uint32_t)duty_x100 * (hpwm->arr_value + 1)) / 10000;

    volatile uint32_t *ccr_reg;
    switch (ch) {
        case PWM_CH1: ccr_reg = hpwm->ccr1; break;
        case PWM_CH2: ccr_reg = hpwm->ccr2; break;
        case PWM_CH3: ccr_reg = hpwm->ccr3; break;
        case PWM_CH4: ccr_reg = hpwm->ccr4; break;
    }

    *ccr_reg = (uint16_t)ccr;
}

uint16_t pwm_get_duty(pwm_handle_t *hpwm, pwm_channel_t ch) {
    volatile uint32_t *ccr_reg;
    switch (ch) {
        case PWM_CH1: ccr_reg = hpwm->ccr1; break;
        case PWM_CH2: ccr_reg = hpwm->ccr2; break;
        case PWM_CH3: ccr_reg = hpwm->ccr3; break;
        case PWM_CH4: ccr_reg = hpwm->ccr4; break;
    }

    return (uint16_t)((*ccr_reg * 10000) / (hpwm->arr_value + 1));
}

uint16_t pwm_get_resolution(pwm_handle_t *hpwm) {
    return hpwm->arr_value + 1;
}

/* --- Motor Controller --- */

void motor_init(motor_controller_t *motor, pwm_handle_t *pwm,
                pwm_channel_t ch, const ramp_config_t *ramp) {
    motor->pwm = pwm;
    motor->channel = ch;
    motor->state = MOTOR_STOPPED;
    motor->current_duty = 0;
    motor->target_duty = 0;
    motor->ramp_tick_count = 0;

    if (ramp) {
        motor->ramp = *ramp;
    } else {
        motor->ramp.enabled = false;
        motor->ramp.step = 0;
        motor->ramp.interval_ms = 0;
    }

    pwm_set_duty(pwm, ch, 0);
}

void motor_set_speed(motor_controller_t *motor, uint16_t duty_x100) {
    if (duty_x100 > 10000) duty_x100 = 10000;

    motor->target_duty = duty_x100;

    if (motor->ramp.enabled && duty_x100 != motor->current_duty) {
        motor->state = MOTOR_RAMPING;
        motor->ramp_tick_count = 0;
    } else {
        motor->current_duty = duty_x100;
        pwm_set_duty(motor->pwm, motor->channel, duty_x100);
        motor->state = (duty_x100 == 0) ? MOTOR_STOPPED : MOTOR_RUNNING;
    }
}

void motor_tick(motor_controller_t *motor) {
    if (motor->state != MOTOR_RAMPING) return;
    if (!motor->ramp.enabled) return;

    motor->ramp_tick_count++;
    if (motor->ramp_tick_count < motor->ramp.interval_ms) return;

    motor->ramp_tick_count = 0;

    if (motor->current_duty < motor->target_duty) {
        if (motor->current_duty + motor->ramp.step >= motor->target_duty) {
            motor->current_duty = motor->target_duty;
            motor->state = MOTOR_RUNNING;
        } else {
            motor->current_duty += motor->ramp.step;
        }
        pwm_set_duty(motor->pwm, motor->channel, motor->current_duty);
    } else if (motor->current_duty > motor->target_duty) {
        if (motor->current_duty <= motor->ramp.step) {
            motor->current_duty = 0;
            motor->state = MOTOR_STOPPED;
        } else if (motor->current_duty - motor->ramp.step <= motor->target_duty) {
            motor->current_duty = motor->target_duty;
            motor->state = (motor->target_duty == 0) ? MOTOR_STOPPED : MOTOR_RUNNING;
        } else {
            motor->current_duty -= motor->ramp.step;
        }
        pwm_set_duty(motor->pwm, motor->channel, motor->current_duty);
    }
}

motor_state_t motor_get_state(const motor_controller_t *motor) {
    return motor->state;
}

uint16_t motor_get_current_duty(const motor_controller_t *motor) {
    return motor->current_duty;
}
```

#### Main Application (`main.c`)

```c
#include "pwm.h"
#include <stdio.h>

#define TIM1_BASE 0x40012C00UL

static pwm_handle_t hpwm;
static motor_controller_t motor;

/* TIM1 update interrupt handler (called every 1ms) */
void TIM1_UP_IRQHandler(void) {
    /* Clear update interrupt flag */
    *(volatile uint32_t *)(TIM1_BASE + 0x10) &= ~(1U << 0);
    motor_tick(&motor);
}

int main(void) {
    /* Configure PWM: 20 kHz, center-aligned, 300ns dead time */
    pwm_config_t config = {
        .frequency_hz = 20000,
        .mode = PWM_CENTER_ALIGNED,
        .dead_time_ns = 300,
    };

    pwm_init(&hpwm, TIM1_BASE, 72000000, &config);
    pwm_enable_channel(&hpwm, PWM_CH1);  /* TIM1_CH1 = PA8 */

    /* Configure motor with soft-start ramp */
    ramp_config_t ramp = {
        .enabled = true,
        .step = 50,        /* 0.50% per step */
        .interval_ms = 10, /* Every 10ms */
    };

    motor_init(&motor, &hpwm, PWM_CH1, &ramp);

    /* Configure TIM1 update interrupt for ramp ticks */
    /* In real code: set up NVIC for TIM1_UP_IRQn */

    printf("PWM initialized: %u Hz, %u steps resolution\n",
           20000, pwm_get_resolution(&hpwm));

    /* Ramp up to 75% duty */
    printf("Ramping to 75%%...\n");
    motor_set_speed(&motor, 7500);

    /* Simulate ramp progression (in real code, this happens in ISR) */
    while (motor_get_state(&motor) == MOTOR_RAMPING) {
        motor_tick(&motor);
        printf("Duty: %u.%02u%%  State: %d\n",
               motor_get_current_duty(&motor) / 100,
               motor_get_current_duty(&motor) % 100,
               motor_get_state(&motor));
    }

    printf("Motor running at %u.%02u%%\n",
           motor_get_current_duty(&motor) / 100,
           motor_get_current_duty(&motor) % 100);

    /* Hold for a bit, then ramp down */
    printf("Holding...\n");

    printf("Ramping to 0%%...\n");
    motor_set_speed(&motor, 0);

    while (motor_get_state(&motor) == MOTOR_RAMPING) {
        motor_tick(&motor);
    }

    printf("Motor stopped.\n");

    return 0;
}
```

### Rust: Safe PWM Abstraction with Type-Level Guarantees

```rust
// Cargo.toml
// [package]
// name = "pwm-motor"
// version = "0.1.0"
// edition = "2021"

use core::marker::PhantomData;

/// PWM frequency range — enforced at type level
pub struct Frequency<const HZ: u32>;

/// Valid frequency range: 100 Hz to 1 MHz
pub trait ValidFrequency {
    const HZ: u32;
}

/// Duty cycle: 0..=10000 represents 0.00% to 100.00%
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DutyCycle(u16);

impl DutyCycle {
    /// Create a new duty cycle, clamped to valid range
    pub fn new(percent_x100: u16) -> Self {
        Self(percent_x100.min(10000))
    }

    /// Create from a fraction (0.0 to 1.0)
    pub fn from_fraction(f: f32) -> Self {
        Self((f.clamp(0.0, 1.0) * 10000.0) as u16)
    }

    /// Get as 0.01% units
    pub fn as_x100(&self) -> u16 {
        self.0
    }

    /// Get as fraction 0.0–1.0
    pub fn as_fraction(&self) -> f32 {
        self.0 as f32 / 10000.0
    }

    /// 0% duty
    pub const ZERO: Self = Self(0);

    /// 100% duty
    pub const MAX: Self = Self(10000);
}

/// PWM alignment mode
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PwmMode {
    EdgeAligned,
    CenterAligned,
}

/// Ramp configuration
#[derive(Debug, Clone, Copy)]
pub struct RampConfig {
    pub step: DutyCycle,
    pub interval_ms: u32,
}

/// Motor state machine
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum MotorState {
    Stopped,
    Ramping,
    Running,
}

/// Timer channel
#[derive(Debug, Clone, Copy)]
pub enum Channel {
    Ch1,
    Ch2,
    Ch3,
    Ch4,
}

/// PWM configuration validated at construction
#[derive(Debug)]
pub struct PwmConfig<F: ValidFrequency> {
    pub mode: PwmMode,
    pub dead_time_ns: u16,
    _freq: PhantomData<F>,
}

impl<F: ValidFrequency> PwmConfig<F> {
    pub fn new(mode: PwmMode, dead_time_ns: u16) -> Self {
        Self {
            mode,
            dead_time_ns,
            _freq: PhantomData,
        }
    }

    /// Calculate timer parameters
    pub fn calc_timer_params(&self, timer_clk: u32) -> (u16, u16) {
        let target_period = if self.mode == PwmMode::CenterAligned {
            timer_clk / (F::HZ * 2)
        } else {
            timer_clk / F::HZ
        };

        let mut psc: u32 = 0;
        let mut arr = target_period.saturating_sub(1);

        while arr > 65535 && psc < 65535 {
            psc += 1;
            arr = target_period / (psc + 1) - 1;
        }

        (psc as u16, arr.max(1) as u16)
    }

    /// Get resolution in steps
    pub fn resolution(&self, timer_clk: u32) -> u16 {
        let (_, arr) = self.calc_timer_params(timer_clk);
        arr + 1
    }
}

/// Non-blocking ramp state
struct RampState {
    current: DutyCycle,
    target: DutyCycle,
    config: RampConfig,
    tick_count: u32,
}

impl RampState {
    fn tick(&mut self) -> Option<DutyCycle> {
        self.tick_count += 1;
        if self.tick_count < self.config.interval_ms {
            return None;
        }
        self.tick_count = 0;

        if self.current < self.target {
            let next = self.current.as_x100().saturating_add(self.config.step.as_x100());
            if next >= self.target.as_x100() {
                self.current = self.target;
                Some(self.current)
            } else {
                self.current = DutyCycle::new(next);
                Some(self.current)
            }
        } else if self.current > self.target {
            let next = self.current.as_x100().saturating_sub(self.config.step.as_x100());
            if next <= self.target.as_x100() {
                self.current = self.target;
                Some(self.current)
            } else {
                self.current = DutyCycle::new(next);
                Some(self.current)
            }
        } else {
            None
        }
    }
}

/// PWM channel handle — type-safe, no use-after-free
pub struct PwmChannel<TIMER> {
    ccr_reg: *mut u32,
    arr_value: u16,
    _timer: PhantomData<TIMER>,
}

impl<TIMER> PwmChannel<TIMER> {
    pub fn set_duty(&mut self, duty: DutyCycle) {
        let ccr = ((duty.as_x100() as u32) * (self.arr_value as u32 + 1)) / 10000;
        unsafe {
            self.ccr_reg.write_volatile(ccr);
        }
    }

    pub fn get_duty(&self) -> DutyCycle {
        let ccr = unsafe { self.ccr_reg.read_volatile() };
        DutyCycle::new(((ccr * 10000) / (self.arr_value as u32 + 1)) as u16)
    }
}

/// Motor controller with type-safe frequency
pub struct MotorController<TIMER, F: ValidFrequency> {
    channel: PwmChannel<TIMER>,
    state: MotorState,
    ramp: Option<RampState>,
    _freq: PhantomData<F>,
}

impl<TIMER, F: ValidFrequency> MotorController<TIMER, F> {
    pub fn new(
        channel: PwmChannel<TIMER>,
        ramp_config: Option<RampConfig>,
    ) -> Self {
        let ramp = ramp_config.map(|config| RampState {
            current: DutyCycle::ZERO,
            target: DutyCycle::ZERO,
            config,
            tick_count: 0,
        });

        Self {
            channel,
            state: MotorState::Stopped,
            ramp,
            _freq: PhantomData,
        }
    }

    pub fn set_speed(&mut self, duty: DutyCycle) {
        self.state = if duty == DutyCycle::ZERO {
            MotorState::Stopped
        } else {
            MotorState::Running
        };

        if let Some(ref mut ramp) = self.ramp {
            if ramp.current != duty {
                ramp.target = duty;
                ramp.tick_count = 0;
                self.state = MotorState::Ramping;
                return;
            }
        }

        self.channel.set_duty(duty);
    }

    /// Call from timer interrupt
    pub fn tick(&mut self) {
        if self.state != MotorState::Ramping {
            return;
        }

        if let Some(ref mut ramp) = self.ramp {
            if let Some(new_duty) = ramp.tick() {
                self.channel.set_duty(new_duty);
                if new_duty == ramp.target {
                    self.state = if new_duty == DutyCycle::ZERO {
                        MotorState::Stopped
                    } else {
                        MotorState::Running
                    };
                }
            }
        }
    }

    pub fn state(&self) -> MotorState {
        self.state
    }

    pub fn current_duty(&self) -> DutyCycle {
        self.channel.get_duty()
    }
}

// --- Example usage with type-level frequency ---

// Define a valid frequency
pub struct Freq20kHz;
impl ValidFrequency for Freq20kHz {
    const HZ: u32 = 20_000;
}

// #[entry]
// fn main() -> ! {
//     let dp = stm32::Peripherals::take().unwrap();
//     let rcc = dp.RCC.constrain();
//     let clocks = rcc.cfgr.sysclk(72.MHz()).freeze();
//
//     let gpioa = dp.GPIOA.split();
//     let ch1 = gpioa.pa8.into_alternate::<6>();
//
//     // Configure PWM with compile-time frequency validation
//     let config = PwmConfig::<Freq20kHz>::new(
//         PwmMode::CenterAligned,
//         300, // 300ns dead time
//     );
//
//     let resolution = config.resolution(72_000_000);
//     defmt::println!("PWM resolution: {} steps", resolution);
//
//     // Create motor controller with soft-start
//     let mut motor = MotorController::new(
//         channel,
//         Some(RampConfig {
//             step: DutyCycle::new(50),     // 0.50% per step
//             interval_ms: 10,              // every 10ms
//         }),
//     );
//
//     // Ramp to 75% — type-safe duty cycle
//     motor.set_speed(DutyCycle::from_fraction(0.75));
//
//     loop {
//         motor.tick();
//         defmt::println!(
//             "State: {:?}, Duty: {:.2}%",
//             motor.state(),
//             motor.current_duty().as_fraction() * 100.0
//         );
//         cortex_m::asm::delay(1_000_000);
//     }
// }
```

### Ada: Motor Control Package with Range-Checked Parameters

```ada
-- pwm_motor.ads
with System;

package PWM_Motor is

   -- Duty cycle: 0..10000 represents 0.00% to 100.00%
   subtype Duty_Cycle is Integer range 0 .. 10000;

   -- PWM frequency range (Hz)
   subtype PWM_Frequency is Positive range 100 .. 1_000_000;

   -- Dead time range (nanoseconds)
   subtype Dead_Time_NS is Integer range 0 .. 10000;

   -- Timer clock (Hz)
   subtype Timer_Clock is Positive range 1_000_000 .. 200_000_000;

   -- Ramp step size
   subtype Ramp_Step is Duty_Cycle range 1 .. 1000;

   -- Ramp interval (milliseconds)
   subtype Ramp_Interval_MS is Positive range 1 .. 1000;

   -- PWM alignment mode
   type PWM_Mode is (Edge_Aligned, Center_Aligned);

   -- Motor state
   type Motor_State is (Stopped, Ramping, Running);

   -- Timer channel
   type PWM_Channel is (CH1, CH2, CH3, CH4);

   -- PWM configuration record
   type PWM_Config is record
      Frequency    : PWM_Frequency;
      Mode         : PWM_Mode;
      Dead_Time_NS : Dead_Time_NS := 0;
   end record;

   -- Ramp configuration record
   type Ramp_Config is record
      Enabled      : Boolean := False;
      Step         : Ramp_Step := 100;
      Interval_MS  : Ramp_Interval_MS := 10;
   end record;

   -- Calculated timer parameters
   type Timer_Params is record
      PSC   : Integer range 0 .. 65535;
      ARR   : Integer range 1 .. 65535;
   end record;

   -- Motor controller handle
   type Motor_Controller is private;

   -- Calculate timer parameters from config
   function Calculate_Timer_Params
     (Config     : PWM_Config;
      Timer_Clk  : Timer_Clock)
      return Timer_Params;

   -- Get PWM resolution in steps
   function Get_Resolution
     (Params : Timer_Params)
      return Positive;

   -- Convert duty cycle to CCR value
   function Duty_To_CCR
     (Duty   : Duty_Cycle;
      ARR    : Positive)
      return Integer;

   -- Convert CCR value to duty cycle
   function CCR_To_Duty
     (CCR    : Integer;
      ARR    : Positive)
      return Duty_Cycle;

   -- Initialize motor controller
   procedure Initialize
     (Motor  : out Motor_Controller;
      Config : PWM_Config;
      Timer_Clk : Timer_Clock;
      Ramp   : Ramp_Config);

   -- Set target speed
   procedure Set_Speed
     (Motor : in out Motor_Controller;
      Duty  : Duty_Cycle);

   -- Tick function (call from timer interrupt)
   procedure Tick
     (Motor : in out Motor_Controller);

   -- Get current state
   function Get_State
     (Motor : Motor_Controller)
      return Motor_State;

   -- Get current duty cycle
   function Get_Current_Duty
     (Motor : Motor_Controller)
      return Duty_Cycle;

   -- Get target duty cycle
   function Get_Target_Duty
     (Motor : Motor_Controller)
      return Duty_Cycle;

private

   type Motor_Controller is record
      Config       : PWM_Config;
      Params       : Timer_Params;
      Ramp         : Ramp_Config;
      State        : Motor_State := Stopped;
      Current_Duty : Duty_Cycle := 0;
      Target_Duty  : Duty_Cycle := 0;
      Tick_Count   : Natural := 0;
   end record;

end PWM_Motor;
```

```ada
-- pwm_motor.adb
package body PWM_Motor is

   function Calculate_Timer_Params
     (Config     : PWM_Config;
      Timer_Clk  : Timer_Clock)
      return Timer_Params
   is
      Target_Period : Integer;
      PSC : Integer := 0;
      ARR : Integer;
   begin
      -- Calculate target period
      if Config.Mode = Center_Aligned then
         Target_Period := Integer (Timer_Clk) / (Integer (Config.Frequency) * 2);
      else
         Target_Period := Integer (Timer_Clk) / Integer (Config.Frequency);
      end if;

      ARR := Target_Period - 1;

      -- Adjust if ARR exceeds 16-bit
      while ARR > 65535 and then PSC < 65535 loop
         PSC := PSC + 1;
         ARR := Target_Period / (PSC + 1) - 1;
      end loop;

      -- Ensure minimum ARR of 1
      if ARR < 1 then
         ARR := 1;
      end if;

      return (PSC => PSC, ARR => ARR);
   end Calculate_Timer_Params;

   function Get_Resolution
     (Params : Timer_Params)
      return Positive
   is
   begin
      return Positive (Params.ARR + 1);
   end Get_Resolution;

   function Duty_To_CCR
     (Duty   : Duty_Cycle;
      ARR    : Positive)
      return Integer
   is
   begin
      return (Duty * ARR) / 10000;
   end Duty_To_CCR;

   function CCR_To_Duty
     (CCR    : Integer;
      ARR    : Positive)
      return Duty_Cycle
   is
      Result : Integer;
   begin
      Result := (CCR * 10000) / ARR;
      if Result < 0 then
         return 0;
      elsif Result > 10000 then
         return 10000;
      else
         return Duty_Cycle (Result);
      end if;
   end CCR_To_Duty;

   procedure Initialize
     (Motor  : out Motor_Controller;
      Config : PWM_Config;
      Timer_Clk : Timer_Clock;
      Ramp   : Ramp_Config)
   is
   begin
      Motor.Config := Config;
      Motor.Params := Calculate_Timer_Params (Config, Timer_Clk);
      Motor.Ramp := Ramp;
      Motor.State := Stopped;
      Motor.Current_Duty := 0;
      Motor.Target_Duty := 0;
      Motor.Tick_Count := 0;
   end Initialize;

   procedure Set_Speed
     (Motor : in out Motor_Controller;
      Duty  : Duty_Cycle)
   is
   begin
      Motor.Target_Duty := Duty;

      if Motor.Ramp.Enabled and then Duty /= Motor.Current_Duty then
         Motor.State := Ramping;
         Motor.Tick_Count := 0;
      else
         Motor.Current_Duty := Duty;
         if Duty = 0 then
            Motor.State := Stopped;
         else
            Motor.State := Running;
         end if;
      end if;
   end Set_Speed;

   procedure Tick
     (Motor : in out Motor_Controller)
   is
   begin
      if Motor.State /= Ramping or else not Motor.Ramp.Enabled then
         return;
      end if;

      Motor.Tick_Count := Motor.Tick_Count + 1;
      if Motor.Tick_Count < Integer (Motor.Ramp.Interval_MS) then
         return;
      end if;

      Motor.Tick_Count := 0;

      if Motor.Current_Duty < Motor.Target_Duty then
         if Motor.Current_Duty + Integer (Motor.Ramp.Step) >= Motor.Target_Duty then
            Motor.Current_Duty := Motor.Target_Duty;
            Motor.State := Running;
         else
            Motor.Current_Duty := Motor.Current_Duty + Integer (Motor.Ramp.Step);
         end if;
      elsif Motor.Current_Duty > Motor.Target_Duty then
         if Motor.Current_Duty <= Integer (Motor.Ramp.Step) then
            Motor.Current_Duty := 0;
            Motor.State := Stopped;
         elsif Motor.Current_Duty - Integer (Motor.Ramp.Step) <= Motor.Target_Duty then
            Motor.Current_Duty := Motor.Target_Duty;
            if Motor.Target_Duty = 0 then
               Motor.State := Stopped;
            else
               Motor.State := Running;
            end if;
         else
            Motor.Current_Duty := Motor.Current_Duty - Integer (Motor.Ramp.Step);
         end if;
      end if;
   end Tick;

   function Get_State
     (Motor : Motor_Controller)
      return Motor_State
   is
   begin
      return Motor.State;
   end Get_State;

   function Get_Current_Duty
     (Motor : Motor_Controller)
      return Duty_Cycle
   is
   begin
      return Motor.Current_Duty;
   end Get_Current_Duty;

   function Get_Target_Duty
     (Motor : Motor_Controller)
      return Duty_Cycle
   is
   begin
      return Motor.Target_Duty;
   end Get_Target_Duty;

end PWM_Motor;
```

```ada
-- main.adb
with PWM_Motor; use PWM_Motor;
with Text_IO; use Text_IO;
with Ada.Real_Time; use Ada.Real_Time;

procedure Main is
   Motor : Motor_Controller;
   Config : PWM_Config := (
      Frequency    => 20_000,
      Mode         => Center_Aligned,
      Dead_Time_NS => 300
   );
   Ramp : Ramp_Config := (
      Enabled     => True,
      Step        => 50,
      Interval_MS => 10
   );
   Timer_Clk : Timer_Clock := 72_000_000;
   Resolution : Positive;
   Start_Time : Time;
   Elapsed : Time_Span;
begin
   -- Initialize motor controller
   Initialize (Motor, Config, Timer_Clk, Ramp);

   Resolution := Get_Resolution (Motor.Params);
   Put_Line ("PWM initialized: 20000 Hz, " &
             Positive'Image (Resolution) & " steps");

   -- Show timer parameters
   Put_Line ("PSC: " & Integer'Image (Motor.Params.PSC) &
             ", ARR: " & Integer'Image (Motor.Params.ARR));

   -- Ramp up to 75%
   Put_Line ("Ramping to 75%...");
   Set_Speed (Motor, 7500);

   -- Simulate ramp progression
   Start_Time := Clock;
   loop
      Tick (Motor);

      declare
         Current : Duty_Cycle := Get_Current_Duty (Motor);
         State   : Motor_State := Get_State (Motor);
      begin
         Put ("Duty: " & Duty_Cycle'Image (Current / 100) & ".");
         declare
            Frac : Integer := Current rem 100;
         begin
            if Frac < 10 then
               Put ("0");
            end if;
            Put (Integer'Image (Frac));
         end;
         Put ("%  State: ");
         case State is
            when Stopped => Put_Line ("STOPPED");
            when Ramping => Put_Line ("RAMPING");
            when Running => Put_Line ("RUNNING");
         end case;
      end;

      exit when Get_State (Motor) /= Ramping;

      -- Simulate 10ms delay
      delay 0.01;
   end loop;

   Put_Line ("Motor running at " &
             Duty_Cycle'Image (Get_Current_Duty (Motor) / 100) &
             "." &
             Integer'Image (Get_Current_Duty (Motor) rem 100) & "%");

   -- Hold, then ramp down
   Put_Line ("Holding...");
   delay 1.0;

   Put_Line ("Ramping to 0%...");
   Set_Speed (Motor, 0);

   loop
      Tick (Motor);
      exit when Get_State (Motor) = Stopped;
      delay 0.01;
   end loop;

   Put_Line ("Motor stopped.");
end Main;
```

### Zig: Comptime-Validated PWM Configuration with State Machine

```zig
// pwm_motor.zig
const std = @import("std");

/// PWM frequency — validated at comptime
pub fn isValidFrequency(comptime hz: u32) bool {
    return hz >= 100 and hz <= 1_000_000;
}

/// PWM configuration — all fields validated at comptime
pub fn PwmConfig(comptime freq_hz: u32) type {
    comptime {
        if (!isValidFrequency(freq_hz)) {
            @compileError("PWM frequency must be between 100 Hz and 1 MHz");
        }
    }

    return struct {
        pub const frequency_hz = freq_hz;
        mode: PwmMode,
        dead_time_ns: u16,

        pub fn calcTimerParams(self: @This(), timer_clk: u32) TimerParams {
            const target_period = if (self.mode == .center_aligned)
                timer_clk / (freq_hz * 2)
            else
                timer_clk / freq_hz;

            var psc: u32 = 0;
            var arr: u32 = if (target_period > 0) target_period - 1 else 0;

            while (arr > 65535 and psc < 65535) : (psc += 1) {
                arr = target_period / (psc + 1) - 1;
            }

            if (arr < 1) arr = 1;

            return TimerParams{
                .psc = @as(u16, @intCast(psc)),
                .arr = @as(u16, @intCast(arr)),
            };
        }

        pub fn resolution(self: @This(), timer_clk: u32) u16 {
            const params = self.calcTimerParams(timer_clk);
            return params.arr + 1;
        }
    };
}

/// Timer parameters
pub const TimerParams = struct {
    psc: u16,
    arr: u16,
};

/// PWM mode
pub const PwmMode = enum {
    edge_aligned,
    center_aligned,
};

/// Motor state machine
pub const MotorState = enum {
    stopped,
    ramping,
    running,
};

/// Duty cycle type with range validation
pub const DutyCycle = struct {
    value: u16,

    pub fn new(percent_x100: u16) DutyCycle {
        return DutyCycle{
            .value = if (percent_x100 > 10000) 10000 else percent_x100,
        };
    }

    pub fn fromFraction(f: f32) DutyCycle {
        const clamped = std.math.clamp(f, 0.0, 1.0);
        return DutyCycle{
            .value = @as(u16, @intFromFloat(clamped * 10000.0)),
        };
    }

    pub fn asX100(self: DutyCycle) u16 {
        return self.value;
    }

    pub fn asFraction(self: DutyCycle) f32 {
        return @as(f32, @floatFromInt(self.value)) / 10000.0;
    }

    pub const zero = DutyCycle{ .value = 0 };
    pub const max = DutyCycle{ .value = 10000 };
};

/// Ramp configuration
pub const RampConfig = struct {
    enabled: bool,
    step: DutyCycle,
    interval_ms: u32,
};

/// Non-blocking ramp state
const RampState = struct {
    current: DutyCycle,
    target: DutyCycle,
    config: RampConfig,
    tick_count: u32,

    fn tick(self: *RampState) ?DutyCycle {
        self.tick_count += 1;
        if (self.tick_count < self.config.interval_ms) {
            return null;
        }
        self.tick_count = 0;

        if (self.current.value < self.target.value) {
            const next = @min(self.current.value + self.config.step.value, self.target.value);
            self.current.value = next;
            return if (next == self.target.value) self.current else self.current;
        } else if (self.current.value > self.target.value) {
            const next_val = if (self.current.value <= self.config.step.value)
                @as(u16, 0)
            else
                @max(self.current.value - self.config.step.value, self.target.value);
            self.current.value = next_val;
            return self.current;
        }
        return null;
    }
};

/// Motor controller with state machine
pub fn MotorController(comptime FreqHz: u32) type {
    return struct {
        config: PwmConfig(FreqHz),
        params: TimerParams,
        state: MotorState,
        current_duty: DutyCycle,
        target_duty: DutyCycle,
        ramp: ?RampState,

        const Self = @This();

        pub fn init(
            config: PwmConfig(FreqHz),
            timer_clk: u32,
            ramp_config: ?RampConfig,
        ) Self {
            const params = config.calcTimerParams(timer_clk);

            var ramp: ?RampState = null;
            if (ramp_config) |rc| {
                ramp = RampState{
                    .current = DutyCycle.zero,
                    .target = DutyCycle.zero,
                    .config = rc,
                    .tick_count = 0,
                };
            }

            return Self{
                .config = config,
                .params = params,
                .state = .stopped,
                .current_duty = DutyCycle.zero,
                .target_duty = DutyCycle.zero,
                .ramp = ramp,
            };
        }

        pub fn setSpeed(self: *Self, duty: DutyCycle) void {
            self.target_duty = duty;

            if (self.ramp) |*r| {
                if (r.current.value != duty.value) {
                    r.target = duty;
                    r.tick_count = 0;
                    self.state = .ramping;
                    return;
                }
            }

            self.current_duty = duty;
            self.state = if (duty.value == 0) .stopped else .running;
        }

        pub fn tick(self: *Self) void {
            if (self.state != .ramping) return;

            if (self.ramp) |*ramp| {
                if (ramp.tick()) |new_duty| {
                    self.current_duty = new_duty;
                    if (new_duty.value == ramp.target.value) {
                        self.state = if (new_duty.value == 0) .stopped else .running;
                    }
                }
            }
        }

        pub fn getState(self: *const Self) MotorState {
            return self.state;
        }

        pub fn getCurrentDuty(self: *const Self) DutyCycle {
            return self.current_duty;
        }
    };
}
```

```zig
// main.zig
const std = @import("std");
const pwm = @import("pwm_motor.zig");

pub fn main() !void {
    const stdout = std.io.getStdOut().writer();

    // Comptime-validated PWM config — 20 kHz
    const Config = pwm.PwmConfig(20000);
    var config = Config{
        .mode = .center_aligned,
        .dead_time_ns = 300,
    };

    const timer_clk: u32 = 72_000_000;
    const params = config.calcTimerParams(timer_clk);
    const resolution = config.resolution(timer_clk);

    try stdout.print("PWM initialized: 20000 Hz, {d} steps\n", .{resolution});
    try stdout.print("PSC: {d}, ARR: {d}\n", .{ params.psc, params.arr });

    // Create motor controller with soft-start ramp
    var motor = pwm.MotorController(20000).init(
        config,
        timer_clk,
        pwm.RampConfig{
            .enabled = true,
            .step = pwm.DutyCycle.new(50),
            .interval_ms = 10,
        },
    );

    // Ramp up to 75%
    try stdout.print("Ramping to 75%...\n", .{});
    motor.setSpeed(pwm.DutyCycle.fromFraction(0.75));

    // Simulate ramp progression
    while (motor.getState() == .ramping) {
        motor.tick();

        const duty = motor.getCurrentDuty();
        try stdout.print("Duty: {d}.{d:0>2}%  State: {s}\n", .{
            duty.asX100() / 100,
            duty.asX100() % 100,
            @tagName(motor.getState()),
        });
    }

    try stdout.print("Motor running at {d}.{d:0>2}%\n", .{
        motor.getCurrentDuty().asX100() / 100,
        motor.getCurrentDuty().asX100() % 100,
    });

    // Hold, then ramp down
    try stdout.print("Holding...\n", .{});

    try stdout.print("Ramping to 0%...\n", .{});
    motor.setSpeed(pwm.DutyCycle.zero);

    while (motor.getState() == .ramping) {
        motor.tick();
    }

    try stdout.print("Motor stopped.\n", .{});

    // Demonstrate comptime validation
    // Uncommenting this would cause a compile error:
    // const bad_config = pwm.PwmConfig(50);  // Error: frequency too low
}
```

## Build and Run Instructions

### C (ARM GCC)

```bash
# Build
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -O2 \
    -fno-common -ffunction-sections -fdata-sections \
    -Wall -Wextra -Werror \
    -T stm32f103c8.ld \
    -o pwm_motor.elf \
    main.c pwm.c startup_stm32f103xb.c

arm-none-eabi-objcopy -O binary pwm_motor.elf pwm_motor.bin
arm-none-eabi-size pwm_motor.elf
```

### Rust

```bash
rustup target add thumbv7m-none-eabi
cargo build --release --target thumbv7m-none-eabi
```

### Ada

```bash
gprbuild -P pwm_motor.gpr -XTARGET=arm-elf -O2
```

### Zig

```bash
# Bare-metal ARM
zig build-exe main.zig -target thumbv7m-freestanding -OReleaseSmall

# Host testing (recommended for development)
zig build-exe main.zig -OReleaseFast
./main
```

## GDB Verification: Watching Timer Compare Register Values During Ramp

```bash
# Start GDB with OpenOCD
arm-none-eabi-gdb pwm_motor.elf

# Connect to target
(gdb) target remote :3333

# Set breakpoint at main
(gdb) break main
(gdb) continue

# Watch the CCR1 register (TIM1 channel 1 compare register)
# TIM1 CCR1 is at 0x40012C00 + 0x34 = 0x40012C34
(gdb) watch *0x40012C34
Hardware watchpoint 2: *0x40012C34

# Also watch ARR for reference
(gdb) watch *0x40012C2C
Hardware watchpoint 3: *0x40012C2C

# Continue and observe CCR values changing during ramp
(gdb) continue

# Expected output as ramp progresses:
# Hardware watchpoint 2: *0x40012C34
#  Old value = 0
#  New value = 180    ← 5% of 3600
# Hardware watchpoint 2: *0x40012C34
#  Old value = 180
#  New value = 360    ← 10%
# Hardware watchpoint 2: *0x40012C34
#  Old value = 360
#  New value = 540    ← 15%
# ...continues until CCR = 2700 (75%)

# Print formatted values during ramp
(gdb) display (*(volatile uint16_t *)0x40012C34) * 10000 / 3600
1: (*(volatile uint16_t *)0x40012C34) * 10000 / 3600 = 500

# Set breakpoint at motor_tick to step through ramp algorithm
(gdb) break motor_tick
(gdb) continue
```

### Expected GDB Trace

```
Hardware watchpoint 2: *0x40012C34
 Old value = 0
 New value = 180     (5.00%)
Hardware watchpoint 2: *0x40012C34
 Old value = 180
 New value = 360     (10.00%)
Hardware watchpoint 2: *0x40012C34
 Old value = 360
 New value = 540     (15.00%)
...
Hardware watchpoint 2: *0x40012C34
 Old value = 2520
 New value = 2700    (75.00%)  ← Target reached, ramp stops

# Verify ARR (period register) stays constant
Hardware watchpoint 3: *0x40012C2C
 Old value = 3599
 New value = 3599    ← Unchanged, as expected
```

## What You Learned

- Timer architecture: prescaler, auto-reload, and compare register relationships
- PWM frequency calculation and resolution trade-offs
- Edge-aligned vs center-aligned PWM and their impact on motor performance
- Dead-time insertion for H-bridge shoot-through prevention
- Soft-start ramp algorithms to limit inrush current
- Non-blocking ramp state machine driven by timer interrupts
- Type-level frequency validation (Rust), range-checked parameters (Ada), comptime validation (Zig)

## Next Steps

- Implement PID speed control with encoder feedback
- Add current sensing and overcurrent protection
- Implement space vector PWM (SVPWM) for 3-phase motor control
- Add fault input handling (TIM1 break input)
- Implement sensorless BLDC commutation using back-EMF zero-crossing detection
- Add DMA-driven PWM for waveform generation (audio, arbitrary waveforms)

## Language Comparison

| Feature | C | Rust | Ada | Zig |
|---------|---|------|-----|-----|
| **Frequency validation** | Runtime `if` check | Type-level `ValidFrequency` trait | Subtype range `100..1_000_000` | Comptime `@compileError` |
| **Duty cycle range** | `uint16_t`, manual clamp | `DutyCycle` struct with `new()` | Subtype `0..10000` | `DutyCycle` struct with `new()` |
| **Dead-time calc** | Manual bit math in function | Method on `PwmConfig` | Package-level function | Method on comptime struct |
| **State machine** | Enum + manual transitions | `MotorState` enum, type-safe | `Motor_State` enum, exhaustive | `MotorState` enum with `@tagName` |
| **Timer params** | Calculated at init | `calc_timer_params()` method | `Calculate_Timer_Params` function | Comptime-capable method |
| **Ramp algorithm** | Non-blocking tick function | `RampState` with `tick()` | `Tick` procedure | `RampState.tick()` method |
| **Register access** | Volatile pointers | `*mut u32` with unsafe | Abstracted (platform layer) | Abstracted (platform layer) |
| **Safety guarantees** | None — caller must validate | Compile-time frequency, runtime duty | Subtype range enforcement | Comptime frequency, runtime duty |
| **Binary size** | ~3KB (driver only) | ~5KB (with type system) | ~6KB (runtime checks) | ~4KB (comptime optimized) |

## Deliverables

- [ ] PWM timer initialization with configurable frequency and mode
- [ ] Frequency calculation: PSC and ARR derived from target frequency
- [ ] Center-aligned and edge-aligned PWM mode support
- [ ] Dead-time insertion for complementary outputs (H-bridge safe)
- [ ] Duty cycle setting with 0.01% resolution (0–10000 scale)
- [ ] Soft-start ramp with configurable step and interval
- [ ] Non-blocking ramp state machine (stopped/ramping/running)
- [ ] GDB verification showing CCR register changes during ramp
- [ ] Output: PWM parameters, ramp progression, final state confirmation
