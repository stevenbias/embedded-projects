---
title: "Prerequisites & Toolchain Setup"
phase: 0
project: 0
---

# Prerequisites & Toolchain Setup

This guide walks you through installing every tool needed for this course. Follow each section in order and verify installations before proceeding.

> **Note:** All commands assume a Debian/Ubuntu-based Linux system. Adapt package managers for your distribution (e.g., `brew` on macOS, `pacman` on Arch).

---

## ARM Cross-Compiler (GCC)

The ARM GCC toolchain is the foundation for C development and is also used by other toolchains for linking.

### Installation (Recommended)

Download the latest Arm GNU Toolchain directly from the Arm Developer website for the newest features and security updates.

```bash
# Download latest for x86_64 Linux
wget https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz

# Extract to /opt/
tar xJf arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi.tar.xz -C /opt/

# Add to PATH (add to ~/.bashrc or ~/.zshrc for persistence)
export PATH="/opt/arm-gnu-toolchain-15.2.Rel1-x86_64-arm-none-eabi/bin:$PATH"
```

### Alternative: Package Manager

If the latest version is not required, you can install via apt:

```bash
sudo apt update
sudo apt install gcc-arm-none-eabi gdb-multiarch binutils-arm-none-eabi
```

### Verify Installation

```bash
arm-none-eabi-gcc --version
# Expected: arm-none-eabi-gcc (15.2.Rel1) 15.2.0 or similar

arm-none-eabi-gdb --version
# Expected: GNU gdb (GDB) 16.x or similar

arm-none-eabi-objdump --version
# Expected: GNU objdump (GNU Binutils) 2.45 or similar
```

### Verify GDB Multiarch

```bash
gdb-multiarch --version
# Expected: GNU gdb (GDB) 16.x or similar (includes Python 3 support)
```

> **Tip:** If `gdb-multiarch` is not available, use `arm-none-eabi-gdb` instead. Both work for Cortex-M debugging.

---

## Rust Toolchain

Rust's embedded ecosystem is mature and well-supported. We use `rustup` for management and `cargo-embed` for flashing/debugging.

### Installation

```bash
# Install rustup
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"

# Verify
rustc --version
# Expected: rustc 1.95.0 or newer

cargo --version
# Expected: cargo 1.95.0 or newer
```

### Install Embedded Targets

This course targets a single ARM Cortex-M4F variant:

```bash
# Cortex-M4F with FPU (STM32F4 — primary target for all projects)
# Used on both QEMU (netduinoplus2 / STM32F405) and real hardware (NUCLEO-F446RE / STM32F446)
rustup target add thumbv7em-none-eabihf
```

### Verify Targets

```bash
rustup target list --installed
# Should include:
#   thumbv7em-none-eabihf
```

### Install Cargo Tools

```bash
# cargo-binutils: LLVM tools (objdump, nm, size) via cargo
cargo install cargo-binutils

# cargo-generate: project scaffolding from templates
cargo install cargo-generate

# cargo-embed: flash and debug embedded Rust (alternative to probe-rs)
cargo install cargo-embed

# probe-rs: modern embedded debugging tool (recommended)
cargo install probe-rs-tools
```

### Verify Cargo Tools

```bash
cargo size --version
cargo generate --version
cargo embed --version
probe-rs --version
```

> **Tip:** `cargo-binutils` requires `rustup component add llvm-tools-preview`. Install it if `cargo size` fails.

```bash
rustup component add llvm-tools-preview
```

---

## Ada Toolchain

Ada for embedded uses the GNAT compiler and the **Alire** package manager. Alire handles both the GNAT toolchain (including ARM cross-compiler) and gprbuild automatically.

### Install Alire

Alire is Ada's package manager and handles GNAT toolchain installation automatically.

```bash
# Download the latest Alire release
wget https://github.com/alire-project/alire/releases/download/v2.1.0/alr-2.1.0-bin-x86_64-linux.tar.gz
tar -xzf alr-2.1.0-bin-x86_64-linux.tar.gz
sudo mv alr /usr/local/bin/

# Verify
alr version
# Expected: 2.1.x or newer
```

### Install ARM Cross-Compiler via Alire

```bash
# First time: select ARM toolchain
alr toolchain --select
# Choose: gnat_arm_elf (latest version)

# Verify installation
alr toolchain
# Should show gnat_arm_elf as installed
```

### Verify GNAT Tools

```bash
arm-eabi-gcc --version
# Expected: gcc (GCC) 14.x or newer (GNAT)

gprbuild --version
# Expected: GPRBUILD x.x.x
```

> **Tip:** Alire automatically manages dependencies between GNAT and gprbuild. Do not install gnat or gprbuild via apt — let Alire handle it.

> **Note:** If you encounter issues with cross-compilers, you can force a specific version:
> ```bash
> alr search --full --external-detect gnat_arm_elf
> alr toolchain --select
> ```

---

## Zig Toolchain

Zig ships as a single self-contained binary with no external dependencies.

### Installation

```bash
# Download latest Zig release
# Visit https://ziglang.org/download/ for current versions
wget https://ziglang.org/download/0.16.0/zig-linux-x86_64-0.16.0.tar.xz

# Extract
tar -xf zig-linux-x86_64-0.16.0.tar.xz

# Move to /opt and create symlink
sudo mv zig-linux-x86_64-0.16.0 /opt/zig
sudo ln -s /opt/zig/zig /usr/local/bin/zig

# Verify
zig version
# Expected: 0.16.0 or newer
```

### Verify Cross-Compilation Targets

```bash
# Zig includes cross-compilation out of the box — no target installation needed
zig build-exe --help | grep "target"

# Test cross-compilation for ARM Cortex-M4F
zig build-exe -lc -target arm-freestanding-eabihf -mcpu cortex_m4 --verbose
```

> **Tip:** Zig's built-in cross-compilation is one of its strongest features. You never need to install separate cross-compilers — Zig bundles everything.

---

## Common Utilities

These tools are used across all languages for emulation, debugging, and communication.

### Installation

```bash
sudo apt update
sudo apt install \
    qemu-system-arm \
    openocd \
    picocom \
    make \
    cmake \
    python3 \
    python3-pip \
    git \
    wget \
    curl \
    tree \
    tmux
```

### Verify Utilities

```bash
qemu-system-arm --version
# Expected: QEMU emulator version 8.x or newer

openocd --version
# Expected: Open On-Chip Debugger 0.12.0 or newer

picocom --version
# Expected: picocom v3.1 or newer

make --version
# Expected: GNU Make 4.x

cmake --version
# Expected: cmake version 3.22 or newer

python3 --version
# Expected: Python 3.10 or newer
```

### Install Python Dependencies (for build scripts)

```bash
pip3 install pyyaml intelhex
```

---

## Complete Verification Script

Save this as `verify-toolchains.sh` and run it to check everything:

```bash
#!/bin/bash
set -e

echo "=== ARM GCC ==="
arm-none-eabi-gcc --version | head -1

echo ""
echo "=== GDB ==="
gdb-multiarch --version | head -1

echo ""
echo "=== Rust ==="
rustc --version
cargo --version

echo ""
echo "=== Rust Targets ==="
rustup target list --installed | grep -E "thumbv"

echo ""
echo "=== Cargo Tools ==="
cargo size --version 2>/dev/null || echo "cargo-binutils not installed"
cargo generate --version 2>/dev/null || echo "cargo-generate not installed"
cargo embed --version 2>/dev/null || echo "cargo-embed not installed"

echo ""
echo "=== Ada/GNAT ==="
arm-eabi-gcc --version 2>/dev/null | head -1 || echo "GNAT ARM-ELF not found"
gprbuild --version 2>/dev/null | head -1 || echo "gprbuild not found"
alr version 2>/dev/null || echo "Alire not installed"

echo ""
echo "=== Zig ==="
zig version

echo ""
echo "=== QEMU ==="
qemu-system-arm --version | head -1

echo ""
echo "=== OpenOCD ==="
openocd --version | head -1

echo ""
echo "=== All checks complete ==="
```

```bash
chmod +x verify-toolchains.sh
./verify-toolchains.sh
```

---

## Target Hardware

This course is designed to work in **emulation** (QEMU) and on **real hardware** with identical code.

| Property | QEMU (Primary) | Real Hardware |
|---|---|---|
| Board | Netduino Plus 2 | NUCLEO-F446RE |
| MCU | STM32F405RGT6 | STM32F446RET6 |
| Core | Cortex-M4F | Cortex-M4F |
| Flash | 1 MiB | 512 KiB |
| SRAM | 128 KiB | 128 KiB |
| QEMU Machine | `netduinoplus2` | N/A (flashed via ST-Link) |

Both MCUs are in the STM32F4 family and share the same peripheral architecture (GPIO, USART, SPI, I2C, timers, etc.). Code written for one runs on the other with only a pin configuration header swap.

> **Note:** You do **not** need physical hardware to complete this course. All projects run in QEMU. The NUCLEO-F446RE (~$20-25) is recommended if you want to test on real silicon — it has an on-board ST-Link debugger, Arduino headers, and is widely available.

---

## Troubleshooting

### ARM GCC: "command not found"

```bash
# Check if installed
dpkg -l | grep gcc-arm

# If missing, reinstall
sudo apt install --reinstall gcc-arm-none-eabi

# Verify PATH
echo $PATH | tr ':' '\n' | grep arm
```

### Rust: "error[E0463]: can't find crate for `core`"

This means the embedded target is not installed:

```bash
rustup target add thumbv7em-none-eabihf
```

### Rust: "cargo size" fails

```bash
rustup component add llvm-tools-preview
```

### GNAT: "gprbuild: command not found"

```bash
sudo apt install gprbuild
# Or if using Alire:
alr get gprbuild
```

### Alire: "no toolchain available for ARM"

```bash
alr update
alr toolchain --select
# Choose gnat_arm_elf from the list
```

### Zig: "unable to find ziglang directory"

Ensure the Zig binary is in your PATH:

```bash
export PATH="/opt/zig:$PATH"
# Add to ~/.bashrc or ~/.zshrc for persistence
```

### QEMU: "could not load PC BIOS"

```bash
sudo apt install --reinstall qemu-system-arm
# Or install BIOS files separately:
sudo apt install qemu-efi-arm
```

### OpenOCD: "no device found"

This is expected when using QEMU (no physical hardware). OpenOCD is only needed for real hardware debugging. For emulation, use QEMU's built-in GDB server.

### Permission Denied on USB Devices (Real Hardware)

```bash
# Add user to dialout and plugdev groups
sudo usermod -aG dialout $USER
sudo usermod -aG plugdev $USER
# Log out and back in for changes to take effect
```

---

## What's Next?

With all toolchains installed and verified, proceed to:

1. **[Emulator Setup & Usage Guide](00b-emulator-setup.md)** — Configure QEMU and Renode for hardware-free development
2. **[Project 1: LED Blinker](01-led-blinker.md)** — Your first embedded project in all 4 languages

> **Tip:** Before starting Project 1, run the verification script above and ensure every tool reports a valid version. Future projects assume a working toolchain.

---

## References

### STMicroelectronics Documentation
- [STM32F4 Reference Manual (RM0090)](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-415-stm32f407-417-stm32f427-437-and-stm32f429-439-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) — Complete peripheral reference for STM32F4 family
- [STM32F405/407 Datasheet](https://www.st.com/resource/en/datasheet/stm32f405rg.pdf) — Pin assignments, memory sizes, electrical characteristics
- [NUCLEO-F446RE Documentation](https://www.st.com/en/evaluation-tools/nucleo-f446re.html) — Board schematics, user manual, ST-Link/V2-1 details

### ARM Documentation
- [Cortex-M4 Technical Reference Manual](https://developer.arm.com/documentation/ddi0439/latest/) — Processor architecture, FPU, NVIC, SysTick
- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/) — Exception model, memory ordering, instruction set

### Tools & Emulation
- [QEMU ARM Documentation](https://www.qemu.org/docs/master/system/target-arm.html) — qemu-system-arm usage, GDB stub, semihosting
- [QEMU STM32 Documentation](https://www.qemu.org/docs/master/system/arm/stm32.html) — netduinoplus2 machine, supported peripherals
- [Renode Documentation](https://docs.renode.io/) — Multi-node simulation, bus analyzers, peripheral models
- [ARM EABI Specification (IHI 0045)](https://github.com/ARM-software/abi-aa/releases) — Procedure call standard, stack alignment, calling conventions
- [GNU make et Makefiles (4SE03)](https://4se03.telecom-paris.fr/supports/makefiles.pdf) — French PDF tutorial
- [Git Memo (4SE03)](https://4se03.telecom-paris.fr/memento/memento-git.html) — Quick reference (French)
