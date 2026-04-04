---
title: "Emulator Setup & Usage Guide"
phase: 0
project: 0
---

# Emulator Setup & Usage Guide

This course uses emulation for the majority of development. You do **not** need physical hardware to complete any project.

---

## Why Emulation for Embedded Learning?

| Benefit | Description |
|---|---|
| **Reproducibility** | Every learner gets identical behavior — no board revisions, no silicon errata |
| **No Hardware Required** | Start immediately without waiting for deliveries or soldering |
| **Debuggability** | Full system introspection: watch any memory address, reverse execution, deterministic replay |
| **Cost** | Free — no development boards, debuggers, or logic analyzers needed |
| **CI/CD** | Run embedded tests in CI pipelines with no physical infrastructure |
| **Safety** | Brick your emulated MCU a hundred times — no risk to real hardware |

> **Note:** Emulation is not perfect. Timing-sensitive code (exact microsecond delays) and some peripheral interactions may differ from real silicon. We note workarounds where applicable.

---

## QEMU Setup

QEMU is the primary emulator for this course. It supports ARM Cortex-M3/M4/M0+ with reasonable peripheral simulation.

### Supported Boards

| Board | QEMU Machine | CPU | Why This Board |
|---|---|---|---|
| **Netduino Plus 2** | `netduinoplus2` | STM32F405 (Cortex-M4F) | FPU support, good peripheral set, used in advanced projects |
| **STM32F103 (Blue Pill)** | `stm32f103` (via `virt` or custom) | STM32F103 (Cortex-M3) | Most common hobbyist board, matches our primary target |
| **Raspberry Pi 2** | `raspi2` | BCM2836 (Cortex-A7) | Application processor projects, Linux-capable |
| **Virt** | `virt` | Configurable (Cortex-M3/M4) | Generic machine, flexible for custom setups |

> **Tip:** For this course, we primarily use `netduinoplus2` (Cortex-M4F) and the `virt` machine configured for Cortex-M3. The Blue Pill (`stm32f103`) has limited QEMU support — we use workarounds noted per project.

---

## QEMU Quick Reference

### Running Firmware (ELF)

```bash
# Run an ELF file on Netduino Plus 2
qemu-system-arm -M netduinoplus2 -kernel firmware.elf

# Run on virt machine with Cortex-M3
qemu-system-arm -M virt -cpu cortex-m3 -kernel firmware.elf

# Run with serial output to terminal
qemu-system-arm -M netduinoplus2 -kernel firmware.elf -nographic
```

### Semihosting Mode

Semihosting allows the emulated MCU to perform I/O through the host (print to terminal, read files, exit).

```bash
# Run with semihosting enabled
qemu-system-arm -M netduinoplus2 -kernel firmware.elf -semihosting

# Semihosting with config file (for file I/O)
qemu-system-arm -M netduinoplus2 -kernel firmware.elf \
    -semihosting-config enable=on,target=native
```

> **Note:** Semihosting is slow. Use it for debugging and development, not for performance testing.

### GDB Debugging Mode

```bash
# Start QEMU paused, waiting for GDB connection (-S = freeze, -s = gdb server on :1234)
qemu-system-arm -M netduinoplus2 -kernel firmware.elf -S -s

# In another terminal, connect with GDB
gdb-multiarch firmware.elf
(gdb) target remote :1234
(gdb) continue
```

### GDB Connection and Useful Commands

```bash
gdb-multiarch firmware.elf
(gdb) target remote :1234

# Breakpoints
(gdb) break main
(gdb) break *0x08000100
(gdb) break HardFault_Handler

# Execution control
(gdb) continue          # Run until breakpoint
(gdb) step              # Step into function
(gdb) next              # Step over function
(gdb) finish            # Run until current function returns

# Register inspection
(gdb) info registers    # Show all registers
(gdb) info registers r0 r1 r2 r3
(gdb) print $pc         # Program counter
(gdb) print $sp         # Stack pointer
(gdb) print $xpsr       # Program status register

# Memory examination
(gdb) x/16x 0x20000000          # 16 words as hex from RAM start
(gdb) x/32b 0x40010800          # 32 bytes from GPIOA base
(gdb) x/i $pc                   # Instruction at current PC
(gdb) x/10i $pc                 # Next 10 instructions

# Watchpoints (break on memory access)
(gdb) watch *(volatile uint32_t*)0x4001080C
(gdb) rwatch *(volatile uint32_t*)0x4001080C   # Read watchpoint
(gdb) awatch *(volatile uint32_t*)0x4001080C   # Access watchpoint

# Backtrace and frames
(gdb) bt                      # Backtrace
(gdb) frame 2                 # Switch to frame 2
(gdb) info locals             # Local variables in current frame

# Quit
(gdb) quit
```

### QEMU Monitor Access

The QEMU monitor provides runtime control and introspection.

```bash
# Access monitor: press Ctrl+A, then C
# In monitor:

(qemu) info registers         # Show CPU registers
(qemu) info mem               # Show memory mappings
(qemu) info cpus              # Show CPU state
(qemu) info history           # Command history
(qemu) stop                   # Pause emulation
(qemu) cont                   # Resume emulation
(qemu) system_reset           # Reset the MCU
(qemu) q                      # Quit QEMU

# Return to console: press Ctrl+A, then C again
```

> **Tip:** Use `Ctrl+A, H` for a list of all QEMU monitor commands.

### GPIO/Peripheral Interaction via QOM

```bash
# List all QOM objects (devices)
(qemu) info qom-tree

# Get properties of a specific device
(qemu) qom-get /machine/unattached/device[0]/nvic property_name

# Set a GPIO pin state (simulated)
(qemu) qom-set /machine/unattached/device[0]/gpio[0] value 1

# List device properties
(qemu) qom-list /machine/unattached/device[0]
```

---

## Renode Setup

Renode is a more feature-rich emulator with excellent peripheral simulation, bus analyzers, and multi-node support. It is essential for **Project 5 (I2C Sensor)** and **Project 9 (CAN Bus)**.

### Installation

```bash
# Add Renode repository and install
sudo apt update
sudo apt install -y policykit-1 libgtk2.0-0 libpixman-1-0 python3-dev

# Download and install Renode
wget https://github.com/renode/renode/releases/download/v1.14.0/renode_1.14.0_amd64.deb
sudo dpkg -i renode_1.14.0_amd64.deb

# Verify
renode --version
# Expected: 1.14.0 or newer
```

> **Note:** For the latest version, visit https://renode.io and follow the installation guide for your platform.

### Starting Renode

```bash
# Launch Renode GUI
renode

# Or launch in terminal mode
renode --disable-xwt
```

### Loading a Platform and ELF

```
# In Renode console:

# Load a platform (predefined board configuration)
(machine-0) mach create
(machine-0) machine LoadPlatformDescription @platforms/cpus/stm32f4.resc

# Or load a specific board
(machine-0) include @platforms/boards/netduino_plus_2.resc

# Load firmware
(machine-0) sysbus LoadELF @firmware.elf

# Start emulation
(machine-0) start

# Stop emulation
(machine-0) pause
```

### Adding I2C Sensors (BMP280 Example)

```
# Create I2C bus and attach BMP280 sensor
(machine-0) i2c CreateI2cBus
(machine-0) i2c AddPeripheral @sensors/bmp280.so 0x76

# Or using a platform file that includes the sensor:
(machine-0) include @platforms/boards/stm32f4_discovery_with_bmp280.resc

# Set sensor values (simulate temperature/pressure)
(machine-0) bmp280 Temperature 25.0
(machine-0) bmp280 Pressure 1013.25

# Monitor I2C traffic
(machine-0) analyzer Enable I2cAnalyzer i2c
```

### CAN Bus Multi-Node Setup

```
# Create CAN bus with two nodes
(machine-0) mach create
(machine-0) machine LoadPlatformDescription @platforms/cpus/stm32f4.resc

# Add CAN controller and bus
(machine-0) can CreateCanBus
(machine-0) can AddNode @stm32f4.can1 0
(machine-0) can AddNode @stm32f4_2.can1 1

# Load firmware on both nodes
(machine-0) sysbus LoadELF @node1_firmware.elf
(machine-0) machine CreateMachine "node2"
(machine-0) sysbus LoadELF @node2_firmware.elf

# Monitor CAN traffic
(machine-0) analyzer Enable CanAnalyzer can
```

### Analyzer Commands for Bus Traffic Logging

```
# Enable analyzer on a bus
(machine-0) analyzer Enable I2cAnalyzer i2c
(machine-0) analyzer Enable CanAnalyzer can
(machine-0) analyzer Enable UartAnalyzer uart1

# Save analyzer output to file
(machine-0) analyzer SaveOutput i2c_analyzer @i2c_log.txt

# List active analyzers
(machine-0) analyzer List

# Disable analyzer
(machine-0) analyzer Disable I2cAnalyzer
```

---

## Emulator Compatibility

| Project | QEMU | Renode | Notes |
|---|---|---|---|
| 1: LED Blinker | Full | Full | GPIO LED simulation works on both |
| 2: Button & Interrupts | Full | Full | EXTI/NVIC fully supported |
| 3: UART Console | Full | Full | Serial output to host terminal |
| 4: PWM & Servo | Partial | Full | QEMU timer works; Renode has better PWM visualization |
| 5: I2C Sensor | Partial | Full | QEMU has limited I2C; use Renode with BMP280 model |
| 6: SPI Display | Partial | Partial | Framebuffer visible in QEMU; Renode has display model |
| 7: Bootloader | Full | Full | Flash emulation works on both |
| 8: RTOS Scheduler | Full | Full | No peripheral dependency |
| 9: CAN Bus | Partial | Full | QEMU CAN support is limited; use Renode |
| 10: Motor Controller | Partial | Full | Encoder simulation better in Renode |
| 11: Sensor Fusion | Partial | Full | Multi-sensor I2C/SPI requires Renode |
| 12: Wireless Mesh | Workaround | Workaround | Simulate UART-connected radios on both |
| 13: Secure Boot | Full | Full | Crypto algorithms are CPU-only |
| 14: OTA Updates | Full | Full | Flash emulation works on both |
| 15: IoT Device | Partial | Full | Full integration best in Renode |

### Legend

| Status | Meaning |
|---|---|
| **Full** | Complete emulation — all features work |
| **Partial** | Core functionality works; some peripherals simulated or stubbed |
| **Workaround** | Requires creative simulation (UART loops, mock peripherals) |

---

## Pro Tips

### 1. Semihosting for Rapid Development

```bash
# Use semihosting for printf without UART setup
qemu-system-arm -M netduinoplus2 -kernel firmware.elf -semihosting
```

In C:
```c
#include <stdio.h>
printf("Hello from semihosting!\n");  // Prints to host terminal
```

In Rust (using `semihosting` crate):
```rust
use semihosting::println;
println!("Hello from semihosting!");
```

### 2. GDB Watchpoints for Peripheral Debugging

```gdb
# Watch when a register changes
(gdb) watch *(volatile uint32_t*)0x4001080C
# Stops when GPIOA_ODR is written

# Watch the stack pointer (catch stack overflows)
(gdb) watch $sp
```

### 3. QEMU Debug Flags

```bash
# Log interrupt activity
qemu-system-arm -M netduinoplus2 -kernel firmware.elf -d int

# Log guest errors
qemu-system-arm -M netduinoplus2 -kernel firmware.elf -d guest_errors

# Log CPU execution (verbose!)
qemu-system-arm -M netduinoplus2 -kernel firmware.elf -d in_asm,exec

# Log to file instead of stderr
qemu-system-arm -M netduinoplus2 -kernel firmware.elf -d int -D qemu.log
```

### 4. Renode Analyzers for Protocol Debugging

```
# Log all I2C transactions with decoded data
(machine-0) analyzer Enable I2cAnalyzer i2c
(machine-0) analyzer SetLoggingLevel I2cAnalyzer Debug

# Log CAN frames with ID and data
(machine-0) analyzer Enable CanAnalyzer can
(machine-0) analyzer SetLoggingLevel CanAnalyzer Debug
```

### 5. Host-First Unit Testing

```bash
# Test algorithms on host before deploying to target

# C: compile for host
gcc -o test_pid test_pid.c -lm
./test_pid

# Rust: run tests natively
cargo test

# Zig: run tests natively
zig test src/pid.zig

# Ada: compile and run on host
gprbuild -P test_pid.gpr
./obj/test_pid
```

> **Tip:** Write and test all algorithms (PID, sensor fusion, crypto) on the host first. Only deploy to the emulator once the logic is verified.

### 6. Deterministic Replay with QEMU

```bash
# Record execution
qemu-system-arm -M netduinoplus2 -kernel firmware.elf \
    -icount shift=7,rr=record,rrfile=replay.bin

# Replay execution (deterministic!)
qemu-system-arm -M netduinoplus2 -kernel firmware.elf \
    -icount shift=7,rr=replay,rrfile=replay.bin
```

### 7. Speed Up QEMU with TCG

```bash
# Use TCG accelerator (default, but can tune)
qemu-system-arm -M netduinoplus2 -kernel firmware.elf \
    -accel tcg,thread=multi

# Skip unused peripherals for faster boot
qemu-system-arm -M virt -cpu cortex-m3 -kernel firmware.elf \
    -no-reboot -nographic
```

---

## What's Next?

With QEMU and Renode configured, you're ready to start building:

1. **[Project 1: LED Blinker](01-led-blinker.md)** — Your first bare-metal project
2. Review the [full project roadmap](00-index.md) for the complete course plan

> **Tip:** Before starting Project 1, verify that `qemu-system-arm` runs a simple ELF file successfully. This confirms your entire toolchain is working end-to-end.
