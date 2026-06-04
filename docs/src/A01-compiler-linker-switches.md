---
title: "Compiler & Linker Switches"
phase: 6
project: 0
---

# Compiler & Linker Switches

This appendix explains common compiler and linker switches used in embedded development. It is reference material — not required to get started.

---

## Size Optimization

### -Os

Optimize for **size** rather than speed.

| Flag | Focus | Inlining | Loop Unrolling |
|------|-------|----------|----------------|
| `-O0` | Debug | None | None |
| `-O1` | Basic | Minimal | None |
| `-O2` | Speed | Moderate | Yes |
| `-O3` | Max speed | Aggressive | Aggressive |
| `-Os` | **Size** | Conservative | Avoided |

`-Os` enables most `-O2` optimizations but disables those that increase code size. It prefers shorter instruction sequences and avoids aggressive inlining.

---

### -ffunction-sections / -fdata-sections

Place each function and data item into its **own section**.

Without these flags:
```
file.o
├── .text      (all functions together)
└── .data      (all data together)
```

With these flags:
```
file.o
├── .text.func_a
├── .text.func_b
├── .data.var_x
└── .data.var_y
```

This granularity enables the linker to remove unused items individually.

---

### -Wl,--gc-sections

Pass `--gc-sections` to the linker. This enables **garbage collection of unused sections**.

The linker traces references from the entry point. Unreachable sections are discarded:

```
[.text.func_a] ✓ called from main   → KEEP
[.text.func_b] ✗ never referenced   → DISCARD
[.data.var_x]  ✓ used in func_a     → KEEP
[.data.var_y]  ✗ never referenced   → DISCARD
```

Requires `-ffunction-sections` and `-fdata-sections` during compilation.

---

### -flto

Enable **Link-Time Optimization**. The compiler emits intermediate representation instead of fully compiled code. At link time, it sees the **entire program** and optimizes globally.

```
         Without LTO                         With LTO
         
file1.c → file1.o (compiled)         file1.c → file1.o (IR)
file2.c → file2.o (compiled)         file2.c → file2.o (IR)
              ↓                                   ↓
         Linker combines               Compiler sees everything:
              ↓                         • Cross-file inlining
         final.elf                      • Constant propagation
                                        • Dead code elimination
                                              ↓
                                         final.elf
```

LTO is safe — it performs the same optimizations as within a single file, just across the entire program.

---

### How They Work Together

```
┌─────────────────────────────────────────────────────────────┐
│  STAGE 1: Compilation (-Os)                                 │
│  Generate compact code, avoid bloat                         │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  STAGE 2: Link-Time Optimization (-flto)                    │
│  Cross-file inlining, constant propagation, dead code       │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  STAGE 3: Section Garbage Collection (--gc-sections)        │
│  Remove whole unused functions and data                     │
└─────────────────────────────────────────────────────────────┘
```

| Technique | Stage | What It Eliminates |
|-----------|-------|-------------------|
| `-Os` | Compile | Bloated sequences, unnecessary unrolling |
| `-flto` | Link | Cross-file redundancy, dead branches |
| `--gc-sections` | Link | Unused functions and data |

Use all three together:

```makefile
CFLAGS  += -Os -flto -ffunction-sections -fdata-sections
LDFLAGS += -Wl,--gc-sections
```

---

## References

- [GCC Optimization Options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)
- [GCC Link-Time Optimization](https://gcc.gnu.org/wiki/LinkTimeOptimization)
- [GNU LD Options](https://sourceware.org/binutils/docs/ld/Options.html)
