# Embedded Mastery Roadmap: C, Ada, Rust & Zig

## A Project-Based Guide from Beginner to Expert

---

## Table of Contents

1. [Prerequisites & Toolchain Setup](#1-prerequisites--toolchain-setup)
2. [Emulator Setup](#2-emulator-setup)
3. [Project Roadmap Overview](#3-project-roadmap-overview)
4. [Phase 1: Bare Metal Foundations](#phase-1-bare-metal-foundations)
5. [Phase 2: Peripherals & Communication](#phase-2-peripherals--communication)
6. [Phase 3: Architecture & Systems](#phase-3-architecture--systems)
7. [Phase 4: Real-Time & Concurrency](#phase-4-real-time--concurrency)
8. [Phase 5: Advanced & Expert](#phase-5-advanced--expert)
9. [Language-Specific Mastery Checklist](#language-specific-mastery-checklist)
10. [Reference & Resources](#reference--resources)

---

## 1. Prerequisites & Toolchain Setup

### What You Need Before Starting

- Strong embedded development background (you have this)
- Familiarity with at least one compiled language
- Basic understanding of computer architecture (registers, memory, interrupts)
- Linux development environment (recommended)

### Toolchain Installation

#### C Toolchain
```bash
# ARM cross-compiler
sudo apt install gcc-arm-none-eabi gdb-multiarch

# Verify
arm-none-eabi-gcc --version
```

#### Rust Toolchain
```bash
# Install rustup
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# Add embedded targets
rustup target add thumbv7m-none-eabi   # Cortex-M3
rustup target add thumbv7em-none-eabihf  # Cortex-M4F
rustup target add thumbv6m-none-eabi    # Cortex-M0+

# Install cargo tools
cargo install cargo-binutils cargo-generate cargo-embed
rustup component add llvm-tools-preview
```

#### Ada Toolchain
```bash
# GNAT for ARM (from AdaCore or package manager)
sudo apt install gnat-arm-elf gprbuild

# Or install via Alire (Ada package manager)
# https://alire.ada.dev/
curl -sL https://raw.githubusercontent.com/alire-project/alire/master/scripts/alire_bootstrap.sh | bash
```

#### Zig Toolchain
```bash
# Install Zig (check latest version)
# https://ziglang.org/download/
wget https://ziglang.org/download/0.13.0/zig-linux-x86_64-0.13.0.tar.xz
tar -xf zig-linux-x86_64-0.13.0.tar.xz
sudo mv zig-linux-x86_64-0.13.0 /opt/zig
sudo ln -s /opt/zig/zig /usr/local/bin/zig

# Verify
zig version
```

### Common Utilities
```bash
sudo apt install \
    qemu-system-arm \
    openocd \
    binutils-arm-none-eabi \
    picocom \
    make \
    cmake \
    python3
```

---

## 2. Emulator Setup

### QEMU (Primary Emulator)

QEMU will be our main emulation platform. It supports multiple ARM boards suitable for embedded learning.

#### Supported Boards for This Roadmap

| Board | QEMU Machine | CPU | Why This Board |
|---|---|---|---|
| Netduino Plus 2 | `netduinoplus2` | STM32F2 (Cortex-M3) | GPIO, UART, timers — perfect for early projects |
| STM32F103 (Blue Pill) | `stm32f103` (custom) | Cortex-M3 | Most popular dev board, extensive examples |
| Raspberry Pi 2 | `raspi2` | Cortex-A7 | More powerful, SD card, good for later projects |
| Virt (ARM) | `virt` | Configurable | Generic platform, no hardware quirks |

#### QEMU Quick Reference

```bash
# Run firmware (ELF or binary)
qemu-system-arm -M netduinoplus2 -kernel firmware.elf -nographic

# Run with semihosting (for printf/debug output)
qemu-system-arm -M netduinoplus2 -kernel firmware.elf -semihosting -nographic

# GDB debugging mode (pauses at start, waits for GDB on port 1234)
qemu-system-arm -M netduinoplus2 -kernel firmware.elf -S -s

# Connect GDB
arm-none-eabi-gdb firmware.elf -ex "target remote :1234"

# Useful GDB commands
(gdb) break main           # Break at main
(gdb) continue             # Run
(gdb) step                 # Step into
(gdb) next                 # Step over
(gdb) info registers       # Show all registers
(gdb) x/16x 0x40011000     # Examine memory (16 words hex)
(gdb) monitor int          # QEMU: log interrupts
```

#### QEMU GPIO/Peripheral Interaction

```bash
# In QEMU monitor (Ctrl+A, C to enter monitor)
(qemu) qom-list /machine/netduino/
(qemu) qom-get /machine/netduino/stm32f2xx-gpio-a/property
```

### Renode (Secondary — for I2C, CAN, Multi-Node)

Install from: https://renode.io/

```bash
# Start Renode
renode

# Load a platform
(monitor) mach create
(monitor) machine LoadPlatformDescription @platforms/boards/stm32f4_discovery-kit.repl
(monitor) sysbus LoadELF @firmware.elf
(monitor) start

# Show peripherals
(monitor) peripherals

# Add I2C sensor (BMP280)
(monitor) i2c AddPeripheral bmp280 i2c0 0x76
```

---

## 3. Project Roadmap Overview

Each project is designed to be implemented in **all four languages**. You'll build the same project in C, Ada, Rust, and Zig to compare approaches and deepen understanding.

### Difficulty Progression

```
Phase 1: Bare Metal Foundations
├── Project 1: LED Blinker (Direct Register Access)
├── Project 2: UART Echo Server
└── Project 3: Button Interrupts + Debouncing

Phase 2: Peripherals & Communication
├── Project 4: I2C Temperature Sensor Driver (BMP280)
├── Project 5: SPI Flash Reader/Writer
└── Project 6: PWM Motor Controller

Phase 3: Architecture & Systems
├── Project 7: Cooperative Task Scheduler
├── Project 8: Ring Buffer Library (Lock-Free)
└── Project 9: Custom Bootloader

Phase 4: Real-Time & Concurrency
├── Project 10: RTOS Kernel (Minimal)
├── Project 11: CAN Bus Node
└── Project 12: Multi-Sensor Data Logger

Phase 5: Advanced & Expert
├── Project 13: USB CDC Device
├── Project 14: Motor Control with PID + Fault Detection
└── Project 15: Safety-Critical System (SPARK Ada / Rust Unsafe Audit)
```

### Estimated Time Per Project

| Phase | Project | C | Rust | Ada | Zig | Total |
|---|---|---|---|---|---|---|
| 1 | LED Blinker | 1h | 2h | 3h | 2h | 8h |
| 1 | UART Echo | 2h | 3h | 4h | 3h | 12h |
| 1 | Button Interrupts | 2h | 3h | 4h | 3h | 12h |
| 2 | I2C Sensor | 3h | 4h | 5h | 4h | 16h |
| 2 | SPI Flash | 3h | 4h | 5h | 4h | 16h |
| 2 | PWM Controller | 2h | 3h | 4h | 3h | 12h |
| 3 | Task Scheduler | 4h | 5h | 6h | 5h | 20h |
| 3 | Ring Buffer | 2h | 3h | 4h | 3h | 12h |
| 3 | Bootloader | 4h | 5h | 6h | 5h | 20h |
| 4 | RTOS Kernel | 8h | 10h | 12h | 10h | 40h |
| 4 | CAN Bus | 4h | 5h | 6h | 5h | 20h |
| 4 | Data Logger | 4h | 5h | 6h | 5h | 20h |
| 5 | USB CDC | 6h | 8h | 10h | 8h | 32h |
| 5 | PID Motor | 4h | 5h | 6h | 5h | 20h |
| 5 | Safety-Critical | 6h | 8h | 12h | 6h | 32h |

**Total estimated time: ~290 hours** (~7 weeks at 10h/week)

---

## Phase 1: Bare Metal Foundations

### Goals
- Understand the boot process from reset to main()
- Learn direct register manipulation in each language
- Master interrupt handling
- Build debugging skills with QEMU + GDB

---

### Project 1: LED Blinker

**Target**: QEMU `netduinoplus2` (STM32F205, Cortex-M3)

**What You'll Learn**:
- Startup code and vector tables
- Linker scripts and memory layout
- Memory-mapped I/O and `volatile` semantics
- Clock configuration basics
- Build systems for each language

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| Register access | `volatile uint32_t *` | `#[repr(C)]` struct + `addr_of!` | `with Volatile` aspect | `@volatileLoad` / `@volatileStore` |
| Startup | `startup.s` + `Reset_Handler` | `cortex-m-rt` entry point | Runtime initialization | `export` + entry section |
| Linker script | `.ld` file | `.x` file (similar) | Linker script or GPR | `.ld` file |
| Build | `Makefile` + `arm-none-eabi-gcc` | `cargo build --target` | `gprbuild` | `zig build` |

#### Key Registers (STM32F2)
```
RCC_AHB1ENR    = 0x40023830   // Enable GPIO clocks
GPIOA_MODER    = 0x40020000   // GPIO mode register
GPIOA_ODR      = 0x40020014   // GPIO output data register
```

#### Deliverables
- [ ] C: Bare-metal blink with custom startup + linker script
- [ ] Rust: Blink using `cortex-m-rt` and `cortex-m` crates
- [ ] Ada: Blink using Ravenscar runtime profile
- [ ] Zig: Blink with comptime register definitions
- [ ] All: Verify LED toggle in QEMU via GDB register inspection

#### Verification
```bash
# Run in QEMU with GDB
qemu-system-arm -M netduinoplus2 -kernel firmware.elf -S -s

# In GDB, watch the ODR register toggle
(gdb) display *(volatile uint32_t*)0x40020014
(gdb) continue
# Watch the value change as LED blinks
```

---

### Project 2: UART Echo Server

**Target**: QEMU `netduinoplus2` or `stm32f103`

**What You'll Learn**:
- UART peripheral configuration (baud rate, parity, stop bits)
- Polling vs interrupt-driven I/O
- String/byte handling in each language
- Semihosting for debug output

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| UART config | Register writes | PAC crate methods | Device driver package | Register struct + methods |
| I/O trait | None | `embedded_hal::serial` | Stream I/O | Custom interface |
| String handling | `char[]`, `strlen` | `&str`, byte slices | `String`, `Ada.Strings` | `[]u8`, slices |
| Error handling | Return codes | `Result<T, E>` | Exceptions | Error unions |

#### Deliverables
- [ ] C: Polling UART echo with baud rate calculation
- [ ] Rust: Interrupt-driven UART using `cortex-m` + PAC
- [ ] Ada: UART package with send/receive procedures
- [ ] Zig: Async-style UART with error unions
- [ ] All: Echo "Hello Embedded World!" then echo all received characters

#### Verification
```bash
# QEMU connects UART to terminal with -nographic
qemu-system-arm -M netduinoplus2 -kernel firmware.elf -nographic
# Type characters, verify they echo back

# Or redirect UART to a pseudo-terminal
qemu-system-arm -M netduinoplus2 -kernel firmware.elf \
    -serial pty -nographic
# QEMU will print: char device redirected to /dev/pts/X
picocom /dev/pts/X
```

---

### Project 3: Button Interrupts + Debouncing

**Target**: QEMU `netduinoplus2`

**What You'll Learn**:
- EXTI (External Interrupt) configuration
- NVIC interrupt priorities
- Software debouncing algorithms
- Atomic operations and critical sections
- Volatile and memory ordering

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| ISR | `__attribute__((interrupt))` | `#[interrupt]` handler | Protected object entry | Exported handler |
| Atomic flag | `volatile sig_atomic_t` | `AtomicBool` | Atomic protected object | `std.atomic.Value` |
| Critical section | `__disable_irq()` | `cortex_m::interrupt::free` | `pragma Priority` | Inline asm |
| Debounce | Counter/timer | State machine | Timed entry call | Timer interrupt |

#### Deliverables
- [ ] C: EXTI interrupt with software debounce counter
- [ ] Rust: Interrupt handler with `AtomicBool` flag
- [ ] Ada: Protected object with debounced entry
- [ ] Zig: Interrupt with atomic state machine
- [ ] All: Button press toggles LED, verified in QEMU

#### Verification
```bash
# In QEMU monitor, simulate button press
# (Netduino Plus 2: user button mapped to a GPIO pin)
(qemu) qom-set /machine/netduino/stm32f2xx-gpio-a/pdin 0x01
# Verify ISR fires and LED toggles
```

---

## Phase 2: Peripherals & Communication

### Goals
- Master bus protocols (I2C, SPI)
- Implement device drivers from datasheets
- Handle timing-critical operations
- Build reusable hardware abstraction layers

---

### Project 4: I2C Temperature Sensor Driver (BMP280)

**Target**: Renode (has BMP280 model) or QEMU with stubbed I2C

**What You'll Learn**:
- I2C protocol (start/stop conditions, ACK, addressing)
- Reading datasheets and register maps
- Fixed-point arithmetic for sensor conversion
- Error handling and timeout management

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| I2C HAL | Direct registers | `embedded-hal` I2C trait | Generic I2C package | Interface + implementation |
| Calibration | Struct + manual math | `#[derive(Copy, Clone)]` | Record type with operations | Packed struct |
| Conversion | Integer math | `fixed` crate | Fixed-point types | Custom fixed-point |
| Error handling | Error codes + errno | `Result<T, SensorError>` | Exception handling | Error unions |

#### Deliverables
- [ ] C: I2C bit-bang or peripheral driver + BMP280 read
- [ ] Rust: Driver using `embedded-hal` I2C trait
- [ ] Ada: Generic sensor driver package
- [ ] Zig: Comptime-validated register map
- [ ] All: Read temperature, convert to human-readable format, output via UART

#### Verification (Renode)
```
renode
(monitor) mach create
(monitor) machine LoadPlatformDescription @platforms/boards/stm32f4_discovery-kit.repl
(monitor) i2c AddPeripheral bmp280 i2c1 0x76
(monitor) sysbus LoadELF @firmware.elf
(monitor) start
(monitor) analyzerFrameShow I2C1 true
# Watch I2C transactions in the log
```

---

### Project 5: SPI Flash Reader/Writer

**Target**: QEMU `netduinoplus2` or Renode

**What You'll Learn**:
- SPI protocol (CPOL, CPHA, chip select)
- Flash memory commands (read, write, erase, JEDEC ID)
- Page programming and sector erase constraints
- Memory-mapped vs command-mode flash access

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| SPI config | Register setup | `embedded-hal` SPI trait | SPI driver package | Comptime config |
| Flash commands | Command byte arrays | Command enum + serialize | Command record + encoding | Packed structs |
| Timing | Busy-wait polling | `nb` crate (non-blocking) | Delay package | Async/timer |
| Data integrity | CRC32 | `crc` crate | CRC package | `std.crypto.hash` |

#### Deliverables
- [ ] C: SPI driver + flash read/write/erase functions
- [ ] Rust: Generic SPI flash driver with trait bounds
- [ ] Ada: Flash memory management package
- [ ] Zig: Zero-copy SPI transactions
- [ ] All: Read JEDEC ID, write data, read back, verify with CRC

---

### Project 6: PWM Motor Controller

**Target**: QEMU `netduinoplus2` (timer/PWM emulation)

**What You'll Learn**:
- Timer configuration (prescaler, auto-reload, compare)
- PWM signal generation (frequency, duty cycle)
- Dead-time insertion for H-bridge control
- Soft-start ramp algorithms

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| Timer setup | Register configuration | PAC timer methods | Timer driver package | Comptime calculations |
| PWM output | CCR register writes | PwmPin trait | Modulated output | Hardware abstraction |
| Ramp | Incremental loop | Iterator-based ramp | Timed state transitions | State machine |
| Dead-time | Careful register timing | Safe wrapper types | Protected timing logic | Inline asm if needed |

#### Deliverables
- [ ] C: Timer-based PWM with configurable frequency/duty
- [ ] Rust: Safe PWM abstraction with type-level guarantees
- [ ] Ada: Motor control package with range-checked parameters
- [ ] Zig: Comptime-validated PWM configuration
- [ ] All: Generate 1kHz PWM, ramp duty 0-100%, output duty via UART

#### Verification
```bash
# In GDB, watch timer compare register
(gdb) display *(volatile uint32_t*)0x40000034  # TIMx_CCR1
(gdb) continue
# Verify value changes smoothly during ramp
```

---

## Phase 3: Architecture & Systems

### Goals
- Design reusable software architectures
- Implement concurrent execution models
- Manage non-volatile memory
- Understand boot processes and firmware updates

---

### Project 7: Cooperative Task Scheduler

**Target**: Any QEMU ARM board

**What You'll Learn**:
- Task control blocks (TCBs)
- Context switching (stack save/restore)
- Priority-based scheduling
- Yield and sleep mechanisms

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| TCB | Struct with stack pointer | Struct with lifetime bounds | Task type | Struct with arena alloc |
| Context switch | Inline asm | `cortex-m` asm + naked fn | Runtime handles it | Inline asm |
| Scheduler | Linked list of TCBs | `LinkedList` or array | Protected scheduler | Allocator-based list |
| Yield | Function call | `yield()` via WFI | `delay until` | `yield` builtin |

#### Deliverables
- [ ] C: Array-based scheduler with round-robin + priorities
- [ ] Rust: Generic scheduler with const-generic task count
- [ ] Ada: Ravenscar-compliant tasking model
- [ ] Zig: Allocator-aware scheduler with compile-time task config
- [ ] All: 3+ tasks with different priorities, each toggling LED at different rates

---

### Project 8: Ring Buffer Library (Lock-Free)

**Target**: Host + cross-compile to ARM

**What You'll Learn**:
- Lock-free data structures
- Memory ordering and barriers
- Producer/consumer patterns
- Generic programming

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| Generic | `void*` + macros | Generics + trait bounds | Generic packages | `comptime T: type` |
| Atomic head/tail | `volatile` + barriers | `AtomicUsize` + `Ordering` | Atomic protected object | `std.atomic.Value` |
| Memory barrier | `__DMB()` intrinsic | `compiler_fence` | Implicit with atomics | `fence` instruction |
| Bounds checking | Manual assertions | `debug_assert!` | Runtime checks | Safety checks |

#### Deliverables
- [ ] C: Lock-free SPSC ring buffer with memory barriers
- [ ] Rust: Generic ring buffer with `AtomicUsize` and proper ordering
- [ ] Ada: Generic bounded buffer with protected entries
- [ ] Zig: Comptime-sized ring buffer with atomic operations
- [ ] All: Unit tests on host, integration test with UART interrupt (producer) + main loop (consumer)

---

### Project 9: Custom Bootloader

**Target**: QEMU (dual-bank flash simulation)

**What You'll Learn**:
- Memory layout for bootloader + application
- CRC/firmware validation
- Jumping from bootloader to application
- Firmware update mechanism
- Vector table relocation

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| Vector table | Assembly + linker | `cortex-m-rt` vectors | Runtime configuration | Custom section |
| Flash write | FPEC registers | PAC flash methods | Flash driver package | Register writes |
| CRC | Software CRC32 | `crc` crate | CRC package | `std.crypto.hash.Crc32` |
| Jump | Function pointer cast | Naked function + asm | System start procedure | Inline asm jump |

#### Deliverables
- [ ] C: Bootloader with UART firmware update + CRC check
- [ ] Rust: Bootloader with safe flash abstraction
- [ ] Ada: Bootloader with SPARK-verified CRC
- [ ] Zig: Bootloader with comptime memory layout validation
- [ ] All: Bootloader validates app, jumps to it; app blinks LED to confirm

#### Verification
```bash
# Load bootloader
qemu-system-arm -M netduinoplus2 -kernel bootloader.elf -S -s

# In GDB, set breakpoint at app entry
(gdb) break *0x08004000
(gdb) continue
# Verify bootloader jumps to application
```

---

## Phase 4: Real-Time & Concurrency

### Goals
- Build real-time scheduling systems
- Implement inter-task communication
- Handle automotive/industrial protocols
- Manage complex peripheral systems

---

### Project 10: RTOS Kernel (Minimal)

**Target**: QEMU `netduinoplus2`

**What You'll Learn**:
- Preemptive scheduling
- Mutexes and semaphores
- Message queues
- Priority inversion handling
- Tick timer and time management

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| Preemption | PendSV context switch | PendSV + `#[naked]` | Built-in tasking | PendSV handler |
| Mutex | Priority inheritance | `Mutex<T>` with priority ceiling | Protected object | Custom with priority |
| Message queue | Ring buffer + semaphore | `mpsc` channel | Protected entry with queue | Lock-free queue |
| Tick | SysTick interrupt | `cortex-m-rtic` or custom | `delay until` | SysTick handler |

#### Deliverables
- [ ] C: Preemptive RTOS with mutex, semaphore, message queue
- [ ] Rust: RTOS with type-safe message passing
- [ ] Ada: Full tasking system using Ravenscar profile
- [ ] Zig: Minimal RTOS with comptime configuration
- [ ] All: 4+ tasks communicating via queues, demonstrating priority inheritance

---

### Project 11: CAN Bus Node

**Target**: Renode (CAN bus support)

**What You'll Learn**:
- CAN protocol (frames, IDs, arbitration)
- CAN filter configuration
- OBD-II PID requests
- Deterministic message handling

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| CAN driver | bxCAN registers | `bxcan` crate | CAN driver package | Register abstraction |
| Frame parsing | Bit manipulation | `bitfield` crate | Record representation | Packed structs |
| Filtering | Hardware filters | Type-level IDs | Constrained types | Comptime validation |
| OBD-II | Protocol state machine | State machine enum | Protocol package | State enum |

#### Deliverables
- [ ] C: CAN driver + OBD-II PID reader (RPM, speed, coolant temp)
- [ ] Rust: Type-safe CAN frame parser
- [ ] Ada: CAN communication package with strong typing
- [ ] Zig: Zero-copy CAN frame parsing
- [ ] All: Two nodes communicating over CAN bus in Renode

---

### Project 12: Multi-Sensor Data Logger

**Target**: QEMU `raspi2` (SD card support)

**What You'll Learn**:
- SD card protocol (SPI mode)
- FAT32 filesystem basics
- Buffered I/O for performance
- Real-time clock integration
- Multi-sensor synchronization

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| SD driver | SPI + CMD protocol | `sdio` or custom | SD card package | SPI-based driver |
| FAT32 | Minimal FAT implementation | `embedded-sdmmc` | FAT package | Custom or port |
| Buffering | Circular buffer | `BufWriter` pattern | Buffered stream | Ring buffer |
| Timestamp | RTC register | RTC abstraction | Time package | RTC driver |

#### Deliverables
- [ ] C: SD card driver + FAT32 writer + multi-sensor logging
- [ ] Rust: Data logger with `embedded-sdmmc`
- [ ] Ada: Sensor logging system with strong type safety
- [ ] Zig: Efficient buffered logger with comptime file format
- [ ] All: Log temperature, pressure, timestamp to CSV on SD card

---

## Phase 5: Advanced & Expert

### Goals
- Implement complex protocols from scratch
- Design safety-critical systems
- Apply formal methods and verification
- Optimize for performance and reliability

---

### Project 13: USB CDC Device

**Target**: QEMU (limited USB device support) + real hardware recommended

**What You'll Learn**:
- USB protocol (descriptors, enumeration, endpoints)
- USB device state machine
- Bulk transfers and endpoint management
- CDC-ACM class implementation

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| USB driver | USB peripheral registers | `usb-device` crate | USB driver package | Register abstraction |
| Descriptors | Byte arrays | `descriptor` macros | Descriptor records | Packed structs |
| Endpoints | Buffer management | `endpoint` traits | Endpoint packages | DMA buffers |
| CDC-ACM | Class-specific requests | `usbd-serial` | CDC class package | Protocol state machine |

#### Deliverables
- [ ] C: USB device stack + CDC-ACM class
- [ ] Rust: USB CDC using `usb-device` + `usbd-serial`
- [ ] Ada: USB device implementation with strong typing
- [ ] Zig: USB stack with comptime descriptor generation
- [ ] All: Enumerate as serial port, echo characters (verify logic via UART if QEMU USB limited)

---

### Project 14: Motor Control with PID + Fault Detection

**Target**: QEMU (logic verification) + real hardware for full testing

**What You'll Learn**:
- PID control algorithms (P, I, D terms)
- Anti-windup strategies
- Fault detection and safe state
- Watchdog timer usage
- Current sensing simulation

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| PID | Fixed-point struct | `pid` crate or custom | PID package with ranges | Comptime-tuned params |
| Anti-windup | Clamping + back-calculation | Saturated arithmetic | Bounded types | Saturation helpers |
| Fault detection | Threshold checks | `Result`-based monitoring | Exception-based faults | Error unions |
| Watchdog | IWDG registers | Watchdog abstraction | Watchdog package | Hardware timer |

#### Deliverables
- [ ] C: PID controller + fault detection + watchdog
- [ ] Rust: Type-safe PID with compile-time tuning validation
- [ ] Ada: SPARK-verified PID with provable bounds
- [ ] Zig: Comptime PID parameter optimization
- [ ] All: PID loop controlling simulated motor, fault detection triggers safe state

---

### Project 15: Safety-Critical System

**Target**: QEMU + static analysis tools

**What You'll Learn**:
- MISRA C compliance
- SPARK Ada formal verification
- Rust unsafe code auditing
- Defensive programming patterns
- Fault tree analysis basics

#### Concepts by Language

| Concept | C | Rust | Ada | Zig |
|---|---|---|---|---|
| Static analysis | PC-Lint, Cppcheck | Clippy, Miri | GNATprove (SPARK) | `zig test`, audit |
| MISRA | MISRA C:2012 rules | N/A (idiomatic Rust) | N/A (Ada is safer) | N/A |
| Formal proof | None (manual review) | `proptest`, Kani | SPARK contracts + proof | Manual verification |
| Unsafe audit | All code is unsafe | Audit `unsafe` blocks | N/A (no unsafe) | Audit `unsafe` |

#### Deliverables
- [ ] C: Motor controller with MISRA C compliance report
- [ ] Rust: Motor controller with zero `unsafe` (or audited unsafe)
- [ ] Ada: Motor controller with SPARK proof of absence of runtime errors
- [ ] Zig: Motor controller with comprehensive compile-time checks
- [ ] All: Same functionality, verified through each language's safety mechanisms

---

## Language-Specific Mastery Checklist

### C Mastery Checklist

- [ ] Write custom startup code and linker script
- [ ] Implement volatile-correct register definitions
- [ ] Use function pointers for HAL design
- [ ] Implement lock-free data structures with memory barriers
- [ ] Write MISRA C compliant code
- [ ] Use `static_assert` for compile-time checks
- [ ] Implement context switching in assembly
- [ ] Use GDB for hardware debugging (watchpoints, trace)
- [ ] Profile and optimize with `perf` or cycle counting
- [ ] Write unit tests with CMocka or Unity

### Rust Mastery Checklist

- [ ] Use `cortex-m-rt` for entry points and exceptions
- [ ] Implement drivers using `embedded-hal` traits
- [ ] Use `unsafe` correctly and audit unsafe blocks
- [ ] Implement zero-cost abstractions with generics
- [ ] Use `const fn` for compile-time hardware validation
- [ ] Implement `Send`/`Sync` correctly for peripherals
- [ ] Use `cortex-m-rtic` for resource-safe concurrency
- [ ] Write `no_std` libraries with proper feature gates
- [ ] Use `defmt` for efficient debug logging
- [ ] Implement custom allocators for heap in embedded

### Ada Mastery Checklist

- [ ] Use Ravenscar profile for real-time systems
- [ ] Implement protected objects for concurrency
- [ ] Use generic packages for reusable drivers
- [ ] Apply SPARK contracts (`Pre`, `Post`, `Invariant`)
- [ ] Prove absence of runtime errors with GNATprove
- [ ] Use representation clauses for hardware mapping
- [ ] Implement task types and entry calls
- [ ] Use discriminant records for state machines
- [ ] Apply pragma `Volatile` and `Atomic` correctly
- [ ] Write GPR project files for multi-library builds

### Zig Mastery Checklist

- [ ] Use `comptime` for compile-time hardware validation
- [ ] Implement error unions for driver error handling
- [ ] Use `packed struct` for register definitions
- [ ] Implement custom allocators for embedded
- [ ] Use `@volatileLoad`/`@volatileStore` for MMIO
- [ ] Write `build.zig` for cross-compilation
- [ ] Implement async patterns (or understand Zig's async evolution)
- [ ] Use inline assembly for context switching
- [ ] Leverage C interop for existing libraries
- [ ] Write comprehensive compile-time tests

---

## Reference & Resources

### Documentation

| Resource | URL |
|---|---|
| STM32F2 Reference Manual | https://www.st.com/resource/en/reference_manual/dm00031020.pdf |
| BMP280 Datasheet | https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf |
| ARM Cortex-M3 TRM | https://developer.arm.com/documentation/ddi0337/latest/ |
| QEMU ARM Docs | https://www.qemu.org/docs/master/system/target-arm.html |
| Renode Documentation | https://docs.renode.io/ |

### Language Resources

| Language | Resource | URL |
|---|---|---|
| C | "Making Embedded Systems" by Elecia White | Book |
| C | "Test Driven Development for Embedded C" by James Grenning | Book |
| Rust | "Programming Embedded Systems in Rust" (online) | https://docs.rust-embedded.org/ |
| Rust | `cortex-m` crate docs | https://docs.rs/cortex-m/ |
| Rust | `embedded-hal` | https://docs.rs/embedded-hal/ |
| Ada | "Ada for Embedded Systems" | Book |
| Ada | SPARK User's Guide | https://docs.adacore.com/spark2014-docs/html/ug/ |
| Ada | Ravenscar Profile Reference | https://docs.adacore.com/live/wave/arm-ada/html/ravenscar.html |
| Zig | Zig Language Reference | https://ziglang.org/documentation/master/ |
| Zig | `zig-cortex-m` | https://github.com/ZigEmbeddedGroup/zig-cortex-m |

### Emulator Resources

| Resource | URL |
|---|---|
| QEMU ARM Emulation | https://wiki.qemu.org/Documentation/Platforms/ARM |
| Renode Platform Descriptions | https://github.com/renode/renode |
| Renode Tutorials | https://docs.renode.io/en/latest/tutorials/index.html |

### Communities

| Community | URL |
|---|---|
| Rust Embedded | https://matrix.to/#/#rust-embedded:matrix.org |
| Zig Embedded Group | https://zigembedded.org/ |
| AdaCore Community | https://www.adacore.com/community |
| r/embedded | https://reddit.com/r/embedded |

---

## How to Use This Roadmap

1. **Start with Phase 1, Project 1** — implement in C first (fastest), then Rust, Ada, Zig
2. **Don't skip projects** — each builds on concepts from the previous
3. **Compare implementations** — after completing a project in all 4 languages, write a short comparison of what was easier/harder in each
4. **Use version control** — create a repo per project with 4 branches (one per language)
5. **Document your learnings** — keep a dev log noting gotchas, patterns, and insights per language
6. **Run on real hardware when possible** — emulation is great for learning, but real hardware reveals timing and electrical issues

### Suggested Weekly Schedule (10 hours/week)

| Day | Activity | Time |
|---|---|---|
| Mon | Study concepts + read datasheets | 1h |
| Tue | C implementation | 2h |
| Wed | Rust implementation | 2h |
| Thu | Ada implementation | 2h |
| Fri | Zig implementation | 2h |
| Sat | Review, compare, document | 1h |

---

*This roadmap is a living document. Update it as you progress, add notes, and adjust timelines based on your pace.*
