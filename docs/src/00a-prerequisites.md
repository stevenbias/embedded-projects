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

### Installation

```bash
sudo apt update
sudo apt install gcc-arm-none-eabi gdb-multiarch binutils-arm-none-eabi
```

### Verify Installation

```bash
arm-none-eabi-gcc --version
# Expected: arm-none-eabi-gcc (15:12.3.rel1-1) 12.3.0 or similar

arm-none-eabi-gdb --version
# Expected: GNU gdb (GDB) 13.x or similar

arm-none-eabi-objdump --version
# Expected: GNU objdump (GNU Binutils) 2.x
```

### Verify GDB Multiarch

```bash
gdb-multiarch --version
# Expected: GNU gdb (GDB) 13.x or similar
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
# Expected: rustc 1.75.0 or newer

cargo --version
# Expected: cargo 1.75.0 or newer
```

### Install Embedded Targets

This course targets three ARM Cortex-M variants:

```bash
# Cortex-M3 (STM32F1, used in most projects)
rustup target add thumbv7m-none-eabi

# Cortex-M4F with FPU (STM32F4, used in advanced projects)
rustup target add thumbv7em-none-eabihf

# Cortex-M0+ (STM32L0, used in low-power projects)
rustup target add thumbv6m-none-eabi
```

### Verify Targets

```bash
rustup target list --installed
# Should include:
#   thumbv7m-none-eabi
#   thumbv7em-none-eabihf
#   thumbv6m-none-eabi
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

Ada for embedded uses GNAT ARM/ELF and the Alire package manager.

### GNAT ARM/ELF Installation

```bash
# Download GNAT Community Edition for ARM
# Visit: https://www.adacore.com/download
# Select: GNAT Studio / GNAT ARM-ELF

# Or install via package manager (community builds):
sudo apt install gnat gprbuild
```

### GNAT ARM-ELF (Recommended for Cross-Compilation)

```bash
# Download from AdaCore
wget https://github.com/AdaCore/gnat-community/releases/download/2023/2023/gnat-arm-elf-2023-x86_64-linux-bin
chmod +x gnat-arm-elf-2023-x86_64-linux-bin
sudo ./gnat-arm-elf-2023-x86_64-linux-bin

# Add to PATH (add to ~/.bashrc or ~/.zshrc for persistence)
export PATH="/opt/GNAT/2023-arm-elf/bin:$PATH"
```

### Verify GNAT

```bash
arm-eabi-gcc --version
# Expected: gcc (GCC) 13.x or similar (GNAT ARM-ELF)

gprbuild --version
# Expected: GPRBUILD Pro x.x.x or GPRBUILD Community
```

### Alire Package Manager

Alire is Ada's equivalent of Cargo — it manages projects, dependencies, and toolchains.

```bash
# Install Alire via bootstrap script
wget https://github.com/alire-project/alire/releases/download/v2.0.0/alr-2.0.0-bin-x86_64-linux.tar.gz
tar -xzf alr-2.0.0-bin-x86_64-linux.tar.gz
sudo mv alr /usr/local/bin/
alr version
# Expected: 2.0.0 or newer

# Configure Alire for ARM cross-compilation
alr toolchain --select
# Select: gnat_arm_elf when prompted
```

### Verify Alire

```bash
alr version
alr toolchain
# Should show gnat_arm_elf as selected toolchain
```

> **Note:** If `alr toolchain --select` does not offer `gnat_arm_elf`, install it manually:
>
> ```bash
> alr get gnat_arm_elf
> alr toolchain --select
> ```

---

## Zig Toolchain

Zig ships as a single self-contained binary with no external dependencies.

### Installation

```bash
# Download latest Zig release
wget https://ziglang.org/download/0.13.0/zig-linux-x86_64-0.13.0.tar.xz

# Extract
tar -xf zig-linux-x86_64-0.13.0.tar.xz

# Move to /opt and create symlink
sudo mv zig-linux-x86_64-0.13.0 /opt/zig
sudo ln -s /opt/zig/zig /usr/local/bin/zig

# Verify
zig version
# Expected: 0.13.0 or newer
```

### Verify Cross-Compilation Targets

```bash
# Zig includes cross-compilation out of the box — no target installation needed
zig build-exe --help | grep "target"

# Test cross-compilation for ARM Cortex-M3
zig build-exe -lc -target arm-freestanding-eabihf -mcpu cortex_m3 --verbose
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
rustup target add thumbv7m-none-eabi
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
