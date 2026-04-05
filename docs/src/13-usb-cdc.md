---
title: "Project 13: USB CDC Device"
phase: 5
project: 13
---

# Project 13: USB CDC Device

## Introduction

In this project, you will implement a USB Communications Device Class (CDC) Abstract Control Model (ACM) device — a virtual serial port — from scratch in C, Rust, Ada, and Zig. This is one of the most practically useful USB device classes and serves as the foundation for understanding how USB enumeration, descriptor negotiation, and bulk data transfers work at the protocol level.

You will build a USB device stack that handles enumeration, responds to standard and class-specific requests, and implements bidirectional bulk transfers over the CDC-ACM profile. When connected to a host, your device will appear as `/dev/ttyACM0` (Linux) or `COMx` (Windows).

> **Tip:** If you completed Project 5 (UART Communication), you already understand serial communication from the MCU side. This project adds the USB transport layer that makes your MCU appear as a serial device to a host PC.

### What You'll Learn

- USB 2.0 protocol fundamentals: differential signaling, NRZI encoding, bit stuffing
- USB device states and the enumeration process
- USB descriptor hierarchy and parsing
- CDC-ACM class specification and virtual serial port emulation
- Bulk endpoint management and buffer handling
- QEMU USB device testing and hardware validation
- Language-specific approaches to USB stack implementation

## USB Protocol Fundamentals

### Physical Layer

USB 2.0 Full-Speed (12 Mbps) uses **differential signaling** on two wires: D+ and D-. The receiver detects the voltage difference between these lines, which provides excellent noise immunity.

| Signal | Logic 0 (SE0) | Logic 0 (J) | Logic 1 (K) |
|--------|--------------|-------------|-------------|
| D+     | 0.0–0.3V     | 2.8–3.6V    | 0.0–0.3V    |
| D-     | 0.0–0.3V     | 0.0–0.3V    | 2.8–3.6V    |

Full-speed devices use a 1.5kΩ pull-up on D+ to signal presence to the host.

### NRZI Encoding and Bit Stuffing

USB uses **Non-Return-to-Zero Inverted (NRZI)** encoding:
- A `0` bit causes the signal to toggle
- A `1` bit keeps the signal unchanged

To prevent long runs of `1`s (which would produce no transitions and lose clock synchronization), USB employs **bit stuffing**: after six consecutive `1`s, the transmitter inserts a `0`. The receiver strips this stuffed bit automatically.

```
Original:  0 0 0 0 0 0 1 1 1 1 1 1 1 0
NRZI:      T T T T T T J J J J J J K T  (T=toggle, J=keep, K=keep inverted)
Stuffed:   0 0 0 0 0 0 1 1 1 1 1 1 0 1 0  (stuffed 0 after 6 ones)
```

### USB Packet Structure

Every USB packet consists of:

```
[SYNC (8 bits)] [PID (8 bits)] [Payload (0-1023 bytes)] [CRC (16 bits)] [EOP]
```

| Field | Description |
|-------|-------------|
| SYNC  | `00000001` — synchronization pattern |
| PID   | Packet ID: `OUT`, `IN`, `SOF`, `SETUP`, `DATA0`, `DATA1`, `ACK`, `NAK`, `STALL` |
| Payload | Address, endpoint, data, or control information |
| CRC   | Cyclic redundancy check for error detection |
| EOP   | End of Packet: SE0 for 2 bit times + J for 1 bit time |

## USB Device States

A USB device progresses through three states during its lifetime:

```
                    ┌─────────────┐
                    │   Powered   │
                    └──────┬──────┘
                           │ Host detects pull-up
                    ┌──────▼──────┐
              ┌─────│   Default   │◄──────┐
              │     └──────┬──────┘       │
              │            │ Set Address  │
              │     ┌──────▼──────┐       │
              │     │  Addressed  │       │
              │     └──────┬──────┘       │
              │            │ Set Config   │
              │     ┌──────▼──────┐       │
              │     │ Configured  │───────┘ Bus Reset
              │     └─────────────┘
              │
        Bus Reset (any state)
```

1. **Default**: Device has power but no address. All communication uses address 0.
2. **Addressed**: Host has assigned a unique 7-bit address (1–127).
3. **Configured**: Host has selected a configuration. Device is fully operational.

## USB Descriptors

Descriptors are data structures that describe the device's capabilities to the host. They form a strict hierarchy:

```
Device Descriptor (1 per device)
└── Configuration Descriptor (1+ per device)
    └── Interface Descriptor (1+ per configuration)
        ├── Endpoint Descriptor (0+ per interface)
        └── CDC Functional Descriptors (CDC-specific)
String Descriptor (0+ per device, referenced by index)
```

### Device Descriptor (18 bytes)

```c
typedef struct __attribute__((packed)) {
    uint8_t  bLength;            // 18
    uint8_t  bDescriptorType;    // 0x01 (DEVICE)
    uint16_t bcdUSB;             // USB spec version (0x0200 = 2.0)
    uint8_t  bDeviceClass;       // 0x00 (defined at interface) or 0x02 (CDC)
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;    // Max packet size for EP0 (8, 16, 32, or 64)
    uint16_t idVendor;           // Vendor ID (USB-IF assigned)
    uint16_t idProduct;          // Product ID
    uint16_t bcdDevice;          // Device release number
    uint8_t  iManufacturer;      // String descriptor index
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations; // Number of configurations
} usb_device_descriptor_t;
```

### Configuration Descriptor (9 bytes)

```c
typedef struct __attribute__((packed)) {
    uint8_t  bLength;             // 9
    uint8_t  bDescriptorType;     // 0x02 (CONFIGURATION)
    uint16_t wTotalLength;        // Total length of config + all sub-descriptors
    uint8_t  bNumInterfaces;      // Number of interfaces
    uint8_t  bConfigurationValue; // Value for SetConfiguration request
    uint8_t  iConfiguration;      // String descriptor index
    uint8_t  bmAttributes;        // 0x80 = bus-powered, 0xC0 = self-powered
    uint8_t  bMaxPower;           // Max power in 2mA units
} usb_configuration_descriptor_t;
```

### Interface Descriptor (9 bytes)

```c
typedef struct __attribute__((packed)) {
    uint8_t bLength;              // 9
    uint8_t bDescriptorType;      // 0x04 (INTERFACE)
    uint8_t bInterfaceNumber;     // Zero-based interface index
    uint8_t bAlternateSetting;    // Alternate setting number
    uint8_t bNumEndpoints;        // Number of endpoints (excl. EP0)
    uint8_t bInterfaceClass;      // Class code (0x02 = CDC)
    uint8_t bInterfaceSubClass;   // Subclass (0x02 = ACM)
    uint8_t bInterfaceProtocol;   // Protocol (0x01 = AT commands, 0x00 = none)
    uint8_t iInterface;           // String descriptor index
} usb_interface_descriptor_t;
```

### Endpoint Descriptor (7 bytes)

```c
typedef struct __attribute__((packed)) {
    uint8_t  bLength;             // 7
    uint8_t  bDescriptorType;     // 0x05 (ENDPOINT)
    uint8_t  bEndpointAddress;    // 0x80 | endpoint_number for IN, 0x00 for OUT
    uint8_t  bmAttributes;        // 0x00=Control, 0x02=Bulk, 0x03=Interrupt
    uint16_t wMaxPacketSize;      // Max packet size
    uint8_t  bInterval;           // Polling interval (ms for interrupt/iso)
} usb_endpoint_descriptor_t;
```

### CDC-ACM Descriptor Hierarchy

The CDC-ACM class uses a **two-interface** model:

```
Configuration
├── Interface 0: Communication Interface (CDC Control)
│   ├── Header Functional Descriptor
│   ├── Call Management Functional Descriptor
│   ├── Abstract Control Management Functional Descriptor
│   ├── Union Functional Descriptor
│   └── Endpoint 1 (Interrupt IN) — notifications
└── Interface 1: Data Interface (CDC Data)
    ├── Endpoint 2 (Bulk OUT) — host to device
    └── Endpoint 3 (Bulk IN) — device to host
```

## USB Enumeration Process

Enumeration is entirely **host-driven**. The device passively responds to requests:

```
Host                          Device
  │                             │
  │───── Bus Reset ────────────>│  Device enters Default state
  │                             │
  │───── Get Descriptor (DEV) ─>│  Device returns 8 bytes (or full 18)
  │<──── Device Descriptor ─────│
  │                             │
  │───── Set Address (addr=5) ─>│  Device remembers address (applies after ACK)
  │<──── ACK ───────────────────│
  │                             │  Device now in Addressed state
  │                             │
  │───── Get Descriptor (DEV) ─>│  Using new address
  │<──── Full Device Desc ──────│
  │                             │
  │───── Get Descriptor (CFG) ─>│
  │<──── Config Descriptor ─────│  (including all sub-descriptors)
  │                             │
  │───── Set Configuration (1) ─>│
  │<──── ACK ───────────────────│
  │                             │  Device now in Configured state
  │                             │
  │───── Class-specific reqs ──>│  CDC: SetLineCoding, SetControlLineState
  │<──── ACK ───────────────────│
  │                             │  Ready for bulk data transfer
```

### Standard Device Requests

| Request | bmRequestType | bRequest | wValue | wIndex | wLength |
|---------|--------------|----------|--------|--------|---------|
| Get Descriptor | 0x80 | 0x06 | Type:Index | 0 | Length |
| Set Address | 0x00 | 0x05 | Address | 0 | 0 |
| Set Configuration | 0x00 | 0x09 | Config | 0 | 0 |
| Get Configuration | 0x80 | 0x08 | 0 | 0 | 1 |
| Get Status | 0x80 | 0x00 | 0 | 0 | 2 |

### CDC-ACM Class Requests

| Request | bmRequestType | bRequest | wValue | wIndex | wLength |
|---------|--------------|----------|--------|--------|---------|
| SetLineCoding | 0x21 | 0x20 | 0 | Interface | 7 |
| GetLineCoding | 0xA1 | 0x21 | 0 | Interface | 7 |
| SetControlLineState | 0x21 | 0x22 | DTR/RTS | Interface | 0 |
| SendBreak | 0x21 | 0x23 | Duration | Interface | 0 |

### Line Coding Structure (7 bytes)

```c
typedef struct __attribute__((packed)) {
    uint32_t dwDTERate;      // Baud rate (e.g., 115200)
    uint8_t  bCharFormat;    // 0=1 stop bit, 1=1.5 stop bits, 2=2 stop bits
    uint8_t  bParityType;    // 0=None, 1=Odd, 2=Even, 3=Mark, 4=Space
    uint8_t  bDataBits;      // 5, 6, 7, 8, or 16
} usb_cdc_line_coding_t;
```

## Endpoint Types

| Type | Direction | Use Case | Max Packet | Guaranteed Bandwidth |
|------|-----------|----------|------------|---------------------|
| Control | Bidirectional | Enumeration, configuration | 8–64 bytes | Yes |
| Bulk | Unidirectional | Large data transfers (CDC data) | 8, 16, 32, 64 bytes | No (best effort) |
| Interrupt | Unidirectional | Small periodic data (CDC notifications) | 1–64 bytes | Yes |
| Isochronous | Unidirectional | Streaming audio/video | 0–1023 bytes | Yes |

For CDC-ACM:
- **EP0** (Control): Enumeration and class requests
- **EP1 IN** (Interrupt): Notification packets (serial state changes)
- **EP2 OUT** (Bulk): Data from host to device
- **EP3 IN** (Bulk): Data from device to host

## QEMU USB Device Limitations

QEMU's USB emulation has important limitations for USB device development:

- **No full enumeration testing**: QEMU's `-device usb-serial` or custom USB device support is limited. You can test the logic of your USB stack but not full host-driven enumeration.
- **Register-level simulation**: QEMU simulates the USB peripheral registers (OTG_FS on STM32), but the host-side USB stack behavior may differ from real hardware.
- **Best used for**: Testing descriptor tables, state machine transitions, and endpoint buffer management logic.
- **USB OTG_FS not simulated**: The `netduinoplus2` machine in QEMU does not simulate the USB OTG_FS peripheral. For full USB testing, use real hardware (NUCLEO-F446RE) or Renode, which provides more complete STM32 USB peripheral emulation.

> **Warning:** For production USB device testing, always validate on real hardware. QEMU is useful for catching descriptor errors and state machine bugs, but cannot replace testing with a real USB host.

## Implementation: C

### USB Device Stack from Scratch

We'll implement a complete USB device stack targeting the STM32F4's USB OTG_FS peripheral. The stack handles enumeration, descriptor responses, and CDC-ACM class operations.

```c
/* usb_defs.h — USB protocol definitions */
#ifndef USB_DEFS_H
#define USB_DEFS_H

#include <stdint.h>

/* USB Request Type breakdown */
#define USB_REQ_TYPE_DIR_HOST_TO_DEVICE  0x00
#define USB_REQ_TYPE_DIR_DEVICE_TO_HOST  0x80
#define USB_REQ_TYPE_TYPE_STANDARD       0x00
#define USB_REQ_TYPE_TYPE_CLASS          0x20
#define USB_REQ_TYPE_TYPE_VENDOR         0x40
#define USB_REQ_TYPE_RECIPIENT_DEVICE    0x00
#define USB_REQ_TYPE_RECIPIENT_INTERFACE 0x01
#define USB_REQ_TYPE_RECIPIENT_ENDPOINT  0x02

/* Standard requests */
#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE     0x0A
#define USB_REQ_SET_INTERFACE     0x0B

/* Descriptor types */
#define USB_DESC_TYPE_DEVICE            0x01
#define USB_DESC_TYPE_CONFIGURATION     0x02
#define USB_DESC_TYPE_STRING            0x03
#define USB_DESC_TYPE_INTERFACE         0x04
#define USB_DESC_TYPE_ENDPOINT          0x05
#define USB_DESC_TYPE_DEVICE_QUALIFIER  0x06
#define USB_DESC_TYPE_OTHER_SPEED       0x07
#define USB_DESC_TYPE_BOS               0x0F

/* CDC class requests */
#define USB_CDC_REQ_SEND_ENCAPSULATED_COMMAND 0x00
#define USB_CDC_REQ_GET_ENCAPSULATED_RESPONSE 0x01
#define USB_CDC_REQ_SET_LINE_CODING           0x20
#define USB_CDC_REQ_GET_LINE_CODING           0x21
#define USB_CDC_REQ_SET_CONTROL_LINE_STATE    0x22
#define USB_CDC_REQ_SEND_BREAK                0x23

/* Endpoint types */
#define USB_EP_TYPE_CONTROL  0x00
#define USB_EP_TYPE_ISOCHRONOUS 0x01
#define USB_EP_TYPE_BULK     0x02
#define USB_EP_TYPE_INTERRUPT 0x03

/* USB device states */
typedef enum {
    USB_STATE_DEFAULT,
    USB_STATE_ADDRESSED,
    USB_STATE_CONFIGURED,
    USB_STATE_SUSPENDED
} usb_device_state_t;

/* Setup packet structure (8 bytes, matches USB spec) */
typedef struct __attribute__((packed)) {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_t;

/* CDC Line Coding */
typedef struct __attribute__((packed)) {
    uint32_t dwDTERate;
    uint8_t  bCharFormat;
    uint8_t  bParityType;
    uint8_t  bDataBits;
} usb_cdc_line_coding_t;

#endif /* USB_DEFS_H */
```

```c
/* usb_descriptors.h — Descriptor declarations */
#ifndef USB_DESCRIPTORS_H
#define USB_DESCRIPTORS_H

#include <stdint.h>
#include "usb_defs.h"

extern const uint8_t usb_device_descriptor[];
extern const uint16_t usb_device_descriptor_len;

extern const uint8_t usb_config_descriptor[];
extern const uint16_t usb_config_descriptor_len;

extern const uint8_t *usb_string_descriptors[];
extern const uint16_t usb_string_descriptor_lens[];
extern const uint8_t usb_num_string_descriptors;

#endif /* USB_DESCRIPTORS_H */
```

```c
/* usb_descriptors.c — All USB descriptors */
#include "usb_descriptors.h"

/* Device Descriptor */
const uint8_t usb_device_descriptor[] = {
    0x12,       /* bLength: 18 bytes */
    0x01,       /* bDescriptorType: DEVICE */
    0x00, 0x02, /* bcdUSB: 2.00 */
    0x02,       /* bDeviceClass: CDC */
    0x00,       /* bDeviceSubClass */
    0x00,       /* bDeviceProtocol */
    0x40,       /* bMaxPacketSize0: 64 bytes */
    0x83, 0x04, /* idVendor: 0x0483 (STMicroelectronics) */
    0x40, 0x57, /* idProduct: 0x5740 (Virtual COM Port) */
    0x00, 0x02, /* bcdDevice: 2.00 */
    0x01,       /* iManufacturer: String index 1 */
    0x02,       /* iProduct: String index 2 */
    0x03,       /* iSerialNumber: String index 3 */
    0x01        /* bNumConfigurations: 1 */
};
const uint16_t usb_device_descriptor_len = sizeof(usb_device_descriptor);

/* Configuration Descriptor with CDC-ACM interfaces */
const uint8_t usb_config_descriptor[] = {
    /* Configuration Descriptor */
    0x09,       /* bLength */
    0x02,       /* bDescriptorType: CONFIGURATION */
    0x43, 0x00, /* wTotalLength: 67 bytes */
    0x02,       /* bNumInterfaces: 2 */
    0x01,       /* bConfigurationValue */
    0x00,       /* iConfiguration */
    0x80,       /* bmAttributes: Bus-powered */
    0xFA,       /* bMaxPower: 500mA (250 * 2mA) */

    /* Interface 0: Communication Interface */
    0x09,       /* bLength */
    0x04,       /* bDescriptorType: INTERFACE */
    0x00,       /* bInterfaceNumber: 0 */
    0x00,       /* bAlternateSetting */
    0x01,       /* bNumEndpoints: 1 (notification) */
    0x02,       /* bInterfaceClass: CDC */
    0x02,       /* bInterfaceSubClass: ACM */
    0x01,       /* bInterfaceProtocol: AT commands */
    0x00,       /* iInterface */

    /* Header Functional Descriptor */
    0x05,       /* bLength */
    0x24,       /* bDescriptorType: CS_INTERFACE */
    0x00,       /* bDescriptorSubtype: HEADER */
    0x10, 0x01, /* bcdCDC: 1.10 */

    /* Call Management Functional Descriptor */
    0x05,       /* bLength */
    0x24,       /* bDescriptorType: CS_INTERFACE */
    0x01,       /* bDescriptorSubtype: CALL_MANAGEMENT */
    0x00,       /* bmCapabilities: No call management */
    0x01,       /* bDataInterface: Interface 1 */

    /* Abstract Control Management Functional Descriptor */
    0x04,       /* bLength */
    0x24,       /* bDescriptorType: CS_INTERFACE */
    0x02,       /* bDescriptorSubtype: ACM */
    0x02,       /* bmCapabilities: Supports SetLineCoding, SetControlLineState */

    /* Union Functional Descriptor */
    0x05,       /* bLength */
    0x24,       /* bDescriptorType: CS_INTERFACE */
    0x06,       /* bDescriptorSubtype: UNION */
    0x00,       /* bMasterInterface: Interface 0 */
    0x01,       /* bSlaveInterface: Interface 1 */

    /* Endpoint 1 IN (Interrupt) — Notifications */
    0x07,       /* bLength */
    0x05,       /* bDescriptorType: ENDPOINT */
    0x81,       /* bEndpointAddress: EP1 IN */
    0x03,       /* bmAttributes: Interrupt */
    0x08, 0x00, /* wMaxPacketSize: 8 bytes */
    0xFF,       /* bInterval: 255ms */

    /* Interface 1: Data Interface */
    0x09,       /* bLength */
    0x04,       /* bDescriptorType: INTERFACE */
    0x01,       /* bInterfaceNumber: 1 */
    0x00,       /* bAlternateSetting */
    0x02,       /* bNumEndpoints: 2 (bulk IN + OUT) */
    0x0A,       /* bInterfaceClass: CDC-Data */
    0x00,       /* bInterfaceSubClass */
    0x00,       /* bInterfaceProtocol */
    0x00,       /* iInterface */

    /* Endpoint 3 IN (Bulk) — Device to Host */
    0x07,       /* bLength */
    0x05,       /* bDescriptorType: ENDPOINT */
    0x83,       /* bEndpointAddress: EP3 IN */
    0x02,       /* bmAttributes: Bulk */
    0x40, 0x00, /* wMaxPacketSize: 64 bytes */
    0x00,       /* bInterval: N/A for bulk */

    /* Endpoint 2 OUT (Bulk) — Host to Device */
    0x07,       /* bLength */
    0x05,       /* bDescriptorType: ENDPOINT */
    0x02,       /* bEndpointAddress: EP2 OUT */
    0x02,       /* bmAttributes: Bulk */
    0x40, 0x00, /* wMaxPacketSize: 64 bytes */
    0x00        /* bInterval: N/A for bulk */
};
const uint16_t usb_config_descriptor_len = sizeof(usb_config_descriptor);

/* String Descriptor 0: Supported languages (US English) */
static const uint8_t string_desc_lang[] = {
    0x04,       /* bLength: 4 bytes */
    0x03,       /* bDescriptorType: STRING */
    0x09, 0x04  /* wLANGID: 0x0409 (US English) */
};

/* String Descriptor 1: Manufacturer */
static const uint8_t string_desc_manufacturer[] = {
    0x1E,       /* bLength: 30 bytes */
    0x03,       /* bDescriptorType: STRING */
    'S', 0, 'a', 0, 'f', 0, 'e', 0, 'E', 0, 'm', 0, 'b', 0, 'e', 0,
    'd', 0, 'd', 0, 'e', 0, 'd', 0, ' ', 0, 'L', 0, 'a', 0,
    'b', 0, 's', 0
};

/* String Descriptor 2: Product */
static const uint8_t string_desc_product[] = {
    0x1C,       /* bLength: 28 bytes */
    0x03,       /* bDescriptorType: STRING */
    'C', 0, 'D', 0, 'C', 0, '-', 0, 'A', 0, 'C', 0, 'M', 0,
    ' ', 0, 'V', 0, 'i', 0, 'r', 0, 't', 0, 'u', 0, 'a', 0,
    'l', 0, ' ', 0, 'C', 0, 'O', 0, 'M', 0
};

/* String Descriptor 3: Serial Number */
static const uint8_t string_desc_serial[] = {
    0x12,       /* bLength: 18 bytes */
    0x03,       /* bDescriptorType: STRING */
    '1', 0, '2', 0, '3', 0, '4', 0, '5', 0, '6', 0, '7', 0, '8', 0,
    '9', 0, 'A', 0, 'B', 0, 'C', 0, 'D', 0, 'E', 0, 'F', 0
};

const uint8_t *usb_string_descriptors[] = {
    string_desc_lang,
    string_desc_manufacturer,
    string_desc_product,
    string_desc_serial
};

const uint16_t usb_string_descriptor_lens[] = {
    sizeof(string_desc_lang),
    sizeof(string_desc_manufacturer),
    sizeof(string_desc_product),
    sizeof(string_desc_serial)
};

const uint8_t usb_num_string_descriptors = 4;
```

```c
/* usb_device.h — USB device stack interface */
#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <stdint.h>
#include "usb_defs.h"

/* USB device context */
typedef struct {
    usb_device_state_t state;
    uint8_t address;
    uint8_t configuration;
    usb_cdc_line_coding_t line_coding;
    uint8_t control_line_state; /* DTR | (RTS << 1) */
    uint8_t ep0_buf[64];
    uint16_t ep0_remaining;
    const uint8_t *ep0_data_ptr;
    volatile uint8_t rx_buf[256];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    volatile uint8_t tx_buf[256];
    volatile uint16_t tx_head;
    volatile uint16_t tx_tail;
} usb_device_t;

/* Global USB device instance */
extern usb_device_t usb_dev;

/* Initialize USB device hardware and stack */
void usb_device_init(void);

/* Process USB events (call from main loop or interrupt) */
void usb_device_poll(void);

/* Send data over CDC bulk IN endpoint */
int usb_cdc_send(const uint8_t *data, uint16_t len);

/* Receive data from CDC bulk OUT endpoint */
int usb_cdc_recv(uint8_t *data, uint16_t max_len);

/* Get current line coding */
const usb_cdc_line_coding_t *usb_cdc_get_line_coding(void);

/* Check if DTR is asserted */
int usb_cdc_dtr_active(void);

#endif /* USB_DEVICE_H */
```

```c
/* usb_device.c — USB device stack implementation (STM32F4) */
#include "usb_device.h"
#include "usb_descriptors.h"
#include <string.h>

/* STM32F4 USB OTG_FS register definitions (simplified) */
#define USB_OTG_FS_BASE     0x50000000UL
#define USB_OTG_GOTGCTL     (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x000))
#define USB_OTG_GAHBCFG     (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x008))
#define USB_OTG_GUSBCFG     (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x00C))
#define USB_OTG_GRSTCTL     (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x010))
#define USB_OTG_GINTSTS     (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x014))
#define USB_OTG_GINTMSK     (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x018))
#define USB_OTG_GRXSTSR     (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x01C))
#define USB_OTG_GRXSTSP     (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x020))
#define USB_OTG_GCCFG       (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x038))
#define USB_OTG_DCFG        (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x800))
#define USB_OTG_DCTL        (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x804))
#define USB_OTG_DSTS        (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x808))
#define USB_OTG_DIEPMSK     (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x810))
#define USB_OTG_DOEPMSK     (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x814))
#define USB_OTG_DAINT       (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x818))
#define USB_OTG_DAINTMSK    (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x81C))
#define USB_OTG_DIEP0CTL    (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x900))
#define USB_OTG_DIEP0TSIZ   (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x910))
#define USB_OTG_DOEP0CTL    (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0xB00))
#define USB_OTG_DOEP0TSIZ   (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0xB10))
#define USB_OTG_DIEP1CTL    (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x920))
#define USB_OTG_DIEP1TSIZ   (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x930))
#define USB_OTG_DIEP2TSIZ   (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x950))
#define USB_OTG_DIEP3TSIZ   (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x970))
#define USB_OTG_DIEP0TXF    (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x1000))
#define USB_OTG_DIEP1TXF    (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x1004))
#define USB_OTG_DIEP2TXF    (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x1008))
#define USB_OTG_DIEP3TXF    (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0x100C))
#define USB_OTG_DOEP0CTL_REG (*(volatile uint32_t *)(USB_OTG_FS_BASE + 0xB00))

/* USB FIFO base */
#define USB_OTG_FIFO_BASE   0x50001000UL
#define USB_OTG_FIFO(n)     (*(volatile uint32_t *)(USB_OTG_FIFO_BASE + ((n) * 0x1000)))

/* Interrupt bits */
#define USB_OTG_GINTSTS_USBRST   (1 << 12)
#define USB_OTG_GINTSTS_ENUMDNE  (1 << 13)
#define USB_OTG_GINTSTS_IEPINT   (1 << 18)
#define USB_OTG_GINTSTS_OEPINT   (1 << 19)
#define USB_OTG_GINTSTS_RXFLVL   (1 << 20)
#define USB_OTG_GINTSTS_USBSUSP  (1 << 11)
#define USB_OTG_GINTSTS_WKUPINT  (1 << 31)

/* RX status bits */
#define USB_OTG_GRXSTSP_EPNUM_MASK  0xF
#define USB_OTG_GRXSTSP_BCNT_MASK   (0x7FF << 4)
#define USB_OTG_GRXSTSP_PKTSTS_MASK (0xF << 17)
#define USB_OTG_GRXSTSP_PKTSTS_SETUP (6 << 17)
#define USB_OTG_GRXSTSP_PKTSTS_OUT   (2 << 17)
#define USB_OTG_GRXSTSP_PKTSTS_OUT_DONE (3 << 17)
#define USB_OTG_GRXSTSP_PKTSTS_SETUP_DONE (4 << 17)

/* Device control */
#define USB_OTG_DCTL_SDIS     (1 << 1)
#define USB_OTG_DCTL_CGNPINAK (1 << 10)

/* Endpoint control */
#define USB_OTG_DIEPCTL_EPENA  (1 << 31)
#define USB_OTG_DIEPCTL_CNAK   (1 << 26)
#define USB_OTG_DIEPCTL_USBACTEP (1 << 15)
#define USB_OTG_DIEPCTL_MPSIZ_MASK 0x3
#define USB_OTG_DOEPCTL_EPENA  (1 << 31)
#define USB_OTG_DOEPCTL_CNAK   (1 << 26)
#define USB_OTG_DOEPCTL_USBACTEP (1 << 15)

/* Transfer size */
#define USB_OTG_TSIZ_PKTCNT_MASK (0x3 << 19)
#define USB_OTG_TSIZ_XFRSIZ_MASK 0x7FFFF

/* Global device instance */
usb_device_t usb_dev;

/* Static buffer for descriptor responses */
static uint8_t desc_buf[256];

/* Forward declarations */
static void usb_core_init(void);
static void usb_handle_reset(void);
static void usb_handle_enum_done(void);
static void usb_handle_setup(usb_setup_t *setup);
static void usb_handle_rx_status(uint32_t status);
static void usb_handle_ep0_in_complete(void);
static void usb_handle_ep0_out_complete(void);
static void usb_handle_ep_bulk_in(uint8_t ep_num);
static void usb_handle_ep_bulk_out(uint8_t ep_num);
static void usb_ep0_send(const uint8_t *data, uint16_t len);
static void usb_ep0_stall(void);
static void usb_activate_endpoint(uint8_t ep_num, uint8_t is_in, uint16_t mps);
static void usb_ep_prepare_rx(uint8_t ep_num, uint16_t mps);

void usb_device_init(void) {
    memset(&usb_dev, 0, sizeof(usb_dev));
    usb_dev.state = USB_STATE_DEFAULT;
    usb_dev.address = 0;
    usb_dev.configuration = 0;

    /* Default line coding: 115200 8N1 */
    usb_dev.line_coding.dwDTERate = 115200;
    usb_dev.line_coding.bCharFormat = 0; /* 1 stop bit */
    usb_dev.line_coding.bParityType = 0; /* No parity */
    usb_dev.line_coding.bDataBits = 8;

    /* Ring buffer init */
    usb_dev.rx_head = 0;
    usb_dev.rx_tail = 0;
    usb_dev.tx_head = 0;
    usb_dev.tx_tail = 0;

    usb_core_init();
}

static void usb_core_init(void) {
    /* Core soft reset */
    USB_OTG_GRSTCTL |= (1 << 30); /* CSRST */
    while (USB_OTG_GRSTCTL & (1 << 30)) { }

    /* Force device mode */
    USB_OTG_GUSBCFG &= ~(1 << 30); /* Clear FDMOD = device mode */
    USB_OTG_GUSBCFG |= (1 << 30);  /* Set FDMOD = device mode */
    for (volatile int i = 0; i < 200000; i++) { }

    /* Set TX FIFO size */
    USB_OTG_GAHBCFG |= (1 << 0); /* Enable global interrupt */

    /* Configure device: Full-speed, 48MHz */
    USB_OTG_DCFG = (3 << 0); /* DSPD: Full-Speed */

    /* Enable VBUS sensing (or disable for self-powered) */
    USB_OTG_GCCFG |= (1 << 21); /* PWRDWN: Power down off */

    /* Clear suspend */
    USB_OTG_DCTL &= ~USB_OTG_DCTL_SDIS;

    /* Enable interrupts */
    USB_OTG_GINTMSK = 0;
    USB_OTG_GINTMSK |= USB_OTG_GINTSTS_USBRST;
    USB_OTG_GINTMSK |= USB_OTG_GINTSTS_ENUMDNE;
    USB_OTG_GINTMSK |= USB_OTG_GINTSTS_IEPINT;
    USB_OTG_GINTMSK |= USB_OTG_GINTSTS_OEPINT;
    USB_OTG_GINTMSK |= USB_OTG_GINTSTS_RXFLVL;
    USB_OTG_GINTMSK |= USB_OTG_GINTSTS_USBSUSP;
    USB_OTG_GINTMSK |= USB_OTG_GINTSTS_WKUPINT;

    /* Unmask EP0 interrupts */
    USB_OTG_DAINTMSK = 0x11; /* EP0 IN + EP0 OUT */

    /* Set EP0 to receive setup packets */
    USB_OTG_DOEP0TSIZ = (3 << 29) | (1 << 19) | (24);
    /* 3 setup packets, 1 packet count, 24 bytes */

    USB_OTG_DOEPMSK |= (1 << 0); /* Setup phase done */
    USB_OTG_DOEPMSK |= (1 << 1); /* Transfer completed */
    USB_OTG_DIEPMSK |= (1 << 1); /* Transfer completed */
}

static void usb_handle_reset(void) {
    usb_dev.state = USB_STATE_DEFAULT;
    usb_dev.address = 0;
    usb_dev.configuration = 0;

    /* Reset all endpoints */
    USB_OTG_DIEP0CTL = 0;
    USB_OTG_DOEP0CTL = 0;

    /* Set EP0 MPS to 64 */
    USB_OTG_DIEP0CTL |= USB_OTG_DIEPCTL_USBACTEP;
    USB_OTG_DOEP0CTL |= USB_OTG_DOEPCTL_USBACTEP;

    /* Deactivate non-EP0 endpoints */
    USB_OTG_DIEP1CTL = 0;
    USB_OTG_DIEP2TSIZ = 0;
    USB_OTG_DIEP3TSIZ = 0;

    /* Prepare EP0 for next setup */
    USB_OTG_DOEP0TSIZ = (3 << 29) | (1 << 19) | 24;
    USB_OTG_DOEPCTL |= USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_CNAK;
}

static void usb_handle_enum_done(void) {
    uint32_t speed = (USB_OTG_DSTS >> 0) & 0x3;
    (void)speed;
    /* Device is now in Default state, ready for enumeration */
}

void usb_device_poll(void) {
    uint32_t gintsts = USB_OTG_GINTSTS;

    /* USB Reset */
    if (gintsts & USB_OTG_GINTSTS_USBRST) {
        USB_OTG_GINTSTS = USB_OTG_GINTSTS_USBRST;
        usb_handle_reset();
    }

    /* Enumeration Done */
    if (gintsts & USB_OTG_GINTSTS_ENUMDNE) {
        USB_OTG_GINTSTS = USB_OTG_GINTSTS_ENUMDNE;
        usb_handle_enum_done();
    }

    /* RX FIFO not empty */
    if (gintsts & USB_OTG_GINTSTS_RXFLVL) {
        uint32_t status = USB_OTG_GRXSTSP;
        usb_handle_rx_status(status);
    }

    /* IN endpoint interrupt */
    if (gintsts & USB_OTG_GINTSTS_IEPINT) {
        uint32_t daint = USB_OTG_DAINT & 0xFFFF;
        for (int ep = 0; ep < 4; ep++) {
            if (daint & (1 << ep)) {
                if (ep == 0) {
                    usb_handle_ep0_in_complete();
                } else {
                    usb_handle_ep_bulk_in(ep);
                }
            }
        }
    }

    /* OUT endpoint interrupt */
    if (gintsts & USB_OTG_GINTSTS_OEPINT) {
        uint32_t daint = (USB_OTG_DAINT >> 16) & 0xFFFF;
        for (int ep = 0; ep < 4; ep++) {
            if (daint & (1 << ep)) {
                if (ep == 0) {
                    usb_handle_ep0_out_complete();
                } else {
                    usb_handle_ep_bulk_out(ep);
                }
            }
        }
    }

    /* Suspend */
    if (gintsts & USB_OTG_GINTSTS_USBSUSP) {
        USB_OTG_GINTSTS = USB_OTG_GINTSTS_USBSUSP;
        usb_dev.state = USB_STATE_SUSPENDED;
    }

    /* Wakeup */
    if (gintsts & USB_OTG_GINTSTS_WKUPINT) {
        USB_OTG_GINTSTS = USB_OTG_GINTSTS_WKUPINT;
        if (usb_dev.state == USB_STATE_SUSPENDED) {
            usb_dev.state = usb_dev.configuration ?
                USB_STATE_CONFIGURED : USB_STATE_ADDRESSED;
        }
    }
}

static void usb_handle_rx_status(uint32_t status) {
    uint8_t ep_num = status & USB_OTG_GRXSTSP_EPNUM_MASK;
    uint16_t bcnt = (status & USB_OTG_GRXSTSP_BCNT_MASK) >> 4;
    uint32_t pktsts = status & USB_OTG_GRXSTSP_PKTSTS_MASK;

    switch (pktsts) {
    case USB_OTG_GRXSTSP_PKTSTS_SETUP:
        /* Read setup packet from FIFO */
        uint32_t *setup_words = (uint32_t *)usb_dev.ep0_buf;
        setup_words[0] = USB_OTG_FIFO(0);
        setup_words[1] = USB_OTG_FIFO(0);
        usb_setup_t *setup = (usb_setup_t *)usb_dev.ep0_buf;
        usb_handle_setup(setup);
        break;

    case USB_OTG_GRXSTSP_PKTSTS_OUT:
        /* Read data from FIFO */
        if (ep_num == 0 && bcnt > 0) {
            uint32_t *data_words = (uint32_t *)usb_dev.ep0_buf;
            for (int i = 0; i < (bcnt + 3) / 4; i++) {
                data_words[i] = USB_OTG_FIFO(0);
            }
        } else if (ep_num == 2) {
            /* Bulk OUT on EP2 — store in ring buffer */
            for (int i = 0; i < bcnt; i++) {
                uint32_t word = USB_OTG_FIFO(0);
                uint8_t byte = word & 0xFF;
                uint16_t next = (usb_dev.rx_head + 1) % 256;
                if (next != usb_dev.rx_tail) {
                    usb_dev.rx_buf[usb_dev.rx_head] = byte;
                    usb_dev.rx_head = next;
                }
            }
            /* Prepare for next packet */
            usb_ep_prepare_rx(2, 64);
        }
        break;

    case USB_OTG_GRXSTSP_PKTSTS_OUT_DONE:
        if (ep_num == 0) {
            usb_handle_ep0_out_complete();
        }
        break;

    case USB_OTG_GRXSTSP_PKTSTS_SETUP_DONE:
        /* Setup phase done, prepare for status or data */
        break;

    default:
        /* Consume FIFO data for unhandled packet types */
        while (bcnt > 0) {
            (void)USB_OTG_FIFO(0);
            bcnt -= 4;
        }
        break;
    }
}

static void usb_handle_setup(usb_setup_t *setup) {
    uint8_t req_type = setup->bmRequestType;
    uint8_t req = setup->bRequest;
    uint16_t wValue = setup->wValue;
    uint16_t wIndex = setup->wIndex;
    uint16_t wLength = setup->wLength;

    if ((req_type & 0x1F) == USB_REQ_TYPE_TYPE_STANDARD) {
        /* Standard request */
        switch (req) {
        case USB_REQ_GET_DESCRIPTOR: {
            uint8_t desc_type = (wValue >> 8) & 0xFF;
            uint8_t desc_index = wValue & 0xFF;

            switch (desc_type) {
            case USB_DESC_TYPE_DEVICE:
                usb_ep0_send(usb_device_descriptor,
                    wLength < usb_device_descriptor_len ?
                    wLength : usb_device_descriptor_len);
                break;

            case USB_DESC_TYPE_CONFIGURATION:
                usb_ep0_send(usb_config_descriptor,
                    wLength < usb_config_descriptor_len ?
                    wLength : usb_config_descriptor_len);
                break;

            case USB_DESC_TYPE_STRING:
                if (desc_index < usb_num_string_descriptors) {
                    usb_ep0_send(usb_string_descriptors[desc_index],
                        wLength < usb_string_descriptor_lens[desc_index] ?
                        wLength : usb_string_descriptor_lens[desc_index]);
                } else {
                    usb_ep0_stall();
                }
                break;

            case USB_DESC_TYPE_DEVICE_QUALIFIER:
                /* Not applicable for FS-only device */
                usb_ep0_stall();
                break;

            default:
                usb_ep0_stall();
                break;
            }
            break;
        }

        case USB_REQ_SET_ADDRESS:
            /* Address takes effect after status stage */
            usb_dev.address = wValue & 0x7F;
            usb_ep0_send(NULL, 0); /* Status stage */
            break;

        case USB_REQ_SET_CONFIGURATION:
            usb_dev.configuration = wValue & 0xFF;
            if (usb_dev.configuration != 0) {
                usb_dev.state = USB_STATE_CONFIGURED;
                /* Activate CDC endpoints */
                usb_activate_endpoint(1, 1, 8);  /* EP1 IN Interrupt */
                usb_activate_endpoint(2, 0, 64); /* EP2 OUT Bulk */
                usb_activate_endpoint(3, 1, 64); /* EP3 IN Bulk */
                /* Prepare EP2 for receiving */
                usb_ep_prepare_rx(2, 64);
            } else {
                usb_dev.state = USB_STATE_ADDRESSED;
            }
            usb_ep0_send(NULL, 0);
            break;

        case USB_REQ_GET_CONFIGURATION:
            usb_dev.ep0_buf[0] = usb_dev.configuration;
            usb_ep0_send(usb_dev.ep0_buf, 1);
            break;

        case USB_REQ_GET_STATUS: {
            uint16_t status = 0;
            usb_dev.ep0_buf[0] = status & 0xFF;
            usb_dev.ep0_buf[1] = (status >> 8) & 0xFF;
            usb_ep0_send(usb_dev.ep0_buf, 2);
            break;
        }

        default:
            usb_ep0_stall();
            break;
        }
    } else if ((req_type & 0x1F) == USB_REQ_TYPE_TYPE_CLASS) {
        /* CDC class request */
        switch (req) {
        case USB_CDC_REQ_SET_LINE_CODING:
            /* Data comes in next OUT transaction */
            usb_dev.ep0_remaining = wLength;
            break;

        case USB_CDC_REQ_GET_LINE_CODING:
            if (wLength >= 7) {
                memcpy(usb_dev.ep0_buf, &usb_dev.line_coding, 7);
                usb_ep0_send(usb_dev.ep0_buf, 7);
            } else {
                usb_ep0_stall();
            }
            break;

        case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
            usb_dev.control_line_state = wValue & 0x03;
            usb_ep0_send(NULL, 0);
            break;

        case USB_CDC_REQ_SEND_ENCAPSULATED_COMMAND:
        case USB_CDC_REQ_GET_ENCAPSULATED_RESPONSE:
        case USB_CDC_REQ_SEND_BREAK:
            /* Not implemented — acknowledge */
            usb_ep0_send(NULL, 0);
            break;

        default:
            usb_ep0_stall();
            break;
        }
    } else {
        usb_ep0_stall();
    }
}

static void usb_handle_ep0_out_complete(void) {
    /* Handle SetLineCoding data */
    if (usb_dev.ep0_remaining > 0) {
        if (usb_dev.ep0_remaining >= 7) {
            memcpy(&usb_dev.line_coding, usb_dev.ep0_buf, 7);
        }
        usb_dev.ep0_remaining = 0;
        usb_ep0_send(NULL, 0); /* Status stage */
    }
}

static void usb_handle_ep0_in_complete(void) {
    /* If address was set, apply it now (after status stage) */
    if (usb_dev.address != 0 && usb_dev.state == USB_STATE_DEFAULT) {
        USB_OTG_DCFG = (USB_OTG_DCFG & ~0x7F) | usb_dev.address;
        usb_dev.state = USB_STATE_ADDRESSED;
    }

    /* If there's more data to send (multi-packet descriptor) */
    if (usb_dev.ep0_data_ptr && usb_dev.ep0_remaining > 0) {
        uint16_t chunk = usb_dev.ep0_remaining > 64 ? 64 : usb_dev.ep0_remaining;
        usb_ep0_send(usb_dev.ep0_data_ptr, chunk);
        usb_dev.ep0_data_ptr += chunk;
        usb_dev.ep0_remaining -= chunk;
    }
}

static void usb_handle_ep_bulk_in(uint8_t ep_num) {
    if (ep_num == 3) {
        /* EP3 IN — more data to send from ring buffer */
        if (usb_dev.tx_head != usb_dev.tx_tail) {
            uint8_t buf[64];
            uint16_t len = 0;
            while (len < 64 && usb_dev.tx_head != usb_dev.tx_tail) {
                buf[len++] = usb_dev.tx_buf[usb_dev.tx_tail];
                usb_dev.tx_tail = (usb_dev.tx_tail + 1) % 256;
            }
            /* Write data to TX FIFO */
            for (uint16_t i = 0; i < (len + 3) / 4; i++) {
                uint32_t word = 0;
                for (int j = 0; j < 4 && (i * 4 + j) < len; j++) {
                    word |= (uint32_t)buf[i * 4 + j] << (j * 8);
                }
                USB_OTG_FIFO(3) = word;
            }
            /* Set transfer size and enable endpoint */
            USB_OTG_DIEP3TSIZ = (1 << 19) | len;
            USB_OTG_DIEP1CTL |= USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_CNAK;
        }
    }
}

static void usb_handle_ep_bulk_out(uint8_t ep_num) {
    if (ep_num == 2) {
        /* EP2 OUT — data already consumed in rx_status handler */
        usb_ep_prepare_rx(2, 64);
    }
}

static void usb_ep0_send(const uint8_t *data, uint16_t len) {
    if (data && len > 0) {
        memcpy(usb_dev.ep0_buf, data, len > 64 ? 64 : len);
    }

    uint16_t actual_len = len > 64 ? 64 : len;
    if (actual_len > 0) {
        /* Write to TX FIFO */
        for (uint16_t i = 0; i < (actual_len + 3) / 4; i++) {
            uint32_t word = 0;
            for (int j = 0; j < 4 && (i * 4 + j) < actual_len; j++) {
                word |= (uint32_t)usb_dev.ep0_buf[i * 4 + j] << (j * 8);
            }
            USB_OTG_FIFO(0) = word;
        }
    }

    /* Set transfer size */
    USB_OTG_DIEP0TSIZ = (1 << 19) | actual_len;
    USB_OTG_DIEP0CTL |= USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_CNAK;

    /* If this is a short packet or zero-length, it's the last one */
    if (actual_len < 64) {
        usb_dev.ep0_data_ptr = NULL;
        usb_dev.ep0_remaining = 0;
    } else if (data) {
        /* Multi-packet: caller handles continuation */
        usb_dev.ep0_data_ptr = data + actual_len;
        usb_dev.ep0_remaining = 0; /* Simplified — full descriptor in one packet */
    }
}

static void usb_ep0_stall(void) {
    USB_OTG_DIEP0CTL |= (1 << 21); /* STALL */
    USB_OTG_DOEP0CTL |= (1 << 21); /* STALL */
}

static void usb_activate_endpoint(uint8_t ep_num, uint8_t is_in, uint16_t mps) {
    volatile uint32_t *epctl;
    if (is_in) {
        switch (ep_num) {
        case 1: epctl = &USB_OTG_DIEP1CTL; break;
        case 3: epctl = &USB_OTG_DIEP1CTL; break; /* Simplified */
        default: return;
        }
    } else {
        switch (ep_num) {
        case 2: epctl = &USB_OTG_DOEP0CTL; break; /* Simplified */
        default: return;
        }
    }

    uint32_t type = (ep_num == 1) ? (USB_EP_TYPE_INTERRUPT << 18) :
                                       (USB_EP_TYPE_BULK << 18);
    *epctl = type | (mps == 8 ? 1 : 2) | USB_OTG_DIEPCTL_USBACTEP |
             USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_CNAK;
}

static void usb_ep_prepare_rx(uint8_t ep_num, uint16_t mps) {
    (void)mps;
    /* For EP2 OUT, set up to receive packets */
    /* In a full implementation, configure DOEP2TSIZ and DOEP2CTL */
}

int usb_cdc_send(const uint8_t *data, uint16_t len) {
    if (usb_dev.state != USB_STATE_CONFIGURED) {
        return -1;
    }

    uint16_t sent = 0;
    while (sent < len) {
        uint16_t next = (usb_dev.tx_head + 1) % 256;
        if (next == usb_dev.tx_tail) {
            break; /* Buffer full */
        }
        usb_dev.tx_buf[usb_dev.tx_head] = data[sent];
        usb_dev.tx_head = next;
        sent++;
    }

    /* Trigger transmission if endpoint is ready */
    if (sent > 0) {
        /* In a full implementation, check endpoint ready and initiate transfer */
    }

    return sent;
}

int usb_cdc_recv(uint8_t *data, uint16_t max_len) {
    uint16_t recv = 0;
    while (recv < max_len && usb_dev.rx_head != usb_dev.rx_tail) {
        data[recv++] = usb_dev.rx_buf[usb_dev.rx_tail];
        usb_dev.rx_tail = (usb_dev.rx_tail + 1) % 256;
    }
    return recv;
}

const usb_cdc_line_coding_t *usb_cdc_get_line_coding(void) {
    return &usb_dev.line_coding;
}

int usb_cdc_dtr_active(void) {
    return usb_dev.control_line_state & 0x01;
}
```

```c
/* main.c — USB CDC Application */
#include "usb_device.h"
#include <string.h>

/* Simple debug UART for logging (from Project 5) */
extern void uart_init(void);
extern void uart_send_string(const char *s);
extern void uart_send_hex(uint32_t val);

static void log_state(const char *msg) {
    uart_send_string("[USB] ");
    uart_send_string(msg);
    uart_send_string("\r\n");
}

int main(void) {
    /* Initialize debug UART */
    uart_init();
    uart_send_string("USB CDC Device Starting\r\n");

    /* Initialize USB device */
    usb_device_init();
    log_state("USB initialized, waiting for host");

    const char *echo_msg = "Hello from USB CDC!\r\n";

    while (1) {
        /* Poll USB stack */
        usb_device_poll();

        /* Log state transitions */
        static usb_device_state_t last_state = USB_STATE_DEFAULT;
        if (usb_dev.state != last_state) {
            last_state = usb_dev.state;
            switch (usb_dev.state) {
            case USB_STATE_DEFAULT:
                log_state("State: DEFAULT");
                break;
            case USB_STATE_ADDRESSED:
                log_state("State: ADDRESSED");
                break;
            case USB_STATE_CONFIGURED:
                log_state("State: CONFIGURED");
                break;
            case USB_STATE_SUSPENDED:
                log_state("State: SUSPENDED");
                break;
            }
        }

        /* Echo received data back */
        if (usb_dev.state == USB_STATE_CONFIGURED) {
            uint8_t buf[64];
            int len = usb_cdc_recv(buf, sizeof(buf));
            if (len > 0) {
                usb_cdc_send(buf, len);

                /* Log on debug UART */
                uart_send_string("[USB] Received ");
                uart_send_hex(len);
                uart_send_string(" bytes\r\n");
            }

            /* Send periodic message if DTR is active */
            if (usb_cdc_dtr_active()) {
                static uint32_t counter = 0;
                static uint32_t last_send = 0;
                /* Simplified timing — use a real timer in production */
                counter++;
                if (counter % 100000 == 0) {
                    char msg[64];
                    int n = 0;
                    const char *p = echo_msg;
                    while (*p && n < 63) msg[n++] = *p++;
                    msg[n] = '\0';
                    usb_cdc_send((uint8_t *)msg, n);
                }
            }
        }
    }

    return 0;
}
```

### Build Instructions (C)

```bash
# Create build directory
mkdir -p build/c-usb-cdc && cd build/c-usb-cdc

# Compile with ARM GCC
arm-none-eabi-gcc \
    -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
    -O2 -Wall -Wextra -Wpedantic \
    -fno-common -ffunction-sections -fdata-sections \
    -nostdlib \
    -I../../src \
    -T ../../src/stm32f405rg.ld \
    ../../src/main.c \
    ../../src/usb_device.c \
    ../../src/usb_descriptors.c \
    ../../src/startup_stm32f405xx.s \
    -o usb_cdc.elf

# Generate binary and hex
arm-none-eabi-objcopy -O binary usb_cdc.elf usb_cdc.bin
arm-none-eabi-objcopy -O ihex usb_cdc.elf usb_cdc.hex

# Generate listing for inspection
arm-none-eabi-objdump -d -S usb_cdc.elf > usb_cdc.lst
```

### Run in QEMU

```bash
# QEMU with USB device support (limited)
qemu-system-arm \
    -M netduinoplus2 \
    -kernel usb_cdc.bin \
    -serial stdio \
    -d unimp,guest_errors \
    -D qemu.log
```

> **Note:** USB OTG_FS is not simulated in QEMU's `netduinoplus2` machine. The QEMU commands above will boot the firmware but USB enumeration cannot be tested. For full USB testing, use real hardware (NUCLEO-F446RE) or Renode, which provides more complete STM32 USB peripheral emulation.

## Implementation: Rust

### USB CDC with usb-device and usbd-serial

The Rust ecosystem provides the `usb-device` crate for USB device stack implementation and `usbd-serial` for CDC-ACM class support. We'll use these with the `stm32f4xx-hal` BSP.

```toml
# Cargo.toml
[package]
name = "usb-cdc-device"
version = "0.1.0"
edition = "2021"

[dependencies]
cortex-m = { version = "0.7", features = ["critical-section-single-core"] }
cortex-m-rt = "0.7"
panic-halt = "0.2"
stm32f4xx-hal = { version = "0.21", features = ["stm32f405"] }
usb-device = "0.3"
usbd-serial = "0.2"
heapless = "0.8"
embedded-hal = "1.0"

[profile.release]
opt-level = "s"
lto = true
codegen-units = 1
debug = true
```

```rust
// src/main.rs
#![no_std]
#![no_main]

use cortex_m_rt::{entry, exception, ExceptionFrame};
use panic_halt as _;
use stm32f4xx_hal::{
    pac,
    prelude::*,
    gpio::{Output, PushPull, PA11, PA12},
    usb_bus::{UsbBus, UsbBusType},
    otg_fs::{UsbBus as OTGFSBus, UsbPeripheral},
};
use usb_device::{
    bus::UsbBusAllocator,
    device::UsbDeviceBuilder,
    prelude::*,
};
use usbd_serial::{SerialPort, USB_CLASS_CDC};
use heapless::String;

// Type alias for our USB bus
type UsbBusType = OTGFSBus<pac::OTG_FS_GLOBAL>;

// USB device state tracking
#[derive(Debug, Clone, Copy, PartialEq)]
enum DeviceState {
    Default,
    Addressed,
    Configured,
    Suspended,
}

// Global state (set in interrupt, read in main)
static mut USB_STATE: DeviceState = DeviceState::Default;

#[entry]
fn main() -> ! {
    let dp = pac::Peripherals::take().unwrap();
    let cp = cortex_m::Peripherals::take().unwrap();

    // Configure clocks: HSE 8MHz -> PLL -> 168MHz SYSCLK, 48MHz USB
    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr
        .use_hse(8.MHz())
        .sysclk(168.MHz())
        .pclk1(42.MHz())
        .pclk2(84.MHz())
        .require_pll48clk()
        .freeze();

    // Verify USB clock is valid
    assert!(clocks.is_usb48_valid());

    // GPIO setup for USB pins (PA11 = DM, PA12 = DP)
    let gpioa = dp.GPIOA.split();
    let _dm = gpioa.pa11.into_alternate::<10>();
    let _dp = gpioa.pa12.into_alternate::<10>();

    // USB peripheral setup
    let usb = OTGFSBus::new(
        dp.OTG_FS_GLOBAL,
        dp.OTG_FS_DEVICE,
        dp.OTG_FS_PCGCCTL,
        dp.RCC,
        clocks,
    );
    let bus = UsbBusAllocator::new(usb);

    // Create CDC serial port
    let mut serial = SerialPort::new(&bus);

    // Build USB device
    let mut usb_dev = UsbDeviceBuilder::new(&bus, UsbVidPid(0x0483, 0x5740))
        .manufacturer("SafeEmbedded Labs")
        .product("CDC-ACM Virtual COM")
        .serial_number("123456789ABCDEF")
        .device_class(USB_CLASS_CDC)
        .max_packet_size_0(64)
        .build();

    // Debug UART (PA2 = TX)
    let tx_pin = gpioa.pa2.into_alternate::<7>();
    // In real code, configure USART2 here

    let mut echo_buf = [0u8; 64];
    let mut tick_counter: u32 = 0;
    let mut last_state = DeviceState::Default;

    loop {
        // Poll USB device
        let polled = usb_dev.poll(&mut [&mut serial]);

        // Track state changes
        let current_state = if usb_dev.state() == UsbDeviceState::Configured {
            DeviceState::Configured
        } else if usb_dev.state() == UsbDeviceState::Addressed {
            DeviceState::Addressed
        } else if usb_dev.state() == UsbDeviceState::Suspend {
            DeviceState::Suspended
        } else {
            DeviceState::Default
        };

        if current_state != last_state {
            last_state = current_state;
            unsafe { USB_STATE = current_state; }
            // Log state change via debug UART
        }

        if polled {
            // Try to read from CDC
            match serial.read(&mut echo_buf) {
                Ok(count) if count > 0 => {
                    // Echo back
                    let _ = serial.write(&echo_buf[..count]);

                    // Log received count via debug UART
                }
                _ => {}
            }

            // Try to write periodic message
            tick_counter += 1;
            if tick_counter % 100000 == 0 {
                let msg = b"Hello from USB CDC!\r\n";
                let _ = serial.write(msg);
            }
        }
    }
}

#[exception]
fn HardFault(ef: &ExceptionFrame) -> ! {
    loop {}
}
```

```rust
// src/descriptors.rs — Custom descriptor inspection
use usb_device::descriptor::{lang_id::LangID, BosWriter, DescriptorWriter};
use usb_device::class::ControlIn;

/// Inspect the raw descriptor bytes for debugging
pub fn print_descriptor_summary() {
    // Device descriptor fields (from usb-device crate internals)
    // bLength: 18
    // bDescriptorType: 0x01 (DEVICE)
    // bcdUSB: 0x0200 (USB 2.0)
    // bDeviceClass: 0x02 (CDC)
    // bMaxPacketSize0: 64
    // idVendor: 0x0483
    // idProduct: 0x5740
    // bNumConfigurations: 1
}

/// Validate descriptor sizes at compile time
pub const fn validate_descriptor_sizes() {
    const DEVICE_DESC_LEN: usize = 18;
    const CONFIG_DESC_LEN: usize = 9;
    const INTERFACE_DESC_LEN: usize = 9;
    const ENDPOINT_DESC_LEN: usize = 7;
    const CDC_HEADER_DESC_LEN: usize = 5;

    // These are enforced by the usb-device crate's type system
    // but we document the expected sizes here
    assert!(DEVICE_DESC_LEN == 18);
    assert!(CONFIG_DESC_LEN == 9);
    assert!(INTERFACE_DESC_LEN == 9);
    assert!(ENDPOINT_DESC_LEN == 7);
}
```

### Build Instructions (Rust)

```bash
# Install ARM target
rustup target add thumbv7em-none-eabihf

# Build
cargo build --target thumbv7em-none-eabihf --release

# Generate binary
cargo objcopy --target thumbv7em-none-eabihf --release -- -O binary usb_cdc.bin

# Size analysis
cargo size --target thumbv7em-none-eabihf --release -- -A
```

### Run in QEMU

```bash
qemu-system-arm \
    -M netduinoplus2 \
    -kernel target/thumbv7em-none-eabihf/release/usb-cdc-device \
    -serial stdio \
    -d unimp,guest_errors \
    -D qemu_rust.log
```

> **Note:** USB OTG_FS is not simulated in QEMU's `netduinoplus2` machine. For full USB enumeration testing, use real hardware (NUCLEO-F446RE) or Renode.

## Implementation: Ada

### USB Device with Strong Typing for Descriptors

Ada's strong typing system is ideal for USB descriptor validation. We'll use Ada 2012 features including aspect specifications and representation clauses to ensure descriptor correctness at compile time.

```ada
-- usb_defs.ads — USB protocol definitions with strong typing
package USB_Defs is
   pragma Pure;

   -- USB Request Type fields
   type USB_Direction is (Device_To_Host, Host_To_Device);
   for USB_Direction use (Device_To_Host => 1, Host_To_Device => 0);

   type USB_Request_Type is (Standard, Class, Vendor);
   for USB_Request_Type use (Standard => 0, Class => 1, Vendor => 2);

   type USB_Recipient is (Device, Interface, Endpoint, Other);
   for USB_Recipient use (Device => 0, Interface => 1, Endpoint => 2, Other => 3);

   -- Standard requests
   type USB_Standard_Request is
     (Get_Status, Clear_Feature, Set_Feature, Set_Address,
      Get_Descriptor, Set_Descriptor, Get_Configuration, Set_Configuration,
      Get_Interface, Set_Interface, Synch_Frame);
   for USB_Standard_Request use
     (Get_Status => 0, Clear_Feature => 1, Set_Feature => 3,
      Set_Address => 5, Get_Descriptor => 6, Set_Descriptor => 7,
      Get_Configuration => 8, Set_Configuration => 9,
      Get_Interface => 10, Set_Interface => 11, Synch_Frame => 12);

   -- Descriptor types
   type USB_Descriptor_Type is
     (Device_Desc, Configuration_Desc, String_Desc,
      Interface_Desc, Endpoint_Desc, Device_Qualifier,
      Other_Speed, Interface_Power, BOS);
   for USB_Descriptor_Type use
     (Device_Desc => 1, Configuration_Desc => 2, String_Desc => 3,
      Interface_Desc => 4, Endpoint_Desc => 5, Device_Qualifier => 6,
      Other_Speed => 7, Interface_Power => 8, BOS => 15);

   -- Endpoint types
   type USB_Endpoint_Type is (Control, Isochronous, Bulk, Interrupt);
   for USB_Endpoint_Type use (Control => 0, Isochronous => 1, Bulk => 2, Interrupt => 3);

   -- Device states
   type USB_Device_State is (Default, Addressed, Configured, Suspended);

   -- CDC class requests
   type USB_CDC_Request is
     (Send_Encapsulated_Command, Get_Encapsulated_Response,
      Set_Line_Coding, Get_Line_Coding, Set_Control_Line_State, Send_Break);
   for USB_CDC_Request use
     (Send_Encapsulated_Command => 0, Get_Encapsulated_Response => 1,
      Set_Line_Coding => 16#20#, Get_Line_Coding => 16#21#,
      Set_Control_Line_State => 16#22#, Send_Break => 16#23#);

   -- Character format
   type CDC_Char_Format is (Stop_Bits_1, Stop_Bits_1_5, Stop_Bits_2);
   for CDC_Char_Format use (Stop_Bits_1 => 0, Stop_Bits_1_5 => 1, Stop_Bits_2 => 2);

   -- Parity type
   type CDC_Parity_Type is (None, Odd, Even, Mark, Space);
   for CDC_Parity_Type use (None => 0, Odd => 1, Even => 2, Mark => 3, Space => 4);

   -- Line coding record (matches USB CDC spec exactly)
   type USB_CDC_Line_Coding is record
      DTERate      : Interfaces.Unsigned_32;
      CharFormat   : CDC_Char_Format;
      ParityType   : CDC_Parity_Type;
      DataBits     : Interfaces.Unsigned_8;
   end record
     with Size => 56,  -- 7 bytes * 8 bits
          Bit_Order => System.Low_Order_First;
   for USB_CDC_Line_Coding use record
      DTERate      at 0 range 0 .. 31;
      CharFormat   at 4 range 0 .. 7;
      ParityType   at 5 range 0 .. 7;
      DataBits     at 6 range 0 .. 7;
   end record;

   -- Setup packet (8 bytes, USB spec)
   type USB_Setup_Packet is record
      bmRequestType : Interfaces.Unsigned_8;
      bRequest      : Interfaces.Unsigned_8;
      wValue        : Interfaces.Unsigned_16;
      wIndex        : Interfaces.Unsigned_16;
      wLength       : Interfaces.Unsigned_16;
   end record
     with Size => 64,  -- 8 bytes * 8 bits
          Bit_Order => System.Low_Order_First;
   for USB_Setup_Packet use record
      bmRequestType at 0 range 0 .. 7;
      bRequest      at 1 range 0 .. 7;
      wValue        at 2 range 0 .. 15;
      wIndex        at 4 range 0 .. 15;
      wLength       at 6 range 0 .. 15;
   end record;

end USB_Defs;
```

```ada
-- usb_descriptors.ads — Descriptor package specification
with USB_Defs; use USB_Defs;
with Interfaces;

package USB_Descriptors is
   pragma Preelaborate;

   -- Maximum number of string descriptors
   Max_String_Descs : constant := 4;

   -- Descriptor tables (stored in ROM)
   Device_Descriptor : constant array (1 .. 18) of Interfaces.Unsigned_8;
   Config_Descriptor : constant array (1 .. 67) of Interfaces.Unsigned_8;

   type String_Descriptor_Access is access constant array (Positive range <>) of Interfaces.Unsigned_8;
   String_Descriptors : constant array (1 .. Max_String_Descs) of String_Descriptor_Access;

   -- Validation function
   function Validate_Descriptors return Boolean;

private
   -- Device Descriptor: 18 bytes
   Device_Descriptor : constant array (1 .. 18) of Interfaces.Unsigned_8 :=
     (16#12#,  -- bLength
      16#01#,  -- bDescriptorType: DEVICE
      16#00#, 16#02#,  -- bcdUSB: 2.00
      16#02#,  -- bDeviceClass: CDC
      16#00#,  -- bDeviceSubClass
      16#00#,  -- bDeviceProtocol
      16#40#,  -- bMaxPacketSize0: 64
      16#83#, 16#04#,  -- idVendor: 0x0483
      16#40#, 16#57#,  -- idProduct: 0x5740
      16#00#, 16#02#,  -- bcdDevice: 2.00
      16#01#,  -- iManufacturer
      16#02#,  -- iProduct
      16#03#,  -- iSerialNumber
      16#01#); -- bNumConfigurations

   -- Configuration Descriptor with CDC-ACM: 67 bytes
   Config_Descriptor : constant array (1 .. 67) of Interfaces.Unsigned_8 :=
     (16#09#, 16#02#, 16#43#, 16#00#, 16#02#, 16#01#, 16#00#, 16#80#, 16#FA#,  -- Config
      16#09#, 16#04#, 16#00#, 16#00#, 16#01#, 16#02#, 16#02#, 16#01#, 16#00#,  -- Interface 0
      16#05#, 16#24#, 16#00#, 16#10#, 16#01#,  -- Header
      16#05#, 16#24#, 16#01#, 16#00#, 16#01#,  -- Call Management
      16#04#, 16#24#, 16#02#, 16#02#,           -- ACM
      16#05#, 16#24#, 16#06#, 16#00#, 16#01#,  -- Union
      16#07#, 16#05#, 16#81#, 16#03#, 16#08#, 16#00#, 16#FF#,  -- EP1 IN
      16#09#, 16#04#, 16#01#, 16#00#, 16#02#, 16#0A#, 16#00#, 16#00#, 16#00#,  -- Interface 1
      16#07#, 16#05#, 16#83#, 16#02#, 16#40#, 16#00#, 16#00#,  -- EP3 IN
      16#07#, 16#05#, 16#02#, 16#02#, 16#40#, 16#00#, 16#00#); -- EP2 OUT

   -- String descriptors (UTF-16LE encoded)
   String_Desc_Lang : aliased constant array (1 .. 4) of Interfaces.Unsigned_8 :=
     (16#04#, 16#03#, 16#09#, 16#04#);

   String_Desc_Manufacturer : aliased constant array (1 .. 30) of Interfaces.Unsigned_8 :=
     (16#1E#, 16#03#,
      16#53#, 16#00#, 16#61#, 16#00#, 16#66#, 16#00#, 16#65#, 16#00#,
      16#45#, 16#00#, 16#6D#, 16#00#, 16#62#, 16#00#, 16#65#, 16#00#,
      16#64#, 16#00#, 16#64#, 16#00#, 16#65#, 16#00#, 16#64#, 16#00#,
      16#20#, 16#00#, 16#4C#, 16#00#, 16#61#, 16#00#, 16#62#, 16#00#,
      16#73#, 16#00#);

   String_Desc_Product : aliased constant array (1 .. 28) of Interfaces.Unsigned_8 :=
     (16#1C#, 16#03#,
      16#43#, 16#00#, 16#44#, 16#00#, 16#43#, 16#00#, 16#2D#, 16#00#,
      16#41#, 16#00#, 16#43#, 16#00#, 16#4D#, 16#00#, 16#20#, 16#00#,
      16#56#, 16#00#, 16#69#, 16#00#, 16#72#, 16#00#, 16#74#, 16#00#,
      16#75#, 16#00#, 16#61#, 16#00#, 16#6C#, 16#00#, 16#20#, 16#00#,
      16#43#, 16#00#, 16#4F#, 16#00#, 16#4D#, 16#00#);

   String_Desc_Serial : aliased constant array (1 .. 18) of Interfaces.Unsigned_8 :=
     (16#12#, 16#03#,
      16#31#, 16#00#, 16#32#, 16#00#, 16#33#, 16#00#, 16#34#, 16#00#,
      16#35#, 16#00#, 16#36#, 16#00#, 16#37#, 16#00#, 16#38#, 16#00#,
      16#39#, 16#00#, 16#41#, 16#00#, 16#42#, 16#00#, 16#43#, 16#00#,
      16#44#, 16#00#, 16#45#, 16#00#, 16#46#, 16#00#);

   String_Descriptors : constant array (1 .. Max_String_Descs) of String_Descriptor_Access :=
     (String_Desc_Lang'Access,
      String_Desc_Manufacturer'Access,
      String_Desc_Product'Access,
      String_Desc_Serial'Access);

end USB_Descriptors;
```

```ada
-- usb_descriptors.adb — Descriptor validation
with USB_Defs; use USB_Defs;

package body USB_Descriptors is

   function Validate_Descriptors return Boolean is
   begin
      -- Validate device descriptor length
      if Device_Descriptor (1) /= 18 then
         return False;
      end if;

      -- Validate device descriptor type
      if Device_Descriptor (2) /= Interfaces.Unsigned_8 (USB_Descriptor_Type'Pos (Device_Desc)) then
         return False;
      end if;

      -- Validate configuration descriptor total length
      if Config_Descriptor (3) + Config_Descriptor (4) * 256 /= 67 then
         return False;
      end if;

      -- Validate string descriptor 0 (language ID)
      if String_Descriptors (1) (1) /= 4 then
         return False;
      end if;

      return True;
   end Validate_Descriptors;

end USB_Descriptors;
```

```ada
-- usb_device.ads — USB device stack interface
with USB_Defs; use USB_Defs;
with Interfaces;

package USB_Device is

   type USB_Device_Context is record
      State        : USB_Device_State := Default;
      Address      : Interfaces.Unsigned_8 := 0;
      Configuration: Interfaces.Unsigned_8 := 0;
      Line_Coding  : USB_CDC_Line_Coding;
      Control_State: Interfaces.Unsigned_8 := 0;
   end record;

   -- Global device context
   Device : USB_Device_Context;

   -- Initialize USB device
   procedure USB_Device_Init;

   -- Process USB events (call from main loop)
   procedure USB_Device_Poll;

   -- Send data over CDC bulk IN
   function USB_CDC_Send (Data : in Interfaces.Unsigned_8_Array) return Natural;

   -- Receive data from CDC bulk OUT
   function USB_CDC_Recv (Data : out Interfaces.Unsigned_8_Array) return Natural;

   -- Get current line coding
   function USB_CDC_Get_Line_Coding return USB_CDC_Line_Coding;

   -- Check DTR state
   function USB_CDC_DTR_Active return Boolean;

end USB_Device;
```

```ada
-- usb_device.adb — USB device implementation
with USB_Descriptors;
with Interfaces;
with System;

package body USB_Device is

   -- STM32F4 USB OTG_FS register addresses (simplified)
   USB_OTG_FS_BASE : constant := 16#5000_0000#;

   type USB_OTG_Registers is record
      GOTGCTL  : Interfaces.Unsigned_32;
      GAHBCFG  : Interfaces.Unsigned_32;
      GUSBCFG  : Interfaces.Unsigned_32;
      -- ... more registers
   end record
     with Volatile, Address => System'To_Address (USB_OTG_FS_BASE);

   USB_Regs : USB_OTG_Registers
     with Import, Address => System'To_Address (USB_OTG_FS_BASE), Volatile;

   -- Endpoint buffer management
   type Endpoint_Buffer is array (1 .. 64) of Interfaces.Unsigned_8;
   type Ring_Buffer is record
      Data : Interfaces.Unsigned_8_Array (1 .. 256);
      Head : Natural := 1;
      Tail : Natural := 1;
   end record;

   RX_Buffer : Ring_Buffer;
   TX_Buffer : Ring_Buffer;

   procedure USB_Device_Init is
   begin
      Device.State := Default;
      Device.Address := 0;
      Device.Configuration := 0;
      Device.Line_Coding := (DTERate => 115200, CharFormat => Stop_Bits_1,
                             ParityType => None, DataBits => 8);
      Device.Control_State := 0;

      -- Validate descriptors at startup
      if not USB_Descriptors.Validate_Descriptors then
         raise Program_Error with "Invalid USB descriptors";
      end if;

      -- Initialize hardware (simplified)
      null;
   end USB_Device_Init;

   procedure USB_Device_Poll is
      -- In a full implementation, this would read interrupt status
      -- and handle USB events
   begin
      null;
   end USB_Device_Poll;

   function USB_CDC_Send (Data : in Interfaces.Unsigned_8_Array) return Natural is
      Count : Natural := 0;
   begin
      if Device.State /= Configured then
         return 0;
      end if;

      for I in Data'Range loop
         declare
            Next : constant Natural := (TX_Buffer.Head mod 256) + 1;
         begin
            exit when Next = TX_Buffer.Tail;
            TX_Buffer.Data (TX_Buffer.Head) := Data (I);
            TX_Buffer.Head := Next;
            Count := Count + 1;
         end;
      end loop;

      return Count;
   end USB_CDC_Send;

   function USB_CDC_Recv (Data : out Interfaces.Unsigned_8_Array) return Natural is
      Count : Natural := 0;
   begin
      for I in Data'Range loop
         exit when RX_Buffer.Head = RX_Buffer.Tail;
         Data (I) := RX_Buffer.Data (RX_Buffer.Tail);
         RX_Buffer.Tail := (RX_Buffer.Tail mod 256) + 1;
         Count := Count + 1;
      end loop;

      return Count;
   end USB_CDC_Recv;

   function USB_CDC_Get_Line_Coding return USB_CDC_Line_Coding is
   begin
      return Device.Line_Coding;
   end USB_CDC_Get_Line_Coding;

   function USB_CDC_DTR_Active return Boolean is
   begin
      return (Device.Control_State and 16#01#) /= 0;
   end USB_CDC_DTR_Active;

end USB_Device;
```

```ada
-- main.adb — USB CDC application
with USB_Device; use USB_Device;
with USB_Defs; use USB_Defs;

procedure Main is
   Echo_Msg : constant String := "Hello from USB CDC!" & ASCII.CR & ASCII.LF;
   Last_State : USB_Device_State := Default;
begin
   USB_Device_Init;

   loop
      USB_Device_Poll;

      -- Log state changes
      if Device.State /= Last_State then
         Last_State := Device.State;
         -- Log via debug UART
      end if;

      -- Echo received data
      if Device.State = Configured then
         declare
            Recv_Buf : Interfaces.Unsigned_8_Array (1 .. 64);
            Len : constant Natural := USB_CDC_Recv (Recv_Buf);
         begin
            if Len > 0 then
               declare
                  Sent : constant Natural := USB_CDC_Send (Recv_Buf (1 .. Len));
               begin
                  null;  -- Log sent count
               end;
            end if;
         end;

         -- Send periodic message if DTR active
         if USB_CDC_DTR_Active then
            declare
               Msg_Buf : Interfaces.Unsigned_8_Array (1 .. Echo_Msg'Length);
            begin
               for I in Echo_Msg'Range loop
                  Msg_Buf (I) := Interfaces.Unsigned_8 (Character'Pos (Echo_Msg (I)));
               end loop;
               null;  -- USB_CDC_Send (Msg_Buf);
            end;
         end if;
      end if;
   end loop;
end Main;
```

### Build Instructions (Ada)

```bash
# Create project file
cat > usb_cdc.gpr << 'EOF'
project USB_CDC is
   for Source_Dirs use ("src");
   for Object_Dir use "obj";
   for Main use ("main.adb");
   for Languages use ("Ada");

   package Compiler is
      for Default_Switches ("Ada") use (
         "-O2", "-gnatp", "-gnatwa",
         "-mcpu=cortex-m4", "-mthumb",
         "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16"
      );
   end Compiler;

   package Binder is
      for Default_Switches ("Ada") use ("-nostdlib");
   end Binder;

   package Linker is
      for Default_Switches ("Ada") use (
         "-mcpu=cortex-m4", "-mthumb",
         "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16",
          "-T", "stm32f405rg.ld",
         "-nostartfiles"
      );
   end Linker;
end USB_CDC;
EOF

# Build with GNAT
gnatmake -P usb_cdc.gpr

# Generate binary
arm-none-eabi-objcopy -O binary main usb_cdc.bin
```

### Run in QEMU

```bash
qemu-system-arm \
    -M netduinoplus2 \
    -kernel usb_cdc.bin \
    -serial stdio \
    -d unimp,guest_errors \
    -D qemu_ada.log
```

> **Note:** USB OTG_FS is not simulated in QEMU's `netduinoplus2` machine. For full USB enumeration testing, use real hardware (NUCLEO-F446RE) or Renode.

## Implementation: Zig

### USB Stack with Comptime Descriptor Generation

Zig's comptime capabilities allow us to generate and validate USB descriptors at compile time, catching errors before the code ever runs.

```zig
// src/usb_defs.zig
const std = @import("std");

pub const Direction = enum(u1) {
    host_to_device = 0,
    device_to_host = 1,
};

pub const RequestType = enum(u2) {
    standard = 0,
    class = 1,
    vendor = 2,
    reserved = 3,
};

pub const Recipient = enum(u2) {
    device = 0,
    interface = 1,
    endpoint = 2,
    other = 3,
};

pub const StandardRequest = enum(u8) {
    get_status = 0x00,
    clear_feature = 0x01,
    set_feature = 0x03,
    set_address = 0x05,
    get_descriptor = 0x06,
    set_descriptor = 0x07,
    get_configuration = 0x08,
    set_configuration = 0x09,
    get_interface = 0x0A,
    set_interface = 0x0B,
};

pub const DescriptorType = enum(u8) {
    device = 0x01,
    configuration = 0x02,
    string = 0x03,
    interface = 0x04,
    endpoint = 0x05,
    device_qualifier = 0x06,
    other_speed = 0x07,
    bos = 0x0F,
};

pub const EndpointType = enum(u2) {
    control = 0,
    isochronous = 1,
    bulk = 2,
    interrupt = 3,
};

pub const DeviceState = enum(u2) {
    default,
    addressed,
    configured,
    suspended,
};

pub const CDCRequest = enum(u8) {
    send_encapsulated_command = 0x00,
    get_encapsulated_response = 0x01,
    set_line_coding = 0x20,
    get_line_coding = 0x21,
    set_control_line_state = 0x22,
    send_break = 0x23,
};

pub const CharFormat = enum(u8) {
    stop_bits_1 = 0,
    stop_bits_1_5 = 1,
    stop_bits_2 = 2,
};

pub const ParityType = enum(u8) {
    none = 0,
    odd = 1,
    even = 2,
    mark = 3,
    space = 4,
};

pub const LineCoding = extern struct {
    dw_dte_rate: u32,
    b_char_format: CharFormat,
    b_parity_type: ParityType,
    b_data_bits: u8,
};

pub const SetupPacket = extern struct {
    bm_request_type: u8,
    b_request: u8,
    w_value: u16,
    w_index: u16,
    w_length: u16,
};

pub const RequestTypeField = packed struct(u8) {
    recipient: Recipient,
    request_type: RequestType,
    direction: Direction,
};
```

```zig
// src/usb_descriptors.zig — Comptime descriptor generation
const std = @import("std");
const usb = @import("usb_defs.zig");

// Descriptor builder types for comptime validation
pub const DeviceDescriptor = extern struct {
    b_length: u8 = 18,
    b_descriptor_type: u8 = @intFromEnum(usb.DescriptorType.device),
    bcd_usb: u16 = 0x0200,
    b_device_class: u8 = 0x02,
    b_device_sub_class: u8 = 0x00,
    b_device_protocol: u8 = 0x00,
    b_max_packet_size_0: u8 = 64,
    id_vendor: u16 = 0x0483,
    id_product: u16 = 0x5740,
    bcd_device: u16 = 0x0200,
    i_manufacturer: u8 = 1,
    i_product: u8 = 2,
    i_serial_number: u8 = 3,
    b_num_configurations: u8 = 1,

    pub fn asBytes(self: @This()) [18]u8 {
        return @bitCast(self);
    }

    pub fn validate(self: @This()) bool {
        return self.b_length == 18 and
            self.b_descriptor_type == @intFromEnum(usb.DescriptorType.device) and
            self.b_max_packet_size_0 == 8 or
            self.b_max_packet_size_0 == 16 or
            self.b_max_packet_size_0 == 32 or
            self.b_max_packet_size_0 == 64;
    }
};

pub const ConfigDescriptor = extern struct {
    b_length: u8 = 9,
    b_descriptor_type: u8 = @intFromEnum(usb.DescriptorType.configuration),
    w_total_length: u16,
    b_num_interfaces: u8,
    b_configuration_value: u8,
    i_configuration: u8,
    bm_attributes: u8,
    b_max_power: u8,

    pub fn asBytes(self: @This()) [9]u8 {
        return @bitCast(self);
    }
};

pub const InterfaceDescriptor = extern struct {
    b_length: u8 = 9,
    b_descriptor_type: u8 = @intFromEnum(usb.DescriptorType.interface),
    b_interface_number: u8,
    b_alternate_setting: u8,
    b_num_endpoints: u8,
    b_interface_class: u8,
    b_interface_sub_class: u8,
    b_interface_protocol: u8,
    i_interface: u8,

    pub fn asBytes(self: @This()) [9]u8 {
        return @bitCast(self);
    }
};

pub const EndpointDescriptor = extern struct {
    b_length: u8 = 7,
    b_descriptor_type: u8 = @intFromEnum(usb.DescriptorType.endpoint),
    b_endpoint_address: u8,
    bm_attributes: u8,
    w_max_packet_size: u16,
    b_interval: u8,

    pub fn asBytes(self: @This()) [7]u8 {
        return @bitCast(self);
    }
};

pub const CDCHeaderDescriptor = extern struct {
    b_length: u8 = 5,
    b_descriptor_type: u8 = 0x24,
    b_descriptor_subtype: u8 = 0x00,
    bcd_cdc: u16 = 0x0110,

    pub fn asBytes(self: @This()) [5]u8 {
        return @bitCast(self);
    }
};

pub const CDCUnionDescriptor = extern struct {
    b_length: u8 = 5,
    b_descriptor_type: u8 = 0x24,
    b_descriptor_subtype: u8 = 0x06,
    b_master_interface: u8,
    b_slave_interface: u8,

    pub fn asBytes(self: @This()) [5]u8 {
        return @bitCast(self);
    }
};

pub const CDCCallManagementDescriptor = extern struct {
    b_length: u8 = 5,
    b_descriptor_type: u8 = 0x24,
    b_descriptor_subtype: u8 = 0x01,
    bm_capabilities: u8,
    b_data_interface: u8,

    pub fn asBytes(self: @This()) [5]u8 {
        return @bitCast(self);
    }
};

pub const CDCACMDescriptor = extern struct {
    b_length: u8 = 4,
    b_descriptor_type: u8 = 0x24,
    b_descriptor_subtype: u8 = 0x02,
    bm_capabilities: u8,

    pub fn asBytes(self: @This()) [4]u8 {
        return @bitCast(self);
    }
};

// Comptime-generated full configuration descriptor
pub const config_descriptor_data = blk: {
    const config = ConfigDescriptor{
        .w_total_length = 67,
        .b_num_interfaces = 2,
        .b_configuration_value = 1,
        .i_configuration = 0,
        .bm_attributes = 0x80,
        .b_max_power = 250,
    };
    const comm_if = InterfaceDescriptor{
        .b_interface_number = 0,
        .b_alternate_setting = 0,
        .b_num_endpoints = 1,
        .b_interface_class = 0x02,
        .b_interface_sub_class = 0x02,
        .b_interface_protocol = 0x01,
        .i_interface = 0,
    };
    const header = CDCHeaderDescriptor{};
    const call_mgmt = CDCCallManagementDescriptor{
        .bm_capabilities = 0x00,
        .b_data_interface = 1,
    };
    const acm = CDCACMDescriptor{ .bm_capabilities = 0x02 };
    const union_desc = CDCUnionDescriptor{
        .b_master_interface = 0,
        .b_slave_interface = 1,
    };
    const ep1_in = EndpointDescriptor{
        .b_endpoint_address = 0x81,
        .bm_attributes = @intFromEnum(usb.EndpointType.interrupt),
        .w_max_packet_size = 8,
        .b_interval = 255,
    };
    const data_if = InterfaceDescriptor{
        .b_interface_number = 1,
        .b_alternate_setting = 0,
        .b_num_endpoints = 2,
        .b_interface_class = 0x0A,
        .b_interface_sub_class = 0x00,
        .b_interface_protocol = 0x00,
        .i_interface = 0,
    };
    const ep3_in = EndpointDescriptor{
        .b_endpoint_address = 0x83,
        .bm_attributes = @intFromEnum(usb.EndpointType.bulk),
        .w_max_packet_size = 64,
        .b_interval = 0,
    };
    const ep2_out = EndpointDescriptor{
        .b_endpoint_address = 0x02,
        .bm_attributes = @intFromEnum(usb.EndpointType.bulk),
        .w_max_packet_size = 64,
        .b_interval = 0,
    };

    // Concatenate all descriptors at comptime
    var buf: [67]u8 = undefined;
    var offset: usize = 0;

    inline for (.{ config.asBytes(), comm_if.asBytes(), header.asBytes(),
        call_mgmt.asBytes(), acm.asBytes(), union_desc.asBytes(),
        ep1_in.asBytes(), data_if.asBytes(), ep3_in.asBytes(),
        ep2_out.asBytes() }) |desc| {
        @memcpy(buf[offset .. offset + desc.len], &desc);
        offset += desc.len;
    }

    // Validate total length at comptime
    std.debug.assert(offset == 67);

    break :blk buf;
};

pub const device_descriptor_data = blk: {
    const dev = DeviceDescriptor{};
    std.debug.assert(dev.validate());
    break :blk dev.asBytes();
};

// String descriptors (UTF-16LE)
pub const string_desc_lang = [_]u8{ 0x04, 0x03, 0x09, 0x04 };

pub fn makeStringDescriptor(comptime s: []const u8) struct {
    data: [4 + s.len * 2]u8,
} {
    var buf: [4 + s.len * 2]u8 = undefined;
    buf[0] = 4 + @as(u8, @intCast(s.len * 2));
    buf[1] = 0x03;
    for (s, 0..) |c, i| {
        buf[2 + i * 2] = c;
        buf[3 + i * 2] = 0;
    }
    return .{ .data = buf };
}

pub const string_desc_manufacturer = makeStringDescriptor("SafeEmbedded Labs").data;
pub const string_desc_product = makeStringDescriptor("CDC-ACM Virtual COM").data;
pub const string_desc_serial = makeStringDescriptor("123456789ABCDEF").data;

pub const string_descriptors = [_][]const u8{
    &string_desc_lang,
    &string_desc_manufacturer,
    &string_desc_product,
    &string_desc_serial,
};
```

```zig
// src/usb_device.zig — USB device stack
const std = @import("std");
const usb = @import("usb_defs.zig");
const descriptors = @import("usb_descriptors.zig");

pub const USBDevice = struct {
    state: usb.DeviceState = .default,
    address: u8 = 0,
    configuration: u8 = 0,
    line_coding: usb.LineCoding = .{
        .dw_dte_rate = 115200,
        .b_char_format = .stop_bits_1,
        .b_parity_type = .none,
        .b_data_bits = 8,
    },
    control_line_state: u8 = 0,

    // Ring buffers for CDC data
    rx_buf: [256]u8 = undefined,
    rx_head: usize = 0,
    rx_tail: usize = 0,
    tx_buf: [256]u8 = undefined,
    tx_head: usize = 0,
    tx_tail: usize = 0,

    // EP0 state
    ep0_buf: [64]u8 = undefined,
    ep0_data_ptr: ?[*]const u8 = null,
    ep0_remaining: usize = 0,

    pub fn init(self: *USBDevice) void {
        self.* = .{};
        // Hardware initialization would go here
    }

    pub fn poll(self: *USBDevice) void {
        // Read interrupt status and handle events
        // Simplified for tutorial — full implementation reads OTG_FS registers
    }

    pub fn handleSetup(self: *USBDevice, setup: *const usb.SetupPacket) void {
        const req_type: usb.RequestTypeField = @bitCast(setup.bm_request_type);

        switch (req_type.request_type) {
            .standard => self.handleStandardRequest(setup),
            .class => self.handleCDCRequest(setup),
            .vendor => self.stallEP0(),
            .reserved => self.stallEP0(),
        }
    }

    fn handleStandardRequest(self: *USBDevice, setup: *const usb.SetupPacket) void {
        const req: usb.StandardRequest = @enumFromInt(setup.b_request);

        switch (req) {
            .get_descriptor => {
                const desc_type: usb.DescriptorType = @enumFromInt(@as(u8, @truncate(setup.w_value >> 8)));
                const desc_index: u8 = @truncate(setup.w_value);

                const data: ?[]const u8 = switch (desc_type) {
                    .device => &descriptors.device_descriptor_data,
                    .configuration => &descriptors.config_descriptor_data,
                    .string => if (desc_index < descriptors.string_descriptors.len)
                        descriptors.string_descriptors[desc_index]
                    else
                        null,
                    else => null,
                };

                if (data) |d| {
                    const len = @min(setup.w_length, @as(u16, @intCast(d.len)));
                    self.ep0Send(d[0..len]);
                } else {
                    self.stallEP0();
                }
            },
            .set_address => {
                self.address = @truncate(setup.w_value);
                self.ep0Send(&.{});
            },
            .set_configuration => {
                self.configuration = @truncate(setup.w_value);
                if (self.configuration != 0) {
                    self.state = .configured;
                    self.activateEndpoints();
                } else {
                    self.state = .addressed;
                }
                self.ep0Send(&.{});
            },
            .get_configuration => {
                self.ep0_buf[0] = self.configuration;
                self.ep0Send(self.ep0_buf[0..1]);
            },
            .get_status => {
                self.ep0_buf[0] = 0;
                self.ep0_buf[1] = 0;
                self.ep0Send(self.ep0_buf[0..2]);
            },
            else => self.stallEP0(),
        }
    }

    fn handleCDCRequest(self: *USBDevice, setup: *const usb.SetupPacket) void {
        const req: usb.CDCRequest = @enumFromInt(setup.b_request);

        switch (req) {
            .set_line_coding => {
                self.ep0_remaining = setup.w_length;
            },
            .get_line_coding => {
                if (setup.w_length >= 7) {
                    const bytes: [*]const u8 = @ptrCast(&self.line_coding);
                    self.ep0Send(bytes[0..7]);
                } else {
                    self.stallEP0();
                }
            },
            .set_control_line_state => {
                self.control_line_state = @truncate(setup.w_value);
                self.ep0Send(&.{});
            },
            .send_encapsulated_command,
            .get_encapsulated_response,
            .send_break => {
                self.ep0Send(&.{});
            },
        }
    }

    fn ep0Send(self: *USBDevice, data: []const u8) void {
        const len = @min(data.len, 64);
        @memcpy(self.ep0_buf[0..len], data[0..len]);
        // Write to TX FIFO and enable endpoint
        _ = len;
    }

    fn stallEP0(self: *USBDevice) void {
        // Set STALL on EP0
    }

    fn activateEndpoints(self: *USBDevice) void {
        // Activate EP1 IN (interrupt), EP2 OUT (bulk), EP3 IN (bulk)
    }

    pub fn cdcSend(self: *USBDevice, data: []const u8) usize {
        if (self.state != .configured) return 0;

        var sent: usize = 0;
        for (data) |byte| {
            const next = (self.tx_head + 1) % 256;
            if (next == self.tx_tail) break;
            self.tx_buf[self.tx_head] = byte;
            self.tx_head = next;
            sent += 1;
        }
        return sent;
    }

    pub fn cdcRecv(self: *USBDevice, buf: []u8) usize {
        var recv: usize = 0;
        for (buf) |*b| {
            if (self.rx_head == self.rx_tail) break;
            b.* = self.rx_buf[self.rx_tail];
            self.rx_tail = (self.rx_tail + 1) % 256;
            recv += 1;
        }
        return recv;
    }

    pub fn dtrActive(self: *USBDevice) bool {
        return (self.control_line_state & 0x01) != 0;
    }
};
```

```zig
// src/main.zig — USB CDC application
const std = @import("std");
const usb_device = @import("usb_device.zig");

var usb_dev: usb_device.USBDevice = .{};

export fn main() noreturn {
    usb_dev.init();

    const echo_msg = "Hello from USB CDC!\r\n";
    var tick_counter: u32 = 0;

    while (true) {
        usb_dev.poll();

        if (usb_dev.state == .configured) {
            var recv_buf: [64]u8 = undefined;
            const len = usb_dev.cdcRecv(&recv_buf);
            if (len > 0) {
                _ = usb_dev.cdcSend(recv_buf[0..len]);
            }

            tick_counter += 1;
            if (tick_counter % 100000 == 0 and usb_dev.dtrActive()) {
                _ = usb_dev.cdcSend(echo_msg);
            }
        }
    }
}
```

### Build Instructions (Zig)

```bash
# Build for ARM Cortex-M4
zig build-exe src/main.zig \
    -target thumb-freestanding-eabihf \
    -mcpu cortex_m4 \
    -O ReleaseSmall \
    -femit-bin=usb_cdc.bin \
    -fno-entry

# Generate assembly output
zig build-exe src/main.zig \
    -target thumb-freestanding-eabihf \
    -mcpu cortex_m4 \
    -O ReleaseSmall \
    -fno-entry \
    --verbose-llvm-ir 2> usb_cdc.ll

# Comptime validation (compile-time descriptor checks)
zig build-exe src/usb_descriptors.zig \
    -target thumb-freestanding-eabihf \
    -mcpu cortex_m4 \
    --verbose-cimport
```

### Run in QEMU

```bash
qemu-system-arm \
    -M netduinoplus2 \
    -kernel usb_cdc.bin \
    -serial stdio \
    -d unimp,guest_errors \
    -D qemu_zig.log
```

> **Note:** USB OTG_FS is not simulated in QEMU's `netduinoplus2` machine. For full USB enumeration testing, use real hardware (NUCLEO-F446RE) or Renode.

## Verification

### UART Debug Output

When running on real hardware or QEMU, monitor the debug UART for state machine transitions:

```
[USB] USB initialized, waiting for host
[USB] State: DEFAULT
[USB] State: ADDRESSED
[USB] State: CONFIGURED
[USB] Received 13 bytes
[USB] Received 5 bytes
```

### Descriptor Validation Checklist

- [ ] Device descriptor: 18 bytes, bcdUSB = 0x0200, bDeviceClass = 0x02
- [ ] Configuration descriptor: wTotalLength = 67, bNumInterfaces = 2
- [ ] Interface 0: bInterfaceClass = 0x02 (CDC), bInterfaceSubClass = 0x02 (ACM)
- [ ] CDC Header: bcdCDC = 0x0110
- [ ] CDC Union: bMasterInterface = 0, bSlaveInterface = 1
- [ ] EP1 IN: Interrupt, 8 bytes, interval = 255
- [ ] EP2 OUT: Bulk, 64 bytes
- [ ] EP3 IN: Bulk, 64 bytes
- [ ] String descriptors: valid UTF-16LE encoding

### Testing on Real Hardware

For full USB enumeration testing, use a Netduino Plus 2 or NUCLEO-F446RE:

```bash
# Flash with OpenOCD
openocd -f interface/stlink-v2.cfg -f target/stm32f4x.cfg \
    -c "program usb_cdc.bin 0x08000000 verify reset exit"

# Connect via USB
# Linux: device appears as /dev/ttyACM0
# Windows: device appears as COMx in Device Manager

# Test with screen/minicom
screen /dev/ttyACM0 115200

# Test with picocom
picocom -b 115200 /dev/ttyACM0
```

> **Warning:** USB enumeration requires proper 5V VBUS detection. On the Netduino Plus 2, ensure the USB connector is properly wired and the board is powered. Self-powered devices should disable VBUS sensing in the OTG_FS configuration.

## What You Learned

- USB 2.0 physical layer: differential signaling, NRZI encoding, bit stuffing
- USB device states and the host-driven enumeration process
- USB descriptor hierarchy: Device → Configuration → Interface → Endpoint
- CDC-ACM class: two-interface model with control and data interfaces
- Bulk endpoint management with ring buffers
- QEMU USB testing limitations and real hardware validation
- Language-specific approaches: C register-level, Rust ecosystem crates, Ada strong typing, Zig comptime generation

## Next Steps

- **Project 14**: Implement PID motor control with fault detection and watchdog timers
- **Project 15**: Apply safety-critical verification techniques to your USB stack
- Extend the CDC device with multiple virtual COM ports
- Implement USB OTG (On-The-Go) host/device switching
- Add USB audio class (UAC) or HID class support
- Implement USB 3.0 SuperSpeed descriptors and link training

## Language Comparison

| Aspect | C | Rust | Ada | Zig |
|--------|---|------|-----|-----|
| Descriptor definition | Manual byte arrays, `__attribute__((packed))` | Type-safe via `usb-device` crate | Record types with representation clauses | `extern struct` with comptime validation |
| Endpoint management | Manual register writes, FIFO management | Abstracted via `usb-device` traits | Explicit type-safe endpoint records | Comptime endpoint configuration |
| Buffer handling | Ring buffers with manual index math | `heapless` VecDeque, no allocation | Array-based ring buffers with bounds checks | Compile-time sized ring buffers |
| State machine | Enum + manual state transitions | Type-state pattern, compile-time enforced | Enum with exhaustive pattern matching | Enum with inline state checks |
| Error handling | Return codes, manual checking | `Result<T, E>` with `?` operator | Exceptions + return codes | Error unions with `try`/`catch` |
| Compile-time checks | `static_assert` macros | Const generics, const fn | Pre/Post conditions, compile-time eval | `comptime` blocks with `std.debug.assert` |
| USB ecosystem | From scratch or ST HAL | `usb-device`, `usbd-serial` | Limited; typically custom implementation | Emerging; mostly from scratch |
| Debugging | GDB + UART logging | `defmt`, `rtt-target` | GNAT debugger + UART | `std.debug.print`, compile-time tracing |

## Deliverables

- [ ] USB device descriptor table with correct values for CDC-ACM
- [ ] Configuration descriptor with all sub-descriptors (67 bytes total)
- [ ] String descriptors (language, manufacturer, product, serial)
- [ ] USB device state machine (Default → Addressed → Configured)
- [ ] CDC-ACM class request handlers (SetLineCoding, GetLineCoding, SetControlLineState)
- [ ] Bulk IN/OUT endpoint data transfer with ring buffers
- [ ] UART debug output showing state transitions
- [ ] Successful enumeration on real hardware (Netduino Plus 2)
- [ ] Bidirectional serial communication via `/dev/ttyACM0` or `COMx`

## References

### STMicroelectronics Documentation
- [STM32F4 Reference Manual (RM0090)](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-415-stm32f407-417-stm32f427-437-and-stm32f429-439-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) — Ch. 33: USB OTG FS (GOTGCTL, GAHBCFG, GUSBCFG, GRSTCTL, GINTSTS/GINTMSK, GRXSTSR/GRXSTSP, endpoint registers, FIFO), Ch. 7: RCC (AHB2ENR OTGFSEN), Ch. 8: GPIO (AF10 for USB on PA11/PA12)
- [NUCLEO-F446RE Documentation](https://www.st.com/en/evaluation-tools/nucleo-f446re.html) — USB OTG FS connector on NUCLEO-F446RE

### USB Specifications
- [USB 2.0 Specification](https://www.usb.org/document-library/usb-20-specification) — Device states (Default/Addressed/Configured), descriptor hierarchy, standard requests, NRZI encoding, bit stuffing, packet structure
- [USB CDC Class Specification (PSTN120)](https://www.usb.org/document-library/class-definitions-communication-devices-12) — CDC-ACM descriptors, class requests (SetLineCoding, GetLineCoding, SetControlLineState), Line Coding structure, two-interface model

### ARM Documentation
- [Cortex-M4 Technical Reference Manual](https://developer.arm.com/documentation/ddi0439/latest/) — USB OTG FS interrupt handling

### Tools & Emulation
- [QEMU ARM Documentation](https://www.qemu.org/docs/master/system/target-arm.html) — USB device emulation limitations for netduinoplus2
