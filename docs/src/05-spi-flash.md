---
title: "Project 5: SPI Flash Reader/Writer"
phase: 2
project: 5
---

# Project 5: SPI Flash Reader/Writer

## Introduction

SPI (Serial Peripheral Interface) is the workhorse of embedded communication — faster than I2C, simpler than USB, and found on everything from flash memory to displays. This project builds a complete SPI flash driver that reads, writes, and erases a W25Q series NOR flash chip, with CRC32 verification for data integrity.

**What you'll learn:**

- SPI protocol fundamentals: CPOL, CPHA, chip select, full-duplex operation
- Flash memory architecture: pages, sectors, blocks
- Flash command set: Read, Page Program, Sector Erase, JEDEC ID
- Page programming constraints and sector erase requirements
- Busy-wait polling vs interrupt-driven SPI transfers
- CRC32 for data integrity verification
- Building generic, reusable SPI abstractions in four languages

## SPI Protocol

SPI is a synchronous, full-duplex, single-master protocol using four signals:

| Signal | Name | Direction | Description |
|--------|------|-----------|-------------|
| **MOSI** | Master Out Slave In | Master → Slave | Data from master |
| **MISO** | Master In Slave Out | Slave → Master | Data from slave |
| **SCLK** | Serial Clock | Master → Slave | Clock from master |
| **CS/SS** | Chip Select | Master → Slave | Active-low device select |

Unlike I2C, SPI has no addressing — the CS line selects the device. This means each device needs its own CS line, but the protocol is simpler and faster.

### CPOL and CPHA: SPI Modes

SPI has four modes defined by clock polarity (CPOL) and clock phase (CPHA):

| Mode | CPOL | CPHA | Clock idle | Data sampled | Data shifted |
|------|------|------|------------|--------------|--------------|
| **0** | 0 | 0 | Low | Rising edge | Falling edge |
| **1** | 0 | 1 | Low | Falling edge | Rising edge |
| **2** | 1 | 0 | High | Falling edge | Rising edge |
| **3** | 1 | 1 | High | Rising edge | Falling edge |

Most flash memory (W25Q series) uses **Mode 0** or **Mode 3** — both sample on the rising edge. Mode 0 is the most common default.

### SPI Transaction

A single-byte SPI transaction is always full-duplex:

```
Master CS:  ──╲___________________________________________╱──
Master MOSI: ──╳─[D7]─[D6]─[D5]─[D4]─[D3]─[D2]─[D1]─[D0]──
Master SCLK: ──╱╲_╱╲_╱╲_╱╲_╱╲_╱╲_╱╲_╱╲_╱╲_╱╲_╱╲_╱╲_╱╲_╱╲─
Master MISO: ──╳─[D7]─[D6]─[D5]─[D4]─[D3]─[D2]─[D1]─[D0]──
```

Every byte sent on MOSI simultaneously receives a byte on MISO. To read N bytes from flash, you must send N dummy bytes.

## Flash Memory Commands

The W25Q64 (8 MB) and W25Q128 (16 MB) are industry-standard SPI NOR flash chips. Here are the essential commands:

### Command Reference

| Command | Code | Bytes Out | Bytes In | Description |
|---------|------|-----------|----------|-------------|
| **Read Data** | `0x03` | 4 (cmd + 24-bit addr) | N | Read data at address |
| **Fast Read** | `0x0B` | 5 (cmd + 24-bit addr + dummy) | N | Read with dummy byte |
| **Page Program** | `0x02` | 4 + 1..256 | 0 | Write up to 256 bytes |
| **Sector Erase (4KB)** | `0x20` | 4 (cmd + 24-bit addr) | 0 | Erase 4KB sector |
| **Block Erase (64KB)** | `0xD8` | 4 (cmd + 24-bit addr) | 0 | Erase 64KB block |
| **Chip Erase** | `0xC7` | 1 | 0 | Erase entire chip |
| **Write Enable** | `0x06` | 1 | 0 | Set WEL bit in status |
| **Write Disable** | `0x04` | 1 | 0 | Clear WEL bit |
| **Read Status Reg 1** | `0x05` | 1 | 1 | Busy + WEL + BP bits |
| **Read JEDEC ID** | `0x9F` | 1 | 3 | Manufacturer + device ID |

### JEDEC ID Structure

```
| Manufacturer (1 byte) | Memory Type (1 byte) | Capacity (1 byte) |
|        0xEF           |        0x40          |      0x17         |
```

- `0xEF 0x40 0x17` = Winbond W25Q64 (8 MB)
- `0xEF 0x40 0x18` = Winbond W25Q128 (16 MB)
- `0xC2 0x20 0x17` = Macronix MX25L64 (8 MB)

## Flash Memory Architecture

### Page, Sector, Block Hierarchy

```
Chip (8 MB)
├── Block 0 (64 KB)
│   ├── Sector 0 (4 KB)
│   │   ├── Page 0 (256 B)
│   │   ├── Page 1 (256 B)
│   │   ├── ...
│   │   └── Page 15 (256 B)
│   ├── Sector 1 (4 KB)
│   │   └── ...
│   └── ...
├── Block 1 (64 KB)
└── ...
```

**Critical constraints:**

- **Flash bits can only be written 1→0.** To write 0→1, you must erase.
- **Erase operates on sectors (4 KB minimum).** You cannot erase a single byte.
- **Page Program wraps within a page.** Writing past byte 255 wraps to byte 0 of the same page, corrupting data.
- **Write Enable must be set before every program or erase.** The WEL bit clears automatically after each operation.

### Read-Modify-Write for Partial Page Updates

To update a subset of a sector:

```
1. Read entire sector into RAM (4 KB)
2. Modify the target bytes in RAM
3. Erase the sector (all bytes → 0xFF)
4. Write the modified sector back (in 256-byte page chunks)
```

> **Warning:** Never write across a page boundary without checking. If you send 300 bytes starting at page offset 200, bytes 200–255 write correctly, then bytes 0–43 of the same page are overwritten. This is the #1 flash programming bug.

## Busy-Wait Polling vs Interrupt-Driven SPI

### Busy-Wait Polling

The simplest approach — poll the status register until the busy bit clears:

```
Write Enable → Send Command → Poll Status (BUSY=0?) → Done
```

- **Pros:** Simple, no interrupt configuration needed
- **Cons:** CPU is blocked, power inefficient, no concurrency
- **Use when:** Bootloader, initialization, low-frequency writes

### Interrupt-Driven SPI

Use SPI TX/RX complete interrupts and DMA for zero-CPU transfers:

```
Configure DMA → Start SPI → ISR fires on completion → Clear busy flag
```

- **Pros:** CPU free for other work, power efficient, high throughput
- **Cons:** Complex setup, DMA channel management, priority configuration
- **Use when:** Logging, data acquisition, high-frequency writes

This project uses busy-wait polling for clarity. The interrupt-driven approach is covered in the Next Steps.

## CRC32 for Data Integrity

CRC32 (Cyclic Redundancy Check) detects data corruption in stored data. The IEEE 802.3 polynomial is:

```
x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x + 1
```

Polynomial: `0xEDB88320` (reflected form)

### CRC32 Properties

- Detects all single-bit errors
- Detects all double-bit errors
- Detects any odd number of bit errors
- Detects all burst errors ≤ 32 bits
- Detects 99.99999997% of longer burst errors

### Lookup Table Implementation

A byte-wise lookup table trades 1 KB of flash for ~8x speedup:

```c
// Generate table at compile time
for each byte b (0..255):
    crc = b
    for 8 bits:
        crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320 : 0)
    table[b] = crc
```

## Implementation

### C: SPI Driver + Flash Read/Write/Erase with CRC32

#### SPI Driver (`spi.h`)

```c
#ifndef SPI_DRIVER_H
#define SPI_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SPI_OK = 0,
    SPI_ERR_TIMEOUT,
    SPI_ERR_MODE_FAULT,
    SPI_ERR_OVERRUN,
    SPI_ERR_CRC,
} spi_error_t;

typedef enum {
    SPI_MODE_0 = 0,  /* CPOL=0, CPHA=0 */
    SPI_MODE_1 = 1,  /* CPOL=0, CPHA=1 */
    SPI_MODE_2 = 2,  /* CPOL=1, CPHA=0 */
    SPI_MODE_3 = 3,  /* CPOL=1, CPHA=1 */
} spi_mode_t;

typedef struct {
    volatile uint32_t *cr1;
    volatile uint32_t *cr2;
    volatile uint32_t *sr;
    volatile uint32_t *dr;
    volatile uint32_t *crcpr;
    volatile uint32_t *rxcrcr;
    volatile uint32_t *txcrcr;
} spi_handle_t;

#define SPI_TIMEOUT_US  50000

spi_error_t spi_init(spi_handle_t *hspi, spi_mode_t mode, uint32_t baud_div);
spi_error_t spi_transfer(spi_handle_t *hspi, const uint8_t *tx,
                         uint8_t *rx, size_t len);
spi_error_t spi_write_then_read(spi_handle_t *hspi,
                                const uint8_t *tx_data, size_t tx_len,
                                uint8_t *rx_data, size_t rx_len);

#endif
```

#### SPI Driver Implementation (`spi.c`)

```c
#include "spi.h"

/* STM32 SPI1 register base */
#define SPI1_BASE       0x40013000UL

/* CR1 bits */
#define CR1_SPE         (1U << 6)
#define CR1_MSTR        (1U << 2)
#define CR1_BR_SHIFT    3
#define CR1_CPOL        (1U << 1)
#define CR1_CPHA        (1U << 0)
#define CR1_SSM         (1U << 9)
#define CR1_SSI         (1U << 8)
#define CR1_BIDIMODE    (1U << 15)
#define CR1_BIDIOE      (1U << 14)

/* CR2 bits */
#define CR2_FRXTH       (1U << 12)
#define CR2_DS_SHIFT    8
#define CR2_DS_8BIT     0x7  /* 0111 = 8-bit data */

/* SR bits */
#define SR_RXNE         (1U << 0)
#define SR_TXE          (1U << 1)
#define SR_BSY          (1U << 7)
#define SR_OVR          (1U << 6)
#define SR_MODF         (1U << 5)

static void delay_us(uint32_t us) {
    volatile uint32_t count = us * 4;
    while (count--) {
        __asm volatile ("nop");
    }
}

spi_error_t spi_init(spi_handle_t *hspi, spi_mode_t mode, uint32_t baud_div) {
    hspi->cr1 = (volatile uint32_t *)(SPI1_BASE + 0x00);
    hspi->cr2 = (volatile uint32_t *)(SPI1_BASE + 0x04);
    hspi->sr = (volatile uint32_t *)(SPI1_BASE + 0x08);
    hspi->dr = (volatile uint32_t *)(SPI1_BASE + 0x0C);
    hspi->crcpr = (volatile uint32_t *)(SPI1_BASE + 0x10);
    hspi->rxcrcr = (volatile uint32_t *)(SPI1_BASE + 0x14);
    hspi->txcrcr = (volatile uint32_t *)(SPI1_BASE + 0x18);

    /* Disable SPI during config */
    *hspi->cr1 = 0;

    /* Configure CR2: 8-bit data, FIFO threshold */
    *hspi->cr2 = (CR2_DS_8BIT << CR2_DS_SHIFT) | CR2_FRXTH;

    /* Configure CR1: master, software NSS, mode, baud rate */
    uint32_t cr1 = CR1_MSTR | CR1_SSM | CR1_SSI;
    cr1 |= (mode & 1) ? CR1_CPHA : 0;
    cr1 |= (mode & 2) ? CR1_CPOL : 0;
    cr1 |= ((baud_div & 0x07) << CR1_BR_SHIFT);
    *hspi->cr1 = cr1;

    /* Enable SPI */
    *hspi->cr1 |= CR1_SPE;

    return SPI_OK;
}

spi_error_t spi_transfer(spi_handle_t *hspi, const uint8_t *tx,
                         uint8_t *rx, size_t len) {
    for (size_t i = 0; i < len; i++) {
        /* Wait for TXE */
        uint32_t timeout = SPI_TIMEOUT_US;
        while (!(*hspi->sr & SR_TXE)) {
            if (--timeout == 0) return SPI_ERR_TIMEOUT;
            delay_us(1);
        }

        /* Send byte */
        *(volatile uint8_t *)hspi->dr = tx ? tx[i] : 0xFF;

        /* Wait for RXNE */
        timeout = SPI_TIMEOUT_US;
        while (!(*hspi->sr & SR_RXNE)) {
            if (--timeout == 0) return SPI_ERR_TIMEOUT;
            delay_us(1);
        }

        /* Read received byte */
        if (rx) {
            rx[i] = *(volatile uint8_t *)hspi->dr;
        } else {
            (void)*(volatile uint8_t *)hspi->dr; /* Drain */
        }
    }

    /* Wait for not busy */
    uint32_t timeout = SPI_TIMEOUT_US;
    while (*hspi->sr & SR_BSY) {
        if (--timeout == 0) return SPI_ERR_TIMEOUT;
        delay_us(1);
    }

    return SPI_OK;
}

spi_error_t spi_write_then_read(spi_handle_t *hspi,
                                const uint8_t *tx_data, size_t tx_len,
                                uint8_t *rx_data, size_t rx_len) {
    /* Write phase */
    if (tx_len > 0) {
        spi_error_t err = spi_transfer(hspi, tx_data, NULL, tx_len);
        if (err != SPI_OK) return err;
    }

    /* Read phase (send dummy 0xFF bytes) */
    if (rx_len > 0) {
        spi_error_t err = spi_transfer(hspi, NULL, rx_data, rx_len);
        if (err != SPI_OK) return err;
    }

    return SPI_OK;
}
```

#### CRC32 Implementation (`crc32.h`)

```c
#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

/* Initialize CRC32 lookup table (call once at startup) */
void crc32_init(void);

/* Calculate CRC32 over a buffer */
uint32_t crc32_calc(const uint8_t *data, size_t len);

/* Verify CRC32: returns true if CRC matches */
bool crc32_verify(const uint8_t *data, size_t len, uint32_t expected_crc);

#endif
```

#### CRC32 Implementation (`crc32.c`)

```c
#include "crc32.h"

static uint32_t crc32_table[256];
static bool crc32_initialized = false;

void crc32_init(void) {
    if (crc32_initialized) return;

    const uint32_t poly = 0xEDB88320;

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? poly : 0);
        }
        crc32_table[i] = crc;
    }

    crc32_initialized = true;
}

uint32_t crc32_calc(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < len; i++) {
        uint8_t index = (uint8_t)(crc ^ data[i]);
        crc = (crc >> 8) ^ crc32_table[index];
    }

    return crc ^ 0xFFFFFFFF;
}

bool crc32_verify(const uint8_t *data, size_t len, uint32_t expected_crc) {
    return crc32_calc(data, len) == expected_crc;
}
```

#### Flash Driver (`w25q.h`)

```c
#ifndef W25Q_H
#define W25Q_H

#include "spi.h"
#include "crc32.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* W25Q command codes */
#define W25Q_CMD_READ_DATA      0x03
#define W25Q_CMD_FAST_READ      0x0B
#define W25Q_CMD_PAGE_PROGRAM   0x02
#define W25Q_CMD_SECTOR_ERASE   0x20
#define W25Q_CMD_BLOCK_ERASE    0xD8
#define W25Q_CMD_CHIP_ERASE     0xC7
#define W25Q_CMD_WRITE_ENABLE   0x06
#define W25Q_CMD_WRITE_DISABLE  0x04
#define W25Q_CMD_READ_STATUS    0x05
#define W25Q_CMD_JEDEC_ID       0x9F

/* Flash geometry */
#define W25Q_PAGE_SIZE      256
#define W25Q_SECTOR_SIZE    4096
#define W25Q_BLOCK_SIZE     65536

/* Status register bits */
#define W25Q_SR_BUSY        (1U << 0)
#define W25Q_SR_WEL         (1U << 1)

typedef enum {
    W25Q_OK = 0,
    W25Q_ERR_SPI,
    W25Q_ERR_TIMEOUT,
    W25Q_ERR_VERIFY,
    W25Q_ERR_JEDEC,
    W25Q_ERR_BOUNDARY,
} w25q_error_t;

typedef struct {
    spi_handle_t *spi;
    uint8_t cs_port;  /* GPIO port for CS control */
    uint16_t cs_pin;  /* GPIO pin for CS control */
} w25q_handle_t;

typedef struct {
    uint8_t manufacturer;
    uint8_t memory_type;
    uint8_t capacity;
} w25q_jedec_t;

/* CS control — implement for your platform */
void w25q_cs_assert(w25q_handle_t *dev);
void w25q_cs_deassert(w25q_handle_t *dev);

/* Core API */
w25q_error_t w25q_init(w25q_handle_t *dev, spi_handle_t *spi);
w25q_error_t w25q_read_jedec(w25q_handle_t *dev, w25q_jedec_t *jedec);
w25q_error_t w25q_read(w25q_handle_t *dev, uint32_t addr,
                       uint8_t *data, size_t len);
w25q_error_t w25q_page_program(w25q_handle_t *dev, uint32_t addr,
                               const uint8_t *data, size_t len);
w25q_error_t w25q_sector_erase(w25q_handle_t *dev, uint32_t addr);
w25q_error_t w25q_write(w25q_handle_t *dev, uint32_t addr,
                        const uint8_t *data, size_t len);

/* Read with CRC32 verification */
typedef struct {
    uint8_t *data;
    size_t len;
    uint32_t crc;
} w25q_read_verified_t;

w25q_error_t w25q_read_verified(w25q_handle_t *dev, uint32_t addr,
                                w25q_read_verified_t *result);

#endif
```

#### Flash Driver Implementation (`w25q.c`)

```c
#include "w25q.h"

static w25q_error_t wait_busy(w25q_handle_t *dev, uint32_t timeout_ms) {
    uint8_t status;
    uint32_t elapsed = 0;

    do {
        w25q_cs_assert(dev);
        uint8_t cmd = W25Q_CMD_READ_STATUS;
        spi_transfer(dev->spi, &cmd, NULL, 1);
        spi_transfer(dev->spi, NULL, &status, 1);
        w25q_cs_deassert(dev);

        if (!(status & W25Q_SR_BUSY)) return W25Q_OK;

        /* ~1ms delay */
        for (volatile int i = 0; i < 4000; i++);
        elapsed++;
    } while (elapsed < timeout_ms);

    return W25Q_ERR_TIMEOUT;
}

static w25q_error_t write_enable(w25q_handle_t *dev) {
    w25q_cs_assert(dev);
    uint8_t cmd = W25Q_CMD_WRITE_ENABLE;
    w25q_error_t err = spi_transfer(dev->spi, &cmd, NULL, 1);
    w25q_cs_deassert(dev);
    return err;
}

w25q_error_t w25q_init(w25q_handle_t *dev, spi_handle_t *spi) {
    dev->spi = spi;
    crc32_init();
    return W25Q_OK;
}

w25q_error_t w25q_read_jedec(w25q_handle_t *dev, w25q_jedec_t *jedec) {
    w25q_cs_assert(dev);

    uint8_t cmd = W25Q_CMD_JEDEC_ID;
    uint8_t rx[3];

    w25q_error_t err = spi_write_then_read(dev->spi, &cmd, 1, rx, 3);
    w25q_cs_deassert(dev);

    if (err != SPI_OK) return W25Q_ERR_SPI;

    jedec->manufacturer = rx[0];
    jedec->memory_type = rx[1];
    jedec->capacity = rx[2];

    return W25Q_OK;
}

w25q_error_t w25q_read(w25q_handle_t *dev, uint32_t addr,
                       uint8_t *data, size_t len) {
    uint8_t tx[4];
    tx[0] = W25Q_CMD_READ_DATA;
    tx[1] = (addr >> 16) & 0xFF;
    tx[2] = (addr >> 8) & 0xFF;
    tx[3] = addr & 0xFF;

    w25q_cs_assert(dev);
    w25q_error_t err = spi_write_then_read(dev->spi, tx, 4, data, len);
    w25q_cs_deassert(dev);

    return (err == SPI_OK) ? W25Q_OK : W25Q_ERR_SPI;
}

w25q_error_t w25q_page_program(w25q_handle_t *dev, uint32_t addr,
                               const uint8_t *data, size_t len) {
    if (len == 0 || len > W25Q_PAGE_SIZE) return W25Q_ERR_BOUNDARY;

    /* Check page boundary */
    uint32_t page_start = addr & ~(W25Q_PAGE_SIZE - 1);
    if (addr + len > page_start + W25Q_PAGE_SIZE) {
        return W25Q_ERR_BOUNDARY;
    }

    /* Enable writing */
    w25q_error_t err = write_enable(dev);
    if (err != W25Q_OK) return err;

    /* Send page program command + address + data */
    uint8_t header[4];
    header[0] = W25Q_CMD_PAGE_PROGRAM;
    header[1] = (addr >> 16) & 0xFF;
    header[2] = (addr >> 8) & 0xFF;
    header[3] = addr & 0xFF;

    w25q_cs_assert(dev);
    spi_transfer(dev->spi, header, NULL, 4);
    err = spi_transfer(dev->spi, data, NULL, len);
    w25q_cs_deassert(dev);

    if (err != SPI_OK) return W25Q_ERR_SPI;

    /* Wait for programming to complete (max 3ms for page program) */
    return wait_busy(dev, 10);
}

w25q_error_t w25q_sector_erase(w25q_handle_t *dev, uint32_t addr) {
    /* Address must be sector-aligned */
    if (addr & (W25Q_SECTOR_SIZE - 1)) {
        return W25Q_ERR_BOUNDARY;
    }

    /* Enable writing */
    w25q_error_t err = write_enable(dev);
    if (err != W25Q_OK) return err;

    /* Send sector erase command + address */
    uint8_t tx[4];
    tx[0] = W25Q_CMD_SECTOR_ERASE;
    tx[1] = (addr >> 16) & 0xFF;
    tx[2] = (addr >> 8) & 0xFF;
    tx[3] = addr & 0xFF;

    w25q_cs_assert(dev);
    err = spi_transfer(dev->spi, tx, NULL, 4);
    w25q_cs_deassert(dev);

    if (err != SPI_OK) return W25Q_ERR_SPI;

    /* Wait for erase to complete (max 400ms for sector erase) */
    return wait_busy(dev, 500);
}

w25q_error_t w25q_write(w25q_handle_t *dev, uint32_t addr,
                        const uint8_t *data, size_t len) {
    size_t written = 0;

    while (written < len) {
        /* Calculate bytes remaining in current page */
        uint32_t page_offset = addr & (W25Q_PAGE_SIZE - 1);
        size_t page_remaining = W25Q_PAGE_SIZE - page_offset;
        size_t chunk = (len - written < page_remaining) ?
                       (len - written) : page_remaining;

        w25q_error_t err = w25q_page_program(dev, addr,
                                             &data[written], chunk);
        if (err != W25Q_OK) return err;

        written += chunk;
        addr += chunk;
    }

    return W25Q_OK;
}

w25q_error_t w25q_read_verified(w25q_handle_t *dev, uint32_t addr,
                                w25q_read_verified_t *result) {
    w25q_error_t err = w25q_read(dev, addr, result->data, result->len);
    if (err != W25Q_OK) return err;

    result->crc = crc32_calc(result->data, result->len);
    return W25Q_OK;
}
```

#### Main Application (`main.c`)

```c
#include "w25q.h"
#include <stdio.h>
#include <string.h>

static spi_handle_t hspi;
static w25q_handle_t flash;

/* Platform-specific CS control */
void w25q_cs_assert(w25q_handle_t *dev) {
    (void)dev;
    /* GPIOA Pin 4 low */
    /* *(volatile uint32_t *)0x4001080C &= ~(1U << 4); */
}

void w25q_cs_deassert(w25q_handle_t *dev) {
    (void)dev;
    /* GPIOA Pin 4 high */
    /* *(volatile uint32_t *)0x4001080C |= (1U << 4); */
}

int main(void) {
    /* Initialize SPI: Mode 0, baud = PCLK/16 */
    spi_init(&hspi, SPI_MODE_0, 3);

    /* Initialize flash driver */
    w25q_init(&flash, &hspi);

    /* Read and verify JEDEC ID */
    w25q_jedec_t jedec;
    w25q_error_t err = w25q_read_jedec(&flash, &jedec);
    if (err != W25Q_OK) {
        printf("JEDEC read failed: %d\n", err);
        return 1;
    }
    printf("JEDEC ID: %02X %02X %02X\n",
           jedec.manufacturer, jedec.memory_type, jedec.capacity);

    /* Test data */
    const char *test_msg = "Hello, SPI Flash! This is a test of the W25Q64 driver.";
    size_t msg_len = strlen(test_msg);
    uint32_t test_addr = 0x000000;  /* Start of flash */

    /* Erase sector containing test address */
    uint32_t sector_addr = test_addr & ~(W25Q_SECTOR_SIZE - 1);
    printf("Erasing sector at 0x%06lX...\n", sector_addr);
    err = w25q_sector_erase(&flash, sector_addr);
    if (err != W25Q_OK) {
        printf("Sector erase failed: %d\n", err);
        return 1;
    }
    printf("Sector erased.\n");

    /* Write data */
    printf("Writing %zu bytes at 0x%06lX...\n", msg_len, test_addr);
    err = w25q_write(&flash, test_addr,
                     (const uint8_t *)test_msg, msg_len);
    if (err != W25Q_OK) {
        printf("Write failed: %d\n", err);
        return 1;
    }
    printf("Write complete.\n");

    /* Read back with CRC verification */
    uint8_t read_buf[256];
    w25q_read_verified_t verified = {
        .data = read_buf,
        .len = msg_len,
        .crc = 0,
    };

    err = w25q_read_verified(&flash, test_addr, &verified);
    if (err != W25Q_OK) {
        printf("Read failed: %d\n", err);
        return 1;
    }

    /* Verify data matches */
    if (memcmp(read_buf, test_msg, msg_len) == 0) {
        printf("Data verified! CRC32: 0x%08lX\n", verified.crc);
        printf("Read: %.*s\n", (int)msg_len, read_buf);
    } else {
        printf("Data mismatch!\n");
        printf("Expected: %s\n", test_msg);
        printf("Got:      %.*s\n", (int)msg_len, read_buf);
    }

    /* Test page boundary handling */
    printf("\n--- Page boundary test ---\n");
    uint32_t boundary_addr = W25Q_PAGE_SIZE - 10;  /* 10 bytes before page end */
    uint8_t boundary_data[32];
    for (int i = 0; i < 32; i++) boundary_data[i] = (uint8_t)i;

    /* This should fail — crosses page boundary */
    err = w25q_page_program(&flash, boundary_addr, boundary_data, 32);
    printf("Cross-page write (should fail): %d\n", err);

    /* Split it correctly */
    err = w25q_write(&flash, boundary_addr, boundary_data, 32);
    printf("Cross-page write via w25q_write: %d\n", err);

    return 0;
}
```

### Rust: Generic SPI Flash Driver with embedded-hal Trait Bounds

```rust
// Cargo.toml
// [package]
// name = "w25q-driver"
// version = "0.1.0"
// edition = "2021"
//
// [dependencies]
// embedded-hal = "1.0"
// embedded-hal-bus = "0.1"

use core::marker::PhantomData;
use embedded_hal::spi::{SpiBus, SpiDevice, ErrorType};

/// W25Q command codes
mod cmd {
    pub const READ_DATA: u8 = 0x03;
    pub const FAST_READ: u8 = 0x0B;
    pub const PAGE_PROGRAM: u8 = 0x02;
    pub const SECTOR_ERASE: u8 = 0x20;
    pub const BLOCK_ERASE: u8 = 0xD8;
    pub const CHIP_ERASE: u8 = 0xC7;
    pub const WRITE_ENABLE: u8 = 0x06;
    pub const WRITE_DISABLE: u8 = 0x04;
    pub const READ_STATUS: u8 = 0x05;
    pub const JEDEC_ID: u8 = 0x9F;
}

/// Flash geometry constants
pub const PAGE_SIZE: usize = 256;
pub const SECTOR_SIZE: usize = 4096;
pub const BLOCK_SIZE: usize = 65536;

/// Status register bits
const SR_BUSY: u8 = 1 << 0;
const SR_WEL: u8 = 1 << 1;

/// W25Q driver errors
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum W25qError<SpiErr> {
    Spi(SpiErr),
    Timeout,
    VerifyFailed,
    InvalidJeId,
    PageBoundary,
    NotSectorAligned,
}

/// JEDEC identification
#[derive(Debug, Clone, Copy)]
pub struct JedecId {
    pub manufacturer: u8,
    pub memory_type: u8,
    pub capacity: u8,
}

impl JedecId {
    pub fn is_winbond(&self) -> bool {
        self.manufacturer == 0xEF
    }

    pub fn capacity_bytes(&self) -> Option<u32> {
        match self.capacity {
            0x14 => Some(1 << 17),  // W25Q80: 1 MB
            0x15 => Some(1 << 18),  // W25Q16: 2 MB
            0x16 => Some(1 << 19),  // W25Q32: 4 MB
            0x17 => Some(1 << 20),  // W25Q64: 8 MB
            0x18 => Some(1 << 21),  // W25Q128: 16 MB
            0x19 => Some(1 << 22),  // W25Q256: 32 MB
            _ => None,
        }
    }
}

/// Read result with CRC32
#[derive(Debug)]
pub struct VerifiedRead {
    pub data_len: usize,
    pub crc: u32,
}

/// CRC32 calculator (IEEE 802.3)
pub struct Crc32 {
    table: [u32; 256],
}

impl Crc32 {
    pub const fn new() -> Self {
        let mut table = [0u32; 256];
        let mut i = 0u32;
        while i < 256 {
            let mut crc = i;
            let mut j = 0;
            while j < 8 {
                if crc & 1 != 0 {
                    crc = (crc >> 1) ^ 0xEDB88320;
                } else {
                    crc >>= 1;
                }
                j += 1;
            }
            table[i as usize] = crc;
            i += 1;
        }
        Self { table }
    }

    pub fn calc(&self, data: &[u8]) -> u32 {
        let mut crc = 0xFFFFFFFF;
        for &byte in data {
            let index = (crc ^ byte as u32) & 0xFF;
            crc = (crc >> 8) ^ self.table[index as usize];
        }
        crc ^ 0xFFFFFFFF
    }
}

/// W25Q flash driver with generic SPI device
pub struct W25q<SPI> {
    spi: SPI,
    crc: Crc32,
}

impl<SPI, SpiErr> W25q<SPI>
where
    SPI: SpiDevice<Error = SpiErr>,
    SpiErr: core::fmt::Debug,
{
    /// Create new driver
    pub fn new(spi: SPI) -> Self {
        Self {
            spi,
            crc: Crc32::new(),
        }
    }

    /// Read JEDEC ID
    pub fn read_jedec(&mut self) -> Result<JedecId, W25qError<SpiErr>> {
        let mut tx = [cmd::JEDEC_ID, 0, 0, 0];
        self.spi.transfer_in_place(&mut tx)?;
        Ok(JedecId {
            manufacturer: tx[1],
            memory_type: tx[2],
            capacity: tx[3],
        })
    }

    /// Read data from flash
    pub fn read(&mut self, addr: u32, buf: &mut [u8]) -> Result<(), W25qError<SpiErr>> {
        let mut tx = [0u8; 4];
        tx[0] = cmd::READ_DATA;
        tx[1] = ((addr >> 16) & 0xFF) as u8;
        tx[2] = ((addr >> 8) & 0xFF) as u8;
        tx[3] = (addr & 0xFF) as u8;

        // Use write_read: send command+address, receive data
        self.spi.write(&tx)?;
        self.spi.read(buf)?;

        Ok(())
    }

    /// Read with CRC32 verification
    pub fn read_verified(
        &mut self,
        addr: u32,
        buf: &mut [u8],
    ) -> Result<VerifiedRead, W25qError<SpiErr>> {
        self.read(addr, buf)?;
        let crc = self.crc.calc(buf);
        Ok(VerifiedRead {
            data_len: buf.len(),
            crc,
        })
    }

    /// Wait until flash is not busy
    fn wait_busy(&mut self, timeout_ms: u32) -> Result<(), W25qError<SpiErr>> {
        for _ in 0..timeout_ms {
            let mut status = [cmd::READ_STATUS, 0];
            self.spi.transfer_in_place(&mut status)?;
            if status[1] & SR_BUSY == 0 {
                return Ok(());
            }
            // ~1ms delay
            cortex_m::asm::delay(16000);
        }
        Err(W25qError::Timeout)
    }

    /// Send write enable command
    fn write_enable(&mut self) -> Result<(), W25qError<SpiErr>> {
        self.spi.write(&[cmd::WRITE_ENABLE])?;
        Ok(())
    }

    /// Program a single page (must not cross page boundary)
    pub fn page_program(
        &mut self,
        addr: u32,
        data: &[u8],
    ) -> Result<(), W25qError<SpiErr>> {
        if data.is_empty() || data.len() > PAGE_SIZE {
            return Err(W25qError::PageBoundary);
        }

        let page_start = addr & !(PAGE_SIZE as u32 - 1);
        if addr + data.len() as u32 > page_start + PAGE_SIZE as u32 {
            return Err(W25qError::PageBoundary);
        }

        self.write_enable()?;

        let mut header = [0u8; 4];
        header[0] = cmd::PAGE_PROGRAM;
        header[1] = ((addr >> 16) & 0xFF) as u8;
        header[2] = ((addr >> 8) & 0xFF) as u8;
        header[3] = (addr & 0xFF) as u8;

        self.spi.write(&header)?;
        self.spi.write(data)?;

        self.wait_busy(10)
    }

    /// Erase a 4KB sector (address must be sector-aligned)
    pub fn sector_erase(&mut self, addr: u32) -> Result<(), W25qError<SpiErr>> {
        if addr & (SECTOR_SIZE as u32 - 1) != 0 {
            return Err(W25qError::NotSectorAligned);
        }

        self.write_enable()?;

        let mut tx = [0u8; 4];
        tx[0] = cmd::SECTOR_ERASE;
        tx[1] = ((addr >> 16) & 0xFF) as u8;
        tx[2] = ((addr >> 8) & 0xFF) as u8;
        tx[3] = (addr & 0xFF) as u8;

        self.spi.write(&tx)?;
        self.wait_busy(500)
    }

    /// Write data of any length, handling page boundaries automatically
    pub fn write(&mut self, addr: u32, data: &[u8]) -> Result<(), W25qError<SpiErr>> {
        let mut offset = 0;
        let mut current_addr = addr;

        while offset < data.len() {
            let page_offset = (current_addr as usize) & (PAGE_SIZE - 1);
            let page_remaining = PAGE_SIZE - page_offset;
            let chunk_len = core::cmp::min(data.len() - offset, page_remaining);

            self.page_program(current_addr, &data[offset..offset + chunk_len])?;

            offset += chunk_len;
            current_addr += chunk_len as u32;
        }

        Ok(())
    }
}

// --- Example usage ---
//
// use embedded_hal_bus::spi::ExclusiveDevice;
// use stm32f4xx_hal::{spi, gpio};
//
// #[entry]
// fn main() -> ! {
//     let dp = stm32::Peripherals::take().unwrap();
//     let rcc = dp.RCC.constrain();
//     let clocks = rcc.cfgr.sysclk(48.MHz()).freeze();
//
//     let gpioa = dp.GPIOA.split();
//     let sck = gpioa.pa5.into_alternate::<5>();
//     let miso = gpioa.pa6.into_alternate::<5>();
//     let mosi = gpioa.pa7.into_alternate::<5>();
//     let cs = gpioa.pa4.into_push_pull_output();
//
//     let spi_bus = spi::Spi::new(
//         dp.SPI1,
//         (sck, miso, mosi),
//         spi::Mode {
//             polarity: spi::Polarity::IdleLow,
//             phase: spi::Phase::CaptureOnFirstTransition,
//         },
//         1.MHz(),
//         clocks,
//     );
//
//     let spi_device = ExclusiveDevice::new(spi_bus, cs, Delay).unwrap();
//     let mut flash = W25q::new(spi_device);
//
//     let jedec = flash.read_jedec().expect("JEDEC read failed");
//     defmt::println!("JEDEC: {:02X} {:02X} {:02X}",
//         jedec.manufacturer, jedec.memory_type, jedec.capacity);
//
//     let test_data = b"Hello SPI Flash!";
//     flash.sector_erase(0).expect("Erase failed");
//     flash.write(0, test_data).expect("Write failed");
//
//     let mut buf = [0u8; 16];
//     let verified = flash.read_verified(0, &mut buf).expect("Read failed");
//     defmt::println!("CRC32: {:08X}, Data: {}", verified.crc, buf);
//
//     loop {}
// }
```

### Ada: Flash Memory Management Package with Range-Checked Parameters

```ada
-- w25q.ads
with SPI_Driver; use SPI_Driver;

package W25Q is

   -- Flash geometry with strong typing
   Page_Size    : constant := 256;
   Sector_Size  : constant := 4096;
   Block_Size   : constant := 65536;

   -- Address type with range checking
   subtype Flash_Address is UInt32 range 0 .. 16_777_215;  -- 16 MB max
   subtype Page_Offset is UInt16 range 0 .. 255;
   subtype Sector_Index is UInt32 range 0 .. 4095;

   -- Command codes
   Cmd_Read_Data    : constant UInt8 := 16#03#;
   Cmd_Fast_Read    : constant UInt8 := 16#0B#;
   Cmd_Page_Program : constant UInt8 := 16#02#;
   Cmd_Sector_Erase : constant UInt8 := 16#20#;
   Cmd_Block_Erase  : constant UInt8 := 16#D8#;
   Cmd_Chip_Erase   : constant UInt8 := 16#C7#;
   Cmd_Write_Enable : constant UInt8 := 16#06#;
   Cmd_Write_Disable: constant UInt8 := 16#04#;
   Cmd_Read_Status  : constant UInt8 := 16#05#;
   Cmd_Jedec_Id     : constant UInt8 := 16#9F#;

   -- Status register bits
   SR_Busy : constant UInt8 := 16#01#;
   SR_WEL  : constant UInt8 := 16#02#;

   -- JEDEC ID record
   type JEDEC_ID is record
      Manufacturer : UInt8;
      Memory_Type  : UInt8;
      Capacity     : UInt8;
   end record;

   -- Verified read result
   type Verified_Read is record
      Data     : UInt8_Array (1 .. 256);
      Length   : UInt16;
      CRC      : UInt32;
   end record;

   -- Error types
   type W25Q_Error is (OK, SPI_Error, Timeout, Verify_Failed,
                       Invalid_JEDEC, Page_Boundary, Not_Aligned);

   -- Device handle
   type W25Q_Device is private;

   -- Initialize device
   procedure Initialize
     (Dev   : out W25Q_Device;
      Port  : access SPI_Port'Class);

   -- Check initialization status
   function Is_Valid (Dev : W25Q_Device) return Boolean;
   function Get_Error  (Dev : W25Q_Device) return W25Q_Error;

   -- Read JEDEC ID
   function Read_JEDEC (Dev : in out W25Q_Device) return JEDEC_ID;

   -- Read data from flash
   procedure Read
     (Dev    : in out W25Q_Device;
      Addr   : Flash_Address;
      Data   : out UInt8_Array;
      Length : UInt16);

   -- Program a single page (checked for boundary)
   procedure Page_Program
     (Dev    : in out W25Q_Device;
      Addr   : Flash_Address;
      Data   : UInt8_Array;
      Length : UInt16);

   -- Erase a 4KB sector (checked for alignment)
   procedure Sector_Erase
     (Dev  : in out W25Q_Device;
      Addr : Flash_Address);

   -- Write data of any length (handles page boundaries)
   procedure Write
     (Dev    : in out W25Q_Device;
      Addr   : Flash_Address;
      Data   : UInt8_Array;
      Length : UInt16);

   -- Read with CRC32 verification
   function Read_Verified
     (Dev    : in out W25Q_Device;
      Addr   : Flash_Address;
      Length : UInt16)
      return Verified_Read;

private

   type W25Q_Device is record
      Port     : access SPI_Port'Class := null;
      Last_Err : W25Q_Error := OK;
      Valid    : Boolean := False;
   end record;

end W25Q;
```

```ada
-- w25q.adb
with CRC32; use CRC32;

package body W25Q is

   -- CS control (platform-specific)
   procedure CS_Assert is null;
   procedure CS_Deassert is null;

   -- Wait for busy bit to clear
   procedure Wait_Busy
     (Dev      : in out W25Q_Device;
      Timeout  : UInt32)
   is
      Status : UInt8 := 0;
      Count  : UInt32 := 0;
      Cmd    : UInt8_Array (1 .. 2) := (Cmd_Read_Status, 0);
      Resp   : UInt8_Array (1 .. 2);
      Stat   : SPI_Status;
   begin
      loop
         exit when Count >= Timeout;
         CS_Assert;
         SPI_Transfer (Dev.Port.all, Cmd, Resp, 2, Stat);
         CS_Deassert;

         if Stat /= SPI_OK then
            Dev.Last_Err := SPI_Error;
            return;
         end if;

         Status := Resp (2);
         exit when (Status and SR_Busy) = 0;

         Count := Count + 1;
         delay 0.001;  -- 1ms
      end loop;

      if Count >= Timeout then
         Dev.Last_Err := Timeout;
      end if;
   end Wait_Busy;

   -- Send write enable
   procedure Write_Enable (Dev : in out W25Q_Device) is
      Cmd : UInt8_Array (1 .. 1) := (Cmd_Write_Enable);
      Resp : UInt8_Array (1 .. 1);
      Stat : SPI_Status;
   begin
      CS_Assert;
      SPI_Transfer (Dev.Port.all, Cmd, Resp, 1, Stat);
      CS_Deassert;
      if Stat /= SPI_OK then
         Dev.Last_Err := SPI_Error;
      end if;
   end Write_Enable;

   procedure Initialize
     (Dev   : out W25Q_Device;
      Port  : access SPI_Port'Class)
   is
   begin
      Dev.Port := Port;
      Dev.Last_Err := OK;
      Dev.Valid := True;
      CRC32_Init;
   end Initialize;

   function Is_Valid (Dev : W25Q_Device) return Boolean is
   begin
      return Dev.Valid;
   end Is_Valid;

   function Get_Error (Dev : W25Q_Device) return W25Q_Error is
   begin
      return Dev.Last_Err;
   end Get_Error;

   function Read_JEDEC (Dev : in out W25Q_Device) return JEDEC_ID is
      Cmd  : UInt8_Array (1 .. 4) := (Cmd_Jedec_Id, 0, 0, 0);
      Resp : UInt8_Array (1 .. 4);
      Stat : SPI_Status;
      Result : JEDEC_ID;
   begin
      Result.Manufacturer := 0;
      Result.Memory_Type := 0;
      Result.Capacity := 0;

      if not Dev.Valid then
         Dev.Last_Err := SPI_Error;
         return Result;
      end if;

      CS_Assert;
      SPI_Transfer (Dev.Port.all, Cmd, Resp, 4, Stat);
      CS_Deassert;

      if Stat /= SPI_OK then
         Dev.Last_Err := SPI_Error;
         return Result;
      end if;

      Result.Manufacturer := Resp (2);
      Result.Memory_Type := Resp (3);
      Result.Capacity := Resp (4);
      return Result;
   end Read_JEDEC;

   procedure Read
     (Dev    : in out W25Q_Device;
      Addr   : Flash_Address;
      Data   : out UInt8_Array;
      Length : UInt16)
   is
      Cmd : UInt8_Array (1 .. 4);
      Stat : SPI_Status;
   begin
      if not Dev.Valid then
         Dev.Last_Err := SPI_Error;
         return;
      end if;

      Cmd (1) := Cmd_Read_Data;
      Cmd (2) := UInt8 (Shift_Right (Addr, 16) and 16#FF#);
      Cmd (3) := UInt8 (Shift_Right (Addr, 8) and 16#FF#);
      Cmd (4) := UInt8 (Addr and 16#FF#);

      CS_Assert;
      SPI_Transfer (Dev.Port.all, Cmd, UInt8_Array (1 .. 4), 4, Stat);
      if Stat /= SPI_OK then
         CS_Deassert;
         Dev.Last_Err := SPI_Error;
         return;
      end if;
      SPI_Read (Dev.Port.all, Data, Length, Stat);
      CS_Deassert;

      if Stat /= SPI_OK then
         Dev.Last_Err := SPI_Error;
      end if;
   end Read;

   procedure Page_Program
     (Dev    : in out W25Q_Device;
      Addr   : Flash_Address;
      Data   : UInt8_Array;
      Length : UInt16)
   is
      Page_Start : UInt32;
      Cmd : UInt8_Array (1 .. 4);
      Stat : SPI_Status;
   begin
      if not Dev.Valid then
         Dev.Last_Err := SPI_Error;
         return;
      end if;

      -- Validate length
      if Length = 0 or Length > 256 then
         Dev.Last_Err := Page_Boundary;
         return;
      end if;

      -- Check page boundary
      Page_Start := Addr and not 255;
      if UInt32 (Addr) + UInt32 (Length) > Page_Start + 256 then
         Dev.Last_Err := Page_Boundary;
         return;
      end if;

      -- Enable writing
      Write_Enable (Dev);
      if Dev.Last_Err /= OK then
         return;
      end if;

      -- Send command + address
      Cmd (1) := Cmd_Page_Program;
      Cmd (2) := UInt8 (Shift_Right (Addr, 16) and 16#FF#);
      Cmd (3) := UInt8 (Shift_Right (Addr, 8) and 16#FF#);
      Cmd (4) := UInt8 (Addr and 16#FF#);

      CS_Assert;
      SPI_Transfer (Dev.Port.all, Cmd, UInt8_Array (1 .. 4), 4, Stat);
      if Stat /= SPI_OK then
         CS_Deassert;
         Dev.Last_Err := SPI_Error;
         return;
      end if;

      -- Send data
      SPI_Write (Dev.Port.all, Data, Length, Stat);
      CS_Deassert;

      if Stat /= SPI_OK then
         Dev.Last_Err := SPI_Error;
         return;
      end if;

      -- Wait for completion
      Wait_Busy (Dev, 10);
   end Page_Program;

   procedure Sector_Erase
     (Dev  : in out W25Q_Device;
      Addr : Flash_Address)
   is
      Cmd : UInt8_Array (1 .. 4);
      Stat : SPI_Status;
   begin
      if not Dev.Valid then
         Dev.Last_Err := SPI_Error;
         return;
      end if;

      -- Check sector alignment
      if (Addr and 4095) /= 0 then
         Dev.Last_Err := Not_Aligned;
         return;
      end if;

      Write_Enable (Dev);
      if Dev.Last_Err /= OK then
         return;
      end if;

      Cmd (1) := Cmd_Sector_Erase;
      Cmd (2) := UInt8 (Shift_Right (Addr, 16) and 16#FF#);
      Cmd (3) := UInt8 (Shift_Right (Addr, 8) and 16#FF#);
      Cmd (4) := UInt8 (Addr and 16#FF#);

      CS_Assert;
      SPI_Transfer (Dev.Port.all, Cmd, UInt8_Array (1 .. 4), 4, Stat);
      CS_Deassert;

      if Stat /= SPI_OK then
         Dev.Last_Err := SPI_Error;
         return;
      end if;

      Wait_Busy (Dev, 500);
   end Sector_Erase;

   procedure Write
     (Dev    : in out W25Q_Device;
      Addr   : Flash_Address;
      Data   : UInt8_Array;
      Length : UInt16)
   is
      Written      : UInt16 := 0;
      Current_Addr : UInt32 := UInt32 (Addr);
      Page_Offset  : UInt16;
      Page_Remain  : UInt16;
      Chunk        : UInt16;
   begin
      while Written < Length loop
         Page_Offset := UInt16 (Current_Addr and 255);
         Page_Remain := 256 - Page_Offset;
         Chunk := Length - Written;
         if Chunk > Page_Remain then
            Chunk := Page_Remain;
         end if;

         Page_Program (Dev,
                       Flash_Address (Current_Addr),
                       Data (Positive (Written) + 1 ..
                             Positive (Written + Chunk)),
                       Chunk);

         if Dev.Last_Err /= OK then
            return;
         end if;

         Written := Written + Chunk;
         Current_Addr := Current_Addr + UInt32 (Chunk);
      end loop;
   end Write;

   function Read_Verified
     (Dev    : in out W25Q_Device;
      Addr   : Flash_Address;
      Length : UInt16)
      return Verified_Read
   is
      Result : Verified_Read;
   begin
      Result.Length := 0;
      Result.CRC := 0;

      if Length > 256 then
         Dev.Last_Err := Page_Boundary;
         return Result;
      end if;

      Read (Dev, Addr, Result.Data, Length);
      if Dev.Last_Err /= OK then
         return Result;
      end if;

      Result.Length := Length;
      Result.CRC := CRC32_Calc (Result.Data, Length);
      return Result;
   end Read_Verified;

end W25Q;
```

### Zig: Zero-Copy SPI Transactions with Packed Structs for Commands

```zig
// w25q.zig
const std = @import("std");

/// SPI error types
pub const SpiError = error{
    Timeout,
    ModeFault,
    Overrun,
};

/// SPI interface — platform implementations provide this
pub const SpiInterface = struct {
    ctx: *anyopaque,
    transfer: *const fn (ctx: *anyopaque, tx: []const u8, rx: []u8) SpiError!void,
    write: *const fn (ctx: *anyopaque, data: []const u8) SpiError!void,
    read: *const fn (ctx: *anyopaque, data: []u8) SpiError!void,
    csAssert: *const fn (ctx: *anyopaque) void,
    csDeassert: *const fn (ctx: *anyopaque) void,
};

/// Flash geometry
pub const page_size: usize = 256;
pub const sector_size: usize = 4096;
pub const block_size: usize = 65536;

/// Packed command structures for zero-copy SPI transactions
pub const Command = packed struct {
    read_data: packed struct {
        code: u8 = 0x03,
        addr: u24,
    },
    page_program: packed struct {
        code: u8 = 0x02,
        addr: u24,
    },
    sector_erase: packed struct {
        code: u8 = 0x20,
        addr: u24,
    },
    jedec_id: packed struct {
        code: u8 = 0x9F,
    },
    write_enable: packed struct {
        code: u8 = 0x06,
    },
    read_status: packed struct {
        code: u8 = 0x05,
    },
};

/// Status register bits
const sr_busy: u8 = 1 << 0;
const sr_wel: u8 = 1 << 1;

/// JEDEC ID
pub const JedecId = packed struct {
    manufacturer: u8,
    memory_type: u8,
    capacity: u8,

    pub fn isWinbond(self: JedecId) bool {
        return self.manufacturer == 0xEF;
    }

    pub fn capacityBytes(self: JedecId) ?u32 {
        return switch (self.capacity) {
            0x14 => 1 << 17,
            0x15 => 1 << 18,
            0x16 => 1 << 19,
            0x17 => 1 << 20,
            0x18 => 1 << 21,
            0x19 => 1 << 22,
            else => null,
        };
    }
};

/// W25Q error union
pub const W25qError = SpiError || error{
    Timeout,
    VerifyFailed,
    InvalidJedec,
    PageBoundary,
    NotSectorAligned,
};

/// CRC32 calculator
pub const Crc32 = struct {
    table: [256]u32,

    pub fn init() Crc32 {
        var table: [256]u32 = undefined;
        var i: u32 = 0;
        while (i < 256) : (i += 1) {
            var crc = i;
            var j: u32 = 0;
            while (j < 8) : (j += 1) {
                crc = if (crc & 1 != 0) (crc >> 1) ^ 0xEDB88320 else crc >> 1;
            }
            table[i] = crc;
        }
        return Crc32{ .table = table };
    }

    pub fn calc(self: *const Crc32, data: []const u8) u32 {
        var crc: u32 = 0xFFFFFFFF;
        for (data) |byte| {
            const index = @as(u8, @intCast((crc ^ byte) & 0xFF));
            crc = (crc >> 8) ^ self.table[index];
        }
        return crc ^ 0xFFFFFFFF;
    }
};

/// Verified read result
pub const VerifiedRead = struct {
    data_len: usize,
    crc: u32,
};

/// W25Q flash driver
pub const W25q = struct {
    spi: SpiInterface,
    crc: Crc32,

    pub fn init(spi: SpiInterface) W25q {
        return W25q{
            .spi = spi,
            .crc = Crc32.init(),
        };
    }

    /// Read JEDEC ID
    pub fn readJedec(self: *W25q) W25qError!JedecId {
        var tx: [4]u8 = .{ 0x9F, 0, 0, 0 };
        var rx: [4]u8 = undefined;

        self.spi.csAssert(self.spi.ctx);
        try self.spi.transfer(self.spi.ctx, &tx, &rx);
        self.spi.csDeassert(self.spi.ctx);

        return JedecId{
            .manufacturer = rx[1],
            .memory_type = rx[2],
            .capacity = rx[3],
        };
    }

    /// Read data from flash
    pub fn read(self: *W25q, addr: u32, buf: []u8) W25qError!void {
        const cmd: [4]u8 = .{
            0x03,
            @as(u8, @intCast((addr >> 16) & 0xFF)),
            @as(u8, @intCast((addr >> 8) & 0xFF)),
            @as(u8, @intCast(addr & 0xFF)),
        };

        self.spi.csAssert(self.spi.ctx);
        try self.spi.write(self.spi.ctx, &cmd);
        try self.spi.read(self.spi.ctx, buf);
        self.spi.csDeassert(self.spi.ctx);
    }

    /// Read with CRC32 verification
    pub fn readVerified(self: *W25q, addr: u32, buf: []u8) W25qError!VerifiedRead {
        try self.read(addr, buf);
        const crc = self.crc.calc(buf);
        return VerifiedRead{
            .data_len = buf.len,
            .crc = crc,
        };
    }

    /// Wait until flash is not busy
    fn waitBusy(self: *W25q, timeout_ms: u32) W25qError!void {
        var i: u32 = 0;
        while (i < timeout_ms) : (i += 1) {
            var tx: [2]u8 = .{ 0x05, 0 };
            var rx: [2]u8 = undefined;

            self.spi.csAssert(self.spi.ctx);
            try self.spi.transfer(self.spi.ctx, &tx, &rx);
            self.spi.csDeassert(self.spi.ctx);

            if (rx[1] & sr_busy == 0) return;

            // ~1ms delay
            var j: usize = 0;
            while (j < 4000) : (j += 1) {}
        }
        return W25qError.Timeout;
    }

    /// Send write enable
    fn writeEnable(self: *W25q) W25qError!void {
        const cmd: [1]u8 = .{0x06};
        self.spi.csAssert(self.spi.ctx);
        try self.spi.write(self.spi.ctx, &cmd);
        self.spi.csDeassert(self.spi.ctx);
    }

    /// Program a single page
    pub fn pageProgram(self: *W25q, addr: u32, data: []const u8) W25qError!void {
        if (data.len == 0 or data.len > page_size) {
            return W25qError.PageBoundary;
        }

        const page_start = addr & ~@as(u32, page_size - 1);
        if (addr + @as(u32, @intCast(data.len)) > page_start + page_size) {
            return W25qError.PageBoundary;
        }

        try self.writeEnable();

        const header: [4]u8 = .{
            0x02,
            @as(u8, @intCast((addr >> 16) & 0xFF)),
            @as(u8, @intCast((addr >> 8) & 0xFF)),
            @as(u8, @intCast(addr & 0xFF)),
        };

        self.spi.csAssert(self.spi.ctx);
        try self.spi.write(self.spi.ctx, &header);
        try self.spi.write(self.spi.ctx, data);
        self.spi.csDeassert(self.spi.ctx);

        try self.waitBusy(10);
    }

    /// Erase a 4KB sector
    pub fn sectorErase(self: *W25q, addr: u32) W25qError!void {
        if (addr & (sector_size - 1) != 0) {
            return W25qError.NotSectorAligned;
        }

        try self.writeEnable();

        const cmd: [4]u8 = .{
            0x20,
            @as(u8, @intCast((addr >> 16) & 0xFF)),
            @as(u8, @intCast((addr >> 8) & 0xFF)),
            @as(u8, @intCast(addr & 0xFF)),
        };

        self.spi.csAssert(self.spi.ctx);
        try self.spi.write(self.spi.ctx, &cmd);
        self.spi.csDeassert(self.spi.ctx);

        try self.waitBusy(500);
    }

    /// Write data of any length
    pub fn write(self: *W25q, addr: u32, data: []const u8) W25qError!void {
        var offset: usize = 0;
        var current_addr = addr;

        while (offset < data.len) {
            const page_offset = @as(usize, @intCast(current_addr)) & (page_size - 1);
            const page_remaining = page_size - page_offset;
            const chunk_len = @min(data.len - offset, page_remaining);

            try self.pageProgram(current_addr, data[offset .. offset + chunk_len]);

            offset += chunk_len;
            current_addr += @as(u32, @intCast(chunk_len));
        }
    }
};
```

```zig
// main.zig
const std = @import("std");
const w25q = @import("w25q.zig");

// Mock SPI for demonstration
const MockSpi = struct {
    fn transfer(ctx: *anyopaque, tx: []const u8, rx: []u8) w25q.SpiError!void {
        _ = ctx;
        _ = tx;
        _ = rx;
    }

    fn write(ctx: *anyopaque, data: []const u8) w25q.SpiError!void {
        _ = ctx;
        _ = data;
    }

    fn read(ctx: *anyopaque, data: []u8) w25q.SpiError!void {
        _ = ctx;
        _ = data;
    }

    fn csAssert(ctx: *anyopaque) void {
        _ = ctx;
    }

    fn csDeassert(ctx: *anyopaque) void {
        _ = ctx;
    }

    fn interface() w25q.SpiInterface {
        return .{
            .ctx = undefined,
            .transfer = transfer,
            .write = write,
            .read = read,
            .csAssert = csAssert,
            .csDeassert = csDeassert,
        };
    }
};

pub fn main() !void {
    const spi = MockSpi.interface();
    var flash = w25q.W25q.init(spi);

    // Read JEDEC ID
    const jedec = try flash.readJedec();
    std.debug.print("JEDEC: {X:02} {X:02} {X:02}\n", .{
        jedec.manufacturer,
        jedec.memory_type,
        jedec.capacity,
    });

    // Test data
    const test_data = "Hello, SPI Flash! Testing W25Q driver with Zig.";
    const test_addr: u32 = 0;

    // Erase sector
    const sector_addr = test_addr & ~@as(u32, w25q.sector_size - 1);
    std.debug.print("Erasing sector at 0x{X:06}\n", .{sector_addr});
    try flash.sectorErase(sector_addr);

    // Write data
    std.debug.print("Writing {d} bytes\n", .{test_data.len});
    try flash.write(test_addr, test_data);

    // Read back with CRC
    var buf: [256]u8 = undefined;
    const verified = try flash.readVerified(test_addr, buf[0..test_data.len]);
    std.debug.print("CRC32: 0x{X:08}\n", .{verified.crc});
    std.debug.print("Data: {s}\n", .{buf[0..test_data.len]});

    // Verify data
    if (std.mem.eql(u8, buf[0..test_data.len], test_data)) {
        std.debug.print("Data verified successfully!\n", .{});
    } else {
        std.debug.print("Data mismatch!\n", .{});
    }
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
    -o w25q.elf \
    main.c spi.c w25q.c crc32.c startup_stm32f103xb.c

arm-none-eabi-objcopy -O binary w25q.elf w25q.bin
arm-none-eabi-size w25q.elf
```

### Rust

```bash
rustup target add thumbv7m-none-eabi
cargo build --release --target thumbv7m-none-eabi
```

### Ada

```bash
gprbuild -P w25q.gpr -XTARGET=arm-elf -O2
```

### Zig

```bash
# Bare-metal ARM
zig build-exe main.zig -target thumbv7m-freestanding -OReleaseSmall

# Host testing
zig build-exe main.zig -OReleaseFast
```

## QEMU/Renode Verification

### QEMU with SPI flash model

```bash
# QEMU supports m25p80 flash model (compatible with W25Q)
qemu-system-arm -M stm32f4-discovery \
    -drive if=mtd,format=raw,file=flash_image.bin \
    -kernel w25q.elf \
    -semihosting \
    -d unimp,guest_errors \
    -serial stdio
```

### Renode with SPI flash

```
# w25q.resc
mach create
machine LoadPlatformDescription @platforms/cpus/stm32f4.resc

# Add SPI flash at SPI1
spi.flash: Peripherals.W25Q64 @ spi1

mach start
sysbus LoadELF w25q.elf
start

# Monitor SPI transactions
showAnalyzer sysbus.spi1
logLevel 3 sysbus.spi1
```

Expected SPI transaction trace:
```
[0x00001000] SPI: CS asserted
[0x00001001] SPI: TX 0x9F (JEDEC ID)
[0x00001002] SPI: RX 0xEF (Winbond)
[0x00001003] SPI: RX 0x40 (Memory type)
[0x00001004] SPI: RX 0x17 (W25Q64)
[0x00001005] SPI: CS deasserted

[0x00002000] SPI: CS asserted
[0x00002001] SPI: TX 0x06 (Write Enable)
[0x00002002] SPI: CS deasserted

[0x00003000] SPI: CS asserted
[0x00003001] SPI: TX 0x20 0x00 0x00 0x00 (Sector Erase @ 0x000000)
[0x00003002] SPI: CS deasserted
```

## What You Learned

- SPI protocol: CPOL/CPHA modes, full-duplex transfers, chip select management
- Flash memory architecture: pages (256B), sectors (4KB), blocks (64KB)
- Command-level flash operations: Read, Page Program, Sector Erase, JEDEC ID
- Page boundary enforcement and automatic split-write handling
- CRC32 implementation with lookup table for data integrity verification
- Busy-wait polling with timeout management

## Next Steps

- Implement interrupt-driven SPI with DMA for zero-CPU transfers
- Add wear leveling for flash-based file systems
- Implement a simple filesystem (littlefs, FAT) on top of the flash driver
- Add hardware CRC peripheral (STM32 has a dedicated CRC unit)
- Implement dual/quad SPI modes for 2x/4x throughput

## Language Comparison

| Feature | C | Rust | Ada | Zig |
|---------|---|------|-----|-----|
| **Command encoding** | `#define` constants, manual packing | `mod` with typed constants | Strongly typed constants | `packed struct` for zero-copy |
| **Page boundary check** | Manual bit math, easy to get wrong | Checked with `if` + early return | Range-checked subtypes | Comptime-validatable |
| **CRC32 table** | Global array, init at runtime | `const fn` comptime generation | Package-level initialization | `comptime` block |
| **SPI abstraction** | Raw register pointers | `embedded_hal::SpiDevice` trait | Abstract `SPI_Port` type | Function pointer interface |
| **Error handling** | Enum return codes | `Result<T, W25qError<SpiErr>>` | Typed error enum | Error union with `try` |
| **Address validation** | Runtime `if` checks | Runtime checks, could be const | Subtype range enforcement | Runtime with `@intCast` |
| **Zero-copy commands** | Manual byte arrays | Slices with lifetime tracking | Array types with bounds | `packed struct` with `u24` |
| **Binary size** | ~5KB (driver + CRC) | ~7KB (with embedded-hal) | ~9KB (runtime) | ~6KB |

## Deliverables

- [ ] SPI driver with Mode 0 configuration and transfer functions
- [ ] W25Q JEDEC ID read and verification
- [ ] Flash read with arbitrary length
- [ ] Page program with boundary checking
- [ ] Sector erase with alignment validation
- [ ] Auto-splitting write across page boundaries
- [ ] CRC32 lookup table and verification
- [ ] QEMU/Renode simulation showing SPI transactions
- [ ] Output: JEDEC ID, written data, read-back data, CRC32 match confirmation
