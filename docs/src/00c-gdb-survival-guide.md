---
title: "GDB Survival Guide for Embedded ARM"
phase: 0
project: 0
---

# GDB Survival Guide for Embedded ARM

This guide covers everything you need to debug embedded ARM Cortex-M firmware with GDB. It is split into two parts: a **quick reference card** for fast lookup, and **detailed workflows** for common debugging scenarios.

> **Note:** All examples assume `gdb-multiarch` or `arm-none-eabi-gdb` with an ARM Cortex-M4F target (STM32F4). Commands work identically on Cortex-M3/M0+.

---

## Quick Reference Card

### Starting a Debug Session

| Action | QEMU | OpenOCD + ST-Link | probe-rs |
|---|---|---|---|
| **Start debug server** | `qemu-system-arm -M netduinoplus2 -kernel firmware.elf -S -s` | `openocd -f interface/stlink.cfg -f target/stm32f4x.cfg` | `probe-rs gdb` |
| **Connect GDB** | `gdb-multiarch firmware.elf` → `target remote :1234` | `gdb-multiarch firmware.elf` → `target remote :3333` | `gdb-multiarch firmware.elf` → `target remote :3333` |
| **One-liner** | `gdb-multiarch -ex "target remote :1234" firmware.elf` | `gdb-multiarch -ex "target remote :3333" firmware.elf` | `probe-rs debug firmware.elf` |

### Essential Commands

| Category | Command | Description |
|---|---|---|
| **Execution** | `continue` (`c`) | Run until next breakpoint |
| | `step` (`s`) | Step into function |
| | `next` (`n`) | Step over function |
| | `finish` | Run until current function returns |
| | `run` (`r`) | Restart from beginning (QEMU only) |
| **Breakpoints** | `break main` | Break at function |
| | `break *0x08000100` | Break at address |
| | `break file.c:42` | Break at file:line |
| | `info breakpoints` | List all breakpoints |
| | `delete 1` | Delete breakpoint #1 |
| | `disable 1` | Temporarily disable #1 |
| **Registers** | `info registers` | Show all registers |
| | `print $pc` | Program counter |
| | `print $sp` | Stack pointer |
| | `print $xpsr` | Program status register |
| | `print $r0` | Specific register |
| **Memory** | `x/16x 0x20000000` | 16 words as hex |
| | `x/32b 0x40010800` | 32 bytes |
| | `x/10i $pc` | 10 instructions at PC |
| | `x/s 0x20000000` | String at address |
| **Watchpoints** | `watch *(uint32_t*)0x4001080C` | Break on write |
| | `rwatch *(uint32_t*)0x...` | Break on read |
| | `awatch *(uint32_t*)0x...` | Break on read/write |
| **Stack** | `bt` | Backtrace |
| | `frame 2` | Switch to frame 2 |
| | `info locals` | Local variables |
| | `info args` | Function arguments |
| **Misc** | `list` | Show source around PC |
| | `disassemble` | Disassemble current function |
| | `quit` | Exit GDB |

### Format Specifiers for `x` (examine memory)

| Specifier | Format | Example |
|---|---|---|
| `x` | Hexadecimal | `x/16x 0x20000000` |
| `d` | Signed decimal | `x/4d 0x20000000` |
| `u` | Unsigned decimal | `x/4u 0x20000000` |
| `t` | Binary | `x/32t 0x40010800` |
| `i` | Instructions | `x/10i $pc` |
| `s` | String | `x/s 0x20000100` |
| `c` | Character | `x/16c 0x20000100` |

---

## Connecting to Targets

### QEMU GDB Stub

QEMU provides a built-in GDB server. This is the simplest setup for learning.

```bash
# Terminal 1: Start QEMU paused, waiting for GDB
qemu-system-arm -M netduinoplus2 -kernel firmware.elf -S -s -nographic

# Terminal 2: Connect with GDB
gdb-multiarch firmware.elf
(gdb) target remote :1234
(gdb) break main
(gdb) continue
```

The `-S` flag freezes the MCU at startup. The `-s` flag starts a GDB server on port 1234. Without `-S`, the MCU runs immediately and you must set breakpoints before it reaches the code you want to inspect.

> **Tip:** Add `-d int` to log interrupt activity to stderr, or `-D qemu.log` to log to a file.

### OpenOCD + ST-Link (Real Hardware)

OpenOCD bridges between GDB and the physical ST-Link debugger on the NUCLEO board.

```bash
# Terminal 1: Start OpenOCD
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg

# Terminal 2: Connect with GDB
gdb-multiarch firmware.elf
(gdb) target remote :3333
(gdb) monitor reset halt
(gdb) load firmware.elf
(gdb) break main
(gdb) continue
```

The `monitor` prefix sends commands directly to OpenOCD. Use `monitor reset halt` to reset the MCU and halt at the reset vector.

> **Note:** The exact config files depend on your debugger and target. Common alternatives include `interface/stlink-v2-1.cfg` and `target/stm32f4x.cfg`.

### probe-rs (Modern Alternative)

probe-rs is a Rust-based debugging tool that replaces OpenOCD for many use cases.

```bash
# Interactive GDB session
probe-rs debug firmware.elf --chip STM32F446RETx

# Or start GDB server manually
probe-rs gdb
# Then in GDB: target remote :3333
```

probe-rs has better chip support detection, faster flashing, and built-in RTT (Real-Time Transfer) support for debug output without UART.

---

## Core Debugging Workflows

### Breakpoints and Execution Control

```gdb
# Set breakpoints before running
(gdb) break main
(gdb) break HardFault_Handler
(gdb) break *0x08001000

# Conditional breakpoints (break only when condition is true)
(gdb) break main.c:42 if counter > 100
(gdb) break uart_send if data == 0xFF

# Temporary breakpoints (auto-delete after hitting once)
(gdb) tbreak main

# Run to a specific location
(gdb) until main.c:50        # Continue until line 50
(gdb) advance uart_send      # Continue until function entry
```

> **Tip:** Use `tbreak` for one-shot breakpoints during initialization code that runs only once.

### Inspecting Variables and Types

```gdb
# Print variables (GDB knows types from debug symbols)
(gdb) print counter
(gdb) print gpio_config
(gdb) print *gpio_config     # Dereference pointer

# Print with specific format
(gdb) print/x counter        # Hexadecimal
(gdb) print/t counter        # Binary
(gdb) print/d counter        # Decimal

# Print arrays and structs
(gdb) print buffer[0]@16     # First 16 elements
(gdb) print ((uint8_t*)ptr)[32]  # Cast and index

# Print type information
(gdb) whatis counter
(gdb) ptype GpioConfig
(gdb) info types Gpio*
```

### Watchpoints for Memory Changes

Watchpoints halt execution when a memory location is read or written. They are invaluable for tracking down unexpected state changes.

```gdb
# Watch a variable
(gdb) watch counter

# Watch a specific memory address (peripheral register)
(gdb) watch *(volatile uint32_t*)0x4001080C

# Read watchpoint (breaks when memory is read)
(gdb) rwatch shared_flag

# Access watchpoint (breaks on read OR write)
(gdb) awatch shared_flag
```

> **Note:** Hardware watchpoints on Cortex-M use the Data Watchpoint and Trace (DWT) unit, which has only 4 slots. GDB uses them automatically when available. Software watchpoints are slower.

### Backtrace and Stack Inspection

When your code crashes or reaches an unexpected state, the backtrace reveals how you got there.

```gdb
# Full backtrace
(gdb) bt

# Backtrace with all frames (including inlined)
(gdb) bt full

# Switch to a specific frame
(gdb) frame 3

# Inspect locals and arguments in that frame
(gdb) info locals
(gdb) info args

# Print the return address of the current frame
(gdb) info frame
```

---

## ARM Cortex-M Specific Debugging

### Understanding Cortex-M Registers

| Register | Name | Description |
|---|---|---|
| `r0`–`r3` | Argument/scratch | Function arguments, return values, caller-saved |
| `r4`–`r11` | Variable registers | Callee-saved, preserved across function calls |
| `r12` | `ip` | Intra-procedure call scratch |
| `r13` | `sp` | Stack pointer (MSP or PSP) |
| `r14` | `lr` | Link register (return address) |
| `r15` | `pc` | Program counter |
| — | `xpsr` | Combined program status register |
| — | `msp` | Main stack pointer |
| — | `psp` | Process stack pointer |

```gdb
# View all registers
(gdb) info registers

# View specific registers
(gdb) print/x $pc
(gdb) print/d $sp
(gdb) print/t $xpsr

# Decode XPSR flags
(gdb) print $xpsr & 0x20000000   # Thumb bit (should be 1)
(gdb) print ($xpsr >> 24) & 0xFF # Exception number (0 = thread mode)
```

### Hard Fault Analysis

Hard faults are the most common crash in embedded systems. GDB can help identify the cause.

```gdb
# Set a breakpoint on the hard fault handler
(gdb) break HardFault_Handler

# When hit, examine the fault status registers
(gdb) print/x *(uint32_t*)0xE000ED28   # CFSR (Configurable Fault Status Register)
(gdb) print/x *(uint32_t*)0xE000ED29   # CFSR byte access (individual bits)
(gdb) print/x *(uint32_t*)0xE000ED38   # HFSR (Hard Fault Status Register)
(gdb) print/x *(uint32_t*)0xE000ED3C   # DFSR (Debug Fault Status Register)
(gdb) print/x *(uint32_t*)0xE000ED34   # BFAR (Bus Fault Address Register)
(gdb) print/x *(uint32_t*)0xE000ED30   # MMFAR (Mem Manage Fault Address Register)
```

#### CFSR Bit Decode

| Bits | Field | Meaning |
|---|---|---|
| 0 | `IACCVIOL` | Instruction access violation (execute from non-executable region) |
| 1 | `DACCVIOL` | Data access violation (read/write to inaccessible region) |
| 3 | `MUNSTKERR` | Unstacking fault (invalid stack during exception return) |
| 4 | `MSTKERR` | Stacking fault (invalid stack during exception entry) |
| 8 | `IBUSERR` | Instruction bus error |
| 9 | `PRECISERR` | Precise data bus error (exact address in BFAR) |
| 10 | `IMPRECISERR` | Imprecise data bus error (address unknown) |
| 11 | `UNSTKERR` | Unstacking fault on bus |
| 12 | `STKERR` | Stacking fault on bus |
| 16 | `UNDEFINSTR` | Undefined instruction |
| 17 | `INVSTATE` | Invalid state (Thumb bit cleared on branch) |
| 18 | `INVPC` | Invalid PC load (bad EXC_RETURN in LR) |
| 19 | `NOCP` | No coprocessor (e.g., FPU disabled) |
| 24–25 | `DIVBYZERO` | Divide by zero |
| 26 | `UNALIGNED` | Unaligned access |

```gdb
# Quick hard fault diagnosis script
(gdb) set $cfsr = *(uint32_t*)0xE000ED28
(gdb) if $cfsr & (1 << 0)
 > print "Instruction access violation"
 > end
(gdb) if $cfsr & (1 << 9)
 > printf "Precise bus error at 0x%08x\n", *(uint32_t*)0xE000ED38
 > end
```

### Inspecting the Stack

Stack overflows and corruption are common in embedded systems.

```gdb
# View the current stack pointer
(gdb) print/x $sp

# Examine the stack contents (top 64 words)
(gdb) x/64x $sp

# Find the stack bounds (from linker script symbols)
(gdb) print/x &_estack
(gdb) print/x &_Min_Stack_Size

# Check if stack is approaching its limit
(gdb) print $sp - &_estack

# View the call stack with frame information
(gdb) bt full
```

### Peripheral Register Inspection

Memory-mapped peripheral registers are the primary way to interact with hardware.

```gdb
# GPIOA registers (STM32F4)
(gdb) x/12x 0x40020000    # MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, ...

# USART2 registers
(gdb) x/8x 0x40004400     # SR, DR, BRR, CR1, CR2, CR3, GTPR

# NVIC registers
(gdb) x/4x 0xE000E100     # ISER (Interrupt Set-Enable)
(gdb) x/4x 0xE000E200     # ISPR (Interrupt Set-Pending)

# RCC (Reset and Clock Control)
(gdb) x/8x 0x40023800     # CR, PLLCFGR, CFGR, CIR, AHB1ENR, ...
```

> **Tip:** Define convenience commands in your `.gdbinit` file to avoid typing addresses repeatedly. See the [GDB Automation](#gdb-automation) section below.

### Exception and Interrupt Debugging

```gdb
# Check which exception is currently active
(gdb) print ($xpsr >> 24) & 0xFF
# 3 = HardFault, 11 = SVCall, 14 = PendSV, 15 = SysTick

# View NVIC enabled interrupts
(gdb) x/4x 0xE000E100

# View pending interrupts
(gdb) x/4x 0xE000E200

# View interrupt priorities
(gdb) x/16x 0xE000E400

# Set a breakpoint on any exception handler
(gdb) break SysTick_Handler
(gdb) break EXTI0_IRQHandler
(gdb) break USART2_IRQHandler
```

---

## Advanced Techniques

### GDB Automation with `.gdbinit`

Create a `.gdbinit` file in your project directory to automate common tasks. GDB auto-loads it when you start a session.

```gdb
# .gdbinit — GDB startup script for STM32F4

# Auto-connect to QEMU
target remote :1234

# Define convenience commands
define regs
    info registers
end

define gpioa
    printf "GPIOA MODER:  0x%08x\n", *(uint32_t*)0x40020000
    printf "GPIOA OTYPER: 0x%08x\n", *(uint32_t*)0x40020004
    printf "GPIOA OSPEED: 0x%08x\n", *(uint32_t*)0x40020008
    printf "GPIOA PUPDR:  0x%08x\n", *(uint32_t*)0x4002000C
    printf "GPIOA IDR:    0x%08x\n", *(uint32_t*)0x40020010
    printf "GPIOA ODR:    0x%08x\n", *(uint32_t*)0x40020014
end

define usart2
    printf "USART2 SR:  0x%08x\n", *(uint32_t*)0x40004400
    printf "USART2 DR:  0x%08x\n", *(uint32_t*)0x40004404
    printf "USART2 BRR: 0x%08x\n", *(uint32_t*)0x40004408
    printf "USART2 CR1: 0x%08x\n", *(uint32_t*)0x4000440C
end

define hardfault
    printf "CFSR:  0x%08x\n", *(uint32_t*)0xE000ED28
    printf "HFSR:  0x%08x\n", *(uint32_t*)0xE000ED38
    printf "BFAR:  0x%08x\n", *(uint32_t*)0xE000ED38
    printf "MMFAR: 0x%08x\n", *(uint32_t*)0xE000ED34
end

# Auto-break on hard faults
break HardFault_Handler
commands
    hardfault
    bt
end

# Useful aliases
alias br = break
alias cont = continue
alias del = delete
alias dis = disassemble
```

> **Note:** GDB may refuse to auto-load `.gdbinit` for security reasons. Add `add-auto-load-safe-path /path/to/project` to `~/.gdbinit` to allow it.

### TUI Mode

GDB's Text User Interface provides a split-screen view with source code, assembly, registers, and command line.

```bash
# Start GDB in TUI mode
gdb-multiarch -tui firmware.elf

# Or toggle TUI after connecting
(gdb) layout src       # Source code view
(gdb) layout asm       # Assembly view
(gdb) layout regs      # Registers view
(gdb) layout split     # Source + Assembly
(gdb) tui enable       # Enable TUI
(gdb) tui disable      # Disable TUI
```

Keyboard shortcuts in TUI mode:

| Key | Action |
|---|---|
| `Ctrl+X`, `Ctrl+A` | Toggle TUI |
| `Ctrl+X`, `1` | Single-window layout |
| `Ctrl+X`, `2` | Two-window layout |
| `Ctrl+X`, `O` | Switch focus between windows |
| `Up`/`Down` | Scroll in source/assembly window |

### Python Scripting

GDB supports Python scripting for complex debugging tasks.

```python
# gdb_script.py — Custom GDB commands
import gdb

class PrintStackUsage(gdb.Command):
    """Print current stack usage as percentage."""
    def __init__(self):
        super().__init__("stack-usage", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        sp = int(gdb.parse_and_eval("$sp"))
        estack = int(gdb.parse_and_eval("&_estack"))
        total = int(gdb.parse_and_eval("_Min_Stack_Size"))
        used = estack - sp
        pct = (used / total) * 100
        print(f"Stack: {used}/{total} bytes ({pct:.1f}%)")

PrintStackUsage()

# Load with: source gdb_script.py
# Use with: stack-usage
```

```gdb
# In GDB:
(gdb) source gdb_script.py
(gdb) stack-usage
Stack: 512/2048 bytes (25.0%)
```

### QEMU Record/Replay for Deterministic Debugging

QEMU can record execution and replay it deterministically, which is invaluable for reproducing intermittent bugs.

```bash
# Record execution
qemu-system-arm -M netduinoplus2 -kernel firmware.elf \
    -icount shift=7,rr=record,rrfile=replay.bin -nographic

# Replay execution (identical every time)
qemu-system-arm -M netduinoplus2 -kernel firmware.elf \
    -icount shift=7,rr=replay,rrfile=replay.bin -S -s -nographic

# Debug the replay
gdb-multiarch firmware.elf
(gdb) target remote :1234
(gdb) break main.c:42
(gdb) continue
```

> **Tip:** Record once, replay as many times as needed. Each replay produces identical behavior, making it easy to test different breakpoints and watchpoints.

---

## Common Debugging Scenarios

### Code Won't Start

```gdb
# Verify the MCU is halted at reset
(gdb) print/x $pc
# Should be 0x08000000 + 4 (reset handler address from vector table)

# Check the vector table
(gdb) x/8x 0x08000000
# First word = initial MSP value
# Second word = Reset_Handler address

# Step through startup code
(gdb) step
(gdb) next

# Check if clocks are configured
(gdb) x/4x 0x40023800    # RCC registers
```

### Hard Fault on Specific Instruction

```gdb
# When hard fault hits, go back to the faulting instruction
(gdb) bt
(gdb) frame 1              # Go to the frame before the handler
(gdb) x/i $pc              # Show the faulting instruction

# Common causes:
# - Null pointer dereference: check if a pointer is 0x00000000
# - Unaligned access: check if address is not word-aligned
# - Stack overflow: check $sp against stack bounds
# - Invalid instruction: check if executing from non-flash region

# Check the fault address
(gdb) print/x *(uint32_t*)0xE000ED38   # BFAR for precise bus faults
```

### ISR Not Firing

```gdb
# Check if the interrupt is enabled in NVIC
(gdb) x/4x 0xE000E100      # ISER registers

# Check if the interrupt is pending
(gdb) x/4x 0xE000E200      # ISPR registers

# Check the interrupt priority
(gdb) x/16x 0xE000E400     # IP registers

# Check if global interrupts are enabled
(gdb) print/x $xpsr        # PRIMASK bit should be 0

# Check the peripheral's own interrupt enable bit
(gdb) x/4x 0x4000440C      # USART2 CR1 — check RXNEIE, TXEIE bits
```

### Stack Overflow Detection

```gdb
# Set a watchpoint at the stack limit
(gdb) watch *(uint32_t*)&_estack - 0x800
# Triggers if stack grows into the watched region

# Or use GDB to check current usage
(gdb) print $sp
(gdb) print &_estack
(gdb) print &_estack - $sp

# Check if the stack pointer is in a valid range
(gdb) if $sp < 0x20000000 || $sp > &_estack
 > print "Stack pointer out of range!"
 > end
```

### Infinite Loop Detection

```gdb
# Set a breakpoint and count hits
(gdb) break main.c:100
(gdb) commands
 > silent
 > set $count = $count + 1
 > if $count > 1000
 >   printf "Hit breakpoint %d times — possible infinite loop\n", $count
 >   bt
 >   stop
 > end
 > continue
 > end
(gdb) set $count = 0
(gdb) continue
```

---

## What's Next?

With GDB skills in hand, you're ready to debug any project in this course:

1. **[Project 1: LED Blinker](01-led-blinker.md)** — Use GDB to verify GPIO register writes and LED toggle
2. Review the [Emulator Setup Guide](00b-emulator-setup.md) for QEMU and Renode configuration

> **Tip:** Create a `.gdbinit` file for each project with peripheral-specific commands. It will save hours of typing register addresses.

---

## References

### GDB Documentation
- [GDB User Manual](https://sourceware.org/gdb/current/onlinedocs/gdb.html) — Complete reference for all GDB features
- [GDB Embedded Processors](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Embedded-Processors.html) — Remote debugging, target-specific commands
- [GDB Python API](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Python-API.html) — Scripting GDB with Python

### ARM Cortex-M Debugging
- [ARM Cortex-M4 Technical Reference Manual](https://developer.arm.com/documentation/ddi0439/latest/) — NVIC, DWT, fault registers
- [ARM Debug Interface Architecture Specification](https://developer.arm.com/documentation/ihi0031/latest/) — CoreSight, SWD, ETM
- [ARMv7-M Exception Model](https://developer.arm.com/documentation/ddi0403/latest/the-cortex-m3-processor/exception-model) — Exception entry/return, stacking

### Tools
- [QEMU GDB Stub Documentation](https://www.qemu.org/docs/master/system/gdb.html) — QEMU-specific GDB features
- [OpenOCD Documentation](https://openocd.org/doc/html/) — Debug adapter configuration, target scripts
- [probe-rs Documentation](https://probe.rs/) — Modern embedded debugging, RTT support
- [ST-Link GDB Server](https://www.st.com/en/development-tools/stsw-link004.html) — ST's official GDB server
