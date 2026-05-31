# AGENTS.md

## Repository Structure

Three top-level concerns:
- `docs/src/*.md` — tutorial content, built by pandoc
- `code/<project>/{c,rust,ada,zig}/` — implementations (C and Rust exist; Ada and Zig are empty placeholders)
- `code/<project>/common/` — shared `linker.ld` and `crt0.s` used by both C and Rust
- `code/<project>/renode/` — Renode simulation platform definitions (`.resc`, `.repl`)

## Build Commands

### Docs
- `make -C docs html|pdf|epub|pages|serve|deploy|clean`
- Requires: `pandoc`, `python3` (CI installs pandoc; PDF additionally needs `weasyprint` — not in CI)
- Chapter order hardcoded in `docs/Makefile` (`SRCS` list). Keep filenames and order in sync.
- `make -C docs deploy` → builds pages, copies `00-index.html` → `index.html` for GitHub Pages
- Search index: `docs/scripts/generate-search-index.py`; sidebar: `docs/scripts/gen-sidebar.py`
- CI deploys `master` via `.github/workflows/deploy.yml`

### C Projects (STM32F446RE)
- Toolchain: `arm-none-eabi-gcc`
- Build: `make -C code/<project>/c` → `<project>.elf`, `<project>.bin`
- Flash: `make -C code/<project>/c flash` (needs `st-flash`)
- Simulate: `make -C code/<project>/c renode` (needs Renode + `../renode/*.resc`)
- Shared config files: `code/.clang-format`, `code/Doxyfile`
- Fuzzing: uses AFL++ (not libFuzzer)

### Rust Projects (STM32F446RE)
- Build: `make -C code/<project>/rust all` → `<project>.elf`, `<project>.bin`
- Uses `cargo build --release` under the hood; target `thumbv7em-none-eabihf`
- **Quirk**: GNAT Pro Rust (`/usr/gnat/rust/bin`, 1.77.2) lacks ARM target. Makefiles explicitly use `~/.cargo/bin/cargo` via `CARGO`/`RUSTC` env vars.
- **Edition 2024**: uses `#[unsafe(no_mangle)]` syntax (not `#[no_mangle]`)
- Fuzzing: uses `cargo fuzz` (libFuzzer)
- Flash/simulate: same targets as C

### Top-Level Orchestration (`code/Makefile`)
- `make -C code all|clean|doc|lint|test|fuzz|format|format-check|check`
- Delegates to per-project Makefiles; targets that don't exist are **silently skipped** (`|| true`).
- Currently only `01-led-blinker` in `PROJECTS` list.

## Pre-commit Hooks (`.pre-commit-config.yaml`)

Install: `pip install pre-commit && pre-commit install`. Runs on every `git commit`:
1. `clang-format --dry-run -Werror` on `.c`/`.h` files
2. `cargo fmt --check` in Rust directories
3. `zig fmt --check` on `.zig` files
4. `editorconfig-checker` on code files

Bypass with `git commit --no-verify`.

## Quality Targets (expected in per-project Makefiles)

| Target | C | Rust |
|--------|---|------|
| `lint` | cppcheck + clang-tidy | clippy |
| `format-check` | clang-format --dry-run -Werror | cargo fmt --check |
| `test` | Unity (host) | cargo test (host) |
| `fuzz` | AFL++ | cargo fuzz / proptest |
| `doc` | Doxygen | cargo doc |

CI (`.github/workflows/ci.yml`) currently builds and runs lint/format-check/doc/test (Rust only). Matrix over projects.

## NASA Power of 10 — Implementation Plan

Rule 10 (zero warnings) enforced via GCC strict flags:

```makefile
P10_CFLAGS  = -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow
P10_CFLAGS += -Wunused-result -Wfloat-equal -Wundef -Wcast-align
HOST_CC ?= gcc
```

Four-tool verification (`make -C code/<project>/c power-of-10`):

| Rule | Tool | Notes |
|------|------|-------|
| 1. No goto/recursion | cppcheck | `--enable=all` flags `goto` |
| 2. Loop bounds | Frama-C EVA | Reports unproven loops |
| 3. No malloc | cppcheck + Frama-C | Both detect dynamic allocation |
| 4. ≤60 lines/function | clang-tidy | `readability-function-size` |
| 5. ≥2 assertions | Manual | No tool — skip |
| 6. Smallest scope | clang-tidy | `cppcoreguidelines-avoid-non-const-global-variables` |
| 7. Check returns | cppcheck | `--enable=warning` |
| 8. Simple preprocessor | clang-tidy | `cert-preprocessor-*` |
| 9. Max one `*` | Frama-C EVA | Tracks pointer alias depth |
| 10. Zero warnings | GCC | `-Wall -Wextra -Wpedantic -Werror ...` |

Caveats:
- Frama-C may need `<stdint.h>` path: `-cpp-extra-args="-I$$(arm-none-eabi-gcc -print-sysroot)/include"`
- `while(1)` in main is expected — suppress with `//@ loop variant 1;` annotation

## .editorconfig (root)

- C/H: space, 2
- Rust: space, 4
- Makefile: tab, 4
- YAML: space, 2
- Markdown: `trim_trailing_whitespace = false`

## Gotchas

- Ada (`code/**/ada/`) and Zig (`code/**/zig/`) directories exist but are empty placeholders — do not expect buildable code.
- Top-level quality targets (`make -C code lint`, etc.) succeed even if per-project Makefiles lack the target (silently skipped via `|| true`).
- `make -C code` builds all projects; to build a single project use `make -C code/<project>/c` or `make -C code/<project>/rust`.
- The Rust fuzzer uses `cargo fuzz` (libFuzzer); the C fuzzer uses AFL++.

## Reference

- https://4se03.telecom-paris.fr/ — Télécom Paris embedded systems course (ARM, assembly, bare-metal, toolchain, Makefiles, GDB). Consult first for embedded topics.
