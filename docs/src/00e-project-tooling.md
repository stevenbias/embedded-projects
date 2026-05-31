---
title: "Project Tooling: Documentation, Analysis, Testing & Formatting"
phase: 0
project: 0
---

# Project Tooling: Documentation, Analysis, Testing & Formatting

> **Prerequisite:** Read this after setting up the toolchain (00a) and before working on any project chapter. Each project chapter assumes you know how to use these tools.

This chapter surveys the **per-language development tools** used across all projects: API documentation generators, static analyzers, formal verifiers, test frameworks, fuzzers, and formatters.

---

## C (arm-none-eabi-gcc)

### Documentation — Doxygen

[Doxygen](https://www.doxygen.nl/) generates HTML API documentation from structured comments (`/** ... */`) in C source and headers.

- **Install:** `sudo apt install doxygen`
- **Usage:** `doxygen Doxyfile` (a shared config is provided at [`code/Doxyfile`](../../code/Doxyfile))
- **Style used in this course:** `@brief`, `@param`, `@return`, `@see`
- **Reference:** [Doxygen Manual](https://www.doxygen.nl/manual/index.html)

### Static Analysis — cppcheck & clang-tidy

**cppcheck** detects buffer overflows, null pointer dereferences, unused variables — no compilation needed.

- **Install:** `sudo apt install cppcheck`
- **Usage:** `cppcheck --error-exitcode=1 src/`
- **Reference:** [cppcheck Manual](https://cppcheck.sourceforge.io/manual.pdf)

**clang-tidy** provides semantic C analysis (requires a `compile_commands.json` compilation database).

- **Install:** `sudo apt install clang-tidy`
- **Usage:** generate `compile_commands.json` via `bear -- make`, then `clang-tidy src/*.c -- -Iinc`
- **Reference:** [clang-tidy Documentation](https://clang.llvm.org/extra/clang-tidy/)

### Coding Standard — NASA Power of 10

The [NASA Power of 10 rules](https://en.wikipedia.org/wiki/The_Power_of_10:_Rules_for_Developing_Safety-Critical_Code) (Holzmann, 2006) are the recommended standard. They are designed to make code statically analyzable by tools like cppcheck and Frama-C.

1. Simple control flow — no `goto`, `setjmp`, `longjmp`, or recursion
2. All loops have a fixed upper bound provable by static analysis
3. No dynamic memory allocation after initialization
4. No function longer than ~60 lines
5. At least two assertions per function
6. Declare all data at the smallest possible scope
7. Check all return values and parameter validity
8. Limit preprocessor to header inclusion and simple macros
9. Restrict pointers to at most one level of dereference
10. Compile with all warnings at the most pedantic setting — zero warnings

**Reference:** [The Power of 10: Rules for Developing Safety-Critical Code](http://web.eecs.umich.edu/~imarkov/10rules.pdf) (Gerard J. Holzmann, 2006)

### Formal Verification — Frama-C

[Frama-C](https://frama-c.com/) is a platform for static and deductive verification of C code. Two plug-ins are relevant:

- **EVA** (abstract interpretation) — proves absence of runtime errors (overflow, out-of-bounds, division by zero) without annotations. Run: `frama-c -eva src/*.c -machdep arm32 -cpp-extra-args="-Iinc"`.
- **WP** (weakest precondition) — deductive verification requiring ACSL annotations (`requires`, `ensures`, `assigns`). Proves full functional correctness.

**References:**
- [Guide to Software Verification with Frama-C](https://link.springer.com/book/10.1007/978-3-031-12808-0) (Springer, 2024)
- [Frama-C User Manual](https://frama-c.com/download/frama-c-user-manual.pdf)
- [The Dogged Pursuit of Bug-Free C Programs](https://cacm.acm.org/research/the-dogged-pursuit-of-bug-free-c-programs/) (CACM, 2023)

### Unit Testing — Unity

[Unity](http://www.throwtheswitch.org/unity) is a lightweight test framework for embedded C — a single `.c` + `.h`, no dependencies.

Tests compile for the **host** (x86_64), not the target. Hardware register access must be abstracted behind macros that can be swapped for mocks:

```c
#ifdef HOST_TEST
extern volatile uint32_t mock_GPIOA_ODR;
#define GPIOA_ODR mock_GPIOA_ODR
#else
#define GPIOA_ODR (*(volatile uint32_t *)0x40020014U)
#endif
```

**Reference:** [Unity Assertion Guide](http://www.throwtheswitch.org/unity)

### Fuzz Testing — AFL++

[AFL++](https://aflplus.plus/) is the community-maintained successor to
Google's American Fuzzy Lop — more mutations, better performance, multi-core
support, and [Unicorn mode](https://aflplus.plus/docs/fuzzing_binary-only_targets/)
for fuzzing firmware blobs directly.

For embedded C code, compile the logic as a **host binary** using AFL++'s
instrumenting compiler, then fuzz with seed inputs:

```bash
# Install
sudo apt install afl++

# Compile harness with AFL++'s compiler
afl-cc -o fuzz_test test/fuzz_harness.c src/main.c -Iinc

# Fuzz
afl-fuzz -i test/seeds/ -o output/ -- ./fuzz_test
```

An AFL++ harness reads input from a file or stdin:

```c
#include "main.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    uint32_t ms;
    if (argc > 1) {
        ms = (uint32_t)atoi(argv[1]) % 2000;
    } else {
        uint8_t buf[4];
        if (fread(buf, 1, 4, stdin) < 4) return 1;
        __builtin_memcpy(&ms, buf, 4);
        ms %= 2000;
    }
    delay_ms(ms);
    return 0;
}
```

- **Install:** `sudo apt install afl++` or build from [source](https://github.com/AFLplusplus/AFLplusplus)
- **Reference:** [AFL++ Documentation](https://aflplus.plus/docs/), [Fuzzing in Depth](https://aflplus.plus/docs/fuzzing_in_depth/)

### Formatting — clang-format

[clang-format](https://clang.llvm.org/docs/ClangFormat.html) auto-formats C code. A shared style config is at [`code/.clang-format`](../../code/.clang-format) (LLVM-based, Allman braces, 2-space indent, 100 cols).

- **Install:** `sudo apt install clang-format`
- **Usage:** `clang-format -i src/*.c inc/*.h` (in-place) or `clang-format --dry-run -Werror src/*.c` (check only)
- **Reference:** [clang-format Style Options](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)

### Stack Usage Analysis

GCC reports stack depth per function when compiled with:

```makefile
CFLAGS += -fstack-usage -Wstack-usage=200
```

**Reference:** [GCC Stack Usage](https://gcc.gnu.org/onlinedocs/gnat_ugn/Stack-Usage-Analysis.html)

---

## Rust (thumbv7em-none-eabihf)

### Documentation — cargo doc / rustdoc

`cargo doc --no-deps --document-private-items` compiles documentation from `///` and `//!` doc comments to HTML.

```rust
//! # LED Blinker
//!
//! Bare-metal GPIO control for STM32F446RE.
```

**Reference:** [rustdoc Book](https://doc.rust-lang.org/rustdoc/)

### Static Analysis — Clippy

[Clippy](https://doc.rust-lang.org/clippy/) is the official Rust linter. Run with `cargo clippy -- -D warnings` to promote all warnings to errors.

**Reference:** [Clippy Documentation](https://doc.rust-lang.org/clippy/)

### Formal Verification — Kani

[Kani](https://model-checking.github.io/kani/) is an AWS-maintained bounded model checker for Rust. It proves safety and correctness properties for **all possible inputs** (within bounded loop depth).

```rust
#[cfg(kani)]
#[kani::proof]
fn check_delay_ms_bounds() {
    let ms: u32 = kani::any();
    kani::assume(ms <= 1048);
    delay_ms(ms);
}
```

**Why Kani over Creusot:** Kani proof harnesses look like tests — lower learning curve, monthly releases, AWS production backing. Creusot (deductive verification via Why3) is more powerful but requires Pearlite annotations and has a steeper learning curve. Kani is the practical choice for this course.

- **Install:** `cargo install kani-verifier && cargo kani setup`
- **Reference:** [Kani Getting Started Guide](https://model-checking.github.io/kani/getting-started.html)

### Unit Testing — cargo test

Bare-metal Rust cannot run tests on the target. Compile for the host instead:

```bash
cargo test --target x86_64-unknown-linux-gnu
```

This requires abstracting hardware register access behind traits with mock implementations for tests:

```rust
pub trait RegisterOps {
    fn write(addr: *mut u32, val: u32);
    fn read(addr: *const u32) -> u32;
}

#[cfg(test)]
pub struct MockOps;
#[cfg(test)]
impl RegisterOps for MockOps { /* store values statically */ }
```

**Reference:** [Rust Testing](https://doc.rust-lang.org/book/ch11-00-testing.html)

### Fuzz Testing — cargo fuzz / proptest

**cargo fuzz** (libFuzzer wrapper):

1. Install: `cargo install cargo-fuzz`
2. Init: `cargo fuzz init`
3. Write a harness in `fuzz_targets/`:
```rust
#![no_main]
use libfuzzer_sys::fuzz_target;
fuzz_target!(|data: &[u8]| {
    if data.len() >= 4 {
        let ms = u32::from_le_bytes(data[..4].try_into().unwrap());
        delay_ms(ms % 2000);
    }
});
```
4. Run: `cargo fuzz run <target>`

**proptest** (property-based testing, lighter weight):

```rust
use proptest::prelude::*;
proptest! {
    #[test]
    fn delay_never_panics(ms in 0..2000u32) {
        delay_ms(ms);
    }
}
```

**References:**
- [cargo fuzz Book](https://rust-fuzz.github.io/book/)
- [proptest Guide](https://proptest-rs.github.io/proptest/)

### Formatting — cargo fmt

[rustfmt](https://github.com/rust-lang/rustfmt) is the official Rust formatter, part of the toolchain.

- **Usage:** `cargo fmt` (in-place), `cargo fmt --check` (CI mode)
- **Reference:** [rustfmt Documentation](https://rust-lang.github.io/rustfmt/)

### Dependency Auditing — cargo deny

`cargo deny check` checks for security vulnerabilities, license conflicts, and duplicate deps.

- **Install:** `cargo install cargo-deny`
- **Reference:** [cargo deny](https://github.com/EmbarkStudios/cargo-deny)

### Stack Usage — cargo-call-stack

[cargo-call-stack](https://github.com/japaric/cargo-call-stack) computes worst-case stack depth for Cortex-M binaries.

- **Install & run:** `cargo install cargo-call-stack && cargo call-stack --bin led-blinker`

---

## Ada / SPARK (Forward-Looking)

> Ada and SPARK implementations are added in later projects. Tools listed for reference.

| Concern | Tool | Reference |
|---------|------|-----------|
| Documentation | [gnatdoc](https://docs.adacore.com/gnatcoll-docs-docs/) | `gnatdoc -P <project>.gpr` |
| Static / formal verification | [GNATprove](https://docs.adacore.com/gnatprove-docs/) | `gnatprove -P <project>.gpr` |
| Unit testing | [GNATtest](https://docs.adacore.com/gnattest-docs/) | `gnattest -P <project>.gpr` |
| Fuzz testing | [GNATfuzz](https://docs.adacore.com/gnatdas-docs/html/gnatfuzz/) | `gnatfuzz -P <project>.gpr` |
| Formatting | [gnatformat](https://docs.adacore.com/live/wave/gnatformat/html/user-guide/index.html) | `gnatformat -P <project>.gpr` |

**References:** [GNAT User's Guide](https://gcc.gnu.org/onlinedocs/gnat_ugn/), [SPARK Reference Manual](https://docs.adacore.com/spark2014-docs/)

---

## Zig (Forward-Looking)

> Zig implementations are added in later projects. Tools listed for reference.

| Concern | Tool | Usage |
|---------|------|-------|
| Documentation | built-in | `zig build doc` |
| Testing | built-in | `zig test src/main.zig` |
| Fuzz testing | built-in (experimental) | `zig test src/main.zig --fuzz` |
| Formatting | built-in | `zig fmt src/main.zig` |
| Static analysis | built-in | `zig build` (compile-time checks) |

**Reference:** [Zig Documentation](https://ziglang.org/documentation/master/)

---

## Cross-Cutting Tools

### EditorConfig

The [`.editorconfig`](../../.editorconfig) file at the repository root ensures consistent indentation, line endings, and encoding across all editors and contributors.

- C / H: space, 2
- Rust: space, 4
- Makefile: tab, 4
- YAML: space, 2

Install the [EditorConfig plugin](https://editorconfig.org/#download) for your editor.

### Pre-commit Hooks

The [`.pre-commit-config.yaml`](../../.pre-commit-config.yaml) at the repository root runs formatting checks before every commit. Install with `pip install pre-commit && pre-commit install`.

Checks run automatically on `git commit`:

1. `clang-format --dry-run -Werror` on `.c` / `.h` files
2. `cargo fmt --check` in Rust directories
3. `zig fmt --check` on Zig files

To bypass: `git commit --no-verify`.

**Reference:** [pre-commit.com](https://pre-commit.com/)

---

## Summary

| Concern | C | Rust | Ada / SPARK | Zig |
|---------|---|---|---|---|
| Doc gen | Doxygen | `cargo doc` | gnatdoc | `zig build doc` |
| Static analysis | cppcheck + clang-tidy | Clippy | GNATprove | `zig build` |
| Coding standard | NASA Power of 10 | — | SPARK rules | — |
| Formal verification | Frama-C (EVA + WP) | Kani (model checking) | GNATprove | — |
| Unit tests | Unity (host) | `cargo test` (host) | GNATtest | `zig test` |
| Fuzzing | AFL++ | `cargo fuzz` / proptest | GNATfuzz | `zig test --fuzz` |
| Formatting | clang-format | `cargo fmt` | gnatformat | `zig fmt` |
| Stack analysis | `-fstack-usage` | `cargo-call-stack` | GNAT stack check | — |
| Dependency audit | — | `cargo deny` | — | — |

The next chapter, [00f-ci-cd](00f-ci-cd.md), shows how these tools are automated in CI/CD.
