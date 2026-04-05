---
title: "Project 4: I2C Temperature Sensor Driver (BMP280)"
phase: 2
project: 4
---

# Project 4: I2C Temperature Sensor Driver (BMP280)

## Introduction

After mastering GPIO and UART in Phase 1, you're ready to tackle peripheral communication protocols. I2C (Inter-Integrated Circuit) is ubiquitous in embedded systems — nearly every sensor you encounter will support it. This project builds a complete BMP280 temperature and pressure sensor driver from scratch in C, Rust, Ada, and Zig.

**What you'll learn:**

- I2C protocol internals: start/stop conditions, ACK/NACK, 7-bit and 10-bit addressing
- Reading sensor datasheets and extracting register maps
- BMP280 compensation math with fixed-point arithmetic
- Error handling strategies across four languages
- Using Renode to simulate I2C peripherals
- Building reusable sensor driver abstractions

## I2C Protocol Basics

I2C is a synchronous, multi-master, multi-slave serial protocol using two wires: **SDA** (data) and **SCL** (clock). Unlike UART, it includes clock synchronization and device addressing on the same bus.

### Start and Stop Conditions

| Condition | SDA | SCL |
|-----------|-----|-----|
| **START** | High → Low (while SCL high) | High |
| **STOP**  | Low → High (while SCL high) | High |
| **Data valid** | Must be stable | High |
| **Data change** | Allowed only when SCL is low | Low |

The START condition alerts all devices on the bus that a transaction is beginning. The STOP condition releases the bus.

### Addressing and ACK

After START, the master sends a 7-bit (or 10-bit) address followed by a read/write bit:

```
| 7-bit Address | R/W | ACK |
| 1 0 1 1 0 0 0 |  0  |  0  |  ← Write to address 0x58
```

- **7-bit addressing:** 126 usable addresses (0x00 and 0x7F are reserved)
- **10-bit addressing:** Extended range for dense systems
- **ACK (0):** Receiver pulled SDA low — "I got it"
- **NACK (1):** Receiver left SDA high — "I'm done" or "I'm not here"

The BMP280 uses two possible addresses depending on the SDO pin:
- SDO grounded: **0x76** (0b1110110)
- SDO to VDD: **0x77** (0b1110111)

### I2C Transaction Flow

A typical register read looks like this:

```
START → [ADDR+W] → ACK → [REG] → ACK → RESTART → [ADDR+R] → ACK → [DATA] → NACK → STOP
```

The repeated START (RESTART) is critical — it keeps the bus locked between the write phase (setting the register address) and the read phase (getting the data).

## BMP280 Sensor Overview

The Bosch BMP280 measures temperature (-40°C to +85°C) and pressure (300–1100 hPa) with high accuracy. It communicates over I2C (up to 3.4 MHz) or SPI.

### Key Registers

| Address | Name           | Description                           | Access |
|---------|----------------|---------------------------------------|--------|
| 0x88    | calib00        | Calibration data start (24 bytes)     | R      |
| 0xE0    | id             | Chip ID (should be 0x58)             | R      |
| 0xE3    | reset          | Soft reset (write 0xB6)              | W      |
| 0xF3    | status         | Measuring/im_update bits             | R      |
| 0xF4    | ctrl_meas      | Oversampling + mode control          | R/W    |
| 0xF5    | config         | Filter + standby time                | R/W    |
| 0xF7    | press_msb      | Pressure data MSB                    | R      |
| 0xFA    | temp_msb       | Temperature data MSB                 | R      |

### Control Measurement Register (0xF4)

```
| osrs_t[2:0] | osrs_p[2:0] | mode[1:0] |
```

- **osrs_t:** Temperature oversampling (0=skipped, 1=x1, 2=x2, 3=x4, 4=x8, 5=x16)
- **osrs_p:** Pressure oversampling (same encoding)
- **mode:** 00=sleep, 01/10=forced, 11=normal

### Configuration Register (0xF5)

```
| t_sb[2:0] | filter[2:0] | spi3w_en |
```

- **t_sb:** Standby time in normal mode
- **filter:** IIR filter coefficient (0=off, 1=2, 2=4, 3=8, 4=16)

### Calibration Data

The BMP280 stores 24 bytes of factory calibration coefficients at 0x88–0xA1. These are **essential** — raw sensor values are meaningless without compensation. The coefficients are:

- `dig_T1`–`dig_T3`: Temperature compensation (unsigned/signed 16-bit)
- `dig_P1`–`dig_P9`: Pressure compensation (unsigned/signed 16-bit)

## Reading the Datasheet: Compensation Math

Bosch provides a reference implementation for converting raw ADC values to compensated temperature and pressure. The math uses 32-bit and 64-bit intermediate values to avoid overflow.

### Temperature Compensation Algorithm

```
var1 = (adc_temp / 16384.0 - dig_T1 / 1024.0) * dig_T2
var2 = ((adc_temp / 131072.0 - dig_T1 / 8192.0) *
        (adc_temp / 131072.0 - dig_T1 / 8192.0)) * dig_T3
t_fine = var1 + var2
temp_c = (t_fine * 5 + 128) / 256   // result in 0.01°C units
```

The `t_fine` value is reused for pressure compensation.

### Fixed-Point Arithmetic

Floating-point is available on Cortex-M4F (hardware FPU), but fixed-point is still useful for determinism and portability. The BMP280 compensation can be done entirely in fixed-point:

- Temperature uses Q24.8 intermediate values
- Pressure uses Q32.32 via 64-bit intermediates
- Final results are scaled integers (temperature in centidegrees, pressure in pascals × 256)

> **Tip:** Always read the "Recommended settings" table in the datasheet. For weather monitoring: osrs_t=1, osrs_p=16, normal mode, filter=16, standby=0.5ms gives you 0.16 hPa RMS noise.

## Error Handling and Timeout Management

I2C is prone to bus hangs — a slave holding SDA low will block all communication. Your driver must handle:

1. **Bus busy:** Another master or stuck slave
2. **NACK on address:** Device not present
3. **NACK on data:** Register doesn't exist
4. **Arbitration lost:** Multi-master collision
5. **Timeout:** Clock stretching exceeded limit

Each language handles these differently, as you'll see below.

## Recommended Emulator: Renode

Renode includes a BMP280 model at I2C address 0x76, making it ideal for development without hardware.

```bash
# Install Renode
sudo apt install renode  # Ubuntu/Debian
brew install renode      # macOS

# Or build from source
git clone https://github.com/renode/renode.git
cd renode && ./build.sh
```

## Implementation

### STM32F405 Hardware Setup

#### Memory Map (STM32F405RG)

| Region | Address Range     | Size   |
|--------|-------------------|--------|
| Flash  | 0x08000000–0x080FFFFF | 1024K |
| SRAM   | 0x20000000–0x2001FFFF | 128K  |

#### RCC Clock Enable (AHB1 for GPIO, APB1 for I2C)

On STM32F4, GPIO clocks are on AHB1 (not APB2 like STM32F1):

```c
#define RCC_BASE        0x40023800UL
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x40))

#define RCC_AHB1ENR_GPIOB_EN  (1U << 1)
#define RCC_APB1ENR_I2C1_EN   (1U << 21)

/* Enable GPIOB and I2C1 clocks */
RCC_AHB1ENR |= RCC_AHB1ENR_GPIOB_EN;
RCC_APB1ENR |= RCC_APB1ENR_I2C1_EN;
```

#### GPIO Configuration (STM32F4 MODER/OTYPER/OSPEEDR/PUPDR/AFR)

STM32F4 replaces the STM32F1 CRL/CRH model with separate registers:

| Register   | Description                          | Bits per pin |
|------------|--------------------------------------|--------------|
| MODER      | Mode: 00=in, 01=out, 10=AF, 11=analog | 2            |
| OTYPER     | Output type: 0=push-pull, 1=open-drain | 1            |
| OSPEEDR    | Speed: 00=2MHz, 01=25MHz, 10=50MHz, 11=100MHz | 2 |
| PUPDR      | Pull-up/down: 00=none, 01=up, 10=down | 2            |
| AFRL/AFRH  | Alternate function number (pins 0-7 / 8-15) | 4       |

For I2C1 on PB6 (SCL) and PB7 (SDA), both use AF4:

```c
#define GPIOB_BASE      0x40020400UL
#define GPIOB_MODER     (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_OTYPER    (*(volatile uint32_t *)(GPIOB_BASE + 0x04))
#define GPIOB_OSPEEDR   (*(volatile uint32_t *)(GPIOB_BASE + 0x08))
#define GPIOB_PUPDR     (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))
#define GPIOB_AFRL      (*(volatile uint32_t *)(GPIOB_BASE + 0x20))

/* Configure PB6 (SCL) and PB7 (SDA) as alternate function, open-drain */

/* MODER: set pins 6 and 7 to alternate function mode (10) */
GPIOB_MODER &= ~((0x3U << 12) | (0x3U << 14));  /* Clear bits for pin 6 and 7 */
GPIOB_MODER |=  (0x2U << 12) | (0x2U << 14);    /* Set AF mode (10) */

/* OTYPER: set pins 6 and 7 to open-drain (1) */
GPIOB_OTYPER |= (1U << 6) | (1U << 7);

/* OSPEEDR: set to high speed (10 = 50 MHz) */
GPIOB_OSPEEDR |= (0x2U << 12) | (0x2U << 14);

/* PUPDR: pull-up (01) for I2C idle-high */
GPIOB_PUPDR &= ~((0x3U << 12) | (0x3U << 14));
GPIOB_PUPDR |=  (0x1U << 12) | (0x1U << 14);

/* AFRL: set AF4 for pins 6 and 7 (4 bits each) */
GPIOB_AFRL &= ~((0xFU << 24) | (0xFU << 28));   /* Clear AF for pin 6 and 7 */
GPIOB_AFRL |=  (0x4U << 24) | (0x4U << 28);     /* Set AF4 (I2C1) */
```

#### I2C TIMINGR Values (STM32F4)

The STM32F4 replaces CCR/TRISE with a single TIMINGR register. The layout is:

```
| 31:28   | 27:24 | 23:20    | 19:16   | 15:8    | 7:0     |
| PRESC   | -     | SCLDEL   | SDADEL  | SCLH    | SCLL    |
```

| Speed   | APB1 Clock | TIMINGR Value | PRESC | SCLDEL | SDADEL | SCLH | SCLL |
|---------|------------|---------------|-------|--------|--------|------|------|
| 100 kHz | 16 MHz     | 0x00300D14    | 3     | 13     | 1      | 20   | 20   |
| 400 kHz | 16 MHz     | 0x0010020A    | 1     | 2      | 0      | 10   | 10   |
```

#### Linker Script (`stm32f405rg.ld`)

```ld
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = 0x20020000;

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    ._sidata = .;

    .data : AT (_sidata)
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM
}
```

### C: I2C Peripheral Driver + BMP280 Read

#### I2C Driver (`i2c.h`)

```c
#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    I2C_OK = 0,
    I2C_ERR_BUSY,
    I2C_ERR_NACK_ADDR,
    I2C_ERR_NACK_DATA,
    I2C_ERR_TIMEOUT,
    I2C_ERR_ARBITRATION,
} i2c_error_t;

typedef struct {
    volatile uint32_t *cr1;
    volatile uint32_t *cr2;
    volatile uint32_t *isr;
    volatile uint32_t *txdr;
    volatile uint32_t *rxdr;
    volatile uint32_t *oar1;
    volatile uint32_t *timingr;
    uint32_t i2c_clk;
} i2c_handle_t;

#define I2C_TIMEOUT_US  10000

i2c_error_t i2c_init(i2c_handle_t *hi2c, uint32_t speed_hz);
i2c_error_t i2c_write(i2c_handle_t *hi2c, uint8_t addr,
                      const uint8_t *data, uint16_t len);
i2c_error_t i2c_read(i2c_handle_t *hi2c, uint8_t addr,
                     uint8_t *data, uint16_t len);
i2c_error_t i2c_write_read(i2c_handle_t *hi2c, uint8_t addr,
                           const uint8_t *tx_data, uint16_t tx_len,
                           uint8_t *rx_data, uint16_t rx_len);

#endif
```

#### I2C Driver Implementation (`i2c.c`)

```c
#include "i2c.h"

/* STM32F4 I2C register offsets (I2C2 peripheral shown) */
#define I2C2_BASE       0x40005800UL
#define I2C_CR1_OFF     0x00
#define I2C_CR2_OFF     0x04
#define I2C_OAR1_OFF    0x08
#define I2C_TIMINGR_OFF 0x10
#define I2C_ISR_OFF     0x18
#define I2C_TXDR_OFF    0x24
#define I2C_RXDR_OFF    0x28

#define CR1_PE          (1U << 0)
#define CR1_TXIE        (1U << 1)
#define CR1_RXIE        (1U << 2)
#define CR1_ERRIE       (1U << 3)

#define CR2_START       (1U << 13)
#define CR2_STOP        (1U << 14)
#define CR2_NACK        (1U << 15)
#define CR2_RD_WRN      (1U << 10)

#define ISR_TXE         (1U << 0)
#define ISR_TXIS        (1U << 1)
#define ISR_RXNE        (1U << 2)
#define ISR_ADDR        (1U << 3)
#define ISR_NACKF       (1U << 4)
#define ISR_STOPF       (1U << 5)
#define ISR_TC          (1U << 6)
#define ISR_BERR        (1U << 8)
#define ISR_ARLO        (1U << 9)
#define ISR_BUSY        (1U << 15)

static void delay_us(uint32_t us) {
    volatile uint32_t count = us * 4;  /* ~4 cycles per count at 16 MHz */
    while (count--) {
        __asm volatile ("nop");
    }
}

i2c_error_t i2c_init(i2c_handle_t *hi2c, uint32_t speed_hz) {
    hi2c->cr1 = (volatile uint32_t *)(I2C2_BASE + I2C_CR1_OFF);
    hi2c->cr2 = (volatile uint32_t *)(I2C2_BASE + I2C_CR2_OFF);
    hi2c->isr = (volatile uint32_t *)(I2C2_BASE + I2C_ISR_OFF);
    hi2c->txdr = (volatile uint32_t *)(I2C2_BASE + I2C_TXDR_OFF);
    hi2c->rxdr = (volatile uint32_t *)(I2C2_BASE + I2C_RXDR_OFF);
    hi2c->oar1 = (volatile uint32_t *)(I2C2_BASE + I2C_OAR1_OFF);
    hi2c->timingr = (volatile uint32_t *)(I2C2_BASE + I2C_TIMINGR_OFF);

    /* Disable peripheral during config */
    *hi2c->cr1 &= ~CR1_PE;

    /* Configure TIMINGR for 100 kHz at 16 MHz APB1 clock */
    /* TIMINGR = 0x00300D14: PRESC=3, SCLDEL=13, SDADEL=1, SCLH=20, SCLL=20 */
    *hi2c->timingr = 0x00300D14;

    /* Set own address (master mode, 7-bit) */
    *hi2c->oar1 = 0;

    /* Enable peripheral and error interrupts */
    *hi2c->cr1 = CR1_PE | CR1_ERRIE;

    return I2C_OK;
}

static i2c_error_t wait_flag(volatile uint32_t *isr, uint32_t flag,
                              uint32_t error_flags, uint32_t timeout_us) {
    uint32_t elapsed = 0;
    while (elapsed < timeout_us) {
        uint32_t status = *isr;
        if (status & error_flags) {
            if (status & ISR_NACKF) return I2C_ERR_NACK_ADDR;
            if (status & ISR_BERR)  return I2C_ERR_BUSY;
            if (status & ISR_ARLO)  return I2C_ERR_ARBITRATION;
        }
        if (status & flag) return I2C_OK;
        delay_us(1);
        elapsed++;
    }
    return I2C_ERR_TIMEOUT;
}

i2c_error_t i2c_write(i2c_handle_t *hi2c, uint8_t addr,
                      const uint8_t *data, uint16_t len) {
    if (*hi2c->isr & ISR_BUSY) return I2C_ERR_BUSY;

    /* Set slave address, number of bytes, auto-end mode, start */
    *hi2c->cr2 = ((uint32_t)addr << 1) | ((uint32_t)len << 16) |
                 (1U << 25) /* AUTOEND */ | CR2_START;

    for (uint16_t i = 0; i < len; i++) {
        i2c_error_t err = wait_flag(hi2c->isr, ISR_TXIS,
                                    ISR_NACKF | ISR_BERR | ISR_ARLO,
                                    I2C_TIMEOUT_US);
        if (err != I2C_OK) return err;
        *hi2c->txdr = data[i];
    }

    return wait_flag(hi2c->isr, ISR_STOPF, 0, I2C_TIMEOUT_US);
}

i2c_error_t i2c_read(i2c_handle_t *hi2c, uint8_t addr,
                     uint8_t *data, uint16_t len) {
    if (*hi2c->isr & ISR_BUSY) return I2C_ERR_BUSY;

    *hi2c->cr2 = ((uint32_t)addr << 1) | ((uint32_t)len << 16) |
                 (1U << 25) /* AUTOEND */ | CR2_RD_WRN | CR2_START;

    for (uint16_t i = 0; i < len; i++) {
        i2c_error_t err = wait_flag(hi2c->isr, ISR_RXNE,
                                    ISR_NACKF | ISR_BERR | ISR_ARLO,
                                    I2C_TIMEOUT_US);
        if (err != I2C_OK) return err;
        data[i] = (uint8_t)(*hi2c->rxdr & 0xFF);
    }

    return wait_flag(hi2c->isr, ISR_STOPF, 0, I2C_TIMEOUT_US);
}

i2c_error_t i2c_write_read(i2c_handle_t *hi2c, uint8_t addr,
                           const uint8_t *tx_data, uint16_t tx_len,
                           uint8_t *rx_data, uint16_t rx_len) {
    if (*hi2c->isr & ISR_BUSY) return I2C_ERR_BUSY;

    /* Write phase: send register address, no STOP (software end) */
    *hi2c->cr2 = ((uint32_t)addr << 1) | ((uint32_t)tx_len << 16) |
                 (0U << 25) /* SOFTEND */ | CR2_START;

    for (uint16_t i = 0; i < tx_len; i++) {
        i2c_error_t err = wait_flag(hi2c->isr, ISR_TXIS,
                                    ISR_NACKF | ISR_BERR | ISR_ARLO,
                                    I2C_TIMEOUT_US);
        if (err != I2C_OK) return err;
        *hi2c->txdr = tx_data[i];
    }

    /* Wait for transfer complete (repeated start next) */
    i2c_error_t err = wait_flag(hi2c->isr, ISR_TC, 0, I2C_TIMEOUT_US);
    if (err != I2C_OK) return err;

    /* Read phase: start + read with auto-end */
    *hi2c->cr2 = ((uint32_t)addr << 1) | ((uint32_t)rx_len << 16) |
                 (1U << 25) /* AUTOEND */ | CR2_RD_WRN | CR2_START;

    for (uint16_t i = 0; i < rx_len; i++) {
        err = wait_flag(hi2c->isr, ISR_RXNE,
                        ISR_NACKF | ISR_BERR | ISR_ARLO,
                        I2C_TIMEOUT_US);
        if (err != I2C_OK) return err;
        rx_data[i] = (uint8_t)(*hi2c->rxdr & 0xFF);
    }

    return wait_flag(hi2c->isr, ISR_STOPF, 0, I2C_TIMEOUT_US);
}
```

#### BMP280 Driver (`bmp280.h`)

```c
#ifndef BMP280_H
#define BMP280_H

#include "i2c.h"
#include <stdint.h>

#define BMP280_ADDR         0x76
#define BMP280_CHIP_ID      0x58

/* Register addresses */
#define BMP280_REG_CHIPID   0xD0
#define BMP280_REG_RESET    0xE0
#define BMP280_REG_STATUS   0xF3
#define BMP280_REG_CTRL_MEAS 0xF4
#define BMP280_REG_CONFIG   0xF5
#define BMP280_REG_PRESS_MSB 0xF7
#define BMP280_REG_TEMP_MSB  0xFA
#define BMP280_REG_CALIB00   0x88

#define BMP280_RESET_VALUE  0xB6

/* Oversampling settings */
#define BMP280_OSRS_SKIPPED 0x00
#define BMP280_OSRS_X1      0x01
#define BMP280_OSRS_X2      0x02
#define BMP280_OSRS_X4      0x03
#define BMP280_OSRS_X8      0x04
#define BMP280_OSRS_X16     0x05

/* Power modes */
#define BMP280_MODE_SLEEP   0x00
#define BMP280_MODE_FORCED  0x01
#define BMP280_MODE_NORMAL  0x03

/* Filter coefficients */
#define BMP280_FILTER_OFF   0x00
#define BMP280_FILTER_2     0x01
#define BMP280_FILTER_4     0x02
#define BMP280_FILTER_8     0x03
#define BMP280_FILTER_16    0x04

/* Standby times (ms) */
#define BMP280_STANDBY_0_5  0x00
#define BMP280_STANDBY_62_5 0x01
#define BMP280_STANDBY_125  0x02

typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
} bmp280_calib_t;

typedef struct {
    i2c_handle_t *i2c;
    bmp280_calib_t calib;
    int32_t t_fine;
} bmp280_t;

typedef enum {
    BMP280_OK = 0,
    BMP280_ERR_I2C,
    BMP280_ERR_CHIP_ID,
    BMP280_ERR_TIMEOUT,
} bmp280_error_t;

bmp280_error_t bmp280_init(bmp280_t *dev, i2c_handle_t *i2c);
bmp280_error_t bmp280_configure(bmp280_t *dev, uint8_t osrs_t,
                                uint8_t osrs_p, uint8_t mode,
                                uint8_t filter, uint8_t standby);
bmp280_error_t bmp280_read_temperature(bmp280_t *dev, int32_t *temp_c_x100);
bmp280_error_t bmp280_read_pressure(bmp280_t *dev, uint32_t *pressure_pa);
bmp280_error_t bmp280_read_both(bmp280_t *dev, int32_t *temp_c_x100,
                                uint32_t *pressure_pa);

#endif
```

#### BMP280 Driver Implementation (`bmp280.c`)

```c
#include "bmp280.h"

static bmp280_error_t read_regs(bmp280_t *dev, uint8_t reg,
                                uint8_t *data, uint16_t len) {
    i2c_error_t err = i2c_write_read(dev->i2c, BMP280_ADDR,
                                     &reg, 1, data, len);
    return (err == I2C_OK) ? BMP280_OK : BMP280_ERR_I2C;
}

static bmp280_error_t write_reg(bmp280_t *dev, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    i2c_error_t err = i2c_write(dev->i2c, BMP280_ADDR, buf, 2);
    return (err == I2C_OK) ? BMP280_OK : BMP280_ERR_I2C;
}

bmp280_error_t bmp280_init(bmp280_t *dev, i2c_handle_t *i2c) {
    dev->i2c = i2c;
    dev->t_fine = 0;

    /* Reset the sensor */
    bmp280_error_t err = write_reg(dev, BMP280_REG_RESET, BMP280_RESET_VALUE);
    if (err != BMP280_OK) return err;

    /* Busy-wait for reset (max 2 ms per datasheet) */
    for (volatile int i = 0; i < 80000; i++);

    /* Verify chip ID */
    uint8_t chip_id = 0;
    err = read_regs(dev, BMP280_REG_CHIPID, &chip_id, 1);
    if (err != BMP280_OK) return err;
    if (chip_id != BMP280_CHIP_ID) return BMP280_ERR_CHIP_ID;

    /* Read calibration data (24 bytes starting at 0x88) */
    uint8_t calib_raw[26];
    err = read_regs(dev, BMP280_REG_CALIB00, calib_raw, 26);
    if (err != BMP280_OK) return err;

    dev->calib.dig_T1 = (uint16_t)(calib_raw[1] << 8) | calib_raw[0];
    dev->calib.dig_T2 = (int16_t)((calib_raw[3] << 8) | calib_raw[2]);
    dev->calib.dig_T3 = (int16_t)((calib_raw[5] << 8) | calib_raw[4]);
    dev->calib.dig_P1 = (uint16_t)(calib_raw[7] << 8) | calib_raw[6];
    dev->calib.dig_P2 = (int16_t)((calib_raw[9] << 8) | calib_raw[8]);
    dev->calib.dig_P3 = (int16_t)((calib_raw[11] << 8) | calib_raw[10]);
    dev->calib.dig_P4 = (int16_t)((calib_raw[13] << 8) | calib_raw[12]);
    dev->calib.dig_P5 = (int16_t)((calib_raw[15] << 8) | calib_raw[14]);
    dev->calib.dig_P6 = (int16_t)((calib_raw[17] << 8) | calib_raw[16]);
    dev->calib.dig_P7 = (int16_t)((calib_raw[19] << 8) | calib_raw[18]);
    dev->calib.dig_P8 = (int16_t)((calib_raw[21] << 8) | calib_raw[20]);
    dev->calib.dig_P9 = (int16_t)((calib_raw[23] << 8) | calib_raw[22]);

    return BMP280_OK;
}

bmp280_error_t bmp280_configure(bmp280_t *dev, uint8_t osrs_t,
                                uint8_t osrs_p, uint8_t mode,
                                uint8_t filter, uint8_t standby) {
    /* ctrl_meas: osrs_t[7:5] | osrs_p[4:2] | mode[1:0] */
    uint8_t ctrl = (osrs_t << 5) | (osrs_p << 2) | (mode & 0x03);
    bmp280_error_t err = write_reg(dev, BMP280_REG_CTRL_MEAS, ctrl);
    if (err != BMP280_OK) return err;

    /* config: t_sb[7:5] | filter[4:2] | spi3w_en[0] */
    uint8_t config = (standby << 5) | (filter << 2);
    return write_reg(dev, BMP280_REG_CONFIG, config);
}

static int32_t compensate_temperature(bmp280_t *dev, int32_t adc_temp) {
    const bmp280_calib_t *c = &dev->calib;

    /* Fixed-point compensation (Bosch reference algorithm) */
    int32_t var1 = ((((adc_temp >> 3) - ((int32_t)c->dig_T1 << 1))) *
                    ((int32_t)c->dig_T2)) >> 11;

    int32_t var2 = (((((adc_temp >> 4) - ((int32_t)c->dig_T1)) *
                      ((adc_temp >> 4) - ((int32_t)c->dig_T1))) >> 12) *
                    ((int32_t)c->dig_T3)) >> 14;

    dev->t_fine = var1 + var2;

    /* Temperature in 0.01°C units */
    return (dev->t_fine * 5 + 128) >> 8;
}

static uint32_t compensate_pressure(bmp280_t *dev, int32_t adc_press) {
    const bmp280_calib_t *c = &dev->calib;
    int64_t var1, var2, p;

    var1 = ((int64_t)dev->t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)c->dig_P6;
    var2 = var2 + ((var1 * (int64_t)c->dig_P5) << 17);
    var2 = var2 + (((int64_t)c->dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)c->dig_P3) >> 8) +
           ((var1 * (int64_t)c->dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)c->dig_P1) >> 33;

    if (var1 == 0) return 0;  /* Avoid division by zero */

    p = 1048576 - adc_press;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)c->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)c->dig_P8) * p) >> 19;

    p = ((p + var1 + var2) >> 8) + (((int64_t)c->dig_P7) << 4);

    return (uint32_t)(p >> 8);  /* Pressure in Pa */
}

bmp280_error_t bmp280_read_temperature(bmp280_t *dev, int32_t *temp_c_x100) {
    uint8_t raw[3];
    bmp280_error_t err = read_regs(dev, BMP280_REG_TEMP_MSB, raw, 3);
    if (err != BMP280_OK) return err;

    int32_t adc = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) |
                  ((int32_t)raw[2] >> 4);

    *temp_c_x100 = compensate_temperature(dev, adc);
    return BMP280_OK;
}

bmp280_error_t bmp280_read_pressure(bmp280_t *dev, uint32_t *pressure_pa) {
    uint8_t raw[3];
    bmp280_error_t err = read_regs(dev, BMP280_REG_PRESS_MSB, raw, 3);
    if (err != BMP280_OK) return err;

    int32_t adc = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) |
                  ((int32_t)raw[2] >> 4);

    /* Must read temperature first to populate t_fine */
    uint8_t temp_raw[3];
    err = read_regs(dev, BMP280_REG_TEMP_MSB, temp_raw, 3);
    if (err != BMP280_OK) return err;

    int32_t adc_temp = ((int32_t)temp_raw[0] << 12) |
                       ((int32_t)temp_raw[1] << 4) |
                       ((int32_t)temp_raw[2] >> 4);
    compensate_temperature(dev, adc_temp);

    *pressure_pa = compensate_pressure(dev, adc);
    return BMP280_OK;
}

bmp280_error_t bmp280_read_both(bmp280_t *dev, int32_t *temp_c_x100,
                                uint32_t *pressure_pa) {
    uint8_t raw[6];
    bmp280_error_t err = read_regs(dev, BMP280_REG_PRESS_MSB, raw, 6);
    if (err != BMP280_OK) return err;

    /* Pressure: bytes 0-2, Temperature: bytes 3-5 */
    int32_t adc_p = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) |
                    ((int32_t)raw[2] >> 4);
    int32_t adc_t = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) |
                    ((int32_t)raw[5] >> 4);

    *temp_c_x100 = compensate_temperature(dev, adc_t);
    *pressure_pa = compensate_pressure(dev, adc_p);
    return BMP280_OK;
}
```

#### Main Application (`main.c`)

```c
#include "bmp280.h"
#include <stdio.h>

static i2c_handle_t hi2c;
static bmp280_t bmp280;

int main(void) {
    /* Initialize I2C at 100 kHz */
    i2c_init(&hi2c, 100000);

    /* Initialize BMP280 */
    bmp280_error_t err = bmp280_init(&bmp280, &hi2c);
    if (err != BMP280_OK) {
        printf("BMP280 init failed: %d\n", err);
        return 1;
    }

    /* Configure: temp x1, pressure x16, normal mode, filter 16, 0.5ms standby */
    err = bmp280_configure(&bmp280, BMP280_OSRS_X1, BMP280_OSRS_X16,
                           BMP280_MODE_NORMAL, BMP280_FILTER_16,
                           BMP280_STANDBY_0_5);
    if (err != BMP280_OK) {
        printf("BMP280 configure failed: %d\n", err);
        return 1;
    }

    /* Continuous reading loop */
    while (1) {
        int32_t temp_x100;
        uint32_t pressure_pa;

        err = bmp280_read_both(&bmp280, &temp_x100, &pressure_pa);
        if (err == BMP280_OK) {
            int32_t temp_int = temp_x100 / 100;
            int32_t temp_frac = temp_x100 % 100;
            uint32_t pressure_hpa = pressure_pa / 100;
            uint32_t pressure_frac = pressure_pa % 100;

            printf("Temp: %ld.%02ld C  Pressure: %lu.%02lu hPa\n",
                   temp_int, temp_frac < 0 ? -temp_frac : temp_frac,
                   pressure_hpa, pressure_frac);
        } else {
            printf("Read error: %d\n", err);
        }

        /* Wait ~1 second between reads */
        for (volatile int i = 0; i < 400000; i++);
    }

    return 0;
}
```

### Rust: Driver Using embedded-hal I2C Trait

```rust
// Cargo.toml
// [package]
// name = "bmp280-driver"
// version = "0.1.0"
// edition = "2021"
//
// [dependencies]
// embedded-hal = "1.0"
// nb = "1.1"

use embedded_hal::i2c::{I2c, Error, ErrorKind, ErrorType};

/// BMP280 register addresses
mod regs {
    pub const CHIP_ID: u8 = 0xD0;
    pub const RESET: u8 = 0xE0;
    pub const STATUS: u8 = 0xF3;
    pub const CTRL_MEAS: u8 = 0xF4;
    pub const CONFIG: u8 = 0xF5;
    pub const PRESS_MSB: u8 = 0xF7;
    pub const TEMP_MSB: u8 = 0xFA;
    pub const CALIB00: u8 = 0x88;
}

const BMP280_CHIP_ID: u8 = 0x58;
const BMP280_ADDR: u8 = 0x76;
const BMP280_RESET_VAL: u8 = 0xB6;

/// BMP280 driver error types
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Bmp280Error<I2cErr> {
    I2cError(I2cErr),
    ChipIdMismatch { expected: u8, found: u8 },
    Busy,
}

/// Calibration coefficients read from device
#[derive(Debug, Clone, Copy)]
pub struct Calibration {
    dig_t1: u16,
    dig_t2: i16,
    dig_t3: i16,
    dig_p1: u16,
    dig_p2: i16,
    dig_p3: i16,
    dig_p4: i16,
    dig_p5: i16,
    dig_p6: i16,
    dig_p7: i16,
    dig_p8: i16,
    dig_p9: i16,
}

/// Oversampling settings
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum Oversampling {
    Skipped = 0,
    X1 = 1,
    X2 = 2,
    X4 = 3,
    X8 = 4,
    X16 = 5,
}

/// Power mode
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum Mode {
    Sleep = 0,
    Forced = 1,
    Normal = 3,
}

/// IIR filter coefficient
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum Filter {
    Off = 0,
    Coeff2 = 1,
    Coeff4 = 2,
    Coeff8 = 3,
    Coeff16 = 4,
}

/// Standby time in normal mode
#[derive(Debug, Clone, Copy, PartialEq)]
#[repr(u8)]
pub enum StandbyTime {
    Ms0_5 = 0,
    Ms62_5 = 1,
    Ms125 = 2,
    Ms250 = 3,
    Ms500 = 4,
    Ms1000 = 5,
}

/// BMP280 sensor reading
#[derive(Debug, Clone, Copy)]
pub struct SensorData {
    pub temperature_c: f32,
    pub pressure_pa: f32,
}

/// BMP280 driver with generic I2C backend
pub struct Bmp280<I2C> {
    i2c: I2C,
    calib: Calibration,
    t_fine: i32,
}

impl<I2C, I2cErr> Bmp280<I2C>
where
    I2C: I2c<Error = I2cErr>,
    I2cErr: Error,
{
    /// Create a new BMP280 driver and initialize the sensor
    pub fn new(mut i2c: I2C) -> Result<Self, Bmp280Error<I2cErr>> {
        let addr = BMP280_ADDR;

        // Reset sensor
        i2c.write(addr, &[regs::RESET, BMP280_RESET_VAL])
            .map_err(Bmp280Error::I2cError)?;

        // Short delay for reset (caller should provide delay impl in real code)
        cortex_m::asm::delay(80_000);

        // Verify chip ID
        let mut chip_id = [0u8];
        i2c.write_read(addr, &[regs::CHIP_ID], &mut chip_id)
            .map_err(Bmp280Error::I2cError)?;

        if chip_id[0] != BMP280_CHIP_ID {
            return Err(Bmp280Error::ChipIdMismatch {
                expected: BMP280_CHIP_ID,
                found: chip_id[0],
            });
        }

        // Read calibration data
        let mut calib_raw = [0u8; 26];
        i2c.write_read(addr, &[regs::CALIB00], &mut calib_raw)
            .map_err(Bmp280Error::I2cError)?;

        let calib = Calibration {
            dig_t1: u16::from_le_bytes([calib_raw[0], calib_raw[1]]),
            dig_t2: i16::from_le_bytes([calib_raw[2], calib_raw[3]]),
            dig_t3: i16::from_le_bytes([calib_raw[4], calib_raw[5]]),
            dig_p1: u16::from_le_bytes([calib_raw[6], calib_raw[7]]),
            dig_p2: i16::from_le_bytes([calib_raw[8], calib_raw[9]]),
            dig_p3: i16::from_le_bytes([calib_raw[10], calib_raw[11]]),
            dig_p4: i16::from_le_bytes([calib_raw[12], calib_raw[13]]),
            dig_p5: i16::from_le_bytes([calib_raw[14], calib_raw[15]]),
            dig_p6: i16::from_le_bytes([calib_raw[16], calib_raw[17]]),
            dig_p7: i16::from_le_bytes([calib_raw[18], calib_raw[19]]),
            dig_p8: i16::from_le_bytes([calib_raw[20], calib_raw[21]]),
            dig_p9: i16::from_le_bytes([calib_raw[22], calib_raw[23]]),
        };

        Ok(Bmp280 {
            i2c,
            calib,
            t_fine: 0,
        })
    }

    /// Configure sensor: oversampling, mode, filter, standby
    pub fn configure(
        &mut self,
        osrs_t: Oversampling,
        osrs_p: Oversampling,
        mode: Mode,
        filter: Filter,
        standby: StandbyTime,
    ) -> Result<(), Bmp280Error<I2cErr>> {
        let ctrl_meas = ((osrs_t as u8) << 5) | ((osrs_p as u8) << 2) | (mode as u8);
        self.i2c
            .write(BMP280_ADDR, &[regs::CTRL_MEAS, ctrl_meas])
            .map_err(Bmp280Error::I2cError)?;

        let config = ((standby as u8) << 5) | ((filter as u8) << 2);
        self.i2c
            .write(BMP280_ADDR, &[regs::CONFIG, config])
            .map_err(Bmp280Error::I2cError)?;

        Ok(())
    }

    /// Read temperature in centidegrees (0.01°C units)
    fn read_temperature_raw(&mut self) -> Result<i32, Bmp280Error<I2cErr>> {
        let mut raw = [0u8; 3];
        self.i2c
            .write_read(BMP280_ADDR, &[regs::TEMP_MSB], &mut raw)
            .map_err(Bmp280Error::I2cError)?;

        let adc = ((raw[0] as i32) << 12) | ((raw[1] as i32) << 4) | ((raw[2] as i32) >> 4);
        Ok(self.compensate_temperature(adc))
    }

    /// Read pressure in pascals
    fn read_pressure_raw(&mut self) -> Result<u32, Bmp280Error<I2cErr>> {
        let mut raw = [0u8; 3];
        self.i2c
            .write_read(BMP280_ADDR, &[regs::PRESS_MSB], &mut raw)
            .map_err(Bmp280Error::I2cError)?;

        let adc = ((raw[0] as i32) << 12) | ((raw[1] as i32) << 4) | ((raw[2] as i32) >> 4);
        Ok(self.compensate_pressure(adc))
    }

    /// Read both temperature and pressure
    pub fn read(&mut self) -> Result<SensorData, Bmp280Error<I2cErr>> {
        // Read all 6 bytes in one transaction (pressure then temperature)
        let mut raw = [0u8; 6];
        self.i2c
            .write_read(BMP280_ADDR, &[regs::PRESS_MSB], &mut raw)
            .map_err(Bmp280Error::I2cError)?;

        let adc_p = ((raw[0] as i32) << 12) | ((raw[1] as i32) << 4) | ((raw[2] as i32) >> 4);
        let adc_t = ((raw[3] as i32) << 12) | ((raw[4] as i32) << 4) | ((raw[5] as i32) >> 4);

        let temp_x100 = self.compensate_temperature(adc_t);
        let pressure = self.compensate_pressure(adc_p);

        Ok(SensorData {
            temperature_c: temp_x100 as f32 / 100.0,
            pressure_pa: pressure as f32 / 256.0,
        })
    }

    fn compensate_temperature(&mut self, adc: i32) -> i32 {
        let c = &self.calib;

        let var1 = ((((adc >> 3) - ((c.dig_t1 as i32) << 1))) * (c.dig_t2 as i32)) >> 11;

        let var2 = (((((adc >> 4) - (c.dig_t1 as i32))
            * ((adc >> 4) - (c.dig_t1 as i32)))
            >> 12)
            * (c.dig_t3 as i32))
            >> 14;

        self.t_fine = var1 + var2;
        (self.t_fine * 5 + 128) >> 8
    }

    fn compensate_pressure(&self, adc: i32) -> u32 {
        let c = &self.calib;

        let var1: i64 = (self.t_fine as i64) - 128000;
        let var2: i64 = var1 * var1 * (c.dig_p6 as i64);
        let var2 = var2 + ((var1 * (c.dig_p5 as i64)) << 17);
        let var2 = var2 + (((c.dig_p4 as i64) as i64) << 35);
        let var1 = ((var1 * var1 * (c.dig_p3 as i64)) >> 8)
            + ((var1 * (c.dig_p2 as i64)) << 12);
        let var1 = (((((1i64) << 47) + var1)) * (c.dig_p1 as i64)) >> 33;

        if var1 == 0 {
            return 0;
        }

        let mut p: i64 = 1048576 - (adc as i64);
        p = (((p << 31) - var2) * 3125) / var1;
        let var1 = ((c.dig_p9 as i64) * (p >> 13) * (p >> 13)) >> 25;
        let var2 = ((c.dig_p8 as i64) * p) >> 19;
        p = ((p + var1 + var2) >> 8) + ((c.dig_p7 as i64) << 4);

        (p >> 8) as u32
    }
}

// --- Example usage with stm32f4xx-hal (STM32F405) ---
//
// #[entry]
// fn main() -> ! {
//     let dp = stm32::Peripherals::take().unwrap();
//     let cp = cortex_m::Peripherals::take().unwrap();
//
//     let rcc = dp.RCC.constrain();
//     let clocks = rcc.cfgr.sysclk(48.MHz()).freeze();
//
//     let gpiob = dp.GPIOB.split();
//     let scl = gpiob.pb6.into_alternate::<4>();
//     let sda = gpiob.pb7.into_alternate::<4>();
//
//     let i2c = I2c::new(dp.I2C2, (scl, sda), 100.kHz(), clocks);
//
//     let mut bmp280 = Bmp280::new(i2c).expect("BMP280 init failed");
//     bmp280
//         .configure(
//             Oversampling::X1,
//             Oversampling::X16,
//             Mode::Normal,
//             Filter::Coeff16,
//             StandbyTime::Ms0_5,
//         )
//         .expect("BMP280 configure failed");
//
//     loop {
//         match bmp280.read() {
//             Ok(data) => {
//                 defmt::println!(
//                     "Temp: {:.2} C  Pressure: {:.2} hPa",
//                     data.temperature_c,
//                     data.pressure_pa / 100.0
//                 );
//             }
//             Err(e) => defmt::println!("Error: {:?}", e),
//         }
//         cortex_m::asm::delay(16_000_000); // ~1 second
//     }
// }
```

### Ada: Generic Sensor Driver Package

```ada
-- bmp280.ads
with I2C_Driver; use I2C_Driver;

package BMP280 is

   -- I2C address (SDO grounded)
   BMP280_Address : constant I2C_Address := 16#76#;
   Chip_ID_Expected : constant UInt8 := 16#58#;

   -- Register addresses
   Reg_ChipID   : constant UInt8 := 16#D0#;
   Reg_Reset    : constant UInt8 := 16#E0#;
   Reg_Status   : constant UInt8 := 16#F3#;
   Reg_CtrlMeas : constant UInt8 := 16#F4#;
   Reg_Config   : constant UInt8 := 16#F5#;
   Reg_PressMSB : constant UInt8 := 16#F7#;
   Reg_TempMSB  : constant UInt8 := 16#FA#;
   Reg_Calib00  : constant UInt8 := 16#88#;

   Reset_Value : constant UInt8 := 16#B6#;

   -- Oversampling settings
   type Oversampling is (Skipped, X1, X2, X4, X8, X16);
   for Oversampling use (Skipped => 0, X1 => 1, X2 => 2,
                         X4 => 3, X8 => 4, X16 => 5);

   -- Power modes
   type Power_Mode is (Sleep, Forced, Normal);
   for Power_Mode use (Sleep => 0, Forced => 1, Normal => 3);

   -- Filter coefficients
   type Filter_Coeff is (Off, Coeff2, Coeff4, Coeff8, Coeff16);
   for Filter_Coeff use (Off => 0, Coeff2 => 1, Coeff4 => 2,
                         Coeff8 => 3, Coeff16 => 4);

   -- Standby times
   type Standby_Time is (Ms0_5, Ms62_5, Ms125);
   for Standby_Time use (Ms0_5 => 0, Ms62_5 => 1, Ms125 => 2);

   -- Calibration data with strong typing
   type Calibration_Data is record
      Dig_T1 : UInt16;
      Dig_T2 : Int16;
      Dig_T3 : Int16;
      Dig_P1 : UInt16;
      Dig_P2 : Int16;
      Dig_P3 : Int16;
      Dig_P4 : Int16;
      Dig_P5 : Int16;
      Dig_P6 : Int16;
      Dig_P7 : Int16;
      Dig_P8 : Int16;
      Dig_P9 : Int16;
   end record;

   -- Sensor reading result
   type Sensor_Reading is record
      Temperature_Centidegrees : Int32;  -- in 0.01°C units
      Pressure_Pa              : UInt32; -- in pascals
   end record;

   -- Error types
   type BMP280_Error is (OK, I2C_Error, Chip_ID_Mismatch, Timeout);

   -- Device handle
   type BMP280_Device is private;

   -- Initialize device and read calibration
   function Initialize
     (Port : access I2C_Port'Class)
      return BMP280_Device;

   -- Get initialization status
   function Is_Valid (Dev : BMP280_Device) return Boolean;
   function Get_Error (Dev : BMP280_Device) return BMP280_Error;

   -- Configure sensor
   procedure Configure
     (Dev      : in out BMP280_Device;
      OSRS_T   : Oversampling;
      OSRS_P   : Oversampling;
      Mode     : Power_Mode;
      Filter   : Filter_Coeff;
      Standby  : Standby_Time);

   -- Read sensor data
   function Read_Sensor
     (Dev : in out BMP280_Device)
      return Sensor_Reading;

private

   type BMP280_Device is record
      Port      : access I2C_Port'Class := null;
      Calib     : Calibration_Data;
      T_Fine    : Int32 := 0;
      Valid     : Boolean := False;
      Last_Err  : BMP280_Error := OK;
   end record;

end BMP280;
```

```ada
-- bmp280.adb
package body BMP280 is

   procedure Write_Reg
     (Dev  : in out BMP280_Device;
      Reg  : UInt8;
      Data : UInt8)
   is
      Buffer : UInt8_Array (1 .. 2) := (Reg, Data);
      Status : I2C_Status;
   begin
      if Dev.Port = null then
         Dev.Last_Err := I2C_Error;
         return;
      end if;
      I2C_Write (Dev.Port.all, BMP280_Address, Buffer, Status);
      if Status /= I2C_OK then
         Dev.Last_Err := I2C_Error;
      end if;
   end Write_Reg;

   procedure Read_Regs
     (Dev   : in out BMP280_Device;
      Reg   : UInt8;
      Data  : out UInt8_Array;
      Count : UInt16)
   is
      Status : I2C_Status;
   begin
      if Dev.Port = null then
         Dev.Last_Err := I2C_Error;
         return;
      end if;
      I2C_Write_Read (Dev.Port.all, BMP280_Address,
                      (1 => Reg), 1, Data, Count, Status);
      if Status /= I2C_OK then
         Dev.Last_Err := I2C_Error;
      end if;
   end Read_Regs;

   function Initialize
     (Port : access I2C_Port'Class)
      return BMP280_Device
   is
      Dev : BMP280_Device;
      Chip_ID : UInt8 := 0;
      Calib_Raw : UInt8_Array (1 .. 26);
   begin
      Dev.Port := Port;
      Dev.T_Fine := 0;
      Dev.Valid := False;
      Dev.Last_Err := OK;

      -- Reset sensor
      Write_Reg (Dev, Reg_Reset, Reset_Value);

      -- Delay for reset (implementation-dependent)
      delay 0.002;

      -- Verify chip ID
      Read_Regs (Dev, Reg_ChipID, (1 => Chip_ID), 1);
      if Dev.Last_Err /= OK then
         return Dev;
      end if;

      if Chip_ID /= Chip_ID_Expected then
         Dev.Last_Err := Chip_ID_Mismatch;
         return Dev;
      end if;

      -- Read calibration data
      Read_Regs (Dev, Reg_Calib00, Calib_Raw, 26);
      if Dev.Last_Err /= OK then
         return Dev;
      end if;

      -- Parse calibration with explicit endianness
      Dev.Calib.Dig_T1 := UInt16 (Calib_Raw (1)) or
                          (UInt16 (Calib_Raw (2)) * 256);
      Dev.Calib.Dig_T2 := Int16 (UInt16 (Calib_Raw (3)) or
                          (UInt16 (Calib_Raw (4)) * 256));
      Dev.Calib.Dig_T3 := Int16 (UInt16 (Calib_Raw (5)) or
                          (UInt16 (Calib_Raw (6)) * 256));
      Dev.Calib.Dig_P1 := UInt16 (Calib_Raw (7)) or
                          (UInt16 (Calib_Raw (8)) * 256);
      Dev.Calib.Dig_P2 := Int16 (UInt16 (Calib_Raw (9)) or
                          (UInt16 (Calib_Raw (10)) * 256));
      Dev.Calib.Dig_P3 := Int16 (UInt16 (Calib_Raw (11)) or
                          (UInt16 (Calib_Raw (12)) * 256));
      Dev.Calib.Dig_P4 := Int16 (UInt16 (Calib_Raw (13)) or
                          (UInt16 (Calib_Raw (14)) * 256));
      Dev.Calib.Dig_P5 := Int16 (UInt16 (Calib_Raw (15)) or
                          (UInt16 (Calib_Raw (16)) * 256));
      Dev.Calib.Dig_P6 := Int16 (UInt16 (Calib_Raw (17)) or
                          (UInt16 (Calib_Raw (18)) * 256));
      Dev.Calib.Dig_P7 := Int16 (UInt16 (Calib_Raw (19)) or
                          (UInt16 (Calib_Raw (20)) * 256));
      Dev.Calib.Dig_P8 := Int16 (UInt16 (Calib_Raw (21)) or
                          (UInt16 (Calib_Raw (22)) * 256));
      Dev.Calib.Dig_P9 := Int16 (UInt16 (Calib_Raw (23)) or
                          (UInt16 (Calib_Raw (24)) * 256));

      Dev.Valid := True;
      return Dev;
   end Initialize;

   function Is_Valid (Dev : BMP280_Device) return Boolean is
   begin
      return Dev.Valid;
   end Is_Valid;

   function Get_Error (Dev : BMP280_Device) return BMP280_Error is
   begin
      return Dev.Last_Err;
   end Get_Error;

   procedure Configure
     (Dev      : in out BMP280_Device;
      OSRS_T   : Oversampling;
      OSRS_P   : Oversampling;
      Mode     : Power_Mode;
      Filter   : Filter_Coeff;
      Standby  : Standby_Time)
   is
      Ctrl : UInt8;
      Config : UInt8;
   begin
      -- ctrl_meas: osrs_t[7:5] | osrs_p[4:2] | mode[1:0]
      Ctrl := (UInt8 (OSRS_T) * 32) or
              (UInt8 (OSRS_P) * 4) or
              UInt8 (Mode);
      Write_Reg (Dev, Reg_CtrlMeas, Ctrl);

      -- config: t_sb[7:5] | filter[4:2] | spi3w_en[0]
      Config := (UInt8 (Standby) * 32) or
                (UInt8 (Filter) * 4);
      Write_Reg (Dev, Reg_Config, Config);
   end Configure;

   function Compensate_Temperature
     (Dev : in out BMP280_Device;
      ADC_Temp : Int32)
      return Int32
   is
      Var1 : Int32;
      Var2 : Int32;
   begin
      Var1 := ((((ADC_Temp / 8) - (Int32 (Dev.Calib.Dig_T1) * 2)) *
                Int32 (Dev.Calib.Dig_T2)) / 2048);

      Var2 := (((((ADC_Temp / 16) - Int32 (Dev.Calib.Dig_T1)) *
                 ((ADC_Temp / 16) - Int32 (Dev.Calib.Dig_T1))) / 4096) *
                Int32 (Dev.Calib.Dig_T3)) / 16384;

      Dev.T_Fine := Var1 + Var2;

      return (Dev.T_Fine * 5 + 128) / 256;
   end Compensate_Temperature;

   function Compensate_Pressure
     (Dev : BMP280_Device;
      ADC_Press : Int32)
      return UInt32
   is
      Var1 : Int64;
      Var2 : Int64;
      P    : Int64;
   begin
      Var1 := Int64 (Dev.T_Fine) - 128_000;
      Var2 := Var1 * Var1 * Int64 (Dev.Calib.Dig_P6);
      Var2 := Var2 + ((Var1 * Int64 (Dev.Calib.Dig_P5)) * 131072);
      Var2 := Var2 + (Int64 (Dev.Calib.Dig_P4) * 34359738368);
      Var1 := ((Var1 * Var1 * Int64 (Dev.Calib.Dig_P3)) / 256) +
              ((Var1 * Int64 (Dev.Calib.Dig_P2)) * 4096);
      Var1 := (((Int64 (1) * 140737488355328) + Var1) *
               Int64 (Dev.Calib.Dig_P1)) / 8589934592;

      if Var1 = 0 then
         return 0;
      end if;

      P := 1_048_576 - Int64 (ADC_Press);
      P := (((P * 2147483648) - Var2) * 3125) / Var1;
      Var1 := (Int64 (Dev.Calib.Dig_P9) * (P / 8192) * (P / 8192)) / 33554432;
      Var2 := (Int64 (Dev.Calib.Dig_P8) * P) / 524288;
      P := ((P + Var1 + Var2) / 256) + (Int64 (Dev.Calib.Dig_P7) * 16);

      return UInt32 (P / 256);
   end Compensate_Pressure;

   function Read_Sensor
     (Dev : in out BMP280_Device)
      return Sensor_Reading
   is
      Raw : UInt8_Array (1 .. 6);
      ADC_Press : Int32;
      ADC_Temp  : Int32;
      Result    : Sensor_Reading;
   begin
      Result.Temperature_Centidegrees := 0;
      Result.Pressure_Pa := 0;

      if not Dev.Valid then
         Dev.Last_Err := I2C_Error;
         return Result;
      end if;

      Read_Regs (Dev, Reg_PressMSB, Raw, 6);
      if Dev.Last_Err /= OK then
         return Result;
      end if;

      ADC_Press := (Int32 (Raw (1)) * 4096) +
                   (Int32 (Raw (2)) * 16) +
                   (Int32 (Raw (3)) / 16);
      ADC_Temp  := (Int32 (Raw (4)) * 4096) +
                   (Int32 (Raw (5)) * 16) +
                   (Int32 (Raw (6)) / 16);

      Result.Temperature_Centidegrees :=
        Compensate_Temperature (Dev, ADC_Temp);
      Result.Pressure_Pa :=
        Compensate_Pressure (Dev, ADC_Press);

      return Result;
   end Read_Sensor;

end BMP280;
```

```ada
-- i2c_driver.ads (minimal interface)
with System;

package I2C_Driver is

   type UInt8 is mod 2**8;
   type UInt16 is mod 2**16;
   type UInt32 is mod 2**32;
   type Int16 is range -2**15 .. 2**15 - 1;
   type Int32 is range -2**31 .. 2**31 - 1;
   type Int64 is range -2**63 .. 2**63 - 1;

   type I2C_Address is range 0 .. 127;

   type UInt8_Array is array (Positive range <>) of UInt8;

   type I2C_Status is (I2C_OK, I2C_Busy, I2C_NACK, I2C_Timeout);

   type I2C_Port is limited private;

   procedure I2C_Init
     (Port : out I2C_Port;
      Speed_Hz : UInt32);

   procedure I2C_Write
     (Port   : in out I2C_Port;
      Addr   : I2C_Address;
      Data   : UInt8_Array;
      Status : out I2C_Status);

   procedure I2C_Write_Read
     (Port     : in out I2C_Port;
      Addr     : I2C_Address;
      Tx_Data  : UInt8_Array;
      Tx_Len   : UInt16;
      Rx_Data  : out UInt8_Array;
      Rx_Len   : UInt16;
      Status   : out I2C_Status);

private

   type I2C_Port is record
      Initialized : Boolean := False;
   end record;

end I2C_Driver;
```

```ada
-- main.adb
with BMP280; use BMP280;
with I2C_Driver; use I2C_Driver;
with Text_IO; use Text_IO;

procedure Main is
   Port : I2C_Port;
   Dev  : BMP280_Device;
   Reading : Sensor_Reading;
begin
   -- Initialize I2C at 100 kHz
   I2C_Init (Port, 100_000);

   -- Initialize BMP280
   Dev := Initialize (Port'Access);
   if not Is_Valid (Dev) then
      Put_Line ("BMP280 initialization failed: " &
                BMP280_Error'Image (Get_Error (Dev)));
      return;
   end if;

   -- Configure: temp x1, pressure x16, normal mode, filter 16
   Configure (Dev, X1, X16, Normal, Coeff16, Ms0_5);

   -- Continuous reading loop
   loop
      Reading := Read_Sensor (Dev);

      if Get_Error (Dev) = OK then
         Put ("Temp: ");
         Put (Integer (Reading.Temperature_Centidegrees / 100));
         Put (".");
         declare
            Frac : Integer :=
              Integer (Reading.Temperature_Centidegrees rem 100);
         begin
            if Frac < 0 then
               Frac := -Frac;
            end if;
            if Frac < 10 then
               Put ("0");
            end if;
            Put (Frac);
         end;
         Put (" C  Pressure: ");
         Put (Integer (Reading.Pressure_Pa / 100));
         Put (".");
         Put (Integer (Reading.Pressure_Pa rem 100));
         Put_Line (" hPa");
      else
         Put_Line ("Read error: " &
                   BMP280_Error'Image (Get_Error (Dev)));
      end if;

      delay 1.0;
   end loop;
end Main;
```

### Zig: Comptime-Validated Register Map with Error Unions

```zig
// bmp280.zig
const std = @import("std");

/// I2C error types
pub const I2cError = error{
    Busy,
    NackAddr,
    NackData,
    Timeout,
    Arbitration,
};

/// I2C interface — platform implementations provide this
pub const I2cInterface = struct {
    ctx: *anyopaque,
    write: *const fn (ctx: *anyopaque, addr: u8, data: []const u8) I2cError!void,
    read: *const fn (ctx: *anyopaque, addr: u8, data: []u8) I2cError!void,
    writeRead: *const fn (ctx: *anyopaque, addr: u8, tx: []const u8, rx: []u8) I2cError!void,
};

/// BMP280 register map — validated at comptime
pub const Register = enum(u8) {
    chip_id = 0xD0,
    reset = 0xE0,
    status = 0xF3,
    ctrl_meas = 0xF4,
    config = 0xF5,
    press_msb = 0xF7,
    temp_msb = 0xFA,
    calib00 = 0x88,

    /// Verify register is in the valid BMP280 range
    pub fn isValid(reg: Register) bool {
        const v = @intFromEnum(reg);
        return (v >= 0x88 and v <= 0xA1) or  // calibration
               (v >= 0xD0 and v <= 0xF7);     // control/data
    }
};

/// Comptime assertion: all registers are valid
comptime {
    inline for (std.meta.tags(Register)) |reg| {
        std.debug.assert(Register.isValid(reg));
    }
}

const bmp280_addr: u8 = 0x76;
const chip_id_expected: u8 = 0x58;
const reset_value: u8 = 0xB6;

/// Calibration coefficients
pub const Calibration = struct {
    dig_t1: u16,
    dig_t2: i16,
    dig_t3: i16,
    dig_p1: u16,
    dig_p2: i16,
    dig_p3: i16,
    dig_p4: i16,
    dig_p5: i16,
    dig_p6: i16,
    dig_p7: i16,
    dig_p8: i16,
    dig_p9: i16,
};

/// Sensor reading
pub const SensorData = struct {
    temperature_c: f32,
    pressure_pa: f32,
};

/// Oversampling settings
pub const Oversampling = enum(u3) {
    skipped = 0,
    x1 = 1,
    x2 = 2,
    x4 = 3,
    x8 = 4,
    x16 = 5,
};

/// Power mode
pub const Mode = enum(u2) {
    sleep = 0,
    forced = 1,
    normal = 3,
};

/// Filter coefficient
pub const Filter = enum(u3) {
    off = 0,
    coeff2 = 1,
    coeff4 = 2,
    coeff8 = 3,
    coeff16 = 4,
};

/// Standby time
pub const StandbyTime = enum(u3) {
    ms0_5 = 0,
    ms62_5 = 1,
    ms125 = 2,
};

/// BMP280 error union
pub const Bmp280Error = I2cError || error{
    ChipIdMismatch,
    InvalidConfig,
};

/// BMP280 driver
pub const Bmp280 = struct {
    i2c: I2cInterface,
    calib: Calibration,
    t_fine: i32,

    pub fn init(i2c: I2cInterface) Bmp280Error!Bmp280 {
        var dev = Bmp280{
            .i2c = i2c,
            .calib = undefined,
            .t_fine = 0,
        };

        // Reset sensor
        try dev.writeReg(.reset, reset_value);

        // Delay for reset (caller should provide actual delay)
        var i: usize = 0;
        while (i < 80000) : (i += 1) {
            asm volatile ("nop");
        }

        // Verify chip ID
        const chip_id = try dev.readReg(.chip_id);
        if (chip_id != chip_id_expected) {
            return Bmp280Error.ChipIdMismatch;
        }

        // Read calibration data
        var calib_raw: [26]u8 = undefined;
        try dev.readRegs(.calib00, &calib_raw);

        dev.calib = Calibration{
            .dig_t1 = @as(u16, calib_raw[0]) | (@as(u16, calib_raw[1]) << 8),
            .dig_t2 = @bitCast(@as(u16, calib_raw[2]) | (@as(u16, calib_raw[3]) << 8)),
            .dig_t3 = @bitCast(@as(u16, calib_raw[4]) | (@as(u16, calib_raw[5]) << 8)),
            .dig_p1 = @as(u16, calib_raw[6]) | (@as(u16, calib_raw[7]) << 8),
            .dig_p2 = @bitCast(@as(u16, calib_raw[8]) | (@as(u16, calib_raw[9]) << 8)),
            .dig_p3 = @bitCast(@as(u16, calib_raw[10]) | (@as(u16, calib_raw[11]) << 8)),
            .dig_p4 = @bitCast(@as(u16, calib_raw[12]) | (@as(u16, calib_raw[13]) << 8)),
            .dig_p5 = @bitCast(@as(u16, calib_raw[14]) | (@as(u16, calib_raw[15]) << 8)),
            .dig_p6 = @bitCast(@as(u16, calib_raw[16]) | (@as(u16, calib_raw[17]) << 8)),
            .dig_p7 = @bitCast(@as(u16, calib_raw[18]) | (@as(u16, calib_raw[19]) << 8)),
            .dig_p8 = @bitCast(@as(u16, calib_raw[20]) | (@as(u16, calib_raw[21]) << 8)),
            .dig_p9 = @bitCast(@as(u16, calib_raw[22]) | (@as(u16, calib_raw[23]) << 8)),
        };

        return dev;
    }

    pub fn configure(
        self: *Bmp280,
        osrs_t: Oversampling,
        osrs_p: Oversampling,
        mode: Mode,
        filter: Filter,
        standby: StandbyTime,
    ) Bmp280Error!void {
        const ctrl_meas: u8 = (@as(u8, @intFromEnum(osrs_t)) << 5) |
                              (@as(u8, @intFromEnum(osrs_p)) << 2) |
                              @as(u8, @intFromEnum(mode));
        try self.writeReg(.ctrl_meas, ctrl_meas);

        const config: u8 = (@as(u8, @intFromEnum(standby)) << 5) |
                           (@as(u8, @intFromEnum(filter)) << 2);
        try self.writeReg(.config, config);
    }

    pub fn read(self: *Bmp280) Bmp280Error!SensorData {
        var raw: [6]u8 = undefined;
        try self.readRegs(.press_msb, &raw);

        const adc_p: i32 = (@as(i32, raw[0]) << 12) |
                           (@as(i32, raw[1]) << 4) |
                           (@as(i32, raw[2]) >> 4);
        const adc_t: i32 = (@as(i32, raw[3]) << 12) |
                           (@as(i32, raw[4]) << 4) |
                           (@as(i32, raw[5]) >> 4);

        const temp_x100 = self.compensateTemperature(adc_t);
        const pressure = self.compensatePressure(adc_p);

        return SensorData{
            .temperature_c = @as(f32, @floatFromInt(temp_x100)) / 100.0,
            .pressure_pa = @as(f32, @floatFromInt(pressure)) / 256.0,
        };
    }

    fn readReg(self: *Bmp280, reg: Register) Bmp280Error!u8 {
        var data: [1]u8 = undefined;
        try self.readRegs(reg, &data);
        return data[0];
    }

    fn readRegs(self: *Bmp280, reg: Register, data: []u8) Bmp280Error!void {
        const reg_byte: u8 = @intFromEnum(reg);
        self.i2c.writeRead(self.i2c.ctx, bmp280_addr, &.{reg_byte}, data) catch |err| {
            return switch (err) {
                I2cError.Busy => Bmp280Error.Busy,
                I2cError.NackAddr => Bmp280Error.NackAddr,
                I2cError.NackData => Bmp280Error.NackData,
                I2cError.Timeout => Bmp280Error.Timeout,
                I2cError.Arbitration => Bmp280Error.Arbitration,
            };
        };
    }

    fn writeReg(self: *Bmp280, reg: Register, value: u8) Bmp280Error!void {
        const reg_byte: u8 = @intFromEnum(reg);
        self.i2c.write(self.i2c.ctx, bmp280_addr, &.{ reg_byte, value }) catch |err| {
            return switch (err) {
                I2cError.Busy => Bmp280Error.Busy,
                I2cError.NackAddr => Bmp280Error.NackAddr,
                I2cError.NackData => Bmp280Error.NackData,
                I2cError.Timeout => Bmp280Error.Timeout,
                I2cError.Arbitration => Bmp280Error.Arbitration,
            };
        };
    }

    fn compensateTemperature(self: *Bmp280, adc: i32) i32 {
        const c = &self.calib;

        const var1: i32 = ((((adc >> 3) - (@as(i32, c.dig_t1) << 1)) *
                            @as(i32, c.dig_t2)) >> 11);

        const var2: i32 = (((((adc >> 4) - @as(i32, c.dig_t1)) *
                             ((adc >> 4) - @as(i32, c.dig_t1))) >> 12) *
                            @as(i32, c.dig_t3)) >> 14;

        self.t_fine = var1 + var2;

        return (self.t_fine * 5 + 128) >> 8;
    }

    fn compensatePressure(self: *const Bmp280, adc: i32) u32 {
        const c = &self.calib;

        var var1: i64 = @as(i64, self.t_fine) - 128000;
        var var2: i64 = var1 * var1 * @as(i64, c.dig_p6);
        var2 = var2 + ((var1 * @as(i64, c.dig_p5)) << 17);
        var2 = var2 + (@as(i64, c.dig_p4) << 35);
        var1 = ((var1 * var1 * @as(i64, c.dig_p3)) >> 8) +
               ((var1 * @as(i64, c.dig_p2)) << 12);
        var1 = ((((@as(i64, 1) << 47) + var1)) * @as(i64, c.dig_p1)) >> 33;

        if (var1 == 0) return 0;

        var p: i64 = 1048576 - @as(i64, adc);
        p = (((p << 31) - var2) * 3125) / var1;
        var1 = (@as(i64, c.dig_p9) * (p >> 13) * (p >> 13)) >> 25;
        var2 = (@as(i64, c.dig_p8) * p) >> 19;
        p = ((p + var1 + var2) >> 8) + (@as(i64, c.dig_p7) << 4);

        return @as(u32, @intCast(p >> 8));
    }
};
```

```zig
// main.zig
const std = @import("std");
const bmp280 = @import("bmp280.zig");

// Mock I2C implementation for demonstration
// In real code, this wraps your hardware peripheral
const MockI2c = struct {
    fn write(ctx: *anyopaque, addr: u8, data: []const u8) bmp280.I2cError!void {
        _ = ctx;
        _ = addr;
        _ = data;
        // Real implementation would write to I2C peripheral
    }

    fn read(ctx: *anyopaque, addr: u8, data: []u8) bmp280.I2cError!void {
        _ = ctx;
        _ = addr;
        _ = data;
        // Real implementation would read from I2C peripheral
    }

    fn writeRead(ctx: *anyopaque, addr: u8, tx: []const u8, rx: []u8) bmp280.I2cError!void {
        _ = ctx;
        _ = addr;
        _ = tx;
        _ = rx;
        // Real implementation would do write-then-read
    }

    fn interface() bmp280.I2cInterface {
        return bmp280.I2cInterface{
            .ctx = undefined,
            .write = write,
            .read = read,
            .writeRead = writeRead,
        };
    }
};

pub fn main() !void {
    const i2c = MockI2c.interface();

    var dev = try bmp280.Bmp280.init(i2c);

    try dev.configure(
        .x1,
        .x16,
        .normal,
        .coeff16,
        .ms0_5,
    );

    while (true) {
        const data = dev.read() catch |err| {
            std.debug.print("Read error: {}\n", .{err});
            continue;
        };

        std.debug.print("Temp: {d:.2} C  Pressure: {d:.2} hPa\n", .{
            data.temperature_c,
            data.pressure_pa / 100.0,
        });

        // Delay ~1 second
        var i: usize = 0;
        while (i < 400000) : (i += 1) {}
    }
}
```

## Build and Run Instructions

### C (ARM GCC)

```bash
# Install toolchain
sudo apt install gcc-arm-none-eabi gdb-multiarch

# Build
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -O2 \
    -fno-common -ffunction-sections -fdata-sections \
    -Wall -Wextra -Werror \
    -T stm32f405rg.ld \
    -o bmp280.elf \
    main.c i2c.c bmp280.c startup_stm32f405xx.c

# Generate binary
arm-none-eabi-objcopy -O binary bmp280.elf bmp280.bin
arm-none-eabi-size bmp280.elf
```

### Rust (embedded)

```bash
# Install toolchain
rustup target add thumbv7em-none-eabihf
cargo install flip-link

# Build
cargo build --release --target thumbv7em-none-eabihf

# Size
cargo bloat --release --target thumbv7em-none-eabihf
```

### Ada (GNAT ARM ELF)

```bash
# Install GNAT for ARM
sudo apt install gnat-arm-elf

# Build with gprbuild
gprbuild -P bmp280.gpr -XTARGET=arm-elf -O2

# Or compile manually
arm-eabi-gcc -c -O2 -gnatp bmp280.adb
arm-eabi-gcc -c -O2 -gnatp main.adb
arm-eabi-gnatbind main.ali
arm-eabi-gnatlink main.ali -o main.elf
```

### Zig

```bash
# Install Zig 0.11+
# https://ziglang.org/download/

# Build for bare-metal ARM
zig build-exe main.zig -target thumbv7em-freestanding-eabihf -OReleaseSmall

# Or build for host testing
zig build-exe main.zig -OReleaseFast
```

## Renode Verification

Create a Renode platform file (`bmp280.resc`):

```
# Create STM32F405 machine
mach create

# Add CPU
machine LoadPlatformDescription @platforms/cpus/stm32f4.resc

# Add BMP280 at I2C address 0x76
i2c.bmp280: Peripherals.BMP280 @ i2c2 0x76

# Start emulation
mach start

# Monitor I2C bus activity
showAnalyzer sysbus.i2c2

# Set logging for I2C transactions
logLevel 3 sysbus.i2c2
```

Run it:

```bash
renode bmp280.resc

# In the Renode monitor:
# Load your binary
sysbus LoadELF bmp280.elf

# Start execution
start

# Watch I2C transactions in the analyzer window
```

Expected output in the analyzer:
```
[0x00001234] I2C: START -> 0xEC (0x76+W) -> ACK -> 0xD0 -> ACK -> RESTART -> 0xED (0x76+R) -> ACK -> 0x58 -> NACK -> STOP
[0x00002345] I2C: START -> 0xEC (0x76+W) -> ACK -> 0x88 -> ACK -> RESTART -> 0xED (0x76+R) -> ACK -> [26 bytes] -> NACK -> STOP
[0x00003456] I2C: START -> 0xEC (0x76+W) -> ACK -> 0xF4 -> ACK -> 0xD5 -> ACK -> STOP
```

## What You Learned

- I2C protocol mechanics: start/stop conditions, ACK/NACK, repeated starts
- BMP280 register map and calibration data parsing
- Fixed-point compensation math for temperature and pressure
- Error handling patterns: C enums, Rust Result, Ada types, Zig error unions
- Using Renode to simulate I2C peripherals and verify transactions

## Next Steps

- Implement interrupt-driven I2C with DMA for zero-CPU transfers
- Add altitude calculation from pressure readings
- Port the driver to a different sensor (BME280 adds humidity)
- Implement I2C multi-master arbitration handling

## Language Comparison

| Feature | C | Rust | Ada | Zig |
|---------|---|------|-----|-----|
| **Error handling** | Enum return codes, manual checking | `Result<T, E>` with `?` operator | Typed error enum, explicit checks | Error unions with `catch`/`try` |
| **Register map** | `#define` constants, no validation | `mod` with `const` values | Strongly typed constants | `enum(u8)` with comptime validation |
| **Calibration struct** | Plain struct, no guarantees | Struct with private fields | Record with explicit types | Struct with `@bitCast` for signed |
| **I2C abstraction** | Raw pointer to registers | Generic `embedded_hal::I2c` trait | Abstract `I2C_Port` tagged type | Function pointer interface |
| **Fixed-point math** | Manual shifts, overflow-prone | Same as C but with `as` casts | Ada arithmetic with range safety | Explicit `@as()` and `@bitCast()` |
| **Type safety** | None — easy to mix up registers | Compile-time trait enforcement | Strong typing prevents misuse | Comptime catches invalid registers |
| **Memory safety** | Manual — easy to overflow buffers | Borrow checker prevents aliasing | Bounds-checked arrays | Explicit slices with length |
| **Binary size** | ~4KB (minimal) | ~6KB (with embedded-hal) | ~8KB (runtime overhead) | ~5KB (no runtime) |

## Deliverables

- [ ] Working I2C driver with start/stop/ACK handling
- [ ] BMP280 initialization with chip ID verification
- [ ] Calibration data parsing from sensor EEPROM
- [ ] Temperature compensation with fixed-point math
- [ ] Pressure compensation using t_fine from temperature
- [ ] Error handling for all I2C failure modes
- [ ] Renode simulation showing correct I2C transactions
- [ ] Output: temperature in °C and pressure in hPa

## References

### STMicroelectronics Documentation
- [STM32F4 Reference Manual (RM0090)](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-415-stm32f407-417-stm32f427-437-and-stm32f429-439-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) — Ch. 27: I2C (CR1, CR2, TIMINGR, OAR1), Ch. 8: GPIO (MODER, OTYPER open-drain, AF4 for I2C1)
- [STM32F405/407 Datasheet](https://www.st.com/resource/en/datasheet/stm32f405rg.pdf) — Pin multiplexing for I2C1 (PB6/PB7)

### ARM Documentation
- [Cortex-M4 Technical Reference Manual](https://developer.arm.com/documentation/ddi0439/latest/) — FPU usage for floating-point compensation math (FPv4-SP-D16)
- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/) — Memory ordering, DMB for I2C transaction synchronization

### Sensor Documentation
- [BMP280 Datasheet (Bosch BST-BMP280-DS001)](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf) — Register map, calibration coefficients, compensation formulas

### Tools & Emulation
- [QEMU STM32 Documentation](https://www.qemu.org/docs/master/system/arm/stm32.html) — I2C peripheral simulation limitations
- [Renode Documentation](https://docs.renode.io/) — BMP280 I2C sensor model, I2C bus analyzer
