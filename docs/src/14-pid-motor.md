---
title: "Project 14: Motor Control with PID & Fault Detection"
phase: 5
project: 14
---

# Project 14: Motor Control with PID & Fault Detection

## Introduction

In this project, you will implement a complete motor control system with PID feedback control, fault detection, and safe state management. This is a critical embedded systems application found in robotics, industrial automation, automotive systems, and aerospace. You will implement the system in C, Rust, Ada, and Zig — each language showcasing its unique strengths for safety-critical control systems.

> **Prerequisite:** This project builds on concepts from earlier phases. Familiarity with PWM generation (Project 4), timer interrupts (Project 6), and ADC sampling (Project 7) is assumed.

### What You'll Learn

- PID control theory: Proportional, Integral, Derivative terms
- Discrete-time PID implementation (position form vs velocity form)
- Anti-windup strategies: clamping, back-calculation, conditional integration
- Fixed-point arithmetic for PID on MCUs without FPU
- Fault detection: overcurrent, overtemperature, undervoltage, stall detection
- Safe state machine: Running, Fault, Safe-Stop, Recovery
- Watchdog timer (IWDG) configuration and feed strategy
- Language-specific approaches to safety-critical control code

## PID Control Theory

### The Three Terms

A PID controller computes a control output `u(t)` from the error `e(t) = setpoint - measured`:

```
                    ∫ₜ
u(t) = Kp·e(t) + Ki·  e(τ)dτ + Kd·de(t)/dt
                    ₀
```

| Term | Formula | Purpose | Effect |
|------|---------|---------|--------|
| **Proportional** | `Kp · e(t)` | Reacts to current error | Higher Kp → faster response, but more overshoot |
| **Integral** | `Ki · ∫e(τ)dτ` | Eliminates steady-state error | Higher Ki → eliminates offset, but causes oscillation |
| **Derivative** | `Kd · de(t)/dt` | Predicts future error | Higher Kd → dampens oscillation, but amplifies noise |

### Visual PID Response

```
Setpoint ─────────────────────────────────────────
              ╱╲
             ╱  ╲        Kp too high
            ╱    ╲╱╲
Measured ──╱      ╳  ╲╱╲╱
          ╱      ╱╲
         ╱      ╱  ╲─────── Well-tuned
        ╱──────╱
       ╱
      ╱
Time ────────────────────────────────────────────
```

## Discrete-Time PID

MCUs sample at discrete intervals `dt`. The continuous PID must be discretized:

### Position Form

```
u[k] = Kp·e[k] + Ki·dt·Σe[i] + Kd·(e[k] - e[k-1])/dt
                  i=0
```

The position form computes the **absolute** control output. It is simple but susceptible to integral windup.

### Velocity (Incremental) Form

```
Δu[k] = Kp·(e[k] - e[k-1]) + Ki·dt·e[k] + Kd·(e[k] - 2·e[k-1] + e[k-2])/dt
u[k] = u[k-1] + Δu[k]
```

The velocity form computes the **change** in control output. It naturally avoids windup since the integral term is bounded by `Ki·dt·e[k]` per step.

### Implementation Comparison

| Aspect | Position Form | Velocity Form |
|--------|--------------|---------------|
| Integral storage | Full sum accumulator | Implicit (previous output) |
| Windup susceptibility | High (needs anti-windup) | Low (naturally bounded) |
| Bumpless transfer | Requires special handling | Automatic |
| Derivative kick | Yes (on setpoint change) | No (error-difference form) |
| Computational cost | Lower | Slightly higher |

## Anti-Windup Strategies

When the actuator saturates (e.g., PWM at 100%), the integral term continues accumulating error — this is **integral windup**. Three common solutions:

### 1. Clamping (Simplest)

```c
if (output > MAX_OUTPUT) {
    output = MAX_OUTPUT;
    if (error > 0) integral -= Ki * dt * error; // Don't integrate
} else if (output < MIN_OUTPUT) {
    output = MIN_OUTPUT;
    if (error < 0) integral -= Ki * dt * error;
}
```

### 2. Back-Calculation

```c
output = Kp * error + integral + derivative;
if (output > MAX_OUTPUT) {
    integral += Ki * dt * error + Kt * (MAX_OUTPUT - output);
} else if (output < MIN_OUTPUT) {
    integral += Ki * dt * error + Kt * (MIN_OUTPUT - output);
} else {
    integral += Ki * dt * error;
}
```

`Kt` is the back-calculation gain, typically `Kt = Ki / Kp`.

### 3. Conditional Integration

```c
if (!(output >= MAX_OUTPUT && error > 0) &&
    !(output <= MIN_OUTPUT && error < 0)) {
    integral += Ki * dt * error;
}
```

Only integrate when it will help reduce the error.

## Fixed-Point Arithmetic

For MCUs without an FPU (or when deterministic timing is critical), PID can be implemented in fixed-point arithmetic.

### Q15.16 Format

```
value_fixed = value_float * 2^16
value_float = value_fixed / 2^16
```

| Component | Range | Resolution |
|-----------|-------|------------|
| Q15.16 signed | -32768.0 to 32767.99998 | 0.000015 |
| Q31.32 (intermediate) | Very large | 2.3e-10 |

```c
typedef int32_t q16_t;  // Q15.16 fixed-point

#define Q16(x)      ((q16_t)((x) * 65536.0f))
#define Q16_TO_FLOAT(x)  ((float)(x) / 65536.0f)
#define Q16_MUL(a, b)    (((int64_t)(a) * (b)) >> 16)
#define Q16_DIV(a, b)    (((int64_t)(a) << 16) / (b))
```

> **Warning:** Fixed-point PID requires careful overflow analysis. Always use 64-bit intermediate results for multiplication, and saturate before converting back to 32-bit.

## Fault Detection

### Fault Types

| Fault | Detection Method | Threshold | Response |
|-------|-----------------|-----------|----------|
| **Overcurrent** | ADC on current sense resistor | > 2× rated current | Immediate PWM off |
| **Overtemperature** | Thermistor or MCU internal temp sensor | > 85°C | Reduce PWM, then off |
| **Undervoltage** | ADC on supply voltage | < 80% nominal | Reduce PWM, alert |
| **Stall** | Current high + speed near zero for > 500ms | Configurable | PWM off, fault state |
| **Over-speed** | Encoder frequency too high | > 120% max rated | Reduce PWM |
| **Open-loop** | No feedback signal for > 100ms | Timeout | Fault state |

### Fault Priority

```
Critical (immediate action):  Overcurrent, Short circuit
High (within 10ms):           Overtemperature, Stall
Medium (within 100ms):        Undervoltage, Over-speed
Low (log only):               Minor anomalies, noise spikes
```

## Safe State Machine

```
                    ┌──────────────┐
              ┌─────│   SAFE-STOP  │◄──────┐
              │     │  (PWM = 0)   │       │
              │     └──────┬───────┘       │
              │            │ Enable cmd    │ Fault cleared
              │     ┌──────▼───────┐       │
              │     │   RECOVERY   │───────┘
              │     │ (ramp up)    │  Fault detected
              │     └──────┬───────┐
              │            │ Ramp    │
              │     ┌──────▼───────┐ │
              │     │   RUNNING    │─┘
              │     │ (PID active) │
              │     └──────────────┘
              │
        Any fault (any state)
```

### State Transitions

| From | To | Trigger |
|------|-----|---------|
| SAFE-STOP | RECOVERY | Enable command received, no active faults |
| RECOVERY | RUNNING | Ramp complete, all parameters nominal |
| RECOVERY | SAFE-STOP | Fault detected during ramp |
| RUNNING | RECOVERY | Fault detected (decelerate first) |
| RUNNING | SAFE-STOP | Critical fault (overcurrent, short) |
| Any | SAFE-STOP | Watchdog timeout |

## Watchdog Timer (IWDG)

The Independent Watchdog (IWDG) on STM32 is a free-running down-counter clocked by an independent 32kHz LSI oscillator. If it reaches zero, the MCU resets.

```
LSI (32kHz) → Prescaler (4-256) → Counter (12-bit) → Reset at 0

Timeout = (Prescaler / 32000) × Counter_Value
```

| Prescaler | Divider | Min Timeout | Max Timeout |
|-----------|---------|-------------|-------------|
| 4 | 4 | 0.125ms | 512ms |
| 8 | 8 | 0.25ms | 1024ms |
| 16 | 16 | 0.5ms | 2048ms |
| 32 | 32 | 1ms | 4096ms |
| 64 | 64 | 2ms | 8192ms |
| 128 | 128 | 4ms | 16384ms |
| 256 | 256 | 8ms | 32768ms |

> **Warning:** The IWDG feed strategy is critical. Feed only when the system is in a known-good state — not blindly in a timer interrupt. A stuck system that keeps feeding the watchdog is worse than one that resets.

## Implementation: C

### PID Controller with Fixed-Point Math

```c
/* pid_controller.h — PID controller interface */
#ifndef PID_CONTROLLER_H
#define PID_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

/* Q15.16 fixed-point type */
typedef int32_t q16_t;

/* PID configuration (float for tuning, converted to fixed at init) */
typedef struct {
    float kp;
    float ki;
    float kd;
    float dt;             /* Sample period in seconds */
    float output_min;
    float output_max;
    float integral_min;
    float integral_max;
    bool use_velocity_form;
    float back_calc_gain; /* Kt for back-calculation anti-windup */
} pid_config_t;

/* PID controller state */
typedef struct {
    /* Fixed-point parameters */
    q16_t kp;
    q16_t ki;
    q16_t kd;
    q16_t dt;
    q16_t output_min;
    q16_t output_max;
    q16_t integral_min;
    q16_t integral_max;
    q16_t back_calc_gain;

    /* State variables */
    q16_t integral;
    q16_t prev_error;
    q16_t prev_prev_error;  /* For velocity form derivative */
    q16_t prev_output;      /* For velocity form */
    q16_t output;
    bool use_velocity_form;
    bool is_initialized;
} pid_t;

/* Initialize PID controller */
void pid_init(pid_t *pid, const pid_config_t *config);

/* Reset PID state (keep parameters) */
void pid_reset(pid_t *pid);

/* Compute PID output — position form */
q16_t pid_compute(pid_t *pid, q16_t setpoint, q16_t measured);

/* Compute PID output — velocity form */
q16_t pid_compute_velocity(pid_t *pid, q16_t setpoint, q16_t measured);

/* Set integral value (for bumpless transfer) */
void pid_set_integral(pid_t *pid, q16_t value);

/* Get current output */
q16_t pid_get_output(const pid_t *pid);

/* Fixed-point conversion helpers */
q16_t q16_from_float(float f);
float q16_to_float(q16_t q);

#endif /* PID_CONTROLLER_H */
```

```c
/* pid_controller.c — PID implementation */
#include "pid_controller.h"
#include <string.h>

#define Q16_ONE   ((q16_t)65536)
#define Q16_SHIFT 16

q16_t q16_from_float(float f) {
    return (q16_t)(f * (float)Q16_ONE);
}

float q16_to_float(q16_t q) {
    return (float)q / (float)Q16_ONE;
}

static q16_t q16_mul(q16_t a, q16_t b) {
    int64_t result = (int64_t)a * (int64_t)b;
    return (q16_t)(result >> Q16_SHIFT);
}

static q16_t q16_div(q16_t a, q16_t b) {
    if (b == 0) return 0;
    int64_t result = ((int64_t)a << Q16_SHIFT) / (int64_t)b;
    /* Saturate to Q15.16 range */
    if (result > (int64_t)0x7FFFFFFF) return (q16_t)0x7FFFFFFF;
    if (result < (int64_t)0x80000000) return (q16_t)0x80000000;
    return (q16_t)result;
}

static q16_t q16_sat(q16_t val, q16_t min_val, q16_t max_val) {
    if (val > max_val) return max_val;
    if (val < min_val) return min_val;
    return val;
}

void pid_init(pid_t *pid, const pid_config_t *config) {
    memset(pid, 0, sizeof(pid_t));

    pid->kp = q16_from_float(config->kp);
    pid->ki = q16_from_float(config->ki);
    pid->kd = q16_from_float(config->kd);
    pid->dt = q16_from_float(config->dt);
    pid->output_min = q16_from_float(config->output_min);
    pid->output_max = q16_from_float(config->output_max);
    pid->integral_min = q16_from_float(config->integral_min);
    pid->integral_max = q16_from_float(config->integral_max);
    pid->back_calc_gain = q16_from_float(config->back_calc_gain);
    pid->use_velocity_form = config->use_velocity_form;
    pid->is_initialized = true;
}

void pid_reset(pid_t *pid) {
    pid->integral = 0;
    pid->prev_error = 0;
    pid->prev_prev_error = 0;
    pid->prev_output = 0;
    pid->output = 0;
}

q16_t pid_compute(pid_t *pid, q16_t setpoint, q16_t measured) {
    if (!pid->is_initialized) return 0;

    q16_t error = setpoint - measured;

    /* Proportional term */
    q16_t p_term = q16_mul(pid->kp, error);

    /* Integral term with back-calculation anti-windup */
    q16_t i_term = q16_mul(pid->ki, q16_mul(pid->dt, error));
    q16_t new_integral = pid->integral + i_term;

    /* Compute raw output to check saturation */
    q16_t d_term = q16_mul(pid->kd, q16_div(error - pid->prev_error, pid->dt));
    q16_t raw_output = p_term + new_integral + d_term;

    /* Anti-windup: back-calculation */
    if (raw_output > pid->output_max) {
        q16_t saturation_error = pid->output_max - raw_output;
        q16_t correction = q16_mul(pid->back_calc_gain, saturation_error);
        new_integral += correction;
        pid->output = pid->output_max;
    } else if (raw_output < pid->output_min) {
        q16_t saturation_error = pid->output_min - raw_output;
        q16_t correction = q16_mul(pid->back_calc_gain, saturation_error);
        new_integral += correction;
        pid->output = pid->output_min;
    } else {
        pid->output = raw_output;
    }

    /* Clamp integral to bounds */
    pid->integral = q16_sat(new_integral, pid->integral_min, pid->integral_max);

    pid->prev_error = error;

    return pid->output;
}

q16_t pid_compute_velocity(pid_t *pid, q16_t setpoint, q16_t measured) {
    if (!pid->is_initialized) return 0;

    q16_t error = setpoint - measured;

    /* Velocity form: compute delta */
    q16_t delta_p = q16_mul(pid->kp, error - pid->prev_error);
    q16_t delta_i = q16_mul(pid->ki, q16_mul(pid->dt, error));
    q16_t delta_d = q16_mul(pid->kd,
        q16_div(error - (q16_t)2 * pid->prev_error + pid->prev_prev_error,
                pid->dt));

    q16_t delta_u = delta_p + delta_i + delta_d;
    q16_t new_output = pid->prev_output + delta_u;

    /* Saturate output */
    pid->output = q16_sat(new_output, pid->output_min, pid->output_max);

    pid->prev_prev_error = pid->prev_error;
    pid->prev_error = error;
    pid->prev_output = pid->output;

    return pid->output;
}

void pid_set_integral(pid_t *pid, q16_t value) {
    pid->integral = q16_sat(value, pid->integral_min, pid->integral_max);
}

q16_t pid_get_output(const pid_t *pid) {
    return pid->output;
}
```

### Fault Detection and State Machine

```c
/* fault_detection.h — Fault detection interface */
#ifndef FAULT_DETECTION_H
#define FAULT_DETECTION_H

#include <stdint.h>
#include <stdbool.h>

/* Fault flags */
typedef enum {
    FAULT_NONE          = 0x00,
    FAULT_OVERCURRENT   = 0x01,
    FAULT_OVERTEMP      = 0x02,
    FAULT_UNDERVOLTAGE  = 0x04,
    FAULT_STALL         = 0x08,
    FAULT_OVERSPEED     = 0x10,
    FAULT_OPEN_LOOP     = 0x20,
    FAULT_WATCHDOG      = 0x40,
    FAULT_CRITICAL      = 0x80  /* Any critical fault */
} fault_code_t;

/* Fault thresholds */
typedef struct {
    float overcurrent_amps;
    float overtemp_celsius;
    float undervoltage_volts;
    float overspeed_rpm;
    uint32_t stall_timeout_ms;  /* Time before stall is declared */
    uint32_t open_loop_timeout_ms;
} fault_thresholds_t;

/* Motor state machine */
typedef enum {
    STATE_SAFE_STOP,
    STATE_RECOVERY,
    STATE_RUNNING
} motor_state_t;

/* Fault detection context */
typedef struct {
    fault_thresholds_t thresholds;
    uint32_t fault_flags;
    motor_state_t state;

    /* Stall detection */
    float current_amps;
    float speed_rpm;
    uint32_t stall_timer_ms;
    bool stall_detected;

    /* Temperature tracking */
    float temperature_c;
    uint32_t overtemp_timer_ms;

    /* Recovery ramp */
    float ramp_target;
    float ramp_current;
    float ramp_rate;  /* Units per second */
    uint32_t ramp_start_ms;
} fault_detector_t;

/* Initialize fault detector */
void fault_detector_init(fault_detector_t *det, const fault_thresholds_t *thresholds);

/* Update fault detection with new sensor readings */
void fault_detector_update(fault_detector_t *det,
                           float current_amps,
                           float speed_rpm,
                           float temperature_c,
                           float supply_voltage,
                           uint32_t timestamp_ms);

/* Check if a specific fault is active */
bool fault_detector_has_fault(const fault_detector_t *det, fault_code_t fault);

/* Get all active faults */
uint32_t fault_detector_get_faults(const fault_detector_t *det);

/* Clear faults (only allowed in SAFE-STOP state) */
void fault_detector_clear_faults(fault_detector_t *det);

/* State machine transitions */
motor_state_t motor_state_transition(fault_detector_t *det,
                                     uint32_t command,  /* 1=enable, 0=disable */
                                     uint32_t timestamp_ms);

/* Get target PWM based on current state */
float motor_get_pwm_target(const fault_detector_t *det);

#endif /* FAULT_DETECTION_H */
```

```c
/* fault_detection.c — Fault detection implementation */
#include "fault_detection.h"
#include <string.h>

#define CRITICAL_FAULTS (FAULT_OVERCURRENT | FAULT_WATCHDOG)

void fault_detector_init(fault_detector_t *det,
                         const fault_thresholds_t *thresholds) {
    memset(det, 0, sizeof(fault_detector_t));
    det->thresholds = *thresholds;
    det->fault_flags = FAULT_NONE;
    det->state = STATE_SAFE_STOP;
    det->ramp_rate = 100.0f; /* 100 RPM/s ramp rate */
}

void fault_detector_update(fault_detector_t *det,
                           float current_amps,
                           float speed_rpm,
                           float temperature_c,
                           float supply_voltage,
                           uint32_t timestamp_ms) {
    det->current_amps = current_amps;
    det->speed_rpm = speed_rpm;
    det->temperature_c = temperature_c;

    /* Overcurrent detection (critical — immediate) */
    if (current_amps > det->thresholds.overcurrent_amps) {
        det->fault_flags |= FAULT_OVERCURRENT | FAULT_CRITICAL;
    }

    /* Overtemperature detection */
    if (temperature_c > det->thresholds.overtemp_celsius) {
        det->fault_flags |= FAULT_OVERTEMP;
        det->overtemp_timer_ms += 10; /* Assuming 10ms sample period */
    } else {
        det->overtemp_timer_ms = 0;
        det->fault_flags &= ~FAULT_OVERTEMP;
    }

    /* Undervoltage detection */
    if (supply_voltage < det->thresholds.undervoltage_volts) {
        det->fault_flags |= FAULT_UNDERVOLTAGE;
    } else {
        det->fault_flags &= ~FAULT_UNDERVOLTAGE;
    }

    /* Stall detection: high current + low speed for sustained period */
    if (current_amps > det->thresholds.overcurrent_amps * 0.5f &&
        speed_rpm < 10.0f) {
        det->stall_timer_ms += 10;
        if (det->stall_timer_ms >= det->thresholds.stall_timeout_ms) {
            det->fault_flags |= FAULT_STALL;
            det->stall_detected = true;
        }
    } else {
        det->stall_timer_ms = 0;
        det->stall_detected = false;
        det->fault_flags &= ~FAULT_STALL;
    }

    /* Over-speed detection */
    if (speed_rpm > det->thresholds.overspeed_rpm) {
        det->fault_flags |= FAULT_OVERSPEED;
    } else {
        det->fault_flags &= ~FAULT_OVERSPEED;
    }
}

bool fault_detector_has_fault(const fault_detector_t *det, fault_code_t fault) {
    return (det->fault_flags & (uint32_t)fault) != 0;
}

uint32_t fault_detector_get_faults(const fault_detector_t *det) {
    return det->fault_flags;
}

void fault_detector_clear_faults(fault_detector_t *det) {
    /* Only clear non-critical faults in safe-stop state */
    if (det->state == STATE_SAFE_STOP) {
        det->fault_flags &= FAULT_CRITICAL; /* Keep critical flags */
        det->stall_timer_ms = 0;
        det->overtemp_timer_ms = 0;
    }
}

motor_state_t motor_state_transition(fault_detector_t *det,
                                     uint32_t command,
                                     uint32_t timestamp_ms) {
    switch (det->state) {
    case STATE_SAFE_STOP:
        if (command && !(det->fault_flags & FAULT_CRITICAL)) {
            det->state = STATE_RECOVERY;
            det->ramp_current = 0.0f;
            det->ramp_target = 1000.0f; /* Target RPM */
            det->ramp_start_ms = timestamp_ms;
        }
        break;

    case STATE_RECOVERY: {
        float elapsed_s = (float)(timestamp_ms - det->ramp_start_ms) / 1000.0f;
        det->ramp_current = det->ramp_rate * elapsed_s;
        if (det->ramp_current >= det->ramp_target) {
            det->ramp_current = det->ramp_target;
            det->state = STATE_RUNNING;
        }
        if (det->fault_flags & FAULT_CRITICAL) {
            det->state = STATE_SAFE_STOP;
        }
        break;
    }

    case STATE_RUNNING:
        if (det->fault_flags & FAULT_CRITICAL) {
            det->state = STATE_SAFE_STOP;
        } else if (det->fault_flags & (FAULT_OVERTEMP | FAULT_STALL)) {
            det->state = STATE_RECOVERY;
            det->ramp_current = det->speed_rpm;
            det->ramp_start_ms = timestamp_ms;
        } else if (!command) {
            det->state = STATE_RECOVERY;
            det->ramp_current = det->speed_rpm;
            det->ramp_target = 0.0f;
            det->ramp_start_ms = timestamp_ms;
        }
        break;
    }

    return det->state;
}

float motor_get_pwm_target(const fault_detector_t *det) {
    switch (det->state) {
    case STATE_SAFE_STOP:
        return 0.0f;
    case STATE_RECOVERY:
        return det->ramp_current / det->ramp_target; /* Normalized 0-1 */
    case STATE_RUNNING:
        return 1.0f; /* PID controller sets actual PWM */
    default:
        return 0.0f;
    }
}
```

### Watchdog Timer (IWDG)

```c
/* watchdog.h — IWDG watchdog interface */
#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>

/* IWDG register addresses (STM32F4) */
#define IWDG_BASE       0x40003000UL
#define IWDG_KR         (*(volatile uint32_t *)(IWDG_BASE + 0x00))
#define IWDG_PR         (*(volatile uint32_t *)(IWDG_BASE + 0x04))
#define IWDG_RLR        (*(volatile uint32_t *)(IWDG_BASE + 0x08))
#define IWDG_SR         (*(volatile uint32_t *)(IWDG_BASE + 0x0C))

/* Key values */
#define IWDG_KEY_RELOAD 0xAAAA
#define IWDG_KEY_ENABLE 0xCCCC
#define IWDG_KEY_WRITE  0x5555

/* Watchdog context */
typedef struct {
    uint32_t timeout_ms;
    uint32_t last_feed_ms;
    bool is_enabled;
    bool feed_allowed;  /* Only feed when system is healthy */
} watchdog_t;

/* Initialize IWDG with specified timeout */
void watchdog_init(watchdog_t *wdg, uint32_t timeout_ms);

/* Feed the watchdog (only if feed_allowed) */
void watchdog_feed(watchdog_t *wdg, uint32_t current_ms);

/* Enable feeding (call when system is healthy) */
void watchdog_enable_feeding(watchdog_t *wdg);

/* Disable feeding (call when entering fault state) */
void watchdog_disable_feeding(watchdog_t *wdg);

/* Check if watchdog is about to expire */
bool watchdog_is_critical(const watchdog_t *wdg, uint32_t current_ms);

#endif /* WATCHDOG_H */
```

```c
/* watchdog.c — IWDG implementation */
#include "watchdog.h"

void watchdog_init(watchdog_t *wdg, uint32_t timeout_ms) {
    wdg->timeout_ms = timeout_ms;
    wdg->last_feed_ms = 0;
    wdg->is_enabled = false;
    wdg->feed_allowed = false;

    /* Unlock IWDG registers */
    IWDG_KR = IWDG_KEY_WRITE;

    /* Calculate prescaler and reload value for desired timeout
     * Timeout = (RLR + 1) * (2^PR) / 32000
     * For 1000ms timeout: PR=6 (256), RLR=124
     * Timeout = 125 * 256 / 32000 = 1000ms */
    uint32_t pr = 6;  /* /256 */
    uint32_t rlr = (timeout_ms * 32) / 256 - 1;
    if (rlr > 0xFFF) rlr = 0xFFF;

    IWDG_PR = pr;
    IWDG_RLR = rlr;

    /* Wait for registers to update */
    while (IWDG_SR & 0x02) { }

    /* Enable IWDG — cannot be disabled except by reset */
    IWDG_KR = IWDG_KEY_ENABLE;
    wdg->is_enabled = true;
}

void watchdog_feed(watchdog_t *wdg, uint32_t current_ms) {
    if (!wdg->feed_allowed) return;

    IWDG_KR = IWDG_KEY_RELOAD;
    wdg->last_feed_ms = current_ms;
}

void watchdog_enable_feeding(watchdog_t *wdg) {
    wdg->feed_allowed = true;
}

void watchdog_disable_feeding(watchdog_t *wdg) {
    wdg->feed_allowed = false;
}

bool watchdog_is_critical(const watchdog_t *wdg, uint32_t current_ms) {
    if (!wdg->is_enabled) return false;
    uint32_t elapsed = current_ms - wdg->last_feed_ms;
    return elapsed > (wdg->timeout_ms / 2); /* Warning at 50% timeout */
}
```

### Main Application

```c
/* main.c — Motor control application */
#include "pid_controller.h"
#include "fault_detection.h"
#include "watchdog.h"
#include <stdint.h>
#include <stdbool.h>

/* External hardware functions */
extern void pwm_init(void);
extern void pwm_set_duty(float duty);
extern float adc_read_current(void);
extern float adc_read_voltage(void);
extern float adc_read_temperature(void);
extern float encoder_read_speed(void);
extern uint32_t get_timestamp_ms(void);
extern void uart_send_string(const char *s);
extern void uart_send_float(float f);

/* Global instances */
static pid_t pid;
static fault_detector_t fault_det;
static watchdog_t wdg;

/* PID tuning parameters for motor */
static const pid_config_t pid_config = {
    .kp = 2.5f,
    .ki = 0.8f,
    .kd = 0.05f,
    .dt = 0.01f,            /* 100Hz control loop */
    .output_min = 0.0f,
    .output_max = 100.0f,   /* PWM percentage */
    .integral_min = -50.0f,
    .integral_max = 50.0f,
    .use_velocity_form = false,
    .back_calc_gain = 0.5f,
};

/* Fault thresholds */
static const fault_thresholds_t fault_thresholds = {
    .overcurrent_amps = 10.0f,
    .overtemp_celsius = 85.0f,
    .undervoltage_volts = 9.6f,  /* 80% of 12V */
    .overspeed_rpm = 6000.0f,
    .stall_timeout_ms = 500,
    .open_loop_timeout_ms = 100,
};

/* Control loop state */
static float setpoint_rpm = 0.0f;
static uint32_t last_control_ms = 0;

int main(void) {
    /* Initialize hardware */
    pwm_init();

    /* Initialize PID */
    pid_init(&pid, &pid_config);

    /* Initialize fault detection */
    fault_detector_init(&fault_det, &fault_thresholds);

    /* Initialize watchdog (1 second timeout) */
    watchdog_init(&wdg, 1000);

    uart_send_string("Motor Control System Started\r\n");

    while (1) {
        uint32_t now = get_timestamp_ms();

        /* 100Hz control loop */
        if (now - last_control_ms >= 10) {
            last_control_ms = now;

            /* Read sensors */
            float current = adc_read_current();
            float voltage = adc_read_voltage();
            float temp = adc_read_temperature();
            float speed = encoder_read_speed();

            /* Update fault detection */
            fault_detector_update(&fault_det, current, speed, temp, voltage, now);

            /* State machine transition */
            motor_state_transition(&fault_det, 1, now);

            /* Update PWM based on state */
            float pwm_duty;
            if (fault_det.state == STATE_RUNNING) {
                /* PID control */
                q16_t sp = q16_from_float(setpoint_rpm);
                q16_t meas = q16_from_float(speed);
                q16_t output = pid_compute(&pid, sp, meas);
                pwm_duty = q16_to_float(output);
            } else {
                pwm_duty = motor_get_pwm_target(&fault_det);
                pid_reset(&pid);
            }

            /* Apply PWM with fault override */
            if (fault_det.fault_flags & FAULT_CRITICAL) {
                pwm_duty = 0.0f;
            }
            pwm_set_duty(pwm_duty);

            /* Feed watchdog only in healthy states */
            if (fault_det.state == STATE_RUNNING &&
                !(fault_det.fault_flags & FAULT_CRITICAL)) {
                watchdog_enable_feeding(&wdg);
            } else {
                watchdog_disable_feeding(&wdg);
            }
            watchdog_feed(&wdg, now);

            /* Debug output */
            uart_send_string("State: ");
            switch (fault_det.state) {
            case STATE_SAFE_STOP: uart_send_string("STOP"); break;
            case STATE_RECOVERY: uart_send_string("RECOVERY"); break;
            case STATE_RUNNING: uart_send_string("RUNNING"); break;
            }
            uart_send_string(" | PWM: ");
            uart_send_float(pwm_duty);
            uart_send_string(" | Speed: ");
            uart_send_float(speed);
            uart_send_string(" | Current: ");
            uart_send_float(current);
            uart_send_string("\r\n");
        }
    }

    return 0;
}
```

### Build Instructions (C)

```bash
# Compile
arm-none-eabi-gcc \
    -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
    -O2 -Wall -Wextra -Wpedantic -Wconversion \
    -fno-common -ffunction-sections -fdata-sections \
    -nostdlib \
    -T stm32f407vg.ld \
    main.c pid_controller.c fault_detection.c watchdog.c \
    startup_stm32f407xx.s \
    -o motor_control.elf

# Generate binary
arm-none-eabi-objcopy -O binary motor_control.elf motor_control.bin

# Size analysis
arm-none-eabi-size motor_control.elf
```

### Run in QEMU

```bash
qemu-system-arm \
    -M stm32f4-discovery \
    -kernel motor_control.bin \
    -serial stdio \
    -d unimp,guest_errors \
    -D qemu_motor.log
```

## Implementation: Rust

### Type-Safe PID with Compile-Time Validation

```toml
# Cargo.toml
[package]
name = "motor-control-pid"
version = "0.1.0"
edition = "2021"

[dependencies]
cortex-m = { version = "0.7", features = ["critical-section-single-core"] }
cortex-m-rt = "0.7"
panic-halt = "0.2"
stm32f4xx-hal = { version = "0.21", features = ["stm32f407"] }
embedded-hal = "1.0"
heapless = "0.8"
num-traits = { version = "0.2", default-features = false }

[profile.release]
opt-level = "s"
lto = true
debug = true
```

```rust
// src/pid.rs — Type-safe PID controller
use core::marker::PhantomData;

/// PID form type marker
pub trait PidForm {}
pub struct PositionForm;
pub struct VelocityForm;
impl PidForm for PositionForm {}
impl PidForm for VelocityForm {}

/// PID tuning parameters with compile-time validation
#[derive(Debug, Clone, Copy)]
pub struct PidTuning {
    pub kp: f32,
    pub ki: f32,
    pub kd: f32,
    pub dt: f32,
    pub output_min: f32,
    pub output_max: f32,
    pub integral_min: f32,
    pub integral_max: f32,
    pub back_calc_gain: f32,
}

impl PidTuning {
    /// Validate tuning parameters at runtime
    pub const fn validate(&self) -> Result<(), PidError> {
        if self.kp < 0.0 {
            return Err(PidError::NegativeKp);
        }
        if self.ki < 0.0 {
            return Err(PidError::NegativeKi);
        }
        if self.kd < 0.0 {
            return Err(PidError::NegativeKd);
        }
        if self.dt <= 0.0 {
            return Err(PidError::InvalidDt);
        }
        if self.output_min >= self.output_max {
            return Err(PidError::InvalidOutputRange);
        }
        if self.integral_min >= self.integral_max {
            return Err(PidError::InvalidIntegralRange);
        }
        Ok(())
    }

    /// Create a validated PID tuning (panics on invalid config)
    pub const fn new_checked(
        kp: f32, ki: f32, kd: f32, dt: f32,
        output_min: f32, output_max: f32,
        integral_min: f32, integral_max: f32,
        back_calc_gain: f32,
    ) -> Self {
        let tuning = Self {
            kp, ki, kd, dt,
            output_min, output_max,
            integral_min, integral_max,
            back_calc_gain,
        };
        match tuning.validate() {
            Ok(()) => tuning,
            Err(_) => panic!("Invalid PID tuning parameters"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PidError {
    NegativeKp,
    NegativeKi,
    NegativeKd,
    InvalidDt,
    InvalidOutputRange,
    InvalidIntegralRange,
    NotInitialized,
}

/// PID controller state
pub struct PidController<F: PidForm> {
    tuning: PidTuning,
    integral: f32,
    prev_error: f32,
    prev_prev_error: f32,
    prev_output: f32,
    output: f32,
    is_initialized: bool,
    _form: PhantomData<F>,
}

impl PidController<PositionForm> {
    pub fn new(tuning: PidTuning) -> Result<Self, PidError> {
        tuning.validate()?;
        Ok(Self {
            tuning,
            integral: 0.0,
            prev_error: 0.0,
            prev_prev_error: 0.0,
            prev_output: 0.0,
            output: 0.0,
            is_initialized: true,
            _form: PhantomData,
        })
    }

    pub fn compute(&mut self, setpoint: f32, measured: f32) -> f32 {
        if !self.is_initialized {
            return 0.0;
        }

        let error = setpoint - measured;
        let p_term = self.tuning.kp * error;
        let i_term = self.tuning.ki * self.tuning.dt * error;
        let d_term = self.tuning.kd * (error - self.prev_error) / self.tuning.dt;

        let mut new_integral = self.integral + i_term;
        let raw_output = p_term + new_integral + d_term;

        // Anti-windup: back-calculation
        if raw_output > self.tuning.output_max {
            let sat_error = self.tuning.output_max - raw_output;
            new_integral += self.tuning.back_calc_gain * sat_error;
            self.output = self.tuning.output_max;
        } else if raw_output < self.tuning.output_min {
            let sat_error = self.tuning.output_min - raw_output;
            new_integral += self.tuning.back_calc_gain * sat_error;
            self.output = self.tuning.output_min;
        } else {
            self.output = raw_output;
        }

        // Clamp integral
        self.integral = new_integral
            .max(self.tuning.integral_min)
            .min(self.tuning.integral_max);

        self.prev_error = error;
        self.output
    }

    /// Convert to velocity form
    pub fn to_velocity_form(self) -> PidController<VelocityForm> {
        PidController {
            tuning: self.tuning,
            integral: 0.0,
            prev_error: self.prev_error,
            prev_prev_error: self.prev_error,
            prev_output: self.output,
            output: self.output,
            is_initialized: self.is_initialized,
            _form: PhantomData,
        }
    }
}

impl PidController<VelocityForm> {
    pub fn compute(&mut self, setpoint: f32, measured: f32) -> f32 {
        if !self.is_initialized {
            return 0.0;
        }

        let error = setpoint - measured;
        let delta_p = self.tuning.kp * (error - self.prev_error);
        let delta_i = self.tuning.ki * self.tuning.dt * error;
        let delta_d = self.tuning.kd * (error - 2.0 * self.prev_error + self.prev_prev_error)
            / self.tuning.dt;

        let delta_u = delta_p + delta_i + delta_d;
        let new_output = (self.prev_output + delta_u)
            .max(self.tuning.output_min)
            .min(self.tuning.output_max);

        self.output = new_output;
        self.prev_prev_error = self.prev_error;
        self.prev_error = error;
        self.prev_output = new_output;
        self.output
    }
}

impl<F: PidForm> PidController<F> {
    pub fn reset(&mut self) {
        self.integral = 0.0;
        self.prev_error = 0.0;
        self.prev_prev_error = 0.0;
        self.prev_output = 0.0;
        self.output = 0.0;
    }

    pub fn set_integral(&mut self, value: f32) {
        self.integral = value
            .max(self.tuning.integral_min)
            .min(self.tuning.integral_max);
    }

    pub fn output(&self) -> f32 {
        self.output
    }
}
```

```rust
// src/fault_detection.rs — Result-based fault monitoring
use core::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Fault {
    Overcurrent,
    Overtemperature,
    Undervoltage,
    Stall,
    OverSpeed,
    OpenLoop,
    Watchdog,
}

impl fmt::Display for Fault {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Fault::Overcurrent => write!(f, "OVERCURRENT"),
            Fault::Overtemperature => write!(f, "OVERTEMP"),
            Fault::Undervoltage => write!(f, "UNDERVOLTAGE"),
            Fault::Stall => write!(f, "STALL"),
            Fault::OverSpeed => write!(f, "OVERSPEED"),
            Fault::OpenLoop => write!(f, "OPEN_LOOP"),
            Fault::Watchdog => write!(f, "WATCHDOG"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MotorState {
    SafeStop,
    Recovery,
    Running,
}

#[derive(Debug, Clone, Copy)]
pub struct FaultThresholds {
    pub overcurrent_amps: f32,
    pub overtemp_celsius: f32,
    pub undervoltage_volts: f32,
    pub overspeed_rpm: f32,
    pub stall_timeout_ms: u32,
    pub open_loop_timeout_ms: u32,
}

pub struct FaultDetector {
    thresholds: FaultThresholds,
    active_faults: heapless::Vec<Fault, 8>,
    state: MotorState,
    current_amps: f32,
    speed_rpm: f32,
    temperature_c: f32,
    stall_timer_ms: u32,
    ramp_current: f32,
    ramp_target: f32,
    ramp_rate: f32,
    ramp_start_ms: u32,
}

impl FaultDetector {
    pub fn new(thresholds: FaultThresholds) -> Self {
        Self {
            thresholds,
            active_faults: heapless::Vec::new(),
            state: MotorState::SafeStop,
            current_amps: 0.0,
            speed_rpm: 0.0,
            temperature_c: 0.0,
            stall_timer_ms: 0,
            ramp_current: 0.0,
            ramp_target: 1000.0,
            ramp_rate: 100.0,
            ramp_start_ms: 0,
        }
    }

    pub fn update(
        &mut self,
        current_amps: f32,
        speed_rpm: f32,
        temperature_c: f32,
        supply_voltage: f32,
        timestamp_ms: u32,
    ) {
        self.current_amps = current_amps;
        self.speed_rpm = speed_rpm;
        self.temperature_c = temperature_c;
        self.active_faults.clear();

        // Check each fault condition
        if current_amps > self.thresholds.overcurrent_amps {
            self.active_faults.push(Fault::Overcurrent).ok();
        }
        if temperature_c > self.thresholds.overtemp_celsius {
            self.active_faults.push(Fault::Overtemperature).ok();
        }
        if supply_voltage < self.thresholds.undervoltage_volts {
            self.active_faults.push(Fault::Undervoltage).ok();
        }

        // Stall detection
        if current_amps > self.thresholds.overcurrent_amps * 0.5
            && speed_rpm < 10.0
        {
            self.stall_timer_ms += 10;
            if self.stall_timer_ms >= self.thresholds.stall_timeout_ms {
                self.active_faults.push(Fault::Stall).ok();
            }
        } else {
            self.stall_timer_ms = 0;
        }

        if speed_rpm > self.thresholds.overspeed_rpm {
            self.active_faults.push(Fault::OverSpeed).ok();
        }
    }

    pub fn has_critical_fault(&self) -> bool {
        self.active_faults.contains(&Fault::Overcurrent)
            || self.active_faults.contains(&Fault::Watchdog)
    }

    pub fn has_fault(&self, fault: Fault) -> bool {
        self.active_faults.contains(&fault)
    }

    pub fn active_faults(&self) -> &[Fault] {
        &self.active_faults
    }

    pub fn state(&self) -> MotorState {
        self.state
    }

    pub fn transition(&mut self, command: bool, timestamp_ms: u32) {
        match self.state {
            MotorState::SafeStop => {
                if command && !self.has_critical_fault() {
                    self.state = MotorState::Recovery;
                    self.ramp_current = 0.0;
                    self.ramp_start_ms = timestamp_ms;
                }
            }
            MotorState::Recovery => {
                let elapsed_s = (timestamp_ms - self.ramp_start_ms) as f32 / 1000.0;
                self.ramp_current = (self.ramp_rate * elapsed_s).min(self.ramp_target);
                if self.ramp_current >= self.ramp_target {
                    self.state = MotorState::Running;
                }
                if self.has_critical_fault() {
                    self.state = MotorState::SafeStop;
                }
            }
            MotorState::Running => {
                if self.has_critical_fault() {
                    self.state = MotorState::SafeStop;
                } else if self.has_fault(Fault::Overtemperature)
                    || self.has_fault(Fault::Stall)
                {
                    self.state = MotorState::Recovery;
                    self.ramp_current = self.speed_rpm;
                    self.ramp_start_ms = timestamp_ms;
                } else if !command {
                    self.state = MotorState::Recovery;
                    self.ramp_current = self.speed_rpm;
                    self.ramp_target = 0.0;
                    self.ramp_start_ms = timestamp_ms;
                }
            }
        }
    }

    pub fn pwm_target(&self) -> f32 {
        match self.state {
            MotorState::SafeStop => 0.0,
            MotorState::Recovery => self.ramp_current / self.ramp_target,
            MotorState::Running => 1.0,
        }
    }

    pub fn clear_faults(&mut self) {
        if self.state == MotorState::SafeStop {
            self.active_faults.retain(|f| *f == Fault::Watchdog);
        }
    }
}
```

```rust
// src/main.rs — Motor control application
#![no_std]
#![no_main]

mod pid;
mod fault_detection;

use cortex_m_rt::{entry, exception, ExceptionFrame};
use panic_halt as _;
use pid::{PidController, PidTuning, PositionForm};
use fault_detection::{FaultDetector, FaultThresholds, MotorState};

#[entry]
fn main() -> ! {
    let dp = stm32f4xx_hal::pac::Peripherals::take().unwrap();

    // Clock configuration
    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr
        .use_hse(8.MHz())
        .sysclk(168.MHz())
        .pclk1(42.MHz())
        .freeze();

    // Initialize PID controller
    let tuning = PidTuning::new_checked(
        2.5, 0.8, 0.05, 0.01,
        0.0, 100.0,
        -50.0, 50.0,
        0.5,
    );
    let mut pid = PidController::<PositionForm>::new(tuning).unwrap();

    // Initialize fault detection
    let thresholds = FaultThresholds {
        overcurrent_amps: 10.0,
        overtemp_celsius: 85.0,
        undervoltage_volts: 9.6,
        overspeed_rpm: 6000.0,
        stall_timeout_ms: 500,
        open_loop_timeout_ms: 100,
    };
    let mut fault_det = FaultDetector::new(thresholds);

    // Initialize PWM, ADC, encoder (omitted for brevity)
    // let pwm = ...;
    // let adc = ...;

    let mut setpoint_rpm: f32 = 1000.0;
    let mut last_control_ms: u32 = 0;

    loop {
        let now = 0u32; // Replace with actual timer

        if now.wrapping_sub(last_control_ms) >= 10 {
            last_control_ms = now;

            // Read sensors (replace with actual hardware reads)
            let current = 0.0f32;
            let voltage = 12.0f32;
            let temp = 25.0f32;
            let speed = 0.0f32;

            // Update fault detection
            fault_det.update(current, speed, temp, voltage, now);

            // State machine
            fault_det.transition(true, now);

            // Compute control output
            let pwm_duty = match fault_det.state() {
                MotorState::Running => {
                    pid.compute(setpoint_rpm, speed)
                }
                _ => {
                    pid.reset();
                    fault_det.pwm_target()
                }
            };

            // Apply PWM (critical fault override)
            let final_pwm = if fault_det.has_critical_fault() {
                0.0
            } else {
                pwm_duty
            };

            // pwm.set_duty(final_pwm);
        }
    }
}

#[exception]
fn HardFault(ef: &ExceptionFrame) -> ! {
    loop {}
}
```

### Build Instructions (Rust)

```bash
rustup target add thumbv7em-none-eabihf
cargo build --target thumbv7em-none-eabihf --release
cargo objcopy --target thumbv7em-none-eabihf --release -- -O binary motor_control.bin
cargo size --target thumbv7em-none-eabihf --release -- -A
```

## Implementation: Ada

### SPARK-Verified PID with Provable Bounds

```ada
-- pid_controller.ads — SPARK-compatible PID specification
with Interfaces; use Interfaces;

package PID_Controller with
  SPARK_Mode => On
is

   -- Fixed-point type: Q15.16
   type Q16 is range -2_147_483_648 .. 2_147_483_647
     with Size => 32;

   -- PID configuration
   type PID_Config is record
      Kp              : Float;
      Ki              : Float;
      Kd              : Float;
      DT              : Float;
      Output_Min      : Float;
      Output_Max      : Float;
      Integral_Min    : Float;
      Integral_Max    : Float;
      Back_Calc_Gain  : Float;
   end record
     with Default_Value => (0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

   -- PID controller state
   type PID_Controller_Type is private
     with Default_Initial_Condition =>
       PID_Controller_Type'Valid (PID_Controller_Type);

   -- Initialize controller with validated parameters
   procedure PID_Init (
      PID    : out PID_Controller_Type;
      Config : in     PID_Config)
     with
       Pre  => Config.Kp >= 0.0 and then
               Config.Ki >= 0.0 and then
               Config.Kd >= 0.0 and then
               Config.DT > 0.0 and then
               Config.Output_Min < Config.Output_Max and then
               Config.Integral_Min < Config.Integral_Max,
       Post => PID_Initialized (PID);

   -- Reset PID state
   procedure PID_Reset (PID : in out PID_Controller_Type)
     with
       Pre  => PID_Initialized (PID),
       Post => PID_Output (PID) = 0.0;

   -- Compute PID output
   function PID_Compute (
      PID       : in out PID_Controller_Type;
      Setpoint  : in     Float;
      Measured  : in     Float) return Float
     with
       Pre  => PID_Initialized (PID),
       Post =>
         PID_Output (PID)'Result >= PID_Output_Min (PID) and then
         PID_Output (PID)'Result <= PID_Output_Max (PID);

   -- Predicates
   function PID_Initialized (PID : PID_Controller_Type) return Boolean
     with Ghost;
   function PID_Output (PID : PID_Controller_Type) return Float
     with Ghost;
   function PID_Output_Min (PID : PID_Controller_Type) return Float
     with Ghost;
   function PID_Output_Max (PID : PID_Controller_Type) return Float
     with Ghost;

private

   type PID_Controller_Type is record
      Kp              : Float := 0.0;
      Ki              : Float := 0.0;
      Kd              : Float := 0.0;
      DT              : Float := 0.0;
      Output_Min      : Float := 0.0;
      Output_Max      : Float := 0.0;
      Integral_Min    : Float := 0.0;
      Integral_Max    : Float := 0.0;
      Back_Calc_Gain  : Float := 0.0;
      Integral        : Float := 0.0;
      Prev_Error      : Float := 0.0;
      Output          : Float := 0.0;
      Initialized     : Boolean := False;
   end record;

   function PID_Initialized (PID : PID_Controller_Type) return Boolean is
     (PID.Initialized)
       with Ghost;

   function PID_Output (PID : PID_Controller_Type) return Float is
     (PID.Output)
       with Ghost;

   function PID_Output_Min (PID : PID_Controller_Type) return Float is
     (PID.Output_Min)
       with Ghost;

   function PID_Output_Max (PID : PID_Controller_Type) return Float is
     (PID.Output_Max)
       with Ghost;

end PID_Controller;
```

```ada
-- pid_controller.adb — PID implementation
package body PID_Controller with
  SPARK_Mode => On
is

   -- Saturate value to range
   function Saturate (Value, Min_Val, Max_Val : Float) return Float is
     Result : Float;
   begin
      if Value > Max_Val then
         Result := Max_Val;
      elsif Value < Min_Val then
         Result := Min_Val;
      else
         Result := Value;
      end if;
      return Result;
   end Saturate;

   procedure PID_Init (
      PID    : out PID_Controller_Type;
      Config : in     PID_Config)
   is
   begin
      PID.Kp             := Config.Kp;
      PID.Ki             := Config.Ki;
      PID.Kd             := Config.Kd;
      PID.DT             := Config.DT;
      PID.Output_Min     := Config.Output_Min;
      PID.Output_Max     := Config.Output_Max;
      PID.Integral_Min   := Config.Integral_Min;
      PID.Integral_Max   := Config.Integral_Max;
      PID.Back_Calc_Gain := Config.Back_Calc_Gain;
      PID.Integral       := 0.0;
      PID.Prev_Error     := 0.0;
      PID.Output         := 0.0;
      PID.Initialized    := True;
   end PID_Init;

   procedure PID_Reset (PID : in out PID_Controller_Type) is
   begin
      PID.Integral   := 0.0;
      PID.Prev_Error := 0.0;
      PID.Output     := 0.0;
   end PID_Reset;

   function PID_Compute (
      PID       : in out PID_Controller_Type;
      Setpoint  : in     Float;
      Measured  : in     Float) return Float
   is
      Error       : constant Float := Setpoint - Measured;
      P_Term      : constant Float := PID.Kp * Error;
      I_Term      : constant Float := PID.Ki * PID.DT * Error;
      D_Term      : constant Float := PID.Kd * (Error - PID.Prev_Error) / PID.DT;
      New_Integral: Float;
      Raw_Output  : Float;
   begin
      New_Integral := PID.Integral + I_Term;
      Raw_Output   := P_Term + New_Integral + D_Term;

      -- Anti-windup: back-calculation
      if Raw_Output > PID.Output_Max then
         New_Integral := New_Integral +
           PID.Back_Calc_Gain * (PID.Output_Max - Raw_Output);
         PID.Output := PID.Output_Max;
      elsif Raw_Output < PID.Output_Min then
         New_Integral := New_Integral +
           PID.Back_Calc_Gain * (PID.Output_Min - Raw_Output);
         PID.Output := PID.Output_Min;
      else
         PID.Output := Raw_Output;
      end if;

      -- Clamp integral
      PID.Integral := Saturate (New_Integral, PID.Integral_Min, PID.Integral_Max);
      PID.Prev_Error := Error;

      return PID.Output;
   end PID_Compute;

end PID_Controller;
```

```ada
-- fault_detection.ads — Fault detection with exception handling
with PID_Controller; use PID_Controller;

package Fault_Detection with
  SPARK_Mode => On
is

   type Fault_Code is
     (No_Fault, Overcurrent, Overtemperature, Undervoltage,
      Stall, OverSpeed, OpenLoop, Watchdog);

   type Fault_Set is array (Fault_Code) of Boolean
     with Default_Component_Value => False;

   type Motor_State is (Safe_Stop, Recovery, Running);

   type Fault_Thresholds is record
      Overcurrent_Amps      : Float;
      Overtemp_Celsius      : Float;
      Undervoltage_Volts    : Float;
      Overspeed_RPM         : Float;
      Stall_Timeout_MS      : Natural;
      Open_Loop_Timeout_MS  : Natural;
   end record;

   type Fault_Detector_Type is private;

   procedure Fault_Detector_Init (
      Det        : out Fault_Detector_Type;
      Thresholds : in     Fault_Thresholds);

   procedure Fault_Detector_Update (
      Det             : in out Fault_Detector_Type;
      Current_Amps    : in     Float;
      Speed_RPM       : in     Float;
      Temperature_C   : in     Float;
      Supply_Voltage  : in     Float;
      Timestamp_MS    : in     Natural);

   function Has_Fault (
      Det  : Fault_Detector_Type;
      Fault: Fault_Code) return Boolean;

   function Has_Critical_Fault (Det : Fault_Detector_Type) return Boolean;

   function Get_State (Det : Fault_Detector_Type) return Motor_State;

   procedure State_Transition (
      Det          : in out Fault_Detector_Type;
      Command      : in     Boolean;
      Timestamp_MS : in     Natural);

   function Get_PWM_Target (Det : Fault_Detector_Type) return Float;

   procedure Clear_Faults (Det : in out Fault_Detector_Type)
     with
       Pre => Get_State (Det) = Safe_Stop;

private

   type Fault_Detector_Type is record
      Thresholds    : Fault_Thresholds;
      Active_Faults : Fault_Set;
      State         : Motor_State := Safe_Stop;
      Current_Amps  : Float := 0.0;
      Speed_RPM     : Float := 0.0;
      Stall_Timer   : Natural := 0;
      Ramp_Current  : Float := 0.0;
      Ramp_Target   : Float := 1000.0;
      Ramp_Rate     : Float := 100.0;
      Ramp_Start_MS : Natural := 0;
   end record;

end Fault_Detection;
```

```ada
-- main.adb — Motor control application
with PID_Controller; use PID_Controller;
with Fault_Detection; use Fault_Detection;

procedure Main is
   PID_Config : constant PID_Config := (
      Kp             => 2.5,
      Ki             => 0.8,
      Kd             => 0.05,
      DT             => 0.01,
      Output_Min     => 0.0,
      Output_Max     => 100.0,
      Integral_Min   => -50.0,
      Integral_Max   => 50.0,
      Back_Calc_Gain => 0.5
   );

   Thresholds : constant Fault_Thresholds := (
      Overcurrent_Amps     => 10.0,
      Overtemp_Celsius     => 85.0,
      Undervoltage_Volts   => 9.6,
      Overspeed_RPM        => 6000.0,
      Stall_Timeout_MS     => 500,
      Open_Loop_Timeout_MS => 100
   );

   PID      : PID_Controller_Type;
   Detector : Fault_Detector_Type;
   Setpoint : Float := 1000.0;

   -- Hardware interface stubs
   function Read_Current return Float is (0.0);
   function Read_Voltage return Float is (12.0);
   function Read_Temperature return Float is (25.0);
   function Read_Speed return Float is (0.0);
   function Get_Timestamp_MS return Natural is (0);
   procedure Set_PWM_Duty (Duty : Float) is null;

begin
   PID_Init (PID, PID_Config);
   Fault_Detector_Init (Detector, Thresholds);

   loop
      declare
         Now      : constant Natural := Get_Timestamp_MS;
         Current  : constant Float := Read_Current;
         Voltage  : constant Float := Read_Voltage;
         Temp     : constant Float := Read_Temperature;
         Speed    : constant Float := Read_Speed;
         PWM_Duty : Float;
      begin
         Fault_Detector_Update (Detector, Current, Speed, Temp, Voltage, Now);
         State_Transition (Detector, True, Now);

         if Get_State (Detector) = Running then
            PWM_Duty := PID_Compute (PID, Setpoint, Speed);
         else
            PID_Reset (PID);
            PWM_Duty := Get_PWM_Target (Detector);
         end if;

         if Has_Critical_Fault (Detector) then
            PWM_Duty := 0.0;
         end if;

         Set_PWM_Duty (PWM_Duty);
      end;
   end loop;
end Main;
```

### Build Instructions (Ada)

```bash
# Build with GNAT
gnatmake -P motor_control.gpr

# Run GNATprove for SPARK verification
gnatprove -P motor_control.gpr --level=4 --report=all

# Generate binary
arm-none-eabi-objcopy -O binary main motor_control.bin
```

## Implementation: Zig

### Comptime PID Parameter Optimization

```zig
// src/pid.zig — PID controller with comptime checks
const std = @import("std");

pub const PidError = error{
    NegativeKp,
    NegativeKi,
    NegativeKd,
    InvalidDt,
    InvalidOutputRange,
    InvalidIntegralRange,
};

pub const PidTuning = struct {
    kp: f32,
    ki: f32,
    kd: f32,
    dt: f32,
    output_min: f32,
    output_max: f32,
    integral_min: f32,
    integral_max: f32,
    back_calc_gain: f32,

    pub fn validate(self: @This()) PidError!void {
        if (self.kp < 0) return PidError.NegativeKp;
        if (self.ki < 0) return PidError.NegativeKi;
        if (self.kd < 0) return PidError.NegativeKd;
        if (self.dt <= 0) return PidError.InvalidDt;
        if (self.output_min >= self.output_max) return PidError.InvalidOutputRange;
        if (self.integral_min >= self.integral_max) return PidError.InvalidIntegralRange;
    }
};

/// Comptime-validated PID tuning
pub fn comptimePidTuning(
    comptime kp: f32,
    comptime ki: f32,
    comptime kd: f32,
    comptime dt: f32,
    comptime output_min: f32,
    comptime output_max: f32,
    comptime integral_min: f32,
    comptime integral_max: f32,
    comptime back_calc_gain: f32,
) PidTuning {
    const tuning = PidTuning{
        .kp = kp,
        .ki = ki,
        .kd = kd,
        .dt = dt,
        .output_min = output_min,
        .output_max = output_max,
        .integral_min = integral_min,
        .integral_max = integral_max,
        .back_calc_gain = back_calc_gain,
    };
    // This runs at comptime — invalid config is a compile error
    tuning.validate() catch @compileError("Invalid PID tuning parameters");
    return tuning;
}

pub const PidController = struct {
    tuning: PidTuning,
    integral: f32,
    prev_error: f32,
    prev_prev_error: f32,
    prev_output: f32,
    output: f32,
    is_initialized: bool,

    pub fn init(tuning: PidTuning) PidError!PidController {
        try tuning.validate();
        return .{
            .tuning = tuning,
            .integral = 0,
            .prev_error = 0,
            .prev_prev_error = 0,
            .prev_output = 0,
            .output = 0,
            .is_initialized = true,
        };
    }

    pub fn reset(self: *PidController) void {
        self.integral = 0;
        self.prev_error = 0;
        self.prev_prev_error = 0;
        self.prev_output = 0;
        self.output = 0;
    }

    pub fn compute(self: *PidController, setpoint: f32, measured: f32) f32 {
        if (!self.is_initialized) return 0;

        const error = setpoint - measured;
        const p_term = self.tuning.kp * error;
        const i_term = self.tuning.ki * self.tuning.dt * error;
        const d_term = self.tuning.kd * (error - self.prev_error) / self.tuning.dt;

        var new_integral = self.integral + i_term;
        const raw_output = p_term + new_integral + d_term;

        // Anti-windup: back-calculation
        if (raw_output > self.tuning.output_max) {
            const sat_error = self.tuning.output_max - raw_output;
            new_integral += self.tuning.back_calc_gain * sat_error;
            self.output = self.tuning.output_max;
        } else if (raw_output < self.tuning.output_min) {
            const sat_error = self.tuning.output_min - raw_output;
            new_integral += self.tuning.back_calc_gain * sat_error;
            self.output = self.tuning.output_min;
        } else {
            self.output = raw_output;
        }

        // Clamp integral
        self.integral = @min(
            @max(new_integral, self.tuning.integral_min),
            self.tuning.integral_max,
        );

        self.prev_error = error;
        return self.output;
    }

    pub fn setIntegral(self: *PidController, value: f32) void {
        self.integral = @min(
            @max(value, self.tuning.integral_min),
            self.tuning.integral_max,
        );
    }
};
```

```zig
// src/fault_detection.zig — Error union-based fault detection
const std = @import("std");

pub const Fault = enum(u8) {
    none = 0x00,
    overcurrent = 0x01,
    overtemperature = 0x02,
    undervoltage = 0x04,
    stall = 0x08,
    overspeed = 0x10,
    open_loop = 0x20,
    watchdog = 0x40,
    critical = 0x80,
};

pub const FaultSet = packed struct(u8) {
    overcurrent: bool = false,
    overtemperature: bool = false,
    undervoltage: bool = false,
    stall: bool = false,
    overspeed: bool = false,
    open_loop: bool = false,
    watchdog: bool = false,
    critical: bool = false,

    pub fn hasCritical(self: @This()) bool {
        return self.overcurrent or self.watchdog;
    }

    pub fn isEmpty(self: @This()) bool {
        return !self.overcurrent and
            !self.overtemperature and
            !self.undervoltage and
            !self.stall and
            !self.overspeed and
            !self.open_loop and
            !self.watchdog;
    }
};

pub const MotorState = enum {
    safe_stop,
    recovery,
    running,
};

pub const FaultThresholds = struct {
    overcurrent_amps: f32,
    overtemp_celsius: f32,
    undervoltage_volts: f32,
    overspeed_rpm: f32,
    stall_timeout_ms: u32,
    open_loop_timeout_ms: u32,
};

pub const FaultDetector = struct {
    thresholds: FaultThresholds,
    faults: FaultSet,
    state: MotorState,
    current_amps: f32,
    speed_rpm: f32,
    stall_timer_ms: u32,
    ramp_current: f32,
    ramp_target: f32,
    ramp_rate: f32,
    ramp_start_ms: u32,

    pub fn init(thresholds: FaultThresholds) FaultDetector {
        return .{
            .thresholds = thresholds,
            .faults = .{},
            .state = .safe_stop,
            .current_amps = 0,
            .speed_rpm = 0,
            .stall_timer_ms = 0,
            .ramp_current = 0,
            .ramp_target = 1000,
            .ramp_rate = 100,
            .ramp_start_ms = 0,
        };
    }

    pub fn update(
        self: *FaultDetector,
        current_amps: f32,
        speed_rpm: f32,
        temperature_c: f32,
        supply_voltage: f32,
        timestamp_ms: u32,
    ) void {
        self.current_amps = current_amps;
        self.speed_rpm = speed_rpm;
        self.faults = .{};

        if (current_amps > self.thresholds.overcurrent_amps) {
            self.faults.overcurrent = true;
            self.faults.critical = true;
        }
        if (temperature_c > self.thresholds.overtemp_celsius) {
            self.faults.overtemperature = true;
        }
        if (supply_voltage < self.thresholds.undervoltage_volts) {
            self.faults.undervoltage = true;
        }

        if (current_amps > self.thresholds.overcurrent_amps * 0.5 and speed_rpm < 10) {
            self.stall_timer_ms += 10;
            if (self.stall_timer_ms >= self.thresholds.stall_timeout_ms) {
                self.faults.stall = true;
            }
        } else {
            self.stall_timer_ms = 0;
        }

        if (speed_rpm > self.thresholds.overspeed_rpm) {
            self.faults.overspeed = true;
        }
    }

    pub fn transition(self: *FaultDetector, command: bool, timestamp_ms: u32) void {
        switch (self.state) {
            .safe_stop => {
                if (command and !self.faults.hasCritical()) {
                    self.state = .recovery;
                    self.ramp_current = 0;
                    self.ramp_start_ms = timestamp_ms;
                }
            },
            .recovery => {
                const elapsed_s = @as(f32, @floatFromInt(timestamp_ms - self.ramp_start_ms)) / 1000.0;
                self.ramp_current = @min(self.ramp_rate * elapsed_s, self.ramp_target);
                if (self.ramp_current >= self.ramp_target) {
                    self.state = .running;
                }
                if (self.faults.hasCritical()) {
                    self.state = .safe_stop;
                }
            },
            .running => {
                if (self.faults.hasCritical()) {
                    self.state = .safe_stop;
                } else if (self.faults.overtemperature or self.faults.stall) {
                    self.state = .recovery;
                    self.ramp_current = self.speed_rpm;
                    self.ramp_start_ms = timestamp_ms;
                } else if (!command) {
                    self.state = .recovery;
                    self.ramp_current = self.speed_rpm;
                    self.ramp_target = 0;
                    self.ramp_start_ms = timestamp_ms;
                }
            },
        }
    }

    pub fn pwmTarget(self: *FaultDetector) f32 {
        return switch (self.state) {
            .safe_stop => 0.0,
            .recovery => self.ramp_current / self.ramp_target,
            .running => 1.0,
        };
    }

    pub fn clearFaults(self: *FaultDetector) void {
        if (self.state == .safe_stop) {
            self.faults = .{ .watchdog = self.faults.watchdog };
        }
    }
};
```

```zig
// src/main.zig — Motor control application
const std = @import("std");
const pid = @import("pid.zig");
const fault = @import("fault_detection.zig");

// Comptime-validated PID tuning
const motor_tuning = pid.comptimePidTuning(
    2.5, 0.8, 0.05, 0.01,
    0.0, 100.0,
    -50.0, 50.0,
    0.5,
);

const fault_thresholds = fault.FaultThresholds{
    .overcurrent_amps = 10.0,
    .overtemp_celsius = 85.0,
    .undervoltage_volts = 9.6,
    .overspeed_rpm = 6000.0,
    .stall_timeout_ms = 500,
    .open_loop_timeout_ms = 100,
};

export fn main() noreturn {
    var pid_ctrl = pid.PidController.init(motor_tuning) catch unreachable;
    var detector = fault.FaultDetector.init(fault_thresholds);

    var setpoint_rpm: f32 = 1000.0;
    var last_control_ms: u32 = 0;

    while (true) {
        const now: u32 = 0; // Replace with actual timer

        if (now -% last_control_ms >= 10) {
            last_control_ms = now;

            // Read sensors (replace with actual hardware)
            const current: f32 = 0.0;
            const voltage: f32 = 12.0;
            const temp: f32 = 25.0;
            const speed: f32 = 0.0;

            detector.update(current, speed, temp, voltage, now);
            detector.transition(true, now);

            const pwm_duty: f32 = if (detector.state == .running)
                pid_ctrl.compute(setpoint_rpm, speed)
            else blk: {
                pid_ctrl.reset();
                break :blk detector.pwmTarget();
            };

            const final_pwm: f32 = if (detector.faults.hasCritical())
                0.0
            else
                pwm_duty;

            // Apply PWM: set_pwm_duty(final_pwm);
        }
    }
}
```

### Build Instructions (Zig)

```bash
zig build-exe src/main.zig \
    -target thumb-freestanding-eabihf \
    -mcpu cortex_m4 \
    -O ReleaseSmall \
    -femit-bin=motor_control.bin \
    -fno-entry
```

## QEMU Verification

### Simulated Motor Response

Create a test harness that simulates motor dynamics:

```c
/* test_pid.c — PID test with simulated motor */
#include "pid_controller.h"
#include <stdio.h>
#include <math.h>

/* Simulated motor: first-order system with delay */
typedef struct {
    float speed;
    float time_constant;  /* seconds */
    float gain;           /* RPM per % PWM */
    float disturbance;    /* Load disturbance */
} motor_model_t;

static float motor_step(motor_model_t *motor, float pwm, float dt) {
    float target = motor->gain * pwm - motor->disturbance;
    float alpha = dt / (motor->time_constant + dt);
    motor->speed += alpha * (target - motor->speed);
    return motor->speed;
}

int main(void) {
    pid_config_t config = {
        .kp = 2.5f, .ki = 0.8f, .kd = 0.05f,
        .dt = 0.01f, .output_min = 0.0f, .output_max = 100.0f,
        .integral_min = -50.0f, .integral_max = 50.0f,
        .back_calc_gain = 0.5f,
    };

    pid_t pid;
    pid_init(&pid, &config);

    motor_model_t motor = {
        .speed = 0, .time_constant = 0.05f,
        .gain = 50.0f, .disturbance = 0,
    };

    float setpoint = 1000.0f;
    printf("Time\tSetpoint\tSpeed\t\tPWM\t\tError\n");

    for (int i = 0; i < 5000; i++) {
        float speed = motor_step(&motor, q16_to_float(pid.output) / 100.0f, 0.01f);
        q16_t output = pid_compute(&pid, q16_from_float(setpoint), q16_from_float(speed));

        if (i % 100 == 0) {
            printf("%d\t%.1f\t\t%.1f\t\t%.1f\t\t%.1f\n",
                i, setpoint, speed, q16_to_float(output),
                setpoint - speed);
        }

        /* Add disturbance at t=2s */
        if (i == 200) motor.disturbance = 200.0f;
    }

    return 0;
}
```

```bash
# Compile and run test
gcc -O2 -o test_pid test_pid.c pid_controller.c -lm
./test_pid
```

### Fault Trigger Testing

```bash
# Expected output showing fault detection:
# t=0.00s  State: SAFE_STOP  PWM: 0.00  Speed: 0.0
# t=0.01s  State: RECOVERY   PWM: 0.10  Speed: 0.0
# t=1.00s  State: RUNNING    PWM: 45.20 Speed: 998.5
# t=2.00s  State: RUNNING    PWM: 78.30 Speed: 850.2  (disturbance applied)
# t=2.50s  State: RUNNING    PWM: 95.10 Speed: 950.1  (PID compensating)
# t=5.00s  State: RECOVERY   PWM: 0.50  Speed: 500.0  (overtemp detected)
```

## What You Learned

- PID control theory: P, I, D terms and their effects on system response
- Discrete-time PID: position form vs velocity form trade-offs
- Anti-windup strategies: clamping, back-calculation, conditional integration
- Fixed-point arithmetic for deterministic timing on MCUs without FPU
- Fault detection: overcurrent, overtemperature, undervoltage, stall
- Safe state machine with graceful degradation
- Watchdog timer configuration and conditional feeding strategy
- Language-specific approaches: C fixed-point, Rust type safety, Ada SPARK contracts, Zig comptime validation

## Next Steps

- **Project 15**: Apply formal verification and safety-critical standards to this motor controller
- Implement field-oriented control (FOC) for brushless motors
- Add adaptive PID with auto-tuning (Ziegler-Nichols, relay method)
- Implement CAN bus communication for multi-motor coordination
- Add position control mode with trajectory planning (S-curve, trapezoidal)

## Language Comparison

| Aspect | C | Rust | Ada | Zig |
|--------|---|------|-----|-----|
| PID tuning validation | Runtime checks, manual | `const fn` validation, `Result` | SPARK preconditions, GNATprove | `comptime` validation, compile error |
| Anti-windup | Manual saturation logic | Method chaining `.max().min()` | Explicit saturate function | `@min`/`@max` builtins |
| Fixed-point math | Manual Q-format macros | `fixed` crate or custom | Modular types with range | Comptime Q-format generation |
| Fault detection | Bit flags, manual checks | `heapless::Vec<Fault, N>`, `contains()` | Boolean arrays, SPARK proofs | `packed struct` with field access |
| State machine | Switch/case, manual transitions | Enum + match, exhaustive | Enum with SPARK mode contracts | Enum + switch, compile-time exhaustive |
| Watchdog | Direct register access | HAL abstraction via traits | Package with preconditions | Direct access or HAL wrapper |
| Overflow protection | Manual 64-bit intermediates | Checked arithmetic by default | Range checks, SPARK proof | `+%`/`-%` explicit wrapping |
| Proof of correctness | Static analysis (PC-Lint) | Kani model checking, Miri | GNATprove formal verification | Compile-time asserts, UBSan |

## Deliverables

- [ ] PID controller with position and velocity form implementations
- [ ] Anti-windup: back-calculation strategy implemented and tested
- [ ] Fixed-point arithmetic version (Q15.16) for FPU-less MCUs
- [ ] Fault detection: overcurrent, overtemperature, undervoltage, stall
- [ ] Safe state machine: Safe-Stop → Recovery → Running transitions
- [ ] Watchdog timer with conditional feeding strategy
- [ ] Simulated motor response test showing PID convergence
- [ ] Fault trigger test showing correct state transitions
- [ ] UART debug output with state, PWM, speed, and current readings
