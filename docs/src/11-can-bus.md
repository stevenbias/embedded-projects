---
title: "Project 11: CAN Bus Node"
phase: 4
project: 11
---

# Project 11: CAN Bus Node

In this project you will build a **CAN bus communication node** that reads OBD-II diagnostic data from a simulated vehicle ECU. You will implement the bxCAN peripheral driver, CAN frame parsing, filter bank configuration, and an OBD-II service 01 PID state machine — in **C, Rust, Ada, and Zig**.

CAN (Controller Area Network) is the dominant in-vehicle networking protocol. Every modern car uses CAN for communication between ECUs (engine, transmission, ABS, airbag, instrument cluster). Understanding CAN at the register level is essential for automotive embedded development, diagnostics, and aftermarket tool development.

## What You'll Learn

- CAN protocol fundamentals: differential signaling, dominant/recessive bits, bitwise arbitration
- CAN frame format: Standard (11-bit) vs Extended (29-bit) IDs, DLC, data field, CRC, ACK
- bxCAN peripheral on STM32: mailbox TX, FIFO RX, filter banks, bit timing
- CAN filter configuration: mask mode vs list mode
- OBD-II protocol: service 01 PIDs (0x0C RPM, 0x0D speed, 0x05 coolant temp)
- Deterministic message handling and latency considerations
- Renode simulation: multi-node CAN bus with simulated ECU responses
- Language-specific approaches: packed structs, bitfield extraction, strong typing, comptime validation

## Prerequisites

- ARM GCC toolchain (`arm-none-eabi-gcc`)
- Rust: `cargo`, `cortex-m` crate, `bxcan` crate, `embedded-hal` traits
- Ada: GNAT ARM toolchain
- Zig: Zig 0.11+ with ARM cross-compilation support
- Renode CAN bus simulation (`renode` with CAN support)
- Familiarity with Projects 4 (I2C Sensor) and 5 (SPI Flash)

---

## CAN Protocol Fundamentals

### Physical Layer

CAN uses **differential signaling** on two wires: CAN_H (high) and CAN_L (low). This provides excellent noise immunity in the electrically noisy automotive environment.

| State | CAN_H | CAN_L | Differential | Logical |
|---|---|---|---|---|
| **Recessive** | 2.5V | 2.5V | 0V | `1` |
| **Dominant** | 3.5V | 1.5V | 2V | `0` |

The bus is wired-AND: any node transmitting a dominant bit overrides recessive bits from other nodes. This property enables **non-destructive bitwise arbitration**.

### Bitwise Arbitration

When multiple nodes transmit simultaneously, they monitor the bus while sending. If a node sends a recessive bit (`1`) but reads a dominant bit (`0`), it knows another node has higher priority and **immediately stops transmitting**. The node with the lowest ID wins arbitration because its ID contains more leading zeros (dominant bits).

```
Node A (ID = 0x100):  0 0 0 1 0 0 0 0 0 0 0  → wins (lower ID)
Node B (ID = 0x120):  0 0 0 1 0 0 1 0 0 0 0  → loses at bit 6
                      ^ ^ ^ ^ ^ ^ ^
                      all match until here
                           Node B sends 1, reads 0 → loses
```

### Bit Timing

CAN does not use a separate clock line. Instead, each node synchronizes to the bus using bit timing segments:

```
Bit Time = Sync_Seg + Prop_Seg + Phase_Seg1 + Phase_Seg2

  +----------+----------+------------+------------+
  | Sync_Seg | Prop_Seg | Phase_Seg1 | Phase_Seg2 |
  |   1 tq   |  N tq    |    M tq    |    K tq    |
  +----------+----------+------------+------------+
  ^                       ^
  Sample point            (hard sync)
```

For 500 kbps CAN with a 36 MHz APB1 clock:
- Prescaler = 4 → time quantum = 4/36 MHz ≈ 111 ns
- Total time quanta per bit = 18 → bit rate = 36 MHz / (4 × 18) = 500 kbps
- Sample point at ~83% (Sync_Seg + Prop_Seg + Phase_Seg1) / Total

---

## CAN Frame Format

### Standard Frame (CAN 2.0A)

```
  +----+----+-----+-----+----+------+-----+------+------+
  |SOF | ID | RTR | IDE | r0 | DLC  |Data | CRC  | ACK  |
  | 1  | 11 |  1  |  1  | 1  |  4   |0-64b| 15+1 | 2+1  |
  +----+----+-----+-----+----+------+-----+------+------+
  Dominant  <--- Arbitration Field --->  <--- Data Field ->
```

| Field | Bits | Description |
|---|---|---|
| **SOF** | 1 | Start of Frame (dominant) |
| **ID** | 11 | Standard identifier (0–0x7FF) |
| **RTR** | 1 | Remote Transmission Request (0 = data, 1 = remote) |
| **IDE** | 1 | Identifier Extension (0 = standard, 1 = extended) |
| **r0** | 1 | Reserved |
| **DLC** | 4 | Data Length Code (0–8 bytes) |
| **Data** | 0–64 | Payload (0–8 bytes) |
| **CRC** | 15+1 | CRC-15 + CRC delimiter |
| **ACK** | 2 | ACK slot + ACK delimiter |
| **EOF** | 7 | End of Frame (recessive) |

### Extended Frame (CAN 2.0B)

```
  +----+-----+----+-----+-----+----+------+-----+------+------+
  |SOF |  ID |SRR | IDE |  ID | RTR | DLC  |Data | CRC  | ACK  |
  | 1  | 11b | 1  |  1  | 18b |  1  |  4   |0-64b| 15+1 | 2+1  |
  +----+-----+----+-----+-----+----+------+-----+------+------+
  <--- Base ID --->         <--- Extended ID --->
```

Extended frames use a 29-bit identifier (11-bit base + 18-bit extended), providing 536 million unique IDs vs 2048 for standard frames.

---

## bxCAN Peripheral (STM32)

The bxCAN (Basic Extended CAN) peripheral on STM32 microcontrollers provides:

### Transmit Mailboxes

Three transmit mailboxes allow queuing up to three frames for transmission. The hardware selects which mailbox to transmit based on priority (lowest ID first).

```
  Mailbox 0 ──┐
  Mailbox 1 ──┼──> Arbitration ──> CAN Bus
  Mailbox 2 ──┘
```

Each mailbox has:
- **TIR**: Transmit ID Register (ID, RTR, IDE, TXRQ)
- **TDTR**: Transmit Data Length and Time Register
- **TDLR/TDHR**: Transmit Data Low/High Registers

### Receive FIFOs

Two receive FIFOs (FIFO0 and FIFO1) store incoming frames. Each FIFO can hold up to 3 messages.

```
  CAN Bus ──> Filter Banks ──> FIFO0 (3 slots)
                            ──> FIFO1 (3 slots)
```

### Filter Banks

The bxCAN has 28 filter banks (14 for CAN1, 14 shared for CAN2 on dual-CAN devices). Each filter bank can be configured in:

| Mode | Scale | Description |
|---|---|---|
| **List** | 16-bit | Exact match against up to 4 IDs per bank |
| **List** | 32-bit | Exact match against up to 2 IDs per bank |
| **Mask** | 16-bit | Match IDs against pattern with mask (up to 4 filters) |
| **Mask** | 32-bit | Match IDs against pattern with mask (up to 2 filters) |

Mask mode is most common: the ID register holds the pattern, the mask register specifies which bits must match.

```
  Filter ID:   0x100  = 001 0000 0000
  Filter Mask: 0x7FF  = 111 1111 1111  (all bits must match)
  → Only accepts frames with ID == 0x100

  Filter ID:   0x100  = 001 0000 0000
  Filter Mask: 0x7F0  = 111 1111 0000  (lower 4 bits don't care)
  → Accepts frames with ID 0x100–0x10F
```

---

## OBD-II Protocol

OBD-II (On-Board Diagnostics II) uses CAN frames with specific IDs for diagnostic communication. The standard defines **services** (modes) and **PIDs** (Parameter IDs).

### Service 01: Show Current Data

Service 01 requests real-time sensor data. The request and response format:

```
Request:  [0x7DF] 02 01 <PID> 00 00 00 00 00
Response: [0x7E8] 03 41 <PID> <byte1> <byte2> 00 00 00
```

| PID | Description | Formula | Bytes |
|---|---|---|---|
| 0x00 | Supported PIDs [01–20] | Bitmask | 4 |
| 0x05 | Engine coolant temperature | A - 40 (°C) | 1 |
| 0x0C | Engine RPM | ((A × 256) + B) / 4 | 2 |
| 0x0D | Vehicle speed | A (km/h) | 1 |
| 0x0F | Intake air temperature | A - 40 (°C) | 1 |
| 0x10 | MAF air flow rate | ((A × 256) + B) / 100 (g/s) | 2 |
| 0x1F | Engine run time | (A × 256) + B (seconds) | 2 |
| 0x2F | Fuel tank level | (A × 100) / 255 (%) | 1 |

### Functional vs Physical Addressing

| Address | Type | Description |
|---|---|---|
| **0x7DF** | Functional | Broadcast request (all ECUs respond) |
| **0x7E0–0x7E7** | Physical | Directed request to specific ECU |
| **0x7E8–0x7EF** | Physical | Response from specific ECU |

---

## Deterministic Message Handling

In automotive systems, message latency must be bounded. The bxCAN hardware handles arbitration and bit timing, but the software must process received frames within deterministic time bounds:

```
  Frame arrives on bus
    → Hardware stores in RX FIFO (instant)
    → RX FIFO interrupt fires
    → ISR reads frame from FIFO (< 10 μs)
    → ISR signals task via semaphore
    → Task processes frame (< 100 μs)
    → Total latency: < 200 μs
```

> **Warning:** Never process CAN frames entirely in the ISR. Read the frame from the FIFO in the ISR (fast), then signal a task to process it. This keeps ISR latency bounded and prevents FIFO overflow.

---

## Implementation: C

### Project Structure

```
can-c/
├── linker.ld
├── startup.c
├── bxcan.h
├── bxcan.c
├── obd2.h
├── obd2.c
├── main.c
└── Makefile
```

### Linker Script (`linker.ld`)

```ld
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 256K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 64K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .text :
    {
        KEEP(*(.vectors))
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

### bxCAN Header (`bxcan.h`)

```c
#ifndef BXCAN_H
#define BXCAN_H

#include <stdint.h>
#include <stdbool.h>

/* CAN frame */
typedef struct {
    uint32_t id;           /* 11-bit standard or 29-bit extended ID */
    bool     extended;     /* true = extended (29-bit) */
    bool     rtr;          /* Remote Transmission Request */
    uint8_t  dlc;          /* Data Length Code (0-8) */
    uint8_t  data[8];      /* Payload */
} CanFrame;

/* bxCAN initialization */
void bxcan_init(uint32_t bitrate);

/* Transmit a frame. Returns true if mailbox was available. */
bool bxcan_transmit(const CanFrame *frame);

/* Receive a frame from FIFO0. Returns true if a frame was available. */
bool bxcan_receive(CanFrame *frame);

/* Configure a filter bank in mask mode (32-bit) */
void bxcan_filter_mask32(uint8_t bank, uint32_t id, uint32_t mask);

/* Configure a filter bank in list mode (32-bit) */
void bxcan_filter_list32(uint8_t bank, uint32_t id1, uint32_t id2);

/* Enable RX FIFO0 interrupt */
void bxcan_enable_rx_irq(void);

/* Check if TX mailbox is ready */
bool bxcan_tx_ready(void);

/* CAN error codes */
typedef enum {
    CAN_OK,
    CAN_ERR_NO_MAILBOX,
    CAN_ERR_FIFO_EMPTY,
    CAN_ERR_BUS_OFF,
    CAN_ERR_OVERFLOW,
} CanError;

CanError bxcan_get_error(void);

#endif
```

### bxCAN Implementation (`bxcan.c`)

```c
#include "bxcan.h"

/* Register base addresses */
#define CAN_BASE        0x40006400
#define CAN_MCR         (*(volatile uint32_t *)(CAN_BASE + 0x000))
#define CAN_MSR         (*(volatile uint32_t *)(CAN_BASE + 0x004))
#define CAN_TSR         (*(volatile uint32_t *)(CAN_BASE + 0x008))
#define CAN_RF0R        (*(volatile uint32_t *)(CAN_BASE + 0x00C))
#define CAN_IER         (*(volatile uint32_t *)(CAN_BASE + 0x014))
#define CAN_BTR         (*(volatile uint32_t *)(CAN_BASE + 0x01C))

/* Mailbox registers */
#define CAN_TI0R        (*(volatile uint32_t *)(CAN_BASE + 0x180))
#define CAN_TDT0R       (*(volatile uint32_t *)(CAN_BASE + 0x184))
#define CAN_TDL0R       (*(volatile uint32_t *)(CAN_BASE + 0x188))
#define CAN_TDH0R       (*(volatile uint32_t *)(CAN_BASE + 0x18C))

#define CAN_TI1R        (*(volatile uint32_t *)(CAN_BASE + 0x190))
#define CAN_TDT1R       (*(volatile uint32_t *)(CAN_BASE + 0x194))
#define CAN_TDL1R       (*(volatile uint32_t *)(CAN_BASE + 0x198))
#define CAN_TDH1R       (*(volatile uint32_t *)(CAN_BASE + 0x19C))

#define CAN_TI2R        (*(volatile uint32_t *)(CAN_BASE + 0x1A0))
#define CAN_TDT2R       (*(volatile uint32_t *)(CAN_BASE + 0x1A4))
#define CAN_TDL2R       (*(volatile uint32_t *)(CAN_BASE + 0x1A8))
#define CAN_TDH2R       (*(volatile uint32_t *)(CAN_BASE + 0x1AC))

/* FIFO0 registers */
#define CAN_RI0R        (*(volatile uint32_t *)(CAN_BASE + 0x1B0))
#define CAN_RD0R        (*(volatile uint32_t *)(CAN_BASE + 0x1B4))
#define CAN_RDL0R       (*(volatile uint32_t *)(CAN_BASE + 0x1B8))
#define CAN_RDH0R       (*(volatile uint32_t *)(CAN_BASE + 0x1BC))

/* Filter registers */
#define CAN_FMR         (*(volatile uint32_t *)(CAN_BASE + 0x200))
#define CAN_FM1R        (*(volatile uint32_t *)(CAN_BASE + 0x204))
#define CAN_FS1R        (*(volatile uint32_t *)(CAN_BASE + 0x20C))
#define CAN_FFA1R       (*(volatile uint32_t *)(CAN_BASE + 0x214))
#define CAN_FA1R        (*(volatile uint32_t *)(CAN_BASE + 0x21C))
#define CAN_FiR0(n)     (*(volatile uint32_t *)(CAN_BASE + 0x240 + (n) * 8))
#define CAN_FiR1(n)     (*(volatile uint32_t *)(CAN_BASE + 0x244 + (n) * 8))

/* RCC registers */
#define RCC_APB1ENR     (*(volatile uint32_t *)0x4002101C)
#define RCC_APB2ENR     (*(volatile uint32_t *)0x40021018)

/* GPIO registers (CAN: PA11=RX, PA12=TX) */
#define GPIOA_CRH       (*(volatile uint32_t *)0x40010804)
#define GPIOA_ODR       (*(volatile uint32_t *)0x4001080C)

/* NVIC registers */
#define NVIC_ISER0      (*(volatile uint32_t *)0xE000E100)
#define NVIC_ICER0      (*(volatile uint32_t *)0xE000E180)

/* Bit definitions */
#define CAN_MCR_INRQ    (1 << 0)
#define CAN_MCR_SLEEP   (1 << 1)
#define CAN_MCR_TXFP    (1 << 2)
#define CAN_MCR_RFLM    (1 << 3)
#define CAN_MCR_ABOM    (1 << 4)
#define CAN_MCR_AWUM    (1 << 5)
#define CAN_MCR_NART    (1 << 6)

#define CAN_MSR_INAK    (1 << 0)
#define CAN_MSR_SLAK    (1 << 1)

#define CAN_TSR_RQCP0   (1 << 0)
#define CAN_TSR_TXOK0   (1 << 1)
#define CAN_TSR_TME0    (1 << 26)
#define CAN_TSR_TME1    (1 << 27)
#define CAN_TSR_TME2    (1 << 28)

#define CAN_RF0R_FMP0   (0x3 << 0)
#define CAN_RF0R_FULL0  (1 << 3)
#define CAN_RF0R_FOVR0  (1 << 4)
#define CAN_RF0R_RFOM0  (1 << 5)

#define CAN_IER_FMPIE0  (1 << 1)
#define CAN_IER_FFIE0   (1 << 2)
#define CAN_IER_FOVIE0  (1 << 3)
#define CAN_IER_TMEIE   (1 << 0)

#define CAN_RI0R_EXTID  (0x1FFFFFFF << 3)
#define CAN_RI0R_STDID  (0x7FF << 21)
#define CAN_RI0R_IDE    (1 << 2)
#define CAN_RI0R_RTR    (1 << 1)

static CanError last_error = CAN_OK;

void bxcan_init(uint32_t bitrate) {
    /* Enable CAN and GPIOA clocks */
    RCC_APB1ENR |= (1 << 25);  /* CAN1 */
    RCC_APB2ENR |= (1 << 2);   /* GPIOA */

    /* Configure PA11 (RX) as input pull-up, PA12 (TX) as alternate function push-pull */
    /* PA11: CNF=10 (input pull-up/down), MODE=00 (input) */
    GPIOA_CRH &= ~(0xF << 12);
    GPIOA_CRH |= (0x8 << 12);  /* Input with pull-up */
    GPIOA_ODR |= (1 << 11);    /* Pull-up */

    /* PA12: CNF=10 (alt func push-pull), MODE=11 (50MHz) */
    GPIOA_CRH &= ~(0xF << 16);
    GPIOA_CRH |= (0xB << 16);

    /* Request initialization mode */
    CAN_MCR |= CAN_MCR_INRQ;
    while (!(CAN_MSR & CAN_MSR_INAK));

    /* Configure bit timing for 500 kbps @ 36 MHz APB1 */
    /* Prescaler=4, SJW=1, BS1=13, BS2=2 → 36M/(4*(1+13+2)) = 500k */
    CAN_BTR = (0 << 30)        /* Normal mode (not loopback) */
            | (0 << 24)        /* SJW = 1 */
            | (12 << 16)       /* BS1 = 13 */
            | (1 << 20)        /* BS2 = 2 */
            | (3);             /* Prescaler = 4 */

    /* Configure filters: accept all standard frames for OBD-II */
    /* Enter filter initialization mode */
    CAN_FMR |= (1 << 0);  /* FINIT */

    /* Filter bank 0: mask mode, 32-bit, accept IDs 0x7E0-0x7EF (ECU responses) */
    CAN_FS1R |= (1 << 0);         /* 32-bit scale */
    CAN_FFA1R &= ~(1 << 0);       /* Assign to FIFO0 */
    CAN_FiR0(0) = (0x7E0 << 21);  /* ID pattern */
    CAN_FiR1(0) = (0x7F0 << 21);  /* Mask: upper 8 bits must match */
    CAN_FA1R |= (1 << 0);         /* Activate filter 0 */

    /* Filter bank 1: accept functional broadcast ID 0x7DF */
    CAN_FiR0(1) = (0x7DF << 21);
    CAN_FiR1(1) = (0x7FF << 21);  /* Exact match */
    CAN_FA1R |= (1 << 1);

    /* Leave filter initialization mode */
    CAN_FMR &= ~(1 << 0);

    /* Leave initialization mode — enter normal mode */
    CAN_MCR &= ~CAN_MCR_INRQ;
    while (CAN_MSR & CAN_MSR_INAK);

    /* Enable RX FIFO0 message pending interrupt */
    CAN_IER |= CAN_IER_FMPIE0;
    NVIC_ISER0 |= (1 << 20);  /* CAN1_RX0 IRQ */

    last_error = CAN_OK;
}

bool bxcan_transmit(const CanFrame *frame) {
    /* Find an empty mailbox */
    uint32_t tsr = CAN_TSR;

    if (tsr & CAN_TSR_TME0) {
        /* Use mailbox 0 */
        if (frame->extended) {
            CAN_TI0R = (frame->id << 3) | (1 << 2) | (frame->rtr << 1) | 1;
        } else {
            CAN_TI0R = (frame->id << 21) | (frame->rtr << 1) | 1;
        }
        CAN_TDT0R = frame->dlc & 0xF;
        CAN_TDL0R = (frame->data[3] << 24) | (frame->data[2] << 16) |
                    (frame->data[1] << 8) | frame->data[0];
        CAN_TDH0R = (frame->data[7] << 24) | (frame->data[6] << 16) |
                    (frame->data[5] << 8) | frame->data[4];
        return true;
    } else if (tsr & CAN_TSR_TME1) {
        /* Use mailbox 1 */
        if (frame->extended) {
            CAN_TI1R = (frame->id << 3) | (1 << 2) | (frame->rtr << 1) | 1;
        } else {
            CAN_TI1R = (frame->id << 21) | (frame->rtr << 1) | 1;
        }
        CAN_TDT1R = frame->dlc & 0xF;
        CAN_TDL1R = (frame->data[3] << 24) | (frame->data[2] << 16) |
                    (frame->data[1] << 8) | frame->data[0];
        CAN_TDH1R = (frame->data[7] << 24) | (frame->data[6] << 16) |
                    (frame->data[5] << 8) | frame->data[4];
        return true;
    } else if (tsr & CAN_TSR_TME2) {
        /* Use mailbox 2 */
        if (frame->extended) {
            CAN_TI2R = (frame->id << 3) | (1 << 2) | (frame->rtr << 1) | 1;
        } else {
            CAN_TI2R = (frame->id << 21) | (frame->rtr << 1) | 1;
        }
        CAN_TDT2R = frame->dlc & 0xF;
        CAN_TDL2R = (frame->data[3] << 24) | (frame->data[2] << 16) |
                    (frame->data[1] << 8) | frame->data[0];
        CAN_TDH2R = (frame->data[7] << 24) | (frame->data[6] << 16) |
                    (frame->data[5] << 8) | frame->data[4];
        return true;
    }

    last_error = CAN_ERR_NO_MAILBOX;
    return false;
}

bool bxcan_receive(CanFrame *frame) {
    if ((CAN_RF0R & CAN_RF0R_FMP0) == 0) {
        last_error = CAN_ERR_FIFO_EMPTY;
        return false;
    }

    /* Read ID */
    uint32_t ri0r = CAN_RI0R;
    frame->extended = (ri0r & CAN_RI0R_IDE) != 0;
    frame->rtr = (ri0r & CAN_RI0R_RTR) != 0;

    if (frame->extended) {
        frame->id = (ri0r >> 3) & 0x1FFFFFFF;
    } else {
        frame->id = (ri0r >> 21) & 0x7FF;
    }

    /* Read DLC and data */
    uint32_t rdtr = CAN_RD0R;
    frame->dlc = rdtr & 0xF;

    uint32_t rdlr = CAN_RDL0R;
    uint32_t rdhr = CAN_RDH0R;

    frame->data[0] = (rdlr >> 0) & 0xFF;
    frame->data[1] = (rdlr >> 8) & 0xFF;
    frame->data[2] = (rdlr >> 16) & 0xFF;
    frame->data[3] = (rdlr >> 24) & 0xFF;
    frame->data[4] = (rdhr >> 0) & 0xFF;
    frame->data[5] = (rdhr >> 8) & 0xFF;
    frame->data[6] = (rdhr >> 16) & 0xFF;
    frame->data[7] = (rdhr >> 24) & 0xFF;

    /* Release FIFO0 */
    CAN_RF0R |= CAN_RF0R_RFOM0;

    return true;
}

void bxcan_filter_mask32(uint8_t bank, uint32_t id, uint32_t mask) {
    CAN_FiR0(bank) = id << 21;
    CAN_FiR1(bank) = mask << 21;
    CAN_FA1R |= (1 << bank);
}

void bxcan_filter_list32(uint8_t bank, uint32_t id1, uint32_t id2) {
    CAN_FiR0(bank) = id1 << 21;
    CAN_FiR1(bank) = id2 << 21;
    CAN_FA1R |= (1 << bank);
}

void bxcan_enable_rx_irq(void) {
    CAN_IER |= CAN_IER_FMPIE0;
    NVIC_ISER0 |= (1 << 20);
}

bool bxcan_tx_ready(void) {
    return (CAN_TSR & (CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2)) != 0;
}

CanError bxcan_get_error(void) {
    return last_error;
}
```

### OBD-II Header (`obd2.h`)

```c
#ifndef OBD2_H
#define OBD2_H

#include <stdint.h>
#include <stdbool.h>
#include "bxcan.h"

/* OBD-II request/response IDs */
#define OBD2_REQUEST_ID     0x7DF
#define OBD2_RESPONSE_ID    0x7E8

/* Service 01 PIDs */
#define PID_ENGINE_RPM          0x0C
#define PID_VEHICLE_SPEED       0x0D
#define PID_COOLANT_TEMP        0x05
#define PID_INTAKE_AIR_TEMP     0x0F
#define PID_ENGINE_LOAD         0x04
#define PID_THROTTLE_POS        0x11
#define PID_FUEL_LEVEL          0x2F

/* OBD-II state machine states */
typedef enum {
    OBD2_IDLE,
    OBD2_SENDING,
    OBD2_WAITING_RESPONSE,
    OBD2_PROCESSING,
    OBD2_ERROR,
} Obd2State;

/* Parsed sensor data */
typedef struct {
    bool     has_rpm;
    uint16_t rpm;
    bool     has_speed;
    uint8_t  speed_kmh;
    bool     has_coolant_temp;
    int16_t  coolant_temp_c;
    bool     has_intake_temp;
    int16_t  intake_temp_c;
    bool     has_fuel_level;
    uint8_t  fuel_level_pct;
} Obd2SensorData;

/* OBD-II module */
void obd2_init(void);
void obd2_request_pid(uint8_t pid);
Obd2State obd2_get_state(void);
bool obd2_process_frame(const CanFrame *frame);
void obd2_get_data(Obd2SensorData *data);
void obd2_tick(void);  /* Call periodically for timeout handling */

#endif
```

### OBD-II Implementation (`obd2.c`)

```c
#include "obd2.h"
#include <string.h>

static Obd2State current_state = OBD2_IDLE;
static Obd2SensorData sensor_data;
static uint8_t requested_pid = 0;
static uint32_t request_timeout = 0;
static uint32_t tick_counter = 0;

#define OBD2_TIMEOUT_TICKS  500  /* 500ms timeout */

void obd2_init(void) {
    memset(&sensor_data, 0, sizeof(sensor_data));
    current_state = OBD2_IDLE;
    requested_pid = 0;
    tick_counter = 0;
}

void obd2_request_pid(uint8_t pid) {
    if (current_state != OBD2_IDLE) return;

    CanFrame frame;
    frame.id = OBD2_REQUEST_ID;
    frame.extended = false;
    frame.rtr = false;
    frame.dlc = 8;
    memset(frame.data, 0, 8);

    /* OBD-II request format: [num_bytes] [service] [PID] [padding...] */
    frame.data[0] = 0x02;  /* 2 data bytes follow */
    frame.data[1] = 0x01;  /* Service 01: Show current data */
    frame.data[2] = pid;

    if (bxcan_transmit(&frame)) {
        current_state = OBD2_WAITING_RESPONSE;
        requested_pid = pid;
        request_timeout = tick_counter + OBD2_TIMEOUT_TICKS;
    } else {
        current_state = OBD2_ERROR;
    }
}

Obd2State obd2_get_state(void) {
    return current_state;
}

bool obd2_process_frame(const CanFrame *frame) {
    /* Only process responses to our request */
    if (frame->id != OBD2_RESPONSE_ID) return false;
    if (current_state != OBD2_WAITING_RESPONSE) return false;

    /* Validate response format: [num_bytes] [service+0x40] [PID] [data...] */
    if (frame->dlc < 4) return false;
    if (frame->data[1] != 0x41) return false;  /* Service 01 + 0x40 = 0x41 */
    if (frame->data[2] != requested_pid) return false;

    current_state = OBD2_PROCESSING;

    /* Parse based on PID */
    switch (requested_pid) {
        case PID_ENGINE_RPM:
            sensor_data.rpm = ((uint16_t)frame->data[3] << 8) | frame->data[4];
            sensor_data.rpm /= 4;
            sensor_data.has_rpm = true;
            break;

        case PID_VEHICLE_SPEED:
            sensor_data.speed_kmh = frame->data[3];
            sensor_data.has_speed = true;
            break;

        case PID_COOLANT_TEMP:
            sensor_data.coolant_temp_c = (int16_t)frame->data[3] - 40;
            sensor_data.has_coolant_temp = true;
            break;

        case PID_INTAKE_AIR_TEMP:
            sensor_data.intake_temp_c = (int16_t)frame->data[3] - 40;
            sensor_data.has_intake_temp = true;
            break;

        case PID_FUEL_LEVEL:
            sensor_data.fuel_level_pct = (uint8_t)((frame->data[3] * 100) / 255);
            sensor_data.has_fuel_level = true;
            break;
    }

    current_state = OBD2_IDLE;
    return true;
}

void obd2_get_data(Obd2SensorData *data) {
    memcpy(data, &sensor_data, sizeof(Obd2SensorData));
}

void obd2_tick(void) {
    tick_counter++;

    if (current_state == OBD2_WAITING_RESPONSE) {
        if (tick_counter >= request_timeout) {
            current_state = OBD2_ERROR;
            /* Reset after error */
            current_state = OBD2_IDLE;
        }
    }
}
```

### Main Application (`main.c`)

```c
#include "bxcan.h"
#include "obd2.h"
#include <stdint.h>

/* GPIO for LED (PC13) */
#define RCC_APB2ENR   (*(volatile uint32_t *)0x40021018)
#define GPIOC_CRH     (*(volatile uint32_t *)0x40011004)
#define GPIOC_ODR     (*(volatile uint32_t *)0x4001100C)

static volatile int rx_frame_available = 0;
static CanFrame rx_frame;

/* CAN RX interrupt handler */
void USB_LP_CAN1_RX0_IRQHandler(void) {
    if (bxcan_receive(&rx_frame)) {
        rx_frame_available = 1;
    }
}

/* Simple delay using SysTick */
static void delay_ms(uint32_t ms) {
    volatile uint32_t *rvr = (volatile uint32_t *)0xE000E014;
    volatile uint32_t *cvr = (volatile uint32_t *)0xE000E018;
    volatile uint32_t *csr = (volatile uint32_t *)0xE000E010;

    *rvr = 8000 - 1;
    *cvr = 0;
    *csr = 0x5;

    while (ms--) {
        while (!(*csr & (1 << 16)));
    }
    *csr = 0;
}

/* State machine for polling multiple PIDs */
static const uint8_t pid_sequence[] = {
    PID_COOLANT_TEMP,
    PID_ENGINE_RPM,
    PID_VEHICLE_SPEED,
    PID_INTAKE_AIR_TEMP,
    PID_FUEL_LEVEL,
};
static int pid_index = 0;

int main(void) {
    /* Enable GPIOC */
    RCC_APB2ENR |= (1 << 4);
    GPIOC_CRH &= ~(0xF << 20);
    GPIOC_CRH |= (0x3 << 20);

    /* Initialize CAN at 500 kbps */
    bxcan_init(500000);

    /* Initialize OBD-II module */
    obd2_init();

    /* Request first PID */
    obd2_request_pid(pid_sequence[pid_index]);

    while (1) {
        /* Process received frames */
        if (rx_frame_available) {
            rx_frame_available = 0;
            obd2_process_frame(&rx_frame);

            /* Toggle LED on successful reception */
            GPIOC_ODR ^= (1 << 13);
        }

        /* Check if we can request next PID */
        if (obd2_get_state() == OBD2_IDLE) {
            pid_index = (pid_index + 1) % 5;
            obd2_request_pid(pid_sequence[pid_index]);
        }

        /* Tick for timeout handling */
        obd2_tick();

        delay_ms(50);
    }

    return 0;
}
```

### Makefile

```makefile
CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
CFLAGS = -mcpu=cortex-m3 -mthumb -Os -g -Wall -Wextra -nostdlib -ffreestanding
LDFLAGS = -T linker.ld

all: can.elf can.bin

can.elf: startup.c bxcan.c obd2.c main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

can.bin: can.elf
	$(OBJCOPY) -O binary $< $@

run: can.bin
	renode --disable-gui can.resc

clean:
	rm -f can.elf can.bin
```

### Startup Code (`startup.c`)

```c
#include <stdint.h>

extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata;
extern uint32_t _sbss, _ebss;

void Reset_Handler(void);
void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USB_LP_CAN1_RX0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

void Default_Handler(void) { while (1); }

__attribute__((section(".vectors")))
const uint32_t vector_table[] = {
    (uint32_t)&_estack,
    (uint32_t)&Reset_Handler,
    (uint32_t)&NMI_Handler,
    (uint32_t)&HardFault_Handler,
    /* ... skip to CAN RX0 at position 20 ... */
    [20] = (uint32_t)&USB_LP_CAN1_RX0_IRQHandler,
};

void Reset_Handler(void) {
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    dst = &_sbss;
    while (dst < &_ebss) *dst++ = 0;
    main();
    while (1);
}

int main(void);
```

### Build and Run

```bash
make
```

---

## Implementation: Rust

### Project Setup

```bash
cargo init --name can-rust
cd can-rust
```

### `Cargo.toml`

```toml
[package]
name = "can-rust"
version = "0.1.0"
edition = "2021"

[dependencies]
cortex-m = "0.7"
cortex-m-rt = "0.7"
panic-halt = "0.2"
bxcan = "0.7"
embedded-hal = "0.2"

[profile.release]
opt-level = "s"
lto = true
```

### `.cargo/config.toml`

```toml
[build]
target = "thumbv7m-none-eabi"

[target.thumbv7m-none-eabi]
runner = "renode --disable-gui can.resc"
rustflags = ["-C", "link-arg=-Tlink.x"]
```

### `memory.x`

```
MEMORY
{
    FLASH : ORIGIN = 0x08000000, LENGTH = 256K
    RAM : ORIGIN = 0x20000000, LENGTH = 64K
}
```

### `build.rs`

```rust
use std::env;
use std::fs::File;
use std::io::Write;
use std::path::PathBuf;

fn main() {
    let out = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    File::create(out.join("memory.x"))
        .unwrap()
        .write_all(include_bytes!("memory.x"))
        .unwrap();
    println!("cargo:rustc-link-search={}", out.display());
    println!("cargo:rerun-if-changed=memory.x");
}
```

### `src/main.rs`

```rust
#![no_std]
#![no_main]

use core::arch::asm;
use cortex_m::peripheral::NVIC;
use cortex_m_rt::{entry, exception, ExceptionFrame};

/* ============================================================
 * CAN Frame — type-safe with bitfield extraction
 * ============================================================ */

/// Standard (11-bit) CAN ID
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct StandardId(u16);

impl StandardId {
    pub const fn new(id: u16) -> Option<Self> {
        if id <= 0x7FF {
            Some(Self(id))
        } else {
            None
        }
    }

    pub const fn as_raw(&self) -> u16 {
        self.0
    }
}

/// Extended (29-bit) CAN ID
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ExtendedId(u32);

impl ExtendedId {
    pub const fn new(id: u32) -> Option<Self> {
        if id <= 0x1FFFFFFF {
            Some(Self(id))
        } else {
            None
        }
    }

    pub const fn as_raw(&self) -> u32 {
        self.0
    }
}

/// CAN identifier — either standard or extended
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CanId {
    Standard(StandardId),
    Extended(ExtendedId),
}

impl CanId {
    pub const fn is_standard(&self) -> bool {
        matches!(self, CanId::Standard(_))
    }

    pub const fn is_extended(&self) -> bool {
        matches!(self, CanId::Extended(_))
    }
}

/// CAN data frame
#[derive(Clone, Copy, Debug)]
pub struct CanFrame {
    pub id: CanId,
    pub rtr: bool,
    pub data: [u8; 8],
    pub dlc: u8,
}

impl CanFrame {
    pub const fn new(id: CanId, data: [u8; 8], dlc: u8) -> Self {
        Self {
            id,
            rtr: false,
            data,
            dlc: dlc.min(8),
        }
    }

    pub const fn rtr(id: CanId) -> Self {
        Self {
            id,
            rtr: true,
            data: [0; 8],
            dlc: 0,
        }
    }

    /// Extract payload as a slice (respects DLC)
    pub fn payload(&self) -> &[u8] {
        &self.data[..self.dlc as usize]
    }
}

/* ============================================================
 * OBD-II Protocol — type-safe PID definitions
 * ============================================================ */

/// OBD-II Service 01 PIDs
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Obd2Pid {
    CoolantTemp = 0x05,
    EngineRpm = 0x0C,
    VehicleSpeed = 0x0D,
    IntakeAirTemp = 0x0F,
    EngineLoad = 0x04,
    ThrottlePos = 0x11,
    FuelLevel = 0x2F,
}

impl Obd2Pid {
    pub const fn as_raw(self) -> u8 {
        self as u8
    }

    /// Parse response data for this PID
    pub fn parse(self, data: &[u8]) -> Option<PidValue> {
        match self {
            Obd2Pid::EngineRpm if data.len() >= 2 => {
                let raw = ((data[0] as u16) << 8) | (data[1] as u16);
                Some(PidValue::Rpm(raw / 4))
            }
            Obd2Pid::VehicleSpeed if data.len() >= 1 => {
                Some(PidValue::Speed(data[0]))
            }
            Obd2Pid::CoolantTemp if data.len() >= 1 => {
                Some(PidValue::CoolantTemp(data[0] as i16 - 40))
            }
            Obd2Pid::IntakeAirTemp if data.len() >= 1 => {
                Some(PidValue::IntakeTemp(data[0] as i16 - 40))
            }
            Obd2Pid::FuelLevel if data.len() >= 1 => {
                Some(PidValue::FuelLevel((data[0] as u16 * 100) / 255))
            }
            _ => None,
        }
    }
}

/// Parsed PID value
#[derive(Clone, Copy, Debug)]
pub enum PidValue {
    Rpm(u16),
    Speed(u8),
    CoolantTemp(i16),
    IntakeTemp(i16),
    FuelLevel(u16),
}

/* ============================================================
 * bxCAN Register Abstraction
 * ============================================================ */

const CAN_BASE: u32 = 0x4000_6400;

struct CanRegisters {
    mcr: *mut u32,
    msr: *mut u32,
    tsr: *mut u32,
    rf0r: *mut u32,
    ier: *mut u32,
    btr: *mut u32,
    ti0r: *mut u32,
    tdt0r: *mut u32,
    tdl0r: *mut u32,
    tdh0r: *mut u32,
    ri0r: *mut u32,
    rdtr: *mut u32,
    rdl0r: *mut u32,
    rdh0r: *mut u32,
    fmr: *mut u32,
    fs1r: *mut u32,
    ffa1r: *mut u32,
    fa1r: *mut u32,
    fir0: *mut u32,
    fir1: *mut u32,
}

impl CanRegisters {
    const fn new() -> Self {
        Self {
            mcr: (CAN_BASE + 0x000) as *mut u32,
            msr: (CAN_BASE + 0x004) as *mut u32,
            tsr: (CAN_BASE + 0x008) as *mut u32,
            rf0r: (CAN_BASE + 0x00C) as *mut u32,
            ier: (CAN_BASE + 0x014) as *mut u32,
            btr: (CAN_BASE + 0x01C) as *mut u32,
            ti0r: (CAN_BASE + 0x180) as *mut u32,
            tdt0r: (CAN_BASE + 0x184) as *mut u32,
            tdl0r: (CAN_BASE + 0x188) as *mut u32,
            tdh0r: (CAN_BASE + 0x18C) as *mut u32,
            ri0r: (CAN_BASE + 0x1B0) as *mut u32,
            rdtr: (CAN_BASE + 0x1B4) as *mut u32,
            rdl0r: (CAN_BASE + 0x1B8) as *mut u32,
            rdh0r: (CAN_BASE + 0x1BC) as *mut u32,
            fmr: (CAN_BASE + 0x200) as *mut u32,
            fs1r: (CAN_BASE + 0x204) as *mut u32,
            ffa1r: (CAN_BASE + 0x214) as *mut u32,
            fa1r: (CAN_BASE + 0x21C) as *mut u32,
            fir0: (CAN_BASE + 0x240) as *mut u32,
            fir1: (CAN_BASE + 0x244) as *mut u32,
        }
    }

    unsafe fn read(&self, ptr: *mut u32) -> u32 {
        ptr.read_volatile()
    }

    unsafe fn write(&self, ptr: *mut u32, val: u32) {
        ptr.write_volatile(val);
    }
}

/* ============================================================
 * CAN Driver
 * ============================================================ */

struct CanDriver {
    regs: CanRegisters,
}

impl CanDriver {
    const fn new() -> Self {
        Self {
            regs: CanRegisters::new(),
        }
    }

    unsafe fn init(&self) {
        let rcc_apb1enr = 0x4002_101C as *mut u32;
        let rcc_apb2enr = 0x4002_1018 as *mut u32;
        let gpioa_crh = 0x4001_0804 as *mut u32;
        let gpioa_odr = 0x4001_080C as *mut u32;

        // Enable clocks
        rcc_apb1enr.write_volatile(rcc_apb1enr.read_volatile() | (1 << 25));
        rcc_apb2enr.write_volatile(rcc_apb2enr.read_volatile() | (1 << 2));

        // PA11 (RX) input pull-up, PA12 (TX) alt func push-pull
        let crh = gpioa_crh.read_volatile();
        gpioa_crh.write_volatile((crh & !(0xFF << 12)) | (0x8 << 12) | (0xB << 16));
        gpioa_odr.write_volatile(gpioa_odr.read_volatile() | (1 << 11));

        // Request init mode
        self.regs.write(self.regs.mcr, self.regs.read(self.regs.mcr) | (1 << 0));
        while self.regs.read(self.regs.msr) & (1 << 0) == 0 {}

        // Bit timing: 500kbps @ 36MHz
        self.regs.write(self.regs.btr,
            (0 << 30) | (0 << 24) | (12 << 16) | (1 << 20) | 3);

        // Filter bank 0: mask mode, 32-bit, accept 0x7E0-0x7EF
        self.regs.write(self.regs.fs1r, self.regs.read(self.regs.fs1r) | 1);
        self.regs.write(self.regs.fir0, 0x7E0 << 21);
        self.regs.write(self.regs.fir1, 0x7F0 << 21);
        self.regs.write(self.regs.fa1r, self.regs.read(self.regs.fa1r) | 1);

        // Leave init mode
        self.regs.write(self.regs.mcr, self.regs.read(self.regs.mcr) & !(1 << 0));
        while self.regs.read(self.regs.msr) & (1 << 0) != 0 {}

        // Enable RX FIFO0 interrupt
        self.regs.write(self.regs.ier, self.regs.read(self.regs.ier) | (1 << 1));
    }

    unsafe fn transmit(&self, frame: &CanFrame) -> bool {
        let tsr = self.regs.read(self.regs.tsr);

        // Find empty mailbox
        let (tir, tdtr, tdlr, tdhr) = if tsr & (1 << 26) != 0 {
            (self.regs.ti0r, self.regs.tdt0r, self.regs.tdl0r, self.regs.tdh0r)
        } else if tsr & (1 << 27) != 0 {
            (self.regs.ti0r + 0x10, self.regs.tdt0r + 0x10,
             self.regs.tdl0r + 0x10, self.regs.tdh0r + 0x10)
        } else if tsr & (1 << 28) != 0 {
            (self.regs.ti0r + 0x20, self.regs.tdt0r + 0x20,
             self.regs.tdl0r + 0x20, self.regs.tdh0r + 0x20)
        } else {
            return false;
        };

        let id_reg = match frame.id {
            CanId::Standard(id) => (id.as_raw() as u32) << 21,
            CanId::Extended(id) => (id.as_raw() << 3) | (1 << 2),
        };

        self.regs.write(tir, id_reg | ((frame.rtr as u32) << 1) | 1);
        self.regs.write(tdtr, frame.dlc as u32);
        self.regs.write(tdlr,
            (frame.data[3] as u32) << 24 | (frame.data[2] as u32) << 16 |
            (frame.data[1] as u32) << 8 | frame.data[0] as u32);
        self.regs.write(tdhr,
            (frame.data[7] as u32) << 24 | (frame.data[6] as u32) << 16 |
            (frame.data[5] as u32) << 8 | frame.data[4] as u32);

        true
    }

    unsafe fn receive(&self, frame: &mut CanFrame) -> bool {
        if self.regs.read(self.regs.rf0r) & 0x3 == 0 {
            return false;
        }

        let ri0r = self.regs.read(self.regs.ri0r);
        let extended = ri0r & (1 << 2) != 0;

        frame.id = if extended {
            CanId::Extended(ExtendedId::new((ri0r >> 3) & 0x1FFFFFFF).unwrap())
        } else {
            CanId::Standard(StandardId::new(((ri0r >> 21) & 0x7FF) as u16).unwrap())
        };

        frame.rtr = ri0r & (1 << 1) != 0;
        frame.dlc = (self.regs.read(self.regs.rdtr) & 0xF) as u8;

        let rdlr = self.regs.read(self.regs.rdl0r);
        let rdhr = self.regs.read(self.regs.rdh0r);

        frame.data[0] = (rdlr >> 0) as u8;
        frame.data[1] = (rdlr >> 8) as u8;
        frame.data[2] = (rdlr >> 16) as u8;
        frame.data[3] = (rdlr >> 24) as u8;
        frame.data[4] = (rdhr >> 0) as u8;
        frame.data[5] = (rdhr >> 8) as u8;
        frame.data[6] = (rdhr >> 16) as u8;
        frame.data[7] = (rdhr >> 24) as u8;

        // Release FIFO
        self.regs.write(self.regs.rf0r, self.regs.read(self.regs.rf0r) | (1 << 5));

        true
    }
}

static mut CAN: CanDriver = CanDriver::new();
static mut RX_FRAME_AVAILABLE: bool = false;
static mut RX_FRAME: CanFrame = CanFrame {
    id: CanId::Standard(StandardId(0)),
    rtr: false,
    data: [0; 8],
    dlc: 0,
};

/* ============================================================
 * OBD-II State Machine
 * ============================================================ */

const PID_SEQUENCE: [Obd2Pid; 5] = [
    Obd2Pid::CoolantTemp,
    Obd2Pid::EngineRpm,
    Obd2Pid::VehicleSpeed,
    Obd2Pid::IntakeAirTemp,
    Obd2Pid::FuelLevel,
];

static mut PID_INDEX: usize = 0;
static mut WAITING_RESPONSE: bool = false;
static mut SENSOR_RPM: u16 = 0;
static mut SENSOR_SPEED: u8 = 0;
static mut SENSOR_COOLANT: i16 = 0;

fn send_obd2_request(pid: Obd2Pid) {
    unsafe {
        let mut data = [0u8; 8];
        data[0] = 0x02;
        data[1] = 0x01;
        data[2] = pid.as_raw();

        let frame = CanFrame::new(
            CanId::Standard(StandardId::new(0x7DF).unwrap()),
            data, 8,
        );

        if CAN.transmit(&frame) {
            WAITING_RESPONSE = true;
        }
    }
}

fn process_obd2_response(frame: &CanFrame) {
    if frame.dlc < 4 {
        return;
    }
    if frame.data[1] != 0x41 {
        return;
    }

    let pid = frame.data[2];
    let payload = &frame.data[3..];

    // Find matching PID in our sequence
    for &p in &PID_SEQUENCE {
        if p.as_raw() == pid {
            if let Some(value) = p.parse(payload) {
                unsafe {
                    match value {
                        PidValue::Rpm(rpm) => SENSOR_RPM = rpm,
                        PidValue::Speed(speed) => SENSOR_SPEED = speed,
                        PidValue::CoolantTemp(temp) => SENSOR_COOLANT = temp,
                        _ => {}
                    }
                }
            }
            break;
        }
    }

    unsafe {
        WAITING_RESPONSE = false;
    }
}

/* ============================================================
 * GPIO and Delay
 * ============================================================ */

const RCC_APB2ENR: *mut u32 = 0x4002_1018 as _;
const GPIOC_CRH: *mut u32 = 0x4001_1004 as _;
const GPIOC_ODR: *mut u32 = 0x4001_100C as _;

fn delay_ms(ms: u32) {
    let systick = unsafe { &*cortex_m::peripheral::SYST::PTR };
    systick.set_reload(8000 - 1);
    systick.clear_current();
    systick.enable_counter();

    for _ in 0..ms {
        while !systick.has_wrapped() {}
    }

    systick.disable_counter();
}

/* ============================================================
 * Main
 * ============================================================ */

#[entry]
fn main() -> ! {
    unsafe {
        // Enable GPIOC
        (*RCC_APB2ENR) |= 1 << 4;
        let crh = (*GPIOC_CRH).read_volatile();
        (*GPIOC_CRH).write_volatile((crh & !(0xF << 20)) | (0x3 << 20));

        // Initialize CAN
        CAN.init();

        // Enable CAN RX interrupt in NVIC
        NVIC::unmask(cortex_m::interrupt::Interrupt::new(20));
    }

    // Request first PID
    send_obd2_request(PID_SEQUENCE[0]);

    loop {
        unsafe {
            if RX_FRAME_AVAILABLE {
                RX_FRAME_AVAILABLE = false;
                process_obd2_response(&RX_FRAME);

                // Toggle LED
                let odr = (*GPIOC_ODR).read_volatile();
                (*GPIOC_ODR).write_volatile(odr ^ (1 << 13));
            }

            if !WAITING_RESPONSE {
                PID_INDEX = (PID_INDEX + 1) % PID_SEQUENCE.len();
                send_obd2_request(PID_SEQUENCE[PID_INDEX]);
            }
        }

        delay_ms(50);
    }
}

#[interrupt]
fn USB_LP_CAN1_RX0() {
    unsafe {
        if CAN.receive(&mut RX_FRAME) {
            RX_FRAME_AVAILABLE = true;
        }
    }
}

#[exception]
fn HardFault(_ef: &ExceptionFrame) -> ! {
    loop {}
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
```

### Build and Run

```bash
cargo build --release
```

---

## Implementation: Ada

### Project Structure

```
can-ada/
├── can.gpr
├── src/
│   ├── can_types.ads
│   ├── can_driver.ads
│   ├── can_driver.adb
│   ├── obd2.ads
│   ├── obd2.adb
│   └── main.adb
```

### Project File (`can.gpr`)

```ada
project CAN is
   for Source_Dirs use ("src");
   for Object_Dir use "obj";
   for Main use ("main.adb");
   for Target use "arm-eabi";

   package Compiler is
      for Default_Switches ("Ada") use
        ("-O2", "-g", "-mcpu=cortex-m3", "-mthumb",
         "-fstack-check", "-gnatp", "-gnata");
   end Compiler;

   package Linker is
      for Default_Switches ("Ada") use
        ("-T", "linker.ld", "-nostartfiles");
   end Linker;
end CAN;
```

### CAN Types (`can_types.ads`)

```ada
with Interfaces; use Interfaces;

package Can_Types is

   -- Constrained subtype for standard IDs (11-bit)
   subtype Standard_Id is Unsigned_16 range 0 .. 16#7FF#;

   -- Constrained subtype for extended IDs (29-bit)
   subtype Extended_Id is Unsigned_32 range 0 .. 16#1FFF_FFFF#;

   -- DLC: 0 to 8 bytes
   subtype Can_Dlc is Unsigned_8 range 0 .. 8;

   -- CAN data payload
   type Can_Data is array (0 .. 7) of Unsigned_8;

   -- CAN frame with discriminated ID type
   type Can_Frame is record
      Id_Standard : Standard_Id := 0;
      Id_Extended : Extended_Id := 0;
      Is_Extended : Boolean := False;
      Is_Rtr      : Boolean := False;
      Dlc         : Can_Dlc := 0;
      Data        : Can_Data := (others => 0);
   end record;

   -- OBD-II PIDs (Service 01)
   type Obd2_Pid is
     (Pid_Coolant_Temp,
      Pid_Engine_Rpm,
      Pid_Vehicle_Speed,
      Pid_Intake_Air_Temp,
      Pid_Fuel_Level);

   for Obd2_Pid use
     (Pid_Coolant_Temp   => 16#05#,
      Pid_Engine_Rpm     => 16#0C#,
      Pid_Vehicle_Speed  => 16#0D#,
      Pid_Intake_Air_Temp => 16#0F#,
      Pid_Fuel_Level     => 16#2F#);

   -- Parsed sensor data
   type Sensor_Data is record
      Has_Rpm        : Boolean := False;
      Rpm            : Unsigned_16 := 0;
      Has_Speed      : Boolean := False;
      Speed_Kmh      : Unsigned_8 := 0;
      Has_Coolant    : Boolean := False;
      Coolant_Temp_C : Integer_16 := 0;
      Has_Intake     : Boolean := False;
      Intake_Temp_C  : Integer_16 := 0;
      Has_Fuel       : Boolean := False;
      Fuel_Level_Pct : Unsigned_8 := 0;
   end record;

end Can_Types;
```

### CAN Driver Spec (`can_driver.ads`)

```ada
with Can_Types; use Can_Types;

package Can_Driver is

   -- Initialize bxCAN peripheral at given bitrate
   procedure Initialize (Bitrate : Unsigned_32);

   -- Transmit a CAN frame
   function Transmit (Frame : Can_Frame) return Boolean;

   -- Receive a CAN frame from FIFO0
   function Receive (Frame : out Can_Frame) return Boolean;

   -- Configure filter bank in mask mode (32-bit)
   procedure Configure_Filter_Mask32
     (Bank : Unsigned_8;
      Id   : Standard_Id;
      Mask : Standard_Id);

   -- Enable RX FIFO0 interrupt
   procedure Enable_Rx_Interrupt;

   -- Check if TX mailbox is available
   function Tx_Ready return Boolean;

end Can_Driver;
```

### CAN Driver Body (`can_driver.adb`)

```ada
with System.Machine_Code; use System.Machine_Code;
with Interfaces; use Interfaces;

package body Can_Driver is

   CAN_BASE : constant := 16#4000_6400#;

   type CAN_Registers is record
      MCR   : Unsigned_32;
      MSR   : Unsigned_32;
      TSR   : Unsigned_32;
      RF0R  : Unsigned_32;
      IER   : Unsigned_32;
      BTR   : Unsigned_32;
   end record;

   for CAN_Registers use record
      MCR  at 16#000# range 0 .. 31;
      MSR  at 16#004# range 0 .. 31;
      TSR  at 16#008# range 0 .. 31;
      RF0R at 16#00C# range 0 .. 31;
      IER  at 16#014# range 0 .. 31;
      BTR  at 16#01C# range 0 .. 31;
   end record;

   for CAN_Registers'Size use 6 * 32;

   CAN : CAN_Registers with
     Address => System'To_Address (CAN_BASE),
     Volatile => True;

   -- Mailbox 0 registers
   CAN_TI0R  : Unsigned_32 with Address => System'To_Address (CAN_BASE + 16#180#), Volatile => True;
   CAN_TDT0R : Unsigned_32 with Address => System'To_Address (CAN_BASE + 16#184#), Volatile => True;
   CAN_TDL0R : Unsigned_32 with Address => System'To_Address (CAN_BASE + 16#188#), Volatile => True;
   CAN_TDH0R : Unsigned_32 with Address => System'To_Address (CAN_BASE + 16#18C#), Volatile => True;

   -- FIFO0 registers
   CAN_RI0R  : Unsigned_32 with Address => System'To_Address (CAN_BASE + 16#1B0#), Volatile => True;
   CAN_RDTR  : Unsigned_32 with Address => System'To_Address (CAN_BASE + 16#1B4#), Volatile => True;
   CAN_RDL0R : Unsigned_32 with Address => System'To_Address (CAN_BASE + 16#1B8#), Volatile => True;
   CAN_RDH0R : Unsigned_32 with Address => System'To_Address (CAN_BASE + 16#1BC#), Volatile => True;

   -- Filter registers
   CAN_FMR  : Unsigned_32 with Address => System'To_Address (CAN_BASE + 16#200#), Volatile => True;
   CAN_FS1R : Unsigned_32 with Address => System'To_Address (CAN_BASE + 16#204#), Volatile => True;
   CAN_FFA1R: Unsigned_32 with Address => System'To_Address (CAN_BASE + 16#214#), Volatile => True;
   CAN_FA1R : Unsigned_32 with Address => System'To_Address (CAN_BASE + 16#21C#), Volatile => True;
   CAN_FIR0 : Unsigned_32 with Address => System'To_Address (CAN_BASE + 16#240#), Volatile => True;
   CAN_FIR1 : Unsigned_32 with Address => System'To_Address (CAN_BASE + 16#244#), Volatile => True;

   -- RCC
   RCC_APB1ENR : Unsigned_32 with Address => System'To_Address (16#4002_101C#), Volatile => True;
   RCC_APB2ENR : Unsigned_32 with Address => System'To_Address (16#4002_1018#), Volatile => True;
   GPIOA_CRH   : Unsigned_32 with Address => System'To_Address (16#4001_0804#), Volatile => True;
   GPIOA_ODR   : Unsigned_32 with Address => System'To_Address (16#4001_080C#), Volatile => True;

   procedure Initialize (Bitrate : Unsigned_32) is
      pragma Unreferenced (Bitrate);
   begin
      -- Enable clocks
      RCC_APB1ENR := RCC_APB1ENR or (1 << 25);
      RCC_APB2ENR := RCC_APB2ENR or (1 << 2);

      -- PA11 RX, PA12 TX
      declare
         CRH : constant Unsigned_32 := GPIOA_CRH;
      begin
         GPIOA_CRH := (CRH and not (16#FF# << 12)) or
                      (16#8# << 12) or (16#B# << 16);
      end;
      GPIOA_ODR := GPIOA_ODR or (1 << 11);

      -- Request init mode
      CAN.MCR := CAN.MCR or (1 << 0);
      while (CAN.MSR and (1 << 0)) = 0 loop
         null;
      end loop;

      -- Bit timing: 500kbps @ 36MHz
      CAN.BTR := (0 << 30) or (0 << 24) or (12 << 16) or (1 << 20) or 3;

      -- Filter: accept 0x7E0-0x7EF
      CAN_FS1R := CAN_FS1R or 1;
      CAN_FIR0 := Unsigned_32 (16#7E0#) << 21;
      CAN_FIR1 := Unsigned_32 (16#7F0#) << 21;
      CAN_FA1R := CAN_FA1R or 1;

      -- Leave init mode
      CAN.MCR := CAN.MCR and not (1 << 0);
      while (CAN.MSR and (1 << 0)) /= 0 loop
         null;
      end loop;

      -- Enable RX interrupt
      CAN.IER := CAN.IER or (1 << 1);
   end Initialize;

   function Transmit (Frame : Can_Frame) return Boolean is
      Tsr_Val : Unsigned_32;
   begin
      Tsr_Val := CAN.TSR;

      if (Tsr_Val and (1 << 26)) /= 0 then
         -- Mailbox 0
         if Frame.Is_Extended then
            CAN_TI0R := (Frame.Id_Extended << 3) or (1 << 2) or
                        (if Frame.Is_Rtr then 2 else 0) or 1;
         else
            CAN_TI0R := (Unsigned_32 (Frame.Id_Standard) << 21) or
                        (if Frame.Is_Rtr then 2 else 0) or 1;
         end if;
         CAN_TDT0R := Unsigned_32 (Frame.Dlc);
         CAN_TDL0R := (Unsigned_32 (Frame.Data (3)) << 24) or
                      (Unsigned_32 (Frame.Data (2)) << 16) or
                      (Unsigned_32 (Frame.Data (1)) << 8) or
                      Unsigned_32 (Frame.Data (0));
         CAN_TDH0R := (Unsigned_32 (Frame.Data (7)) << 24) or
                      (Unsigned_32 (Frame.Data (6)) << 16) or
                      (Unsigned_32 (Frame.Data (5)) << 8) or
                      Unsigned_32 (Frame.Data (4));
         return True;
      end if;

      return False;
   end Transmit;

   function Receive (Frame : out Can_Frame) return Boolean is
      Rf0r_Val : Unsigned_32;
      Ri0r_Val : Unsigned_32;
   begin
      Rf0r_Val := CAN.RF0R;
      if (Rf0r_Val and 16#3#) = 0 then
         return False;
      end if;

      Ri0r_Val := CAN_RI0R;
      Frame.Is_Extended := (Ri0r_Val and (1 << 2)) /= 0;
      Frame.Is_Rtr := (Ri0r_Val and (1 << 1)) /= 0;

      if Frame.Is_Extended then
         Frame.Id_Extended := (Ri0r_Val >> 3) and 16#1FFF_FFFF#;
      else
         Frame.Id_Standard := Standard_Id ((Ri0r_Val >> 21) and 16#7FF#);
      end if;

      Frame.Dlc := Can_Dlc (CAN_RDTR and 16#F#);

      declare
         Rdlr : constant Unsigned_32 := CAN_RDL0R;
         Rdhr : constant Unsigned_32 := CAN_RDH0R;
      begin
         Frame.Data (0) := Unsigned_8 (Rdlr and 16#FF#);
         Frame.Data (1) := Unsigned_8 ((Rdlr >> 8) and 16#FF#);
         Frame.Data (2) := Unsigned_8 ((Rdlr >> 16) and 16#FF#);
         Frame.Data (3) := Unsigned_8 ((Rdlr >> 24) and 16#FF#);
         Frame.Data (4) := Unsigned_8 (Rdhr and 16#FF#);
         Frame.Data (5) := Unsigned_8 ((Rdhr >> 8) and 16#FF#);
         Frame.Data (6) := Unsigned_8 ((Rdhr >> 16) and 16#FF#);
         Frame.Data (7) := Unsigned_8 ((Rdhr >> 24) and 16#FF#);
      end;

      -- Release FIFO
      CAN.RF0R := CAN.RF0R or (1 << 5);

      return True;
   end Receive;

   procedure Configure_Filter_Mask32
     (Bank : Unsigned_8;
      Id   : Standard_Id;
      Mask : Standard_Id)
   is
      pragma Unreferenced (Bank);
   begin
      CAN_FIR0 := Unsigned_32 (Id) << 21;
      CAN_FIR1 := Unsigned_32 (Mask) << 21;
      CAN_FA1R := CAN_FA1R or 1;
   end Configure_Filter_Mask32;

   procedure Enable_Rx_Interrupt is
   begin
      CAN.IER := CAN.IER or (1 << 1);
   end Enable_Rx_Interrupt;

   function Tx_Ready return Boolean is
   begin
      return (CAN.TSR and (16#7# << 26)) /= 0;
   end Tx_Ready;

end Can_Driver;
```

### OBD-II Package (`obd2.ads`)

```ada
with Can_Types; use Can_Types;

package Obd2 is

   type Obd2_State is (Idle, Sending, Waiting_Response, Processing, Error);

   procedure Initialize;
   procedure Request_Pid (Pid : Obd2_Pid);
   function  Get_State return Obd2_State;
   function  Process_Frame (Frame : Can_Frame) return Boolean;
   procedure Get_Data (Data : out Sensor_Data);
   procedure Tick;

end Obd2;
```

### OBD-II Body (`obd2.adb`)

```ada
with Can_Driver;

package body Obd2 is

   Current_State : Obd2_State := Idle;
   Sensor_Data   : Can_Types.Sensor_Data;
   Requested_Pid : Obd2_Pid;
   Tick_Count    : Natural := 0;
   Timeout_Limit : constant Natural := 500;

   OBD2_Request_Id  : constant Standard_Id := 16#7DF#;
   OBD2_Response_Id : constant Standard_Id := 16#7E8#;

   procedure Initialize is
   begin
      Sensor_Data := (others => <>);
      Current_State := Idle;
      Tick_Count := 0;
   end Initialize;

   procedure Request_Pid (Pid : Obd2_Pid) is
      Frame : Can_Frame;
   begin
      if Current_State /= Idle then
         return;
      end if;

      Frame := (Id_Standard => OBD2_Request_Id,
                Id_Extended => 0,
                Is_Extended => False,
                Is_Rtr      => False,
                Dlc         => 8,
                Data        => (0 => 2, 1 => 1, 2 => Unsigned_8'Val (Obd2_Pid'Pos (Pid)),
                                others => 0));

      if Can_Driver.Transmit (Frame) then
         Current_State := Waiting_Response;
         Requested_Pid := Pid;
         Timeout_Limit := Tick_Count + 500;
      else
         Current_State := Error;
      end if;
   end Request_Pid;

   function Get_State return Obd2_State is
   begin
      return Current_State;
   end Get_State;

   function Process_Frame (Frame : Can_Frame) return Boolean is
   begin
      if Frame.Id_Standard /= OBD2_Response_Id then
         return False;
      end if;
      if Current_State /= Waiting_Response then
         return False;
      end if;
      if Frame.Dlc < 4 then
         return False;
      end if;
      if Frame.Data (1) /= 16#41# then
         return False;
      end if;
      if Frame.Data (2) /= Unsigned_8'Val (Obd2_Pid'Pos (Requested_Pid)) then
         return False;
      end if;

      Current_State := Processing;

      case Requested_Pid is
         when Pid_Engine_Rpm =>
            Sensor_Data.Rpm :=
              (Unsigned_16 (Frame.Data (3)) * 256 +
               Unsigned_16 (Frame.Data (4))) / 4;
            Sensor_Data.Has_Rpm := True;

         when Pid_Vehicle_Speed =>
            Sensor_Data.Speed_Kmh := Frame.Data (3);
            Sensor_Data.Has_Speed := True;

         when Pid_Coolant_Temp =>
            Sensor_Data.Coolant_Temp_C :=
              Integer_16 (Frame.Data (3)) - 40;
            Sensor_Data.Has_Coolant := True;

         when Pid_Intake_Air_Temp =>
            Sensor_Data.Intake_Temp_C :=
              Integer_16 (Frame.Data (3)) - 40;
            Sensor_Data.Has_Intake := True;

         when Pid_Fuel_Level =>
            Sensor_Data.Fuel_Level_Pct :=
              Unsigned_8 ((Unsigned_16 (Frame.Data (3)) * 100) / 255);
            Sensor_Data.Has_Fuel := True;
      end case;

      Current_State := Idle;
      return True;
   end Process_Frame;

   procedure Get_Data (Data : out Sensor_Data) is
   begin
      Data := Sensor_Data;
   end Get_Data;

   procedure Tick is
   begin
      Tick_Count := Tick_Count + 1;

      if Current_State = Waiting_Response then
         if Tick_Count >= Timeout_Limit then
            Current_State := Idle;
         end if;
      end if;
   end Tick;

end Obd2;
```

### Main Application (`main.adb`)

```ada
with System.Machine_Code; use System.Machine_Code;
with Can_Driver;
with Can_Types; use Can_Types;
with Obd2;

procedure Main is

   type UInt32 is mod 2**32;

   RCC_APB2ENR : UInt32 with
     Address => System'To_Address (16#4002_1018#),
     Volatile => True;

   GPIOC_CRH : UInt32 with
     Address => System'To_Address (16#4001_1004#),
     Volatile => True;

   GPIOC_ODR : UInt32 with
     Address => System'To_Address (16#4001_100C#),
     Volatile => True;

   Rx_Frame_Available : Boolean := False;
   Rx_Frame : Can_Frame;

   Pid_Sequence : constant array (0 .. 4) of Obd2_Pid :=
     (Pid_Coolant_Temp, Pid_Engine_Rpm, Pid_Vehicle_Speed,
      Pid_Intake_Air_Temp, Pid_Fuel_Level);
   Pid_Index : Natural := 0;

   procedure Delay_MS (MS : Natural) is
      Count : Natural := MS * 8000;
   begin
      while Count > 0 loop
         Count := Count - 1;
      end loop;
   end Delay_MS;

begin
   -- Enable GPIOC
   RCC_APB2ENR := RCC_APB2ENR or (1 << 4);
   declare
      CRH : constant UInt32 := GPIOC_CRH;
   begin
      GPIOC_CRH := (CRH and not (16#F# << 20)) or (16#3# << 20);
   end;

   -- Initialize CAN and OBD-II
   Can_Driver.Initialize (500_000);
   Obd2.Initialize;

   -- Request first PID
   Obd2.Request_Pid (Pid_Sequence (0));

   loop
      if Rx_Frame_Available then
         Rx_Frame_Available := False;
         if Obd2.Process_Frame (Rx_Frame) then
            GPIOC_ODR := GPIOC_ODR xor (1 << 13);
         end if;
      end if;

      if Obd2.Get_State = Idle then
         Pid_Index := (Pid_Index + 1) mod 5;
         Obd2.Request_Pid (Pid_Sequence (Pid_Index));
      end if;

      Obd2.Tick;
      Delay_MS (50);
   end loop;

end Main;

-- CAN RX interrupt handler
procedure USB_LP_CAN1_RX0_Interrupt is
   pragma Interrupt;
   pragma Export (C, USB_LP_CAN1_RX0_Interrupt,
                  "USB_LP_CAN1_RX0_IRQHandler");
begin
   if Can_Driver.Receive (Rx_Frame) then
      Rx_Frame_Available := True;
   end if;
end USB_LP_CAN1_RX0_Interrupt;
```

### Build

```bash
gprbuild -P can.gpr
```

---

## Implementation: Zig

### Project Structure

```
can-zig/
├── build.zig
├── linker.ld
├── src/
│   └── main.zig
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

    const exe = b.addExecutable(.{
        .name = "can",
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = .ReleaseSmall,
    });

    exe.entry = .disabled;
    exe.setLinkerScript(b.path("linker.ld"));
    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());

    const run_step = b.step("run", "Run in Renode");
    run_step.dependOn(&run_cmd.step);
}
```

### Linker Script (`linker.ld`)

```ld
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 256K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 64K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .text :
    {
        KEEP(*(.vectors))
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

### `src/main.zig`

```zig
const std = @import("std");

// ============================================================
// CAN Frame — zero-copy with packed structs
// ============================================================

/// Packed CAN ID register layout (for hardware register access)
const CanIdReg = packed struct(u32) {
    txrq: bool,
    rtr: bool,
    ide: bool,
    ext_id: u29,      // Extended ID (29 bits)
    _padding: u0,     // Align to 32 bits
};

/// Packed standard ID register layout
const CanStdIdReg = packed struct(u32) {
    txrq: bool,
    rtr: bool,
    _r0: bool,
    std_id: u11,      // Standard ID (11 bits)
    _padding: u17,
};

/// CAN frame — zero-copy friendly
const CanFrame = extern struct {
    id: u32,
    extended: bool,
    rtr: bool,
    dlc: u4,
    data: [8]u8,

    /// Comptime-validated constructor for standard IDs
    pub fn standard(id: u11, data: [8]u8, dlc: u4) CanFrame {
        std.debug.assert(id <= 0x7FF);
        return .{
            .id = id,
            .extended = false,
            .rtr = false,
            .dlc = dlc,
            .data = data,
        };
    }

    /// Comptime-validated constructor for extended IDs
    pub fn extended(id: u29, data: [8]u8, dlc: u4) CanFrame {
        std.debug.assert(id <= 0x1FFFFFFF);
        return .{
            .id = id,
            .extended = true,
            .rtr = false,
            .dlc = dlc,
            .data = data,
        };
    }

    /// Get payload slice (respects DLC)
    pub fn payload(self: *const CanFrame) []const u8 {
        return self.data[0..self.dlc];
    }
};

// Comptime validation of frame constructors
comptime {
    const f1 = CanFrame.standard(0x7DF, [_]u8{ 0x02, 0x01, 0x0C, 0, 0, 0, 0, 0 }, 8);
    std.debug.assert(f1.extended == false);
    std.debug.assert(f1.dlc == 8);

    const f2 = CanFrame.extended(0x18DAF110, [_]u8{ 0x02, 0x01, 0x0C, 0, 0, 0, 0, 0 }, 8);
    std.debug.assert(f2.extended == true);
}

// ============================================================
// OBD-II PIDs — comptime-validated
// ============================================================

const Obd2Pid = enum(u8) {
    coolant_temp = 0x05,
    engine_rpm = 0x0C,
    vehicle_speed = 0x0D,
    intake_air_temp = 0x0F,
    engine_load = 0x04,
    throttle_pos = 0x11,
    fuel_level = 0x2F,
};

const Obd2Request = packed struct {
    num_bytes: u8 = 0x02,
    service: u8 = 0x01,
    pid: Obd2Pid,
    padding: [5]u8 = [_]u8{0} ** 5,
};

comptime {
    // Verify request structure is exactly 8 bytes
    std.debug.assert(@sizeOf(Obd2Request) == 8);
}

const PidValue = union(enum) {
    rpm: u16,
    speed: u8,
    coolant_temp: i16,
    intake_temp: i16,
    fuel_level: u8,
};

/// Parse PID response data — returns null on invalid data
fn parsePidResponse(pid: Obd2Pid, data: []const u8) ?PidValue {
    return switch (pid) {
        .engine_rpm => if (data.len >= 2)
            PidValue{ .rpm = ((@as(u16, data[0]) << 8) | data[1]) / 4 }
        else
            null,
        .vehicle_speed => if (data.len >= 1)
            PidValue{ .speed = data[0] }
        else
            null,
        .coolant_temp => if (data.len >= 1)
            PidValue{ .coolant_temp = @as(i16, @intCast(data[0])) - 40 }
        else
            null,
        .intake_air_temp => if (data.len >= 1)
            PidValue{ .intake_temp = @as(i16, @intCast(data[0])) - 40 }
        else
            null,
        .fuel_level => if (data.len >= 1)
            PidValue{ .fuel_level = @as(u8, @intCast((@as(u16, data[0]) * 100) / 255)) }
        else
            null,
        else => null,
    };
}

// ============================================================
// bxCAN Register Abstraction
// ============================================================

const CanRegs = extern struct {
    mcr: volatile u32,
    msr: volatile u32,
    tsr: volatile u32,
    rf0r: volatile u32,
    ier: volatile u32,
    btr: volatile u32,
    ti0r: volatile u32,
    tdt0r: volatile u32,
    tdl0r: volatile u32,
    tdh0r: volatile u32,
    ri0r: volatile u32,
    rdtr: volatile u32,
    rdl0r: volatile u32,
    rdh0r: volatile u32,
    fmr: volatile u32,
    fs1r: volatile u32,
    ffa1r: volatile u32,
    fa1r: volatile u32,
    fir0: volatile u32,
    fir1: volatile u32,
};

const CAN_BASE: u32 = 0x40006400;
const can = @as(*CanRegs, @ptrFromInt(CAN_BASE));

// RCC
const RCC_APB1ENR = @as(*volatile u32, @ptrFromInt(0x4002101C));
const RCC_APB2ENR = @as(*volatile u32, @ptrFromInt(0x40021018));
const GPIOA_CRH = @as(*volatile u32, @ptrFromInt(0x40010804));
const GPIOA_ODR = @as(*volatile u32, @ptrFromInt(0x4001080C));

// NVIC
const NVIC_ISER0 = @as(*volatile u32, @ptrFromInt(0xE000E100));

// ============================================================
// CAN Driver
// ============================================================

fn can_init() void {
    // Enable clocks
    RCC_APB1ENR.* |= (1 << 25);
    RCC_APB2ENR.* |= (1 << 2);

    // PA11 RX (input pull-up), PA12 TX (alt func push-pull)
    const crh = GPIOA_CRH.*;
    GPIOA_CRH.* = (crh & ~(@as(u32, 0xFF) << 12)) | (@as(u32, 0x8) << 12) | (@as(u32, 0xB) << 16);
    GPIOA_ODR.* |= (1 << 11);

    // Request init mode
    can.mcr |= (1 << 0);
    while (can.msr & (1 << 0) == 0) {}

    // Bit timing: 500kbps @ 36MHz
    can.btr = (0 << 30) | (0 << 24) | (12 << 16) | (1 << 20) | 3;

    // Filter bank 0: mask mode, 32-bit, accept 0x7E0-0x7EF
    can.fs1r |= 1;
    can.fir0 = 0x7E0 << 21;
    can.fir1 = 0x7F0 << 21;
    can.fa1r |= 1;

    // Leave init mode
    can.mcr &= ~(@as(u32, 1) << 0);
    while (can.msr & (1 << 0) != 0) {}

    // Enable RX FIFO0 interrupt
    can.ier |= (1 << 1);
    NVIC_ISER0.* |= (1 << 20);
}

fn can_transmit(frame: *const CanFrame) bool {
    const tsr = can.tsr;

    const tir: *volatile u32 = if (tsr & (1 << 26) != 0)
        &can.ti0r
    else if (tsr & (1 << 27) != 0)
        @as(*volatile u32, @ptrFromInt(CAN_BASE + 0x190))
    else if (tsr & (1 << 28) != 0)
        @as(*volatile u32, @ptrFromInt(CAN_BASE + 0x1A0))
    else
        return false;

    const tdtr = @as(*volatile u32, @ptrFromInt(@intFromPtr(tir) + 0x4));
    const tdlr = @as(*volatile u32, @ptrFromInt(@intFromPtr(tir) + 0x8));
    const tdhr = @as(*volatile u32, @ptrFromInt(@intFromPtr(tir) + 0xC));

    if (frame.extended) {
        tir.* = (frame.id << 3) | (1 << 2) | (@as(u32, @intFromBool(frame.rtr)) << 1) | 1;
    } else {
        tir.* = (frame.id << 21) | (@as(u32, @intFromBool(frame.rtr)) << 1) | 1;
    }

    tdtr.* = frame.dlc;
    tdlr.* = (@as(u32, frame.data[3]) << 24) | (@as(u32, frame.data[2]) << 16) |
             (@as(u32, frame.data[1]) << 8) | frame.data[0];
    tdhr.* = (@as(u32, frame.data[7]) << 24) | (@as(u32, frame.data[6]) << 16) |
             (@as(u32, frame.data[5]) << 8) | frame.data[4];

    return true;
}

fn can_receive(frame: *CanFrame) bool {
    if (can.rf0r & 0x3 == 0) return false;

    const ri0r = can.ri0r;
    frame.extended = ri0r & (1 << 2) != 0;
    frame.rtr = ri0r & (1 << 1) != 0;

    if (frame.extended) {
        frame.id = (ri0r >> 3) & 0x1FFFFFFF;
    } else {
        frame.id = (ri0r >> 21) & 0x7FF;
    }

    frame.dlc = @truncate(can.rdtr & 0xF);

    const rdlr = can.rdl0r;
    const rdhr = can.rdh0r;

    frame.data[0] = @truncate(rdlr >> 0);
    frame.data[1] = @truncate(rdlr >> 8);
    frame.data[2] = @truncate(rdlr >> 16);
    frame.data[3] = @truncate(rdlr >> 24);
    frame.data[4] = @truncate(rdhr >> 0);
    frame.data[5] = @truncate(rdhr >> 8);
    frame.data[6] = @truncate(rdhr >> 16);
    frame.data[7] = @truncate(rdhr >> 24);

    // Release FIFO
    can.rf0r |= (1 << 5);

    return true;
}

// ============================================================
// OBD-II State Machine
// ============================================================

const Obd2State = enum {
    idle,
    waiting_response,
    processing,
    error,
};

var current_state: Obd2State = .idle;
var requested_pid: Obd2Pid = .coolant_temp;
var tick_counter: u32 = 0;
var request_timeout: u32 = 0;

var sensor_rpm: u16 = 0;
var sensor_speed: u8 = 0;
var sensor_coolant: i16 = 0;
var sensor_intake: i16 = 0;
var sensor_fuel: u8 = 0;

var rx_frame_available: bool = false;
var rx_frame: CanFrame = undefined;

const pid_sequence = [_]Obd2Pid{
    .coolant_temp,
    .engine_rpm,
    .vehicle_speed,
    .intake_air_temp,
    .fuel_level,
};
var pid_index: usize = 0;

fn obd2_request_pid(pid: Obd2Pid) void {
    if (current_state != .idle) return;

    const req = Obd2Request{ .pid = pid };
    const req_bytes = std.mem.asBytes(&req);

    var data: [8]u8 = undefined;
    @memcpy(&data, req_bytes);

    var frame = CanFrame.standard(0x7DF, data, 8);

    if (can_transmit(&frame)) {
        current_state = .waiting_response;
        requested_pid = pid;
        request_timeout = tick_counter + 500;
    } else {
        current_state = .error;
    }
}

fn obd2_process_frame(frame: *const CanFrame) bool {
    if (frame.id != 0x7E8) return false;
    if (current_state != .waiting_response) return false;
    if (frame.dlc < 4) return false;
    if (frame.data[1] != 0x41) return false;
    if (frame.data[2] != @intFromEnum(requested_pid)) return false;

    current_state = .processing;

    const payload = frame.payload();
    if (parsePidResponse(requested_pid, payload)) |value| {
        switch (value) {
            .rpm => |v| sensor_rpm = v,
            .speed => |v| sensor_speed = v,
            .coolant_temp => |v| sensor_coolant = v,
            .intake_temp => |v| sensor_intake = v,
            .fuel_level => |v| sensor_fuel = v,
        }
    }

    current_state = .idle;
    return true;
}

fn obd2_tick() void {
    tick_counter += 1;
    if (current_state == .waiting_response and tick_counter >= request_timeout) {
        current_state = .idle;
    }
}

// ============================================================
// GPIO and Delay
// ============================================================

const RCC_APB2ENR_CAN = @as(*volatile u32, @ptrFromInt(0x40021018));
const GPIOC_CRH_CAN = @as(*volatile u32, @ptrFromInt(0x40011004));
const GPIOC_ODR_CAN = @as(*volatile u32, @ptrFromInt(0x4001100C));

fn delay_ms(ms: u32) void {
    const rvr = @as(*volatile u32, @ptrFromInt(0xE000E014));
    const cvr = @as(*volatile u32, @ptrFromInt(0xE000E018));
    const csr = @as(*volatile u32, @ptrFromInt(0xE000E010));

    rvr.* = 8000 - 1;
    cvr.* = 0;
    csr.* = 0x5;

    var m: u32 = 0;
    while (m < ms) : (m += 1) {
        while (csr.* & (1 << 16) == 0) {}
    }
    csr.* = 0;
}

// ============================================================
// CAN RX Interrupt
// ============================================================

export fn USB_LP_CAN1_RX0_IRQHandler() void {
    if (can_receive(&rx_frame)) {
        rx_frame_available = true;
    }
}

// ============================================================
// Reset Handler and Main
// ============================================================

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
    // Enable GPIOC
    RCC_APB2ENR_CAN.* |= (1 << 4);
    const crh = GPIOC_CRH_CAN.*;
    GPIOC_CRH_CAN.* = (crh & ~(@as(u32, 0xF) << 20)) | (@as(u32, 0x3) << 20);

    // Initialize CAN
    can_init();

    // Request first PID
    obd2_request_pid(pid_sequence[pid_index]);

    while (true) {
        if (rx_frame_available) {
            rx_frame_available = false;
            if (obd2_process_frame(&rx_frame)) {
                GPIOC_ODR_CAN.* ^= (1 << 13);
            }
        }

        if (current_state == .idle) {
            pid_index = (pid_index + 1) % pid_sequence.len;
            obd2_request_pid(pid_sequence[pid_index]);
        }

        obd2_tick();
        delay_ms(50);
    }
}

// Vector table
comptime {
    _ = @export(&Reset_Handler, .{ .name = "Reset_Handler", .linkage = .strong });
    _ = @export(&USB_LP_CAN1_RX0_IRQHandler, .{ .name = "USB_LP_CAN1_RX0_IRQHandler", .linkage = .strong });
}
```

### Build and Run

```bash
zig build
```

---

## Renode Verification

### Renode Script (`can.resc`)

```
# Create two STM32F103 nodes on a CAN bus
$bus?=@BusBus

$ecu?=@STM32F103
$ecu.bus -> $bus

$node?=@STM32F103
$node.bus -> $bus

# Load firmware
sysbus LoadELF @rtos.elf
    $node LoadELF @can.elf

# Start both nodes
start
```

### Running the Simulation

```bash
renode --disable-gui can.resc
```

### Verification Steps

1. **Check CAN bus activity**: Renode logs show frames transmitted and received on the bus.
2. **Verify filter configuration**: Only frames matching the configured filter (0x7E0-0x7EF) should reach the RX FIFO.
3. **Verify OBD-II responses**: The ECU node should respond to service 01 PID requests with properly formatted data.
4. **Check LED toggling**: Each successful OBD-II response should toggle PC13.

### Renode Monitor Commands

```
(monitor) showAnalyzer sysbus.uart1   # View UART output
(monitor) exec @sysbus 1000000        # Run for 1M instructions
(monitor) log Level:Debug             # Enable debug logging
(monitor) showPeripherals             # List all peripherals
```

---

## Deliverables

- [ ] CAN frame struct with standard/extended ID, DLC, 8-byte payload
- [ ] bxCAN initialization: clock enable, GPIO config, bit timing, filter setup
- [ ] Transmit function using mailbox selection (check TME bits)
- [ ] Receive function reading from FIFO0 and releasing with RFOM
- [ ] Filter configuration: mask mode (32-bit) and list mode (32-bit)
- [ ] OBD-II service 01 request builder (0x7DF, 8-byte frame)
- [ ] OBD-II response parser (RPM, speed, coolant temp, intake temp, fuel level)
- [ ] State machine: IDLE → SENDING → WAITING → PROCESSING → IDLE
- [ ] RX interrupt handler: read frame from FIFO, signal processing task
- [ ] Timeout handling for unresponsive ECUs
- [ ] All four language implementations (C, Rust, Ada, Zig)

---

## Language Comparison

| Feature | C | Rust | Ada | Zig |
|---|---|---|---|---|
| **CAN frame** | Struct with `uint32_t id`, `bool extended` | `CanId` enum (`Standard`/`Extended`), `CanFrame` with `payload()` | Record with discriminated `Is_Extended`, constrained subtypes for IDs | `extern struct` with `standard()`/`extended()` constructors |
| **ID validation** | Runtime check (or none) | `StandardId::new()` returns `Option`, compile-time bounds | `subtype Standard_Id is range 0 .. 16#7FF#` (enforced) | `comptime { std.debug.assert(id <= 0x7FF) }` |
| **Register access** | `#define` macros with volatile pointers | `*mut u32` with `read_volatile`/`write_volatile` | `with Address => ..., Volatile => True` | `@as(*volatile u32, @ptrFromInt(addr))` |
| **Bit timing config** | Manual bit shifts | Manual bit shifts | Manual bit shifts | Manual bit shifts |
| **Filter config** | Direct register writes | Wrapped in `CanDriver` methods | Packaged procedure with typed parameters | Direct register writes in `can_init()` |
| **OBD-II PID** | `#define` constants | `enum Obd2Pid` with `parse()` method | `type Obd2_Pid is (...)` with representation clause | `enum(u8)` with `parsePidResponse()` |
| **Response parsing** | Switch on PID, manual byte extraction | `PidValue` union enum, type-safe `parse()` | Case statement with typed fields | `union(enum)` with `parsePidResponse()` |
| **Frame layout** | Manual byte packing | Struct with explicit fields | Record with explicit fields | `packed struct` for hardware register layout |
| **Zero-copy** | None — always copies | `payload()` returns slice | None — copies to local record | `payload()` returns slice, `extern struct` matches wire format |
| **Comptime checks** | None | `const fn` for compile-time validation | Compile-time range checks on subtypes | `comptime { std.debug.assert(...) }` |

---

## What You Learned

- CAN physical layer: differential signaling, dominant/recessive bits, wired-AND bus
- Bitwise arbitration: how the lowest ID wins without collision damage
- CAN frame format: standard (11-bit) vs extended (29-bit) identifiers
- bxCAN peripheral: transmit mailboxes, receive FIFOs, filter banks
- Filter configuration: mask mode (pattern matching) vs list mode (exact match)
- OBD-II protocol: service 01 PIDs, request/response format, functional vs physical addressing
- Deterministic message handling: ISR reads frame, task processes it
- How each language approaches CAN:
  - C: Direct register access with macros, manual bit packing
  - Rust: Type-safe `CanId` enum, `Option` for validated IDs, `bxcan` crate integration
  - Ada: Constrained subtypes for IDs, strong typing throughout
  - Zig: `packed struct` for register layout, `extern struct` for wire format, comptime validation

## Next Steps

- **Project 12**: Build a multi-sensor data logger with SD card and FAT32
- Implement CAN FD (Flexible Data-rate) with up to 64-byte payloads
- Add UDS (Unified Diagnostic Services) protocol support (services 0x10, 0x22, 0x27, 0x31)
- Build a CANopen stack with NMT state machine and SDO/PDO communication
- Add J1939 support for heavy-duty vehicles (29-bit IDs, PGN/SPN)
- Implement a CAN bus sniffer/decoder with real-time frame display
- Compare your driver's latency to a production CAN stack (SocketCAN, CANopenNode)