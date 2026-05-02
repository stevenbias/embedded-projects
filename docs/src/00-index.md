---
title: "Embedded Mastery: C, Ada, Rust & Zig — A Project-Based Tutorial"
phase: 0
project: 0
---

# Embedded Mastery: C, Ada, Rust & Zig — A Project-Based Tutorial

> **Note:** This is a comprehensive, project-based course for experienced embedded developers who want to master four modern embedded languages by building the same 15 projects in each.

---

## What Is This Course?

This course teaches embedded systems development through **comparative implementation**. You will build **15 real projects** — each one implemented in **C, Ada, Rust, and Zig** — so you can directly compare how each language approaches memory safety, concurrency, hardware access, and real-time constraints.

Rather than learning each language in isolation, you'll see them side-by-side on the same problems. By the end, you'll understand not just *how* to write embedded code in each language, but *why* each language makes different trade-offs — and when to reach for which tool.

## Who Is This For?

This course is designed for:

- **Experienced embedded developers** who know one language (likely C) and want to expand their toolkit
- **Firmware engineers** evaluating languages for a new project or migration
- **Systems programmers** curious about embedded constraints and real-time guarantees
- **Technical leads** who need to make informed language decisions for embedded teams

### Prerequisites

| Requirement | Details |
|---|---|
| Embedded background | Understanding of registers, interrupts, memory-mapped I/O, and basic MCU architecture |
| Programming experience | Comfortable with at least one compiled language (C, C++, or similar) |
| Architecture knowledge | Familiarity with ARM Cortex-M (registers, NVIC, clock trees) or equivalent |
| Build systems | Basic understanding of Makefiles, linkers, and cross-compilation concepts |
| Debugging | Experience with GDB, JTAG/SWD, or similar debuggers |

> **Tip:** If you're new to embedded development entirely, consider completing a basic ARM Cortex-M tutorial first. This course assumes you know what a vector table is.

---

## The Philosophy: Learn by Comparing

Every project in this course follows the same pattern:

1. **Build in C first** — the baseline everyone understands
2. **Build in Rust** — see how ownership and zero-cost abstractions change your approach
3. **Build in Ada** — experience strong typing, SPARK contracts, and Ravenscar profile
4. **Build in Zig** — explore comptime, explicit allocators, and no hidden control flow

This structure reveals what each language does differently — and what stays the same. You'll develop intuition for when each language's strengths matter most.

---

## Project Roadmap

### Phase 1: Bare Metal Foundations (Projects 1-3)

| # | Project | Description | Est. Hours | Key Concepts |
|---|---|---|---|---|
| 1 | [LED Blinker](01-led-blinker.md) | Hello World of embedded — GPIO output, clock config, busy-wait delays | 12 | GPIO, clocks, linker scripts, startup code |
| 2 | [UART Echo Server](02-uart-echo.md) | Serial communication, printf-style output, interrupt-driven RX/TX | 20 | UART/USART, ring buffers, stdio retargeting |
| 3 | [Button Interrupts & Debouncing](03-button-interrupts.md) | External interrupts, debouncing, NVIC configuration | 16 | NVIC, EXTI, interrupt handlers, volatile |

**Phase 1 total: 48 hours**

### Phase 2: Peripherals & Communication (Projects 4-6)

| # | Project | Description | Est. Hours | Key Concepts |
|---|---|---|---|---|
| 4 | [I2C Temperature Sensor](04-i2c-sensor.md) | Read temperature/pressure from BMP280 over I2C | 22 | I2C protocol, peripheral drivers, error handling |
| 5 | [SPI Flash Memory](05-spi-flash.md) | Read/write/erase W25Q flash with CRC verification | 24 | SPI, DMA, flash commands, framebuffer |
| 6 | [PWM Motor Control](06-pwm-motor.md) | Timer-based PWM generation, duty cycle control, servo sweep | 18 | Timers, PWM, compare registers, prescalers |

**Phase 2 total: 64 hours**

### Phase 3: Architecture & Systems (Projects 7-9)

| # | Project | Description | Est. Hours | Key Concepts |
|---|---|---|---|---|
| 7 | [Cooperative Task Scheduler](07-cooperative-scheduler.md) | Build a minimal task scheduler from scratch | 28 | Task control blocks, context switching, stacks |
| 8 | [Lock-Free Ring Buffer](08-ring-buffer.md) | SPSC ring buffer with memory barriers | 24 | Lock-free data structures, atomics, memory ordering |
| 9 | [Custom Bootloader](09-bootloader.md) | Dual-bank firmware update with CRC validation and jump to application | 28 | Flash programming, memory regions, vector table relocation |

**Phase 3 total: 80 hours**

### Phase 4: Real-Time & Concurrency (Projects 10-12)

| # | Project | Description | Est. Hours | Key Concepts |
|---|---|---|---|---|
| 10 | [Minimal RTOS Kernel](10-rtos-kernel.md) | Build a minimal preemptive RTOS from scratch | 32 | Preemptive scheduling, mutexes, message queues |
| 11 | [CAN Bus Node](11-can-bus.md) | CAN 2.0B message transmit/receive with filtering | 24 | CAN protocol, message buffers, filters |
| 12 | [Multi-Sensor Data Logger](12-data-logger.md) | SD card logging with FAT32 and multi-sensor sync | 28 | SD card, FAT32, buffered I/O, RTC |

**Phase 4 total: 84 hours**

### Phase 5: Advanced & Expert (Projects 13-15)

| # | Project | Description | Est. Hours | Key Concepts |
|---|---|---|---|---|
| 13 | [USB CDC Device](13-usb-cdc.md) | USB device enumeration, CDC-ACM class | 28 | USB protocol, descriptors, endpoints |
| 14 | [Motor Control with PID](14-pid-motor.md) | PID control loop with encoder feedback | 26 | PID algorithms, encoder input, watchdog |
| 15 | [Safety-Critical System](15-safety-critical.md) | SPARK formal verification, MISRA compliance | 32 | Formal methods, Safety analysis |

**Phase 5 total: 86 hours**

### Grand Total: ~370 Hours (all languages)

> **Note:** The 290-hour estimate is per language. Total course time across all 4 languages is approximately 1,160 hours. Most learners spread this across 6-12 months.

---

## Language Comparison Overview

| Feature | C | Ada | Rust | Zig |
|---|---|---|---|---|
| **Memory Safety** | Manual — full control, full responsibility | Compile-time checks, SPARK formal verification | Ownership system, borrow checker | Explicit allocators, no hidden allocations |
| **Concurrency** | None built-in — rely on RTOS or bare-metal patterns | Ravenscar profile, protected objects, tasks | `Send`/`Sync`, async/await, fearless concurrency | Manual — single-threaded by default, explicit threading |
| **Compile-Time** | Preprocessor macros, limited constexpr | Generics, aspects, compile-time evaluation | `const` generics, macros, procedural macros | `comptime` — full language execution at compile time |
| **Error Handling** | Return codes, `errno`, setjmp/longjmp | Exceptions (configurable), result types | `Result<T, E>`, `?` operator, `unwrap()` | Error unions, `try`/`catch`, `unreachable` |
| **Hardware Access** | Direct pointer casting, volatile | Address clauses, import conventions | `volatile` pointers, PAC crates, MMIO structs | `extern` structs, `volatile` loads/stores |
| **Binary Size** | Smallest — minimal runtime | Larger — runtime checks, tasking support | Moderate — monomorphization, panic handling | Small — no hidden runtime, strip-friendly |
| **Build System** | Make, CMake, hand-rolled scripts | GPRBuild, Alire | Cargo (excellent) | `zig build` (built-in, excellent) |
| **Best For** | Legacy code, maximum control, smallest binaries | Safety-critical systems, formal verification, defense/aerospace | Modern embedded, memory safety guarantees, growing ecosystem | Systems programming, explicit control, clean C replacement |

---

## How to Use This Roadmap

### Recommended Workflow

1. **Start with Phase 1, Project 1** — do not skip ahead
2. **Implement in C first** — this establishes the baseline understanding
3. **Then implement in your choice of Rust, Ada, or Zig** — pick one, complete it, then move to the next
4. **Compare your implementations** — note what was easier, harder, or different
5. **Document your findings** — keep a journal of language-specific insights
6. **Move to the next project** only when all 4 implementations are complete

> **Tip:** If you're short on time, do C + one other language. Rust is recommended for modern embedded work; Ada for safety-critical; Zig for systems-level clarity.

### Suggested Weekly Schedule (10 Hours/Week)

| Day | Activity | Time |
|---|---|---|
| Monday | Study project requirements, read reference material | 1.5h |
| Tuesday | C implementation — setup, core logic | 2h |
| Wednesday | C implementation — finish, test, document | 1.5h |
| Thursday | Second language implementation | 2h |
| Friday | Third language implementation | 1.5h |
| Saturday | Fourth language + comparison notes | 1.5h |
| Sunday | Review, cleanup, prepare for next project | 1h |

At this pace, each project takes 1-2 weeks, and the full course spans approximately **12-15 months**.

---

## Emulator Overview

You do **not** need physical hardware for most of this course. We use emulation extensively:

| Emulator | Primary Use | Strengths |
|---|---|---|
| **QEMU** (`qemu-system-arm`) | Primary emulator for all projects | Excellent Cortex-M support, GDB integration, semihosting |
| **Renode** | I2C sensors (Project 5), CAN bus (Project 9) | Peripheral simulation, bus analyzers, multi-node networks |

> **Note:** Projects 5 (I2C) and 9 (CAN) benefit greatly from Renode's peripheral simulation. All other projects run fully in QEMU.

See [Emulator Setup & Usage Guide](00b-emulator-setup.md) for complete installation and configuration instructions.

---

## Quick Navigation

### Phase 1: Bare Metal Foundations
- [Project 1: LED Blinker](01-led-blinker.md)
- [Project 2: UART Echo Server](02-uart-echo.md)
- [Project 3: Button Interrupts & Debouncing](03-button-interrupts.md)

### Phase 2: Peripherals & Communication
- [Project 4: I2C Temperature Sensor](04-i2c-sensor.md)
- [Project 5: SPI Flash Memory](05-spi-flash.md)
- [Project 6: PWM Motor Control](06-pwm-motor.md)

### Phase 3: Architecture & Systems
- [Project 7: Cooperative Task Scheduler](07-cooperative-scheduler.md)
- [Project 8: Lock-Free Ring Buffer](08-ring-buffer.md)
- [Project 9: Custom Bootloader](09-bootloader.md)

### Phase 4: Real-Time & Concurrency
- [Project 10: Minimal RTOS Kernel](10-rtos-kernel.md)
- [Project 11: CAN Bus Node](11-can-bus.md)
- [Project 12: Multi-Sensor Data Logger](12-data-logger.md)

### Phase 5: Advanced & Expert
- [Project 13: USB CDC Device](13-usb-cdc.md)
- [Project 14: Motor Control with PID](14-pid-motor.md)
- [Project 15: Safety-Critical System](15-safety-critical.md)

### Setup & Reference
- [Prerequisites & Toolchain Setup](00a-prerequisites.md)
- [Emulator Setup & Usage Guide](00b-emulator-setup.md)
- [GDB Survival Guide](00c-gdb-survival-guide.md)

---

> **Ready to begin?** Start with [Prerequisites & Toolchain Setup](00a-prerequisites.md), then [Emulator Setup](00b-emulator-setup.md), review the [GDB Survival Guide](00c-gdb-survival-guide.md), and head to [Project 1: LED Blinker](01-led-blinker.md).

---

## References

### STMicroelectronics Documentation
- [STM32F4 Reference Manual (RM0090)](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-415-stm32f407-417-stm32f427-437-and-stm32f429-439-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) — Complete peripheral reference for STM32F4 family
- [STM32F405/407 Datasheet](https://www.st.com/resource/en/datasheet/stm32f405rg.pdf) — Pin assignments, memory sizes, electrical characteristics
- [NUCLEO-F446RE Documentation](https://www.st.com/en/evaluation-tools/nucleo-f446re.html) — Board schematics, user manual, ST-Link/V2-1 details

### ARM Documentation
- [Cortex-M4 Technical Reference Manual](https://developer.arm.com/documentation/ddi0439/latest/) — Processor architecture, FPU, NVIC, SysTick
- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/) — Exception model, memory ordering, instruction set
- [4SE03/4SE07 Course Site](https://4se03.telecom-paris.fr/) — ARM architecture, assembly, bare-metal TP, toolchain docs (French)

### Tools & Emulation
- [QEMU ARM Documentation](https://www.qemu.org/docs/master/system/target-arm.html) — qemu-system-arm usage, GDB stub, semihosting
- [QEMU STM32 Documentation](https://www.qemu.org/docs/master/system/arm/stm32.html) — netduinoplus2 machine, supported peripherals
- [Renode Documentation](https://docs.renode.io/) — Multi-node simulation, bus analyzers, peripheral models
