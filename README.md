# Embedded Mastery Roadmap: C, Ada, Rust & Zig

## A Project-Based Guide from Beginner to Expert

---

## Repository Structure

```
.
├── docs/                   # Tutorial source files (markdown)
│   ├── src/                # 19 chapter files (00-index.md through 15-safety-critical.md)
│   ├── Makefile            # Build system: make html|pdf|epub|pages|deploy
│   ├── templates/          # HTML/EPUB templates
│   └── assets/             # CSS, images, fonts
├── code/                   # All implementations
│   ├── 01-led-blinker/
│   │   ├── c/              # C implementation
│   │   ├── rust/           # Rust implementation
│   │   ├── ada/            # Ada implementation
│   │   └── zig/            # Zig implementation
│   └── ...
└── README.md               # This file
```

## Quick Start

```bash
# Build tutorial (HTML)
make -C docs html

# Build all formats
make -C docs all

# Build individual HTML pages (for GitHub Pages)
make -C docs pages

# Deploy to GitHub Pages (on master branch)
make -C docs deploy
```

## Tutorial Content

The tutorial consists of 15 projects implemented in C, Ada, Rust, and Zig:

| Phase | Projects |
|-------|----------|
| 1. Bare Metal Foundations | LED Blinker, UART Echo, Button Interrupts |
| 2. Peripherals & Communication | I2C Sensor, SPI Flash, PWM Motor |
| 3. Architecture & Systems | Task Scheduler, Ring Buffer, Bootloader |
| 4. Real-Time & Concurrency | RTOS Kernel, CAN Bus, Data Logger |
| 5. Advanced & Expert | USB CDC, PID Motor, Safety-Critical |

See [docs/src/00-index.md](docs/src/00-index.md) for the full project roadmap.
