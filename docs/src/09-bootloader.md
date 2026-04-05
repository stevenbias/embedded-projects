---
title: "Project 9: Custom Bootloader"
phase: 3
project: 9
---

# Project 9: Custom Bootloader

In this project you will build a custom bootloader for ARM Cortex-M microcontrollers in **C, Rust, Ada, and Zig**. The bootloader will validate firmware images via CRC32, support UART-based firmware updates using an XMODEM-like protocol, relocate the vector table, and safely jump to application code.

Bootloaders are the first code that runs on every embedded device. They are responsible for firmware integrity, secure updates, and recovery from bricked devices. Understanding how bootloaders work is essential for any embedded developer.

## What You'll Learn

- Flash memory layout: bootloader region, application region, firmware slot
- Firmware image format: magic number, version header, CRC32 checksum, payload
- CRC32 computation and verification
- Vector table relocation via `SCB->VTOR`
- Jumping from bootloader to application: stack pointer setup, function pointer cast
- UART-based firmware update protocol (XMODEM-like)
- Dual-bank flash simulation in QEMU
- Application firmware that confirms successful boot
- GDB verification of the bootloader-to-application jump

## Prerequisites

- ARM GCC toolchain (`arm-none-eabi-gcc`)
- Rust: `cargo`, `cortex-m` crate, `cortex-m-rt` crate
- Ada: GNAT ARM toolchain
- Zig: Zig 0.11+
- QEMU with UART support
- GDB with ARM support
- Python 3 (for host-side firmware update tool)

---

## Memory Layout

The flash is divided into regions:

```
0x08000000 +---------------------------+
           |   Bootloader (16 KB)      |
           |   0x08000000-0x08003FFF   |
           +---------------------------+
0x08004000 |   Firmware Header (256 B) |
           |   - Magic: 0x424F4F54     |
           |   - Version: u32          |
           |   - Length: u32           |
           |   - CRC32: u32            |
           |   - Entry Point: u32      |
           +---------------------------+
0x08004100 |   Application Code        |
           |   0x08004100+             |
           |                           |
           +---------------------------+
           |   Firmware Slot B         |
           |   (dual-bank simulation)  |
           +---------------------------+
0x20000000 |   RAM                     |
           +---------------------------+
```

### Firmware Header

```c
typedef struct {
    uint32_t magic;          /* 0x424F4F54 ("BOOT") */
    uint32_t version;        /* Firmware version */
    uint32_t length;         /* Payload length in bytes */
    uint32_t crc32;          /* CRC32 of payload */
    uint32_t entry_point;    /* Application entry (Reset_Handler) */
    uint32_t reserved[59];   /* Pad to 256 bytes */
} FirmwareHeader;
```

The magic number allows the bootloader to detect whether a valid firmware image exists. The CRC32 ensures the image was not corrupted during transfer.

---

## CRC32 Computation

We use the standard CRC-32 (Ethernet/ZIP) polynomial: `0xEDB88320` (reversed form of `0x04C11DB7`).

```c
uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320 : 0);
        }
    }
    return ~crc;
}
```

For production, use a lookup table for speed. The table-based version processes one byte per iteration with no inner loop.

---

## Vector Table Relocation

On Cortex-M, the vector table is at `0x00000000` by default (aliased to flash at `0x08000000`). When the application starts, it must relocate the vector table to its own location so that its interrupt handlers are used:

```c
SCB->VTOR = 0x08004000;  /* Application's vector table address */
```

The bootloader does not need to relocate VTOR — it uses the default location. The application must do this before enabling any interrupts.

---

## Jumping to the Application

The jump sequence is:

1. Disable all interrupts
2. Set the Main Stack Pointer (MSP) to the value at the application's vector table offset 0
3. Set VTOR to the application's vector table address
4. Call the application's Reset_Handler (vector table offset 4)

```c
typedef void (*reset_handler_t)(void);

void jump_to_app(uint32_t app_addr) {
    FirmwareHeader *header = (FirmwareHeader *)app_addr;

    /* Verify magic */
    if (header->magic != 0x424F4F54) return;

    /* Verify CRC */
    uint8_t *payload = (uint8_t *)(app_addr + sizeof(FirmwareHeader));
    uint32_t computed = crc32(payload, header->length);
    if (computed != header->crc32) return;

    /* Disable interrupts */
    __asm volatile ("cpsid i" ::: "memory");

    /* Set MSP to application's initial stack pointer */
    uint32_t app_sp = *(uint32_t *)app_addr;
    __asm volatile ("MSR MSP, %0" : : "r" (app_sp) : "memory");

    /* Relocate vector table */
    SCB->VTOR = app_addr;

    /* Jump to Reset_Handler */
    reset_handler_t reset_handler = (reset_handler_t)(*(uint32_t *)(app_addr + 4));

    /* Use inline asm to ensure no stack frame is set up */
    __asm volatile (
        "BX %0\n"
        :
        : "r" (reset_handler)
        : "memory"
    );

    /* Should never reach here */
    while (1);
}
```

> **Warning:** The jump must be done via inline assembly (`BX`), not a C function call. A C function call would set up a stack frame and return address, corrupting the application's execution context.

---

## UART Firmware Update Protocol

We implement a simple XMODEM-like protocol:

```
Host                          Target
  |                              |
  |--- 'U' (update request) --->|
  |                              |
  |<-- 'Y' (ready) -------------|
  |                              |
  |--- [SOH][SEQ][~SEQ][128B]-->|
  |<-- 'A' (ACK) ---------------|
  |                              |
  |--- [SOH][SEQ+1][~SEQ+1]...->|
  |<-- 'A' (ACK) ---------------|
  |          ...                 |
  |                              |
  |--- [EOT] ------------------->|
  |<-- 'A' (ACK) ---------------|
  |                              |
  |<-- 'O' (OK) or 'F' (FAIL) --|
```

- **SOH** = 0x01 (start of header)
- **SEQ** = sequence number (1-255, wraps)
- **~SEQ** = one's complement of SEQ
- **EOT** = 0x04 (end of transmission)
- **ACK** = 0x06
- **NAK** = 0x15

Each 128-byte block is written to flash. After EOT, the bootloader computes CRC32 and verifies the image.

---

## Implementation: C

### Project Structure

```
bootloader-c/
├── linker_boot.ld
├── linker_app.ld
├── startup_boot.c
├── startup_app.c
├── crc32.h
├── crc32.c
├── flash.h
├── flash.c
├── uart.h
├── uart.c
├── xmodem.h
├── xmodem.c
├── bootloader.h
├── bootloader.c
├── main_boot.c
├── main_app.c
└── Makefile
```

### Bootloader Linker Script (`linker_boot.ld`)

```ld
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 16K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .vectors :
    {
        KEEP(*(.vectors))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
    } > FLASH

    .data :
    {
        _sdata = .;
        *(.data*)
        _edata = .;
    } > RAM AT > FLASH

    _sidata = LOADADDR(.data);

    .bss :
    {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
```

### Application Linker Script (`linker_app.ld`)

```ld
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08004000, LENGTH = 1008K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .vectors :
    {
        KEEP(*(.vectors))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
    } > FLASH

    .data :
    {
        _sdata = .;
        *(.data*)
        _edata = .;
    } > RAM AT > FLASH

    _sidata = LOADADDR(.data);

    .bss :
    {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
    } > RAM

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
```

### CRC32 (`crc32.h`)

```c
#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

uint32_t crc32(const uint8_t *data, size_t len);

/* Table-based version (faster) */
void crc32_init_table(void);
uint32_t crc32_table(const uint8_t *data, size_t len);

#endif
```

### CRC32 (`crc32.c`)

```c
#include "crc32.h"

static uint32_t crc32_table[256];
static int table_initialized = 0;

void crc32_init_table(void) {
    if (table_initialized) return;

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
    table_initialized = 1;
}

uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320 : 0);
        }
    }
    return ~crc;
}

uint32_t crc32_table(const uint8_t *data, size_t len) {
    crc32_init_table();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return ~crc;
}
```

### Flash Abstraction (`flash.h`)

```c
#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>
#include <stdbool.h>

#define FLASH_BASE      0x08000000
#define FLASH_SECTOR_SIZE 16384  /* STM32F405: 16KB sectors (sector 0) */

#define APP_ADDR        0x08004000
#define APP_MAX_SIZE    (1008 * 1024)

/* Flash status codes */
typedef enum {
    FLASH_OK,
    FLASH_ERR_LOCKED,
    FLASH_ERR_ERASE,
    FLASH_ERR_WRITE,
    FLASH_ERR_VERIFY,
} flash_status_t;

flash_status_t flash_unlock(void);
flash_status_t flash_lock(void);
flash_status_t flash_erase_page(uint32_t page_addr);
flash_status_t flash_write(uint32_t addr, const uint8_t *data, size_t len);
flash_status_t flash_verify(uint32_t addr, const uint8_t *expected, size_t len);

#endif
```

### Flash Implementation (`flash.c`)

```c
#include "flash.h"

/* STM32F405 Flash registers */
#define FLASH_KEYR      (*(volatile uint32_t *)0x40023C04)
#define FLASH_SR        (*(volatile uint32_t *)0x40023C0C)
#define FLASH_CR        (*(volatile uint32_t *)0x40023C10)

#define FLASH_KEY1      0x45670123
#define FLASH_KEY2      0xCDEF89AB

#define FLASH_CR_SER    (1 << 1)   /* Sector erase */
#define FLASH_CR_PG     (1 << 0)   /* Programming */
#define FLASH_CR_STRT   (1 << 16)  /* Start */
#define FLASH_CR_SNB_0  (0 << 3)   /* Sector 0 */
#define FLASH_SR_BSY    (1 << 0)   /* Busy */

flash_status_t flash_unlock(void) {
    if (!(FLASH_CR & (1 << 7))) { /* Already unlocked */
        return FLASH_OK;
    }

    FLASH_KEYR = FLASH_KEY1;
    FLASH_KEYR = FLASH_KEY2;

    if (FLASH_CR & (1 << 7)) {
        return FLASH_ERR_LOCKED;
    }
    return FLASH_OK;
}

flash_status_t flash_lock(void) {
    FLASH_CR |= (1 << 7);
    return FLASH_OK;
}

flash_status_t flash_erase_page(uint32_t page_addr) {
    /* Wait for busy */
    while (FLASH_SR & FLASH_SR_BSY);

    FLASH_CR |= FLASH_CR_SER;
    FLASH_CR |= FLASH_CR_SNB_0;
    FLASH_CR |= FLASH_CR_STRT;

    while (FLASH_SR & FLASH_SR_BSY);
    FLASH_CR &= ~FLASH_CR_SER;

    return FLASH_OK;
}

flash_status_t flash_write(uint32_t addr, const uint8_t *data, size_t len) {
    /* Wait for busy */
    while (FLASH_SR & FLASH_SR_BSY);

    FLASH_CR |= FLASH_CR_PG;

    for (size_t i = 0; i < len; i += 2) {
        uint16_t halfword = data[i];
        if (i + 1 < len) {
            halfword |= (uint16_t)data[i + 1] << 8;
        }
        *(volatile uint16_t *)addr = halfword;

        while (FLASH_SR & FLASH_SR_BSY);
        addr += 2;
    }

    FLASH_CR &= ~FLASH_CR_PG;
    return FLASH_OK;
}

flash_status_t flash_verify(uint32_t addr, const uint8_t *expected, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (*(volatile uint8_t *)(addr + i) != expected[i]) {
            return FLASH_ERR_VERIFY;
        }
    }
    return FLASH_OK;
}
```

### UART (`uart.h`)

```c
#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stdbool.h>

void uart_init(uint32_t baud);
void uart_putc(char c);
void uart_puts(const char *s);
bool uart_rx_ready(void);
char uart_getc(void);

#endif
```

### UART Implementation (`uart.c`)

```c
#include "uart.h"

/* STM32F405 USART1 registers */
#define USART1_SR     (*(volatile uint32_t *)0x40011000)
#define USART1_DR     (*(volatile uint32_t *)0x40011004)
#define USART1_BRR    (*(volatile uint32_t *)0x40011008)
#define USART1_CR1    (*(volatile uint32_t *)0x4001100C)

#define RCC_AHB1ENR   (*(volatile uint32_t *)0x40023830)
#define RCC_APB2ENR   (*(volatile uint32_t *)0x40023844)
#define GPIOA_MODER   (*(volatile uint32_t *)0x40020000)
#define GPIOA_AFRH    (*(volatile uint32_t *)0x40020024)

#define USART_SR_RXNE (1 << 5)
#define USART_SR_TXE  (1 << 7)

void uart_init(uint32_t baud) {
    /* Enable GPIOA and USART1 clocks */
    RCC_AHB1ENR |= (1 << 0);    /* GPIOA */
    RCC_APB2ENR |= (1 << 4);    /* USART1 */

    /* PA9 (TX) = alternate function mode (10), AF7 */
    GPIOA_MODER &= ~(0x3 << (9 * 2));
    GPIOA_MODER |= (0x2 << (9 * 2));
    GPIOA_AFRH &= ~(0xF << ((9 - 8) * 4));
    GPIOA_AFRH |= (0x7 << ((9 - 8) * 4));

    /* PA10 (RX) = alternate function mode (10), AF7 */
    GPIOA_MODER &= ~(0x3 << (10 * 2));
    GPIOA_MODER |= (0x2 << (10 * 2));
    GPIOA_AFRH &= ~(0xF << ((10 - 8) * 4));
    GPIOA_AFRH |= (0x7 << ((10 - 8) * 4));

    /* Configure USART1: 8N1, enable TX/RX */
    uint32_t div = (16000000 + baud / 2) / baud; /* 16MHz HSI */
    USART1_BRR = div;
    USART1_CR1 = (1 << 13) | (1 << 3) | (1 << 2); /* UE | TE | RE */
}

void uart_putc(char c) {
    while (!(USART1_SR & USART_SR_TXE));
    USART1_DR = (uint8_t)c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

bool uart_rx_ready(void) {
    return USART1_SR & USART_SR_RXNE;
}

char uart_getc(void) {
    while (!uart_rx_ready());
    return (char)(USART1_DR & 0xFF);
}
```

### XMODEM Protocol (`xmodem.h`)

```c
#ifndef XMODEM_H
#define XMODEM_H

#include <stdint.h>
#include <stdbool.h>

#define XMODEM_SOH    0x01
#define XMODEM_EOT    0x04
#define XMODEM_ACK    0x06
#define XMODEM_NAK    0x15
#define XMODEM_BLOCK_SIZE 128

typedef int (*write_block_fn)(uint32_t addr, const uint8_t *data, size_t len);

/* Receive firmware via XMODEM. Returns total bytes received or -1 on error. */
int xmodem_receive(uint32_t flash_addr, write_block_fn write_fn);

#endif
```

### XMODEM Implementation (`xmodem.c`)

```c
#include "xmodem.h"
#include "uart.h"

int xmodem_receive(uint32_t flash_addr, write_block_fn write_fn) {
    uint8_t seq_expected = 1;
    int total_bytes = 0;

    /* Signal ready */
    uart_putc('Y');

    while (1) {
        char c = uart_getc();

        if (c == XMODEM_EOT) {
            uart_putc(XMODEM_ACK);
            return total_bytes;
        }

        if (c != XMODEM_SOH) {
            return -1; /* Protocol error */
        }

        uint8_t seq = uart_getc();
        uint8_t seq_inv = uart_getc();

        if ((uint8_t)(seq + seq_inv) != 0xFF) {
            return -1; /* Sequence error */
        }

        if (seq != seq_expected) {
            return -1; /* Out of sequence */
        }

        uint8_t block[XMODEM_BLOCK_SIZE];
        for (int i = 0; i < XMODEM_BLOCK_SIZE; i++) {
            block[i] = uart_getc();
        }

        /* Write block to flash */
        if (write_fn(flash_addr + total_bytes, block, XMODEM_BLOCK_SIZE) != 0) {
            uart_putc(XMODEM_NAK);
            return -1;
        }

        uart_putc(XMODEM_ACK);
        seq_expected++;
        total_bytes += XMODEM_BLOCK_SIZE;
    }
}
```

### Firmware Header (`bootloader.h`)

```c
#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>

#define FIRMWARE_MAGIC  0x424F4F54  /* "BOOT" */
#define APP_ADDR        0x08004000

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t length;
    uint32_t crc32;
    uint32_t entry_point;
    uint32_t reserved[59];
} FirmwareHeader;

typedef void (*reset_handler_t)(void);

/* Check if valid firmware exists and jump to it */
void bootloader_check_and_jump(void);

/* Run firmware update via UART */
void bootloader_run_update(void);

#endif
```

### Bootloader Implementation (`bootloader.c`)

```c
#include "bootloader.h"
#include "crc32.h"
#include "uart.h"
#include "xmodem.h"
#include "flash.h"

/* STM32 SCB registers */
#define SCB_VTOR    (*(volatile uint32_t *)0xE000ED08)

static int write_flash_block(uint32_t addr, const uint8_t *data, size_t len) {
    flash_status_t status;

    /* Erase page if this is the first block */
    if (addr == APP_ADDR) {
        status = flash_unlock();
        if (status != FLASH_OK) return -1;

        uint32_t page = addr;
        while (page < addr + len + APP_MAX_SIZE) {
            flash_erase_page(page);
            page += FLASH_SECTOR_SIZE;
        }
    }

    status = flash_write(addr, data, len);
    if (status != FLASH_OK) return -1;

    return 0;
}

void bootloader_check_and_jump(void) {
    FirmwareHeader *header = (FirmwareHeader *)APP_ADDR;

    /* Check magic */
    if (header->magic != FIRMWARE_MAGIC) {
        uart_puts("\r\nNo valid firmware found. Entering update mode...\r\n");
        bootloader_run_update();
        return;
    }

    /* Verify CRC */
    uint8_t *payload = (uint8_t *)(APP_ADDR + sizeof(FirmwareHeader));
    uint32_t computed = crc32_table(payload, header->length);

    if (computed != header->crc32) {
        uart_puts("\r\nFirmware CRC mismatch! Entering update mode...\r\n");
        bootloader_run_update();
        return;
    }

    uart_puts("\r\nFirmware verified. Jumping to application...\r\n");

    /* Disable interrupts */
    __asm volatile ("cpsid i" ::: "memory");

    /* Set MSP to application's initial stack pointer */
    uint32_t app_sp = *(uint32_t *)APP_ADDR;
    __asm volatile ("MSR MSP, %0" : : "r" (app_sp) : "memory");

    /* Relocate vector table */
    SCB_VTOR = APP_ADDR;

    /* Get Reset_Handler address */
    reset_handler_t reset_handler = (reset_handler_t)(*(uint32_t *)(APP_ADDR + 4));

    /* Jump */
    __asm volatile (
        "BX %0\n"
        :
        : "r" (reset_handler)
        : "memory"
    );

    while (1);
}

void bootloader_run_update(void) {
    uart_puts("\r\n=== Firmware Update Mode ===\r\n");
    uart_puts("Send firmware via XMODEM protocol.\r\n");

    int bytes = xmodem_receive(APP_ADDR + sizeof(FirmwareHeader), write_flash_block);

    if (bytes < 0) {
        uart_puts("\r\nUpdate FAILED.\r\n");
        return;
    }

    /* Verify the written firmware */
    FirmwareHeader *header = (FirmwareHeader *)APP_ADDR;
    uint8_t *payload = (uint8_t *)(APP_ADDR + sizeof(FirmwareHeader));
    uint32_t computed = crc32_table(payload, header->length);

    if (computed == header->crc32) {
        uart_puts("\r\nUpdate OK. Rebooting...\r\n");
        /* Reset via NVIC */
        *(volatile uint32_t *)0xE000ED0C = 0x05FA0004; /* AIRCR.SYSRESETREQ */
    } else {
        uart_puts("\r\nUpdate FAILED (CRC mismatch after write).\r\n");
    }
}
```

### Bootloader Main (`main_boot.c`)

```c
#include "bootloader.h"
#include "uart.h"

extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata;
extern uint32_t _sbss, _ebss;

void Reset_Handler(void) {
    /* Copy .data */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;

    /* Zero .bss */
    dst = &_sbss;
    while (dst < &_ebss) *dst++ = 0;

    uart_init(115200);
    uart_puts("\r\nBootloader v1.0\r\n");

    bootloader_check_and_jump();

    while (1);
}

__attribute__((section(".vectors")))
const uint32_t vector_table[] = {
    (uint32_t)&_estack,
    (uint32_t)&Reset_Handler,
};

void NMI_Handler(void) { while (1); }
void HardFault_Handler(void) { while (1); }
```

### Application Main (`main_app.c`)

```c
#include <stdint.h>

/* GPIO for STM32F405 (LED on PA5) */
#define RCC_AHB1ENR   (*(volatile uint32_t *)0x40023830)
#define GPIOA_MODER   (*(volatile uint32_t *)0x40020000)
#define GPIOA_ODR     (*(volatile uint32_t *)0x40020014)

/* SCB */
#define SCB_VTOR      (*(volatile uint32_t *)0xE000ED08)

/* SysTick */
#define SYST_CSR      (*(volatile uint32_t *)0xE000E010)
#define SYST_RVR      (*(volatile uint32_t *)0xE000E014)
#define SYST_CVR      (*(volatile uint32_t *)0xE000E018)

extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata;
extern uint32_t _sbss, _ebss;

void delay_ms(uint32_t ms) {
    SYST_RVR = 16000 - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x5; /* Enable, no interrupt */
    while (ms--) {
        while (!(SYST_CSR & (1 << 16)));
    }
    SYST_CSR = 0;
}

void Reset_Handler(void) {
    /* Copy .data */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;

    /* Zero .bss */
    dst = &_sbss;
    while (dst < &_ebss) *dst++ = 0;

    /* Relocate vector table */
    SCB_VTOR = 0x08004000;

    /* Configure PA5 as output */
    RCC_AHB1ENR |= (1 << 0);
    GPIOA_MODER &= ~(0x3 << (5 * 2));
    GPIOA_MODER |= (0x1 << (5 * 2));

    /* Blink LED to confirm successful boot */
    while (1) {
        GPIOA_ODR ^= (1 << 5);
        delay_ms(500);
    }
}

__attribute__((section(".vectors")))
const uint32_t app_vector_table[] = {
    (uint32_t)&_estack,
    (uint32_t)&Reset_Handler,
};

void NMI_Handler(void) { while (1); }
void HardFault_Handler(void) { while (1); }
```

### Makefile

```makefile
CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
CFLAGS = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os -g -Wall -Wextra -nostdlib

# Bootloader
BOOT_SRCS = main_boot.c bootloader.c crc32.c flash.c uart.c xmodem.c
BOOT_ELF = bootloader.elf
BOOT_BIN = bootloader.bin

# Application
APP_SRCS = main_app.c
APP_ELF = app.elf
APP_BIN = app.bin

all: $(BOOT_BIN) $(APP_BIN) firmware.bin

$(BOOT_ELF): $(BOOT_SRCS)
	$(CC) $(CFLAGS) -T linker_boot.ld -o $@ $^

$(BOOT_BIN): $(BOOT_ELF)
	$(OBJCOPY) -O binary $< $@

$(APP_ELF): $(APP_SRCS)
	$(CC) $(CFLAGS) -T linker_app.ld -o $@ $^

$(APP_BIN): $(APP_ELF)
	$(OBJCOPY) -O binary $< $@

# Combine header + application binary into firmware image
firmware.bin: $(APP_BIN)
	python3 make_firmware.py $< $@

flash: $(BOOT_BIN)
	qemu-system-arm -M netduinoplus2 -kernel $(BOOT_BIN) -serial stdio -S -s &

clean:
	rm -f $(BOOT_ELF) $(BOOT_BIN) $(APP_ELF) $(APP_BIN) firmware.bin
```

### Firmware Image Builder (`make_firmware.py`)

```python
#!/usr/bin/env python3
import struct
import sys
import binascii

def crc32(data):
    return binascii.crc32(data) & 0xFFFFFFFF

def build_firmware(app_bin, output):
    with open(app_bin, 'rb') as f:
        payload = f.read()

    # Find Reset_Handler (entry point) from the binary
    # For simplicity, assume it's at offset 4 from the vector table
    entry_point = 0x08004000 + 4  # Reset_Handler is at offset 4

    header = struct.pack('<IIIII',
        0x424F4F54,     # magic
        1,              # version
        len(payload),   # length
        crc32(payload), # crc32
        entry_point,    # entry point
    )
    header += b'\x00' * (256 - len(header))  # Pad to 256 bytes

    with open(output, 'wb') as f:
        f.write(header)
        f.write(payload)

    print(f"Firmware image: {len(payload)} bytes, CRC32: 0x{crc32(payload):08X}")

if __name__ == '__main__':
    build_firmware(sys.argv[1], sys.argv[2])
```

### Build and Run

```bash
make
make flash
```

---

## Implementation: Rust

### Project Structure

```
bootloader-rust/
├── bootloader/
│   ├── Cargo.toml
│   ├── build.rs
│   ├── memory.x
│   └── src/
│       └── main.rs
├── app/
│   ├── Cargo.toml
│   ├── build.rs
│   ├── memory.x
│   └── src/
│       └── main.rs
└── Makefile
```

### Bootloader `Cargo.toml`

```toml
[package]
name = "bootloader"
version = "0.1.0"
edition = "2021"

[dependencies]
cortex-m = "0.7"
cortex-m-rt = "0.7"
panic-halt = "0.2"

[profile.release]
opt-level = "s"
lto = true
```

### Bootloader `memory.x`

```
MEMORY
{
    FLASH : ORIGIN = 0x08000000, LENGTH = 16K
    RAM : ORIGIN = 0x20000000, LENGTH = 128K
}
```

### Bootloader `src/main.rs`

```rust
#![no_std]
#![no_main]

use core::arch::asm;
use cortex_m::peripheral::SCB;
use cortex_m_rt::{entry, exception};

const APP_ADDR: u32 = 0x0800_4000;
const FIRMWARE_MAGIC: u32 = 0x424F_4F54;
const HEADER_SIZE: usize = 256;

#[repr(C)]
struct FirmwareHeader {
    magic: u32,
    version: u32,
    length: u32,
    crc32: u32,
    entry_point: u32,
    _reserved: [u32; 59],
}

fn crc32(data: &[u8]) -> u32 {
    let mut crc: u32 = 0xFFFF_FFFF;
    for &byte in data {
        crc ^= byte as u32;
        for _ in 0..8 {
            if crc & 1 != 0 {
                crc = (crc >> 1) ^ 0xEDB8_8320;
            } else {
                crc >>= 1;
            }
        }
    }
    !crc
}

fn uart_init() {
    unsafe {
        let rcc_ahb1enr = &*(0x4002_3830 as *mut u32);
        let rcc_apb2enr = &*(0x4002_3844 as *mut u32);
        rcc_ahb1enr.write_volatile(rcc_ahb1enr.read_volatile() | (1 << 0));
        rcc_apb2enr.write_volatile(rcc_apb2enr.read_volatile() | (1 << 4));

        let gpioa_moder = &*(0x4002_0000 as *mut u32);
        let gpioa_afrh = &*(0x4002_0024 as *mut u32);
        // PA9 TX: alternate function mode, AF7
        let moder = gpioa_moder.read_volatile();
        gpioa_moder.write_volatile((moder & !(0x3 << 18)) | (0x2 << 18));
        let afrh = gpioa_afrh.read_volatile();
        gpioa_afrh.write_volatile((afrh & !(0xF << 4)) | (0x7 << 4));
        // PA10 RX: alternate function mode, AF7
        let moder = gpioa_moder.read_volatile();
        gpioa_moder.write_volatile((moder & !(0x3 << 20)) | (0x2 << 20));
        let afrh = gpioa_afrh.read_volatile();
        gpioa_afrh.write_volatile((afrh & !(0xF << 8)) | (0x7 << 8));

        let usart1_brr = &*(0x4001_1008 as *mut u32);
        usart1_brr.write_volatile((16_000_000 + 115_200 / 2) / 115_200);

        let usart1_cr1 = &*(0x4001_100C as *mut u32);
        usart1_cr1.write_volatile((1 << 13) | (1 << 3) | (1 << 2));
    }
}

fn uart_putc(c: u8) {
    unsafe {
        let sr = &*(0x4001_1000 as *mut u32);
        let dr = &*(0x4001_1004 as *mut u32);
        while sr.read_volatile() & (1 << 7) == 0 {}
        dr.write_volatile(c as u32);
    }
}

fn uart_puts(s: &[u8]) {
    for &c in s {
        uart_putc(c);
    }
}

fn uart_getc() -> u8 {
    unsafe {
        let sr = &*(0x4001_1000 as *mut u32);
        let dr = &*(0x4001_1004 as *mut u32);
        while sr.read_volatile() & (1 << 5) == 0 {}
        dr.read_volatile() as u8
    }
}

fn flash_unlock() -> bool {
    unsafe {
        let cr = &*(0x4002_3C10 as *mut u32);
        if cr.read_volatile() & (1 << 7) == 0 {
            return true;
        }
        let keyr = &*(0x4002_3C04 as *mut u32);
        keyr.write_volatile(0x4567_0123);
        keyr.write_volatile(0xCDEF_89AB);
        cr.read_volatile() & (1 << 7) == 0
    }
}

fn flash_lock() {
    unsafe {
        let cr = &*(0x4002_3C10 as *mut u32);
        cr.write_volatile(cr.read_volatile() | (1 << 7));
    }
}

fn flash_erase_page(addr: u32) {
    unsafe {
        let sr = &*(0x4002_3C0C as *mut u32);
        let cr = &*(0x4002_3C10 as *mut u32);

        while sr.read_volatile() & 1 != 0 {}
        cr.write_volatile(cr.read_volatile() | (1 << 1));  /* SER */
        cr.write_volatile(cr.read_volatile() | (1 << 16)); /* STRT */
        while sr.read_volatile() & 1 != 0 {}
        cr.write_volatile(cr.read_volatile() & !(1 << 1));
    }
}

fn flash_write_halfword(addr: u32, data: u16) {
    unsafe {
        let sr = &*(0x4002_3C0C as *mut u32);
        let cr = &*(0x4002_3C10 as *mut u32);

        while sr.read_volatile() & 1 != 0 {}
        cr.write_volatile(cr.read_volatile() | 1);
        (addr as *mut u16).write_volatile(data);
        while sr.read_volatile() & 1 != 0 {}
        cr.write_volatile(cr.read_volatile() & !1);
    }
}

fn xmodem_receive(flash_start: u32) -> Result<u32, ()> {
    uart_putc(b'Y');

    let mut seq_expected: u8 = 1;
    let mut total: u32 = 0;

    loop {
        let c = uart_getc();

        if c == 0x04 { // EOT
            uart_putc(0x06); // ACK
            return Ok(total);
        }

        if c != 0x01 { // SOH
            return Err(());
        }

        let seq = uart_getc();
        let seq_inv = uart_getc();

        if seq.wrapping_add(seq_inv) != 0xFF || seq != seq_expected {
            return Err(());
        }

        let mut block = [0u8; 128];
        for b in block.iter_mut() {
            *b = uart_getc();
        }

        // Write to flash
        if total == 0 {
            if !flash_unlock() {
                return Err(());
            }
            flash_erase_page(flash_start);
        }

        let mut offset = 0;
        while offset < 128 {
            let hw = u16::from_le_bytes([block[offset], block[offset + 1]]);
            flash_write_halfword(flash_start + total + offset as u32, hw);
            offset += 2;
        }

        uart_putc(0x06); // ACK
        seq_expected = seq_expected.wrapping_add(1);
        total += 128;
    }
}

fn jump_to_app() -> ! {
    let header = unsafe { &*(APP_ADDR as *const FirmwareHeader) };

    if header.magic != FIRMWARE_MAGIC {
        uart_puts(b"\r\nNo valid firmware. Update mode.\r\n");
        run_update();
    }

    let payload = unsafe {
        core::slice::from_raw_parts(
            (APP_ADDR + HEADER_SIZE as u32) as *const u8,
            header.length as usize,
        )
    };

    if crc32(payload) != header.crc32 {
        uart_puts(b"\r\nCRC mismatch. Update mode.\r\n");
        run_update();
    }

    uart_puts(b"\r\nFirmware OK. Jumping...\r\n");

    unsafe {
        cortex_m::interrupt::disable();

        let app_sp = *(APP_ADDR as *const u32);
        asm!("MSR MSP, {}", in(reg) app_sp);

        SCB::set_vtor(APP_ADDR);

        let reset_handler = *((APP_ADDR + 4) as *const extern "C" fn() -> !);
        reset_handler();
    }
}

fn run_update() -> ! {
    uart_puts(b"\r\n=== Update Mode ===\r\n");

    match xmodem_receive(APP_ADDR + HEADER_SIZE as u32) {
        Ok(bytes) => {
            let header = unsafe { &*(APP_ADDR as *const FirmwareHeader) };
            let payload = unsafe {
                core::slice::from_raw_parts(
                    (APP_ADDR + HEADER_SIZE as u32) as *const u8,
                    header.length as usize,
                )
            };

            if crc32(payload) == header.crc32 {
                uart_puts(b"\r\nUpdate OK. Rebooting.\r\n");
                unsafe {
                    *(0xE000_ED0C as *mut u32) = 0x05FA_0004;
                }
            } else {
                uart_puts(b"\r\nUpdate FAILED.\r\n");
            }
        }
        Err(()) => {
            uart_puts(b"\r\nUpdate FAILED.\r\n");
        }
    }

    loop {}
}

#[entry]
fn main() -> ! {
    uart_init();
    uart_puts(b"\r\nBootloader v1.0\r\n");
    jump_to_app()
}

#[exception]
fn HardFault(_ef: &cortex_m_rt::ExceptionFrame) -> ! {
    loop {}
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
```

### Application `memory.x`

```
MEMORY
{
    FLASH : ORIGIN = 0x08004000, LENGTH = 1008K
    RAM : ORIGIN = 0x20000000, LENGTH = 128K
}
```

### Application `src/main.rs`

```rust
#![no_std]
#![no_main]

use cortex_m::peripheral::SCB;
use cortex_m_rt::{entry, exception};

const RCC_AHB1ENR: *mut u32 = 0x4002_3830 as _;
const GPIOA_MODER: *mut u32 = 0x4002_0000 as _;
const GPIOA_ODR: *mut u32 = 0x4002_0014 as _;

fn delay_ms(ms: u32) {
    let systick = unsafe { &*cortex_m::peripheral::SYST::PTR };
    systick.set_reload(16000 - 1);
    systick.clear_current();
    systick.enable_counter();

    for _ in 0..ms {
        while !systick.has_wrapped() {}
    }

    systick.disable_counter();
}

#[entry]
fn main() -> ! {
    // Relocate vector table
    unsafe {
        SCB::set_vtor(0x0800_4000);
    }

    // Configure PA5
    unsafe {
        (*RCC_AHB1ENR) |= 1 << 0;
        let moder = (*GPIOA_MODER).read_volatile();
        (*GPIOA_MODER).write_volatile((moder & !(0x3 << 10)) | (0x1 << 10));
    }

    // Blink LED
    loop {
        unsafe {
            let odr = (*GPIOA_ODR).read_volatile();
            (*GPIOA_ODR).write_volatile(odr ^ (1 << 5));
        }
        delay_ms(500);
    }
}

#[exception]
fn HardFault(_ef: &cortex_m_rt::ExceptionFrame) -> ! {
    loop {}
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
```

### Build and Run

```bash
cd bootloader && cargo build --release
cd ../app && cargo build --release

# Create firmware image
python3 ../make_firmware.py ../app/target/thumbv7em-none-eabihf/release/app

# Run in QEMU
qemu-system-arm -M netduinoplus2 \
    -kernel bootloader/target/thumbv7em-none-eabihf/release/bootloader \
    -serial stdio -S -s &

arm-none-eabi-gdb bootloader/target/thumbv7em-none-eabihf/release/bootloader
(gdb) target remote :1234
(gdb) break bootloader::jump_to_app
(gdb) continue
```

---

## Implementation: Ada

### Project Structure

```
bootloader-ada/
├── bootloader.gpr
├── app.gpr
├── src/
│   ├── bootloader.ads
│   ├── bootloader.adb
│   ├── crc32.ads
│   ├── crc32.adb
│   ├── uart.ads
│   ├── uart.adb
│   ├── main_boot.adb
│   ├── main_app.adb
│   └── link_boot.ld
│   └── link_app.ld
```

### CRC32 Package (`crc32.ads`)

```ada
with Interfaces; use Interfaces;

package CRC32 is

   function Compute (Data    : System.Address;
                     Length  : Natural) return Unsigned_32;

   -- SPARK-compatible version with formal contract
   pragma Pure;

end CRC32;
```

### CRC32 Package Body (`crc32.adb`)

```ada
with System.Storage_Elements; use System.Storage_Elements;

package body CRC32 is

   function Compute (Data   : System.Address;
                     Length : Natural) return Unsigned_32
   is
      pragma Precondition (Length > 0);
      pragma Precondition (Data /= System.Null_Address);

      Addr : Storage_Offset := To_Offset (Data);
      Crc  : Unsigned_32 := 16#FFFF_FFFF#;
   begin
      for I in 1 .. Length loop
         Crc := Crc xor Unsigned_32 (Unsigned_8'Val (Addr));
         for J in 1 .. 8 loop
            if (Crc and 1) = 1 then
               Crc := Shift_Right (Crc, 1) xor 16#EDB8_8320#;
            else
               Crc := Shift_Right (Crc, 1);
            end if;
         end loop;
         Addr := Addr + 1;
      end loop;
      return not Crc;
   end Compute;

end CRC32;
```

### Bootloader Package (`bootloader.ads`)

```ada
with Interfaces; use Interfaces;

package Bootloader is

   App_Addr       : constant := 16#0800_4000#;
   Firmware_Magic : constant := 16#424F_4F54#;
   Header_Size    : constant := 256;

   type Firmware_Header is record
      Magic      : Unsigned_32;
      Version    : Unsigned_32;
      Length     : Unsigned_32;
      CRC32      : Unsigned_32;
      Entry      : Unsigned_32;
      Reserved   : array (1 .. 59) of Unsigned_32;
   end record;

   for Firmware_Header use record
      Magic    at 0  range 0 .. 31;
      Version  at 4  range 0 .. 31;
      Length   at 8  range 0 .. 31;
      CRC32    at 12 range 0 .. 31;
      Entry    at 16 range 0 .. 31;
      Reserved at 20 range 0 .. (59 * 32 - 1);
   end record;

   for Firmware_Header'Size use 256 * 8;

   procedure Check_And_Jump;
   procedure Run_Update;

end Bootloader;
```

### Bootloader Body (`bootloader.adb`)

```ada
with System.Machine_Code; use System.Machine_Code;
with System.Storage_Elements; use System.Storage_Elements;
with UART;
with CRC32;

package body Bootloader is

   type Reset_Handler is access procedure;

   procedure Disable_Interrupts is
   begin
      Asm ("cpsid i", Volatile => True);
   end Disable_Interrupts;

   procedure Set_MSP (Value : Unsigned_32) is
   begin
      Asm ("MSR MSP, %0",
           Inputs => Unsigned_32'Asm_Input ("r", Value),
           Volatile => True);
   end Set_MSP;

   procedure Set_VTOR (Value : Unsigned_32) is
      VTOR : Unsigned_32 with Address => System'To_Address (16#E000_ED08#);
      pragma Volatile (VTOR);
   begin
      VTOR := Value;
   end Set_VTOR;

   procedure Jump (Addr : Unsigned_32) is
   begin
      Asm ("BX %0",
           Inputs => Unsigned_32'Asm_Input ("r", Addr),
           Volatile => True);
   end Jump;

   procedure Check_And_Jump is
      Header : Firmware_Header with
        Address => System'To_Address (App_Addr),
        Import => True;

      Payload_Addr : System.Address;
      Computed_CRC : Unsigned_32;
   begin
      if Header.Magic /= Firmware_Magic then
         UART.Put_String (ASCII.CR & ASCII.LF &
                          "No valid firmware. Update mode.");
         Run_Update;
         return;
      end if;

      Payload_Addr := System'To_Address (App_Addr + Header_Size);
      Computed_CRC := CRC32.Compute (Payload_Addr,
                                     Natural (Header.Length));

      if Computed_CRC /= Header.CRC32 then
         UART.Put_String (ASCII.CR & ASCII.LF &
                          "CRC mismatch. Update mode.");
         Run_Update;
         return;
      end if;

      UART.Put_String (ASCII.CR & ASCII.LF &
                       "Firmware OK. Jumping...");

      Disable_Interrupts;
      Set_MSP (Unsigned_32'Val (Header.Magic)); -- Read SP from vector[0]
      Set_VTOR (App_Addr);

      -- Read Reset_Handler address from vector[1]
      declare
         Handler_Addr : Unsigned_32;
         Handler_Ptr : System.Address :=
           System'To_Address (App_Addr + 4);
      begin
         Handler_Addr := Unsigned_32'Val (
           Storage_Offset (Handler_Ptr));
         Jump (Handler_Addr);
      end;
   end Check_And_Jump;

   procedure Run_Update is
   begin
      UART.Put_String (ASCII.CR & ASCII.LF &
                       "=== Update Mode ===");
      -- XMODEM receive implementation
      -- (similar to C version, using UART.Get_Char)
      null; -- Placeholder for brevity
   end Run_Update;

end Bootloader;
```

### Bootloader Main (`main_boot.adb`)

```ada
with UART;
with Bootloader;

procedure Main_Boot is
begin
   UART.Init (115_200);
   UART.Put_String (ASCII.CR & ASCII.LF & "Bootloader v1.0");
   Bootloader.Check_And_Jump;

   loop
      null;
   end loop;
end Main_Boot;
```

### Application Main (`main_app.adb`)

```ada
with System.Machine_Code; use System.Machine_Code;

procedure Main_App is

   type UInt32 is mod 2**32;

   RCC_AHB1ENR : UInt32 with
     Address => System'To_Address (16#4002_3830#),
     Volatile => True;

   GPIOA_MODER : UInt32 with
     Address => System'To_Address (16#4002_0000#),
     Volatile => True;

   GPIOA_ODR : UInt32 with
     Address => System'To_Address (16#4002_0014#),
     Volatile => True;

   SCB_VTOR : UInt32 with
     Address => System'To_Address (16#E000_ED08#),
     Volatile => True;

    procedure Delay_MS (MS : Natural) is
       Count : Natural := MS * 16000;
    begin
      while Count > 0 loop
         Count := Count - 1;
      end loop;
   end Delay_MS;

begin
   -- Relocate vector table
   SCB_VTOR := 16#0800_4000#;

    -- Configure PA5
    RCC_AHB1ENR := RCC_AHB1ENR or (1 << 0);
    declare
       MODER : constant UInt32 := GPIOA_MODER;
    begin
       GPIOA_MODER := (MODER and not (16#3# << 10)) or (16#1# << 10);
    end;

    -- Blink LED
    loop
       GPIOA_ODR := GPIOA_ODR xor (1 << 5);
       Delay_MS (500);
    end loop;
end Main_App;
```

### Build

```bash
gprbuild -P bootloader.gpr
gprbuild -P app.gpr
```

---

## Implementation: Zig

### Project Structure

```
bootloader-zig/
├── build.zig
├── link_boot.ld
├── link_app.ld
├── src/
│   ├── bootloader.zig
│   └── app.zig
```

### `build.zig`

```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .thumb,
        .os_tag = .freestanding,
        .abi = .eabi,
    });

    // Bootloader
    const boot = b.addExecutable(.{
        .name = "bootloader",
        .root_source_file = b.path("src/bootloader.zig"),
        .target = target,
        .optimize = .ReleaseSmall,
    });
    boot.entry = .disabled;
    boot.setLinkerScript(b.path("link_boot.ld"));
    b.installArtifact(boot);

    // Application
    const app = b.addExecutable(.{
        .name = "app",
        .root_source_file = b.path("src/app.zig"),
        .target = target,
        .optimize = .ReleaseSmall,
    });
    app.entry = .disabled;
    app.setLinkerScript(b.path("link_app.ld"));
    b.installArtifact(app);

    const run = b.step("run", "Run bootloader in QEMU");
    const run_cmd = b.addRunArtifact(boot);
    run.dependOn(&run_cmd.step);
}
```

### `src/bootloader.zig`

```zig
const std = @import("std");

// Comptime memory layout validation
comptime {
    std.debug.assert(boot_start == 0x08000000);
    std.debug.assert(boot_size == 0x4000); // 16KB
    std.debug.assert(app_addr == 0x08004000);
    std.debug.assert(header_size == 256);
    std.debug.assert(std.math.isPowerOfTwo(boot_size));
}

const boot_start: u32 = 0x08000000;
const boot_size: u32 = 0x4000;
const app_addr: u32 = 0x08004000;
const firmware_magic: u32 = 0x424F4F54;
const header_size: usize = 256;

const FirmwareHeader = extern struct {
    magic: u32,
    version: u32,
    length: u32,
    crc32: u32,
    entry_point: u32,
    reserved: [59]u32,
};

fn crc32(data: []const u8) u32 {
    var crc: u32 = 0xFFFFFFFF;
    for (data) |byte| {
        crc ^= @as(u32, byte);
        var j: u5 = 0;
        while (j < 8) : (j += 1) {
            if (crc & 1 != 0) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

// UART
const USART1_SR = @as(*volatile u32, @ptrFromInt(0x40011000));
const USART1_DR = @as(*volatile u32, @ptrFromInt(0x40011004));
const USART1_BRR = @as(*volatile u32, @ptrFromInt(0x40011008));
const USART1_CR1 = @as(*volatile u32, @ptrFromInt(0x4001100C));
const RCC_AHB1ENR = @as(*volatile u32, @ptrFromInt(0x40023830));
const RCC_APB2ENR = @as(*volatile u32, @ptrFromInt(0x40023844));
const GPIOA_MODER = @as(*volatile u32, @ptrFromInt(0x40020000));
const GPIOA_AFRH = @as(*volatile u32, @ptrFromInt(0x40020024));

fn uart_init() void {
    RCC_AHB1ENR.* |= (1 << 0);
    RCC_APB2ENR.* |= (1 << 4);

    // PA9 TX: alternate function mode, AF7
    const moder = GPIOA_MODER.*;
    GPIOA_MODER.* = (moder & ~(@as(u32, 0x3) << 18)) | (@as(u32, 0x2) << 18);
    const afrh = GPIOA_AFRH.*;
    GPIOA_AFRH.* = (afrh & ~(@as(u32, 0xF) << 4)) | (@as(u32, 0x7) << 4);
    // PA10 RX: alternate function mode, AF7
    const moder2 = GPIOA_MODER.*;
    GPIOA_MODER.* = (moder2 & ~(@as(u32, 0x3) << 20)) | (@as(u32, 0x2) << 20);
    const afrh2 = GPIOA_AFRH.*;
    GPIOA_AFRH.* = (afrh2 & ~(@as(u32, 0xF) << 8)) | (@as(u32, 0x7) << 8);

    USART1_BRR.* = (16000000 + 115200 / 2) / 115200;
    USART1_CR1.* = (1 << 13) | (1 << 3) | (1 << 2);
}

fn uart_putc(c: u8) void {
    while (USART1_SR.* & (1 << 7) == 0) {}
    USART1_DR.* = c;
}

fn uart_puts(s: []const u8) void {
    for (s) |c| uart_putc(c);
}

fn uart_getc() u8 {
    while (USART1_SR.* & (1 << 5) == 0) {}
    return @truncate(USART1_DR.*);
}

// Flash
const FLASH_KEYR = @as(*volatile u32, @ptrFromInt(0x40023C04));
const FLASH_SR = @as(*volatile u32, @ptrFromInt(0x40023C0C));
const FLASH_CR = @as(*volatile u32, @ptrFromInt(0x40023C10));

fn flash_unlock() bool {
    if (FLASH_CR.* & (1 << 7) == 0) return true;
    FLASH_KEYR.* = 0x45670123;
    FLASH_KEYR.* = 0xCDEF89AB;
    return FLASH_CR.* & (1 << 7) == 0;
}

fn flash_lock() void {
    FLASH_CR.* |= (1 << 7);
}

fn flash_erase_page(addr: u32) void {
    while (FLASH_SR.* & 1 != 0) {}
    FLASH_CR.* |= (1 << 1);
    FLASH_CR.* |= (1 << 16);
    while (FLASH_SR.* & 1 != 0) {}
    FLASH_CR.* &= ~(@as(u32, 1) << 1);
}

fn flash_write(addr: u32, data: []const u8) void {
    while (FLASH_SR.* & 1 != 0) {}
    FLASH_CR.* |= 1;

    var i: usize = 0;
    while (i < data.len) : (i += 2) {
        var hw: u16 = data[i];
        if (i + 1 < data.len) {
            hw |= @as(u16, data[i + 1]) << 8;
        }
        @as(*volatile u16, @ptrFromInt(addr + i)).* = hw;
        while (FLASH_SR.* & 1 != 0) {}
    }

    FLASH_CR.* &= ~@as(u32, 1);
}

// XMODEM
fn xmodem_receive(flash_start: u32) error{Protocol, Flash}!u32 {
    uart_putc('Y');

    var seq_expected: u8 = 1;
    var total: u32 = 0;

    while (true) {
        const c = uart_getc();

        if (c == 0x04) {
            uart_putc(0x06);
            return total;
        }

        if (c != 0x01) return error.Protocol;

        const seq = uart_getc();
        const seq_inv = uart_getc();

        if (seq +% seq_inv != 0xFF or seq != seq_expected) {
            return error.Protocol;
        }

        var block: [128]u8 = undefined;
        for (&block) |*b| b.* = uart_getc();

        if (total == 0) {
            if (!flash_unlock()) return error.Flash;
            flash_erase_page(flash_start);
        }

        flash_write(flash_start + total, &block);

        uart_putc(0x06);
        seq_expected +%= 1;
        total += 128;
    }
}

fn jump_to_app() noreturn {
    const header = @as(*const FirmwareHeader, @ptrFromInt(app_addr));

    if (header.magic != firmware_magic) {
        uart_puts("\r\nNo valid firmware. Update mode.\r\n");
        run_update();
    }

    const payload = @as([*]const u8, @ptrFromInt(app_addr + header_size))[0..header.length];

    if (crc32(payload) != header.crc32) {
        uart_puts("\r\nCRC mismatch. Update mode.\r\n");
        run_update();
    }

    uart_puts("\r\nFirmware OK. Jumping...\r\n");

    const app_sp: u32 = @as(*const u32, @ptrFromInt(app_addr)).*;

    // Disable interrupts
    asm volatile ("cpsid i" ::: "memory");

    // Set MSP
    asm volatile ("MSR MSP, $0"
        :
        : [sp] "{r0}" (app_sp),
    );

    // Relocate VTOR
    const scb_vtor = @as(*volatile u32, @ptrFromInt(0xE000ED08));
    scb_vtor.* = app_addr;

    // Jump to Reset_Handler
    const reset_handler: *const fn () callconv(.C) noreturn =
        @ptrFromInt(@as(*const u32, @ptrFromInt(app_addr + 4)).*);

    asm volatile ("BX $0"
        :
        : [addr] "{r0}" (@intFromPtr(reset_handler)),
        : "memory"
    );

    unreachable;
}

fn run_update() noreturn {
    uart_puts("\r\n=== Update Mode ===\r\n");

    xmodem_receive(app_addr + header_size) catch {
        uart_puts("\r\nUpdate FAILED.\r\n");
        while (true) {}
    };

    const header = @as(*const FirmwareHeader, @ptrFromInt(app_addr));
    const payload = @as([*]const u8, @ptrFromInt(app_addr + header_size))[0..header.length];

    if (crc32(payload) == header.crc32) {
        uart_puts("\r\nUpdate OK. Rebooting.\r\n");
        const aircr = @as(*volatile u32, @ptrFromInt(0xE000ED0C));
        aircr.* = 0x05FA0004;
    } else {
        uart_puts("\r\nUpdate FAILED (CRC mismatch).\r\n");
    }

    while (true) {}
}

export fn Reset_Handler() callconv(.Naked) noreturn {
    asm volatile (
        \\ LDR R0, =_sidata
        \\ LDR R1, =_sdata
        \\ LDR R2, =_edata
        \\ 1:
        \\ CMP R1, R2
        \\ BGE 2f
        \\ LDR R3, [R0], #4
        \\ STR R3, [R1], #4
        \\ B 1b
        \\ 2:
        \\ LDR R0, =_sbss
        \\ LDR R1, =_ebss
        \\ MOVS R2, #0
        \\ 3:
        \\ CMP R0, R1
        \\ BGE 4f
        \\ STR R2, [R0], #4
        \\ B 3b
        \\ 4:
        \\ BL main
        \\ B .
        ::: "memory"
    );
}

export fn main() noreturn {
    uart_init();
    uart_puts("\r\nBootloader v1.0\r\n");
    jump_to_app();
}
```

### `src/app.zig`

```zig
const std = @import("std");

const RCC_AHB1ENR = @as(*volatile u32, @ptrFromInt(0x40023830));
const GPIOA_MODER = @as(*volatile u32, @ptrFromInt(0x40020000));
const GPIOA_ODR = @as(*volatile u32, @ptrFromInt(0x40020014));
const SCB_VTOR = @as(*volatile u32, @ptrFromInt(0xE000ED08));
const SYST_CSR = @as(*volatile u32, @ptrFromInt(0xE000E010));
const SYST_RVR = @as(*volatile u32, @ptrFromInt(0xE000E014));
const SYST_CVR = @as(*volatile u32, @ptrFromInt(0xE000E018));

fn delay_ms(ms: u32) void {
    SYST_RVR.* = 16000 - 1;
    SYST_CVR.* = 0;
    SYST_CSR.* = 0x5;
    var m: u32 = 0;
    while (m < ms) : (m += 1) {
        while (SYST_CSR.* & (1 << 16) == 0) {}
    }
    SYST_CSR.* = 0;
}

export fn Reset_Handler() callconv(.Naked) noreturn {
    asm volatile (
        \\ LDR R0, =_sidata
        \\ LDR R1, =_sdata
        \\ LDR R2, =_edata
        \\ 1:
        \\ CMP R1, R2
        \\ BGE 2f
        \\ LDR R3, [R0], #4
        \\ STR R3, [R1], #4
        \\ B 1b
        \\ 2:
        \\ LDR R0, =_sbss
        \\ LDR R1, =_ebss
        \\ MOVS R2, #0
        \\ 3:
        \\ CMP R0, R1
        \\ BGE 4f
        \\ STR R2, [R0], #4
        \\ B 3b
        \\ 4:
        \\ BL main
        \\ B .
        ::: "memory"
    );
}

export fn main() noreturn {
    // Relocate vector table
    SCB_VTOR.* = 0x08004000;

    // Configure PA5
    RCC_AHB1ENR.* |= (1 << 0);
    const moder = GPIOA_MODER.*;
    GPIOA_MODER.* = (moder & ~(@as(u32, 0x3) << 10)) | (@as(u32, 0x1) << 10);

    // Blink LED
    while (true) {
        GPIOA_ODR.* ^= (1 << 5);
        delay_ms(500);
    }
}
```

### Build and Run

```bash
zig build
qemu-system-arm -M netduinoplus2 -kernel zig-out/bin/bootloader -serial stdio -S -s &
arm-none-eabi-gdb zig-out/bin/bootloader
```

---

## GDB Verification

### Verify the Boot-to-Application Jump

```bash
# Terminal 1
qemu-system-arm -M netduinoplus2 -kernel bootloader.bin -serial stdio -S -s &

# Terminal 2
arm-none-eabi-gdb bootloader.elf
```

```
(gdb) target remote :1234

# Set breakpoint at the jump
(gdb) break jump_to_app
(gdb) continue

# Verify firmware header
(gdb) x/4wx 0x08004000
0x08004000: 0x424f4f54  0x00000001  0x00001234  0xaabbccdd
             ^ magic     ^ version   ^ length    ^ crc32

# Verify CRC
(gdb) set $payload_len = *(unsigned int*)(0x08004008)
(gdb) printf "Payload length: %d bytes\n", $payload_len

# Step through the jump
(gdb) step

# After the jump, check we're in the application
(gdb) info registers pc
pc             0x08004101  0x8004101 <Reset_Handler+1>
                                       ^ Application Reset_Handler

(gdb) info registers msp
msp            0x20005000  0x20005000
               ^ Application's initial stack pointer

(gdb) x/4wx 0xE000ED08
0xe000ed08: 0x08004000  0x00000000  0x00000000  0x00000000
            ^ VTOR now points to application
```

### Verify LED Blinking in Application

```
# Set breakpoint in application's LED toggle
(gdb) break main_app.c:35  # or equivalent in your language
(gdb) continue

# Verify the LED pin toggles
(gdb) x/x 0x40020014
0x40020014: 0x00000020  # PA5 high

(gdb) continue
Breakpoint ...

(gdb) x/x 0x40020014
0x40020014: 0x00000000  # PA5 low
```

---

## Deliverables

- [ ] Bootloader that fits in 16KB flash region (0x08000000-0x08003FFF)
- [ ] Firmware header with magic number, version, length, CRC32, entry point
- [ ] CRC32 computation and verification (table-based for speed)
- [ ] UART-based XMODEM-like firmware update protocol
- [ ] Flash erase and write operations
- [ ] Vector table relocation (SCB->VTOR)
- [ ] Clean jump from bootloader to application (MSP setup, BX instruction)
- [ ] Application firmware that blinks LED to confirm boot
- [ ] GDB verification showing the jump and VTOR relocation
- [ ] All four language implementations (C, Rust, Ada, Zig)

---

## Language Comparison

| Feature | C | Rust | Ada | Zig |
|---|---|---|---|---|
| **Memory layout** | Linker script only | `memory.x` + `build.rs` | Linker script + project file | `build.zig` + linker script |
| **Firmware header** | `struct` with explicit layout | `#[repr(C)] struct` | Record with address clauses | `extern struct` |
| **CRC32** | Manual bit-by-bit or table | Same, or `crc` crate | Pure function with contracts | Comptime-validated algorithm |
| **Flash operations** | Volatile pointer writes | `unsafe` volatile access | `System.Machine_Code.Asm` | Volatile pointer writes |
| **VTOR relocation** | `SCB->VTOR = addr` | `SCB::set_vtor(addr)` | Volatile variable assignment | Volatile pointer write |
| **Jump to app** | `BX` via inline asm | Function pointer cast + call | `BX` via inline asm | `BX` via inline asm |
| **MSP setup** | `MSR MSP, %0` inline asm | `asm!("MSR MSP, {}")` | `MSR MSP, %0` inline asm | `asm volatile ("MSR MSP, $0")` |
| **Layout validation** | None (linker enforces) | None (linker enforces) | `pragma Precondition` | `comptime` assertions |
| **Error handling** | Return codes | `Result<T, E>` | Exception or return code | Error unions |
| **XMODEM protocol** | State machine | State machine | State machine | State machine |
| **Safety** | None — UB on bad pointer | `unsafe` blocks required | Strong typing, SPARK contracts | Comptime checks, error unions |

---

## What You Learned

- How flash memory is organized into bootloader and application regions
- The structure of a firmware image: header, payload, CRC32 checksum
- How CRC32 detects corruption and why it's the standard for firmware validation
- The mechanics of vector table relocation via `SCB->VTOR`
- The exact sequence for jumping from bootloader to application: disable interrupts, set MSP, relocate VTOR, BX to Reset_Handler
- How XMODEM provides reliable serial firmware transfer with per-block acknowledgment
- How each language handles the critical jump sequence:
  - C: inline asm with naked functions
  - Rust: `asm!` macro with function pointer casts
  - Ada: `System.Machine_Code` with address clauses
  - Zig: comptime-validated layout with inline asm
- How to verify the bootloader-to-application transition in GDB

## Next Steps

- Add secure boot: verify firmware with ECDSA or Ed25519 signatures
- Implement dual-bank (A/B) firmware updates with rollback on failure
- Add firmware encryption (AES-128-CBC) for over-the-air updates
- Build a host-side firmware update tool in Python with progress reporting
- Port the bootloader to a different MCU family (STM32F4, nRF52, RP2040)
- Compare your bootloader's size and speed to MCUboot or LittleFS
---

## References

### STMicroelectronics Documentation
- [STM32F4 Reference Manual (RM0090)](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-415-stm32f407-417-stm32f427-437-and-stm32f429-439-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) — Ch. 3: Flash interface (FLASH_KEYR, FLASH_SR, FLASH_CR — PG, SER, STRT, BSY), sector erase, half-word programming; Ch. 7: RCC (clock enables)
- [STM32F405/407 Datasheet](https://www.st.com/resource/en/datasheet/stm32f405rg.pdf) — Flash sector layout (16KB sectors for sector 0), memory map

### ARM Documentation
- [Cortex-M4 Technical Reference Manual](https://developer.arm.com/documentation/ddi0439/latest/) — Ch. 3: Vector table (initial MSP, Reset_Handler), SCB->VTOR (Vector Table Offset Register at 0xE000ED08), AIRCR (Application Interrupt and Reset Control Register at 0xE000ED0C, SYSRESETREQ)
- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/) — B1.4: Exception model (vector table relocation, MSP manipulation, exception return via BX LR), CPSID/CPSIE instructions
- [ARM EABI Specification](https://github.com/ARM-software/abi-aa/releases) — Binary image format, ELF sections (.vectors, .text, .data, .bss)

### Tools & Emulation
- [QEMU STM32 Documentation](https://www.qemu.org/docs/master/system/arm/stm32.html) — Flash emulation, dual-bank simulation
