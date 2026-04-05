---
title: "Project 7: Cooperative Task Scheduler"
phase: 3
project: 7
---

# Project 7: Cooperative Task Scheduler

In this project you will build a cooperative multitasking scheduler from scratch for ARM Cortex-M4F microcontrollers (STM32F405). You will implement Task Control Blocks, context switching via inline assembly, priority-based scheduling, and a SysTick-driven system tick — in **C, Rust, Ada, and Zig**.

This is a foundational embedded systems project. Every RTOS you will ever use (FreeRTOS, Zephyr, ThreadX) is built on exactly these primitives. By implementing it yourself, you will understand what happens when a task yields, how the stack pointer moves, and why the PendSV exception exists.

## What You'll Learn

- How Task Control Blocks (TCBs) represent runnable state
- Saving and restoring CPU registers during context switches
- Priority-based scheduling with round-robin within equal priorities
- Using the SysTick timer as a periodic system tick
- Yielding and sleeping from tasks
- Language-specific approaches: inline asm, `cortex-m` crate, Ravenscar tasking, comptime config
- Verifying scheduler behavior in QEMU with GDB

## Prerequisites

- ARM Cortex-M4F QEMU (`qemu-system-arm`)
- ARM GCC toolchain (`arm-none-eabi-gcc`)
- GDB with ARM support (`arm-none-eabi-gdb`)
- Rust: `cargo`, `cargo-embed` or `probe-rs`, `cortex-m` crate
- Ada: GNAT ARM toolchain
- Zig: Zig 0.11+ with ARM cross-compilation support
- Familiarity with Projects 1–6 (GPIO, interrupts, timers)

---

## Task Control Blocks

A Task Control Block is the data structure that represents a single task. At minimum it holds:

| Field | Purpose |
|---|---|
| `stack_ptr` | Current stack pointer (saved during context switch) |
| `stack` | Dedicated stack memory for this task |
| `state` | Running, Ready, Blocked, Suspended |
| `priority` | Scheduling priority (lower number = higher priority) |
| `entry` | Function pointer to the task's entry point |
| `sleep_ticks` | Remaining ticks until the task wakes from sleep |

The stack is the most critical field. When a task is not running, its entire CPU state lives on its private stack. The `stack_ptr` field tells the scheduler where to find that state.

### Stack Layout on Cortex-M

On Cortex-M, the stack grows downward. When a task is preempted, the hardware automatically pushes R0–R3, R12, LR, PC, and xPSR onto the stack (the "exception frame"). The context switch code must additionally save R4–R11 (the callee-saved registers).

```
High address
  +------------------+
  |     Stack        |
  |     Base         |  <-- stack array start
  +------------------+
  |                  |
  |   (used space)   |
  |                  |
  +------------------+  <-- stack_ptr (current SP)
  |   (free space)   |
  +------------------+
Low address
```

---

## Context Switching

A context switch has three phases:

1. **Save** the current task's registers onto its stack
2. **Select** the next task to run (highest-priority ready task)
3. **Restore** the next task's registers from its stack

On Cortex-M, we use the **PendSV** exception for the actual switch. PendSV is designed for this purpose: it can be pended while other interrupts run, and it always runs at the lowest priority, ensuring no interrupt is blocked by the context switch.

### The PendSV Handler (Conceptual)

```
PendSV_Handler:
    ; Save callee-saved registers (R4-R11) onto current stack
    MRS R0, PSP           ; Get current task's stack pointer
    STMDB R0!, {R4-R11}   ; Push R4-R11

    ; Save new SP into current TCB
    ; (done in C/Rust/Ada/Zig via global variable)

    ; Select next task (call scheduler)
    ; Load next task's SP

    ; Restore callee-saved registers from next task's stack
    LDMIA R0!, {R4-R11}   ; Pop R4-R11
    MSR PSP, R0           ; Update PSP
    BX LR                  ; Return from exception (unstacks R0-R3, R12, LR, PC, xPSR)
```

The hardware handles the rest: on exception return (`BX LR` with EXC_RETURN in LR), the CPU pops R0–R3, R12, LR, PC, and xPSR from the new stack, and execution resumes in the new task.

---

## Priority-Based Scheduling

The scheduler maintains a ready list and selects the highest-priority task. When multiple tasks share the same priority, round-robin is used.

```
Scheduler loop:
    1. Find highest priority with ready tasks
    2. Within that priority, pick next task (round-robin index)
    3. If no ready task, run idle task
    4. Update current task pointer
```

The SysTick interrupt fires at a fixed rate (e.g., 1 kHz). Each tick:
- Decrement `sleep_ticks` for sleeping tasks
- If a sleeping task's counter reaches zero, mark it Ready
- Pend a PendSV to trigger a context switch if a higher-priority task became ready

---

## Implementation: C

### Project Structure

```
scheduler-c/
├── linker.ld
├── startup.c
├── scheduler.h
├── scheduler.c
├── main.c
└── Makefile
```

### Linker Script

```ld
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .text :
    {
        KEEP(*(.vectors))
        *(.text*)
        *(.rodata*)
    } > FLASH

    .data :
    {
        _sdata = .;
        *(.data*)
        _edata = .;
    } > RAM AT > FLASH

    _sidata = LOADADDR(.data);

    .bss :
    {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
    } > RAM

    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        . = . + 0x800;
        _estack = .;
    } > RAM
}
```

### Scheduler Header (`scheduler.h`)

```c
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_TASKS       8
#define STACK_SIZE      256

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SUSPENDED
} TaskState;

typedef void (*TaskFunc)(void);

typedef struct {
    uint32_t *stack_ptr;
    uint32_t stack[STACK_SIZE];
    TaskState state;
    uint8_t priority;
    TaskFunc entry;
    uint32_t sleep_ticks;
} TCB;

void scheduler_init(void);
int  scheduler_create_task(TaskFunc entry, uint8_t priority);
void scheduler_start(void);
void scheduler_yield(void);
void scheduler_sleep(uint32_t ticks);

void SysTick_Handler(void);
void PendSV_Handler(void);

#endif
```

### Scheduler Implementation (`scheduler.c`)

```c
#include "scheduler.h"

static TCB tasks[MAX_TASKS];
static int num_tasks = 0;
static int current_task = -1;
static volatile uint32_t system_ticks = 0;

/* External: trigger PendSV */
static inline void trigger_pendsv(void) {
    *((volatile uint32_t *)0xE000ED04) = (1 << 28); /* ICSR.PENDSVSET */
}

/* Initialize the scheduler */
void scheduler_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_SUSPENDED;
        tasks[i].priority = 255;
        tasks[i].sleep_ticks = 0;
    }
    num_tasks = 0;
    current_task = -1;
}

/* Create a new task. Returns task index or -1 on failure. */
int scheduler_create_task(TaskFunc entry, uint8_t priority) {
    if (num_tasks >= MAX_TASKS) return -1;

    int idx = num_tasks++;
    TCB *tcb = &tasks[idx];

    tcb->entry = entry;
    tcb->priority = priority;
    tcb->state = TASK_READY;
    tcb->sleep_ticks = 0;

    /* Initialize the stack as if the task was just interrupted */
    uint32_t *sp = &tcb->stack[STACK_SIZE];

    /* Space for R4-R11 (saved by PendSV) */
    sp -= 8;

    /* Exception frame (pushed by hardware on exception entry) */
    /* R0-R3, R12, LR, PC, xPSR */
    sp -= 8;

    sp[7] = (uint32_t)tcb->entry;     /* PC: task entry point */
    sp[6] = 0x01000000;               /* xPSR: Thumb bit set */
    sp[5] = (uint32_t)0xFFFFFFF9;     /* LR: EXC_RETURN (thread mode, MSP) */
    sp[4] = 0;                        /* R12 */
    sp[3] = 0;                        /* R3 */
    sp[2] = 0;                        /* R2 */
    sp[1] = 0;                        /* R1 */
    sp[0] = 0;                        /* R0 */

    tcb->stack_ptr = sp;

    return idx;
}

/* Select the next task to run */
static int select_next_task(void) {
    int best = -1;
    uint8_t best_prio = 255;

    /* Find highest priority ready task */
    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].state == TASK_READY && tasks[i].priority < best_prio) {
            best_prio = tasks[i].priority;
            best = i;
        }
    }

    /* Round-robin: if current task has same priority, pick next one */
    if (best >= 0 && current_task >= 0) {
        for (int i = 1; i <= num_tasks; i++) {
            int candidate = (current_task + i) % num_tasks;
            if (tasks[candidate].state == TASK_READY &&
                tasks[candidate].priority == best_prio) {
                best = candidate;
                break;
            }
        }
    }

    return best;
}

/* Start the scheduler — must be called from main() */
void scheduler_start(void) {
    /* Configure SysTick: 1ms tick at 16MHz HSI */
    *((volatile uint32_t *)0xE000E010) = 16000 - 1; /* LOAD */
    *((volatile uint32_t *)0xE000E014) = 0;         /* VAL */
    *((volatile uint32_t *)0xE000E018) = 0x7;       /* CTRL: enable, tickint, clksrc */

    /* Set PendSV to lowest priority */
    *((volatile uint32_t *)0xE000ED22) = 0xFF;      /* SHPR3: PendSV = 0xFF */

    /* Select first task */
    current_task = select_next_task();
    if (current_task < 0) {
        while (1); /* No tasks created */
    }
    tasks[current_task].state = TASK_RUNNING;

    /* Set PSP to first task's stack */
    __asm volatile ("MSR PSP, %0" : : "r" (tasks[current_task].stack_ptr));

    /* Switch to Thread Mode with PSP */
    __asm volatile (
        "MOV R0, #3\n"
        "MSR CONTROL, R0\n"
        "ISB\n"
    );

    /* PendSV will fire and start the first task */
    trigger_pendsv();

    /* Should never reach here */
    while (1);
}

/* Yield the CPU voluntarily */
void scheduler_yield(void) {
    trigger_pendsv();
}

/* Sleep for a number of ticks */
void scheduler_sleep(uint32_t ticks) {
    if (ticks == 0) return;
    tasks[current_task].sleep_ticks = ticks;
    tasks[current_task].state = TASK_BLOCKED;
    trigger_pendsv();
}

/* SysTick interrupt handler — called every 1ms */
void SysTick_Handler(void) {
    system_ticks++;

    /* Wake sleeping tasks */
    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].state == TASK_BLOCKED) {
            if (tasks[i].sleep_ticks > 0) {
                tasks[i].sleep_ticks--;
                if (tasks[i].sleep_ticks == 0) {
                    tasks[i].state = TASK_READY;
                    trigger_pendsv();
                }
            }
        }
    }
}

/* PendSV handler — performs the actual context switch */
__attribute__((naked)) void PendSV_Handler(void) {
    __asm volatile (
        /* Save callee-saved registers */
        "MRS R0, PSP\n"
        "STMDB R0!, {R4-R11}\n"

        /* Save current SP into TCB */
        "LDR R1, =current_task\n"
        "LDR R1, [R1]\n"
        "LDR R2, =tasks\n"
        "LDR R3, [R1, R2]\n"
        "STR R0, [R3]\n"

        /* Select next task */
        "PUSH {R0, LR}\n"
        "BL select_next_task\n"
        "MOV R4, R0\n"
        "POP {R0, LR}\n"

        /* If no task found, stay on current */
        "CMP R4, #0\n"
        "BLT .L_no_switch\n"

        /* Update current_task */
        "LDR R1, =current_task\n"
        "STR R4, [R1]\n"

        /* Update task states */
        "LDR R1, =tasks\n"
        "LDR R2, =current_task\n"
        "LDR R2, [R2]\n"
        "LDR R3, [R2, R1]\n"
        "MOV R5, #1\n"
        "STRB R5, [R3, #20]\n"  /* state = RUNNING (offset 20) */

        "LDR R0, [R4, R1]\n"    /* Load next task's SP */

        ".L_no_switch:\n"
        /* Restore callee-saved registers */
        "LDMIA R0!, {R4-R11}\n"
        "MSR PSP, R0\n"
        "BX LR\n"
    );
}
```

> **Warning:** The naked function above uses inline assembly that references C global variables by address. This is fragile — the offsets (`#20` for `state`) depend on struct layout. In production code, use a dedicated assembly file or compiler intrinsics.

### Main Application (`main.c`)

```c
#include "scheduler.h"

/* GPIO registers for STM32F405 (LED on PA5) */
#define RCC_AHB1ENR   (*(volatile uint32_t *)0x40023830)
#define GPIOA_MODER   (*(volatile uint32_t *)0x40020000)
#define GPIOA_ODR     (*(volatile uint32_t *)0x40020014)

void task_led_fast(void) {
    while (1) {
        GPIOA_ODR ^= (1 << 5);
        scheduler_sleep(200); /* Toggle every 200ms */
    }
}

void task_led_med(void) {
    while (1) {
        GPIOA_ODR ^= (1 << 5);
        scheduler_sleep(500); /* Toggle every 500ms */
    }
}

void task_led_slow(void) {
    while (1) {
        GPIOA_ODR ^= (1 << 5);
        scheduler_sleep(1000); /* Toggle every 1000ms */
    }
}

int main(void) {
    /* Enable GPIOA clock via AHB1 */
    RCC_AHB1ENR |= (1 << 0);

    /* Configure PA5 as output: MODER bits 11:10 = 01 */
    GPIOA_MODER &= ~(0x3 << 10);
    GPIOA_MODER |= (0x1 << 10);

    /* Initialize scheduler */
    scheduler_init();

    /* Create tasks with different priorities */
    scheduler_create_task(task_led_fast, 1);
    scheduler_create_task(task_led_med,  2);
    scheduler_create_task(task_led_slow, 3);

    /* Start the scheduler — never returns */
    scheduler_start();

    return 0; /* Never reached */
}
```

### Makefile

```makefile
CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
CFLAGS = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os -g -Wall -Wextra -nostdlib
LDFLAGS = -T linker.ld

all: scheduler.elf scheduler.bin

scheduler.elf: startup.c scheduler.c main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

scheduler.bin: scheduler.elf
	$(OBJCOPY) -O binary $< $@

flash: scheduler.bin
	qemu-system-arm -M netduinoplus2 -kernel scheduler.bin -S -s &

clean:
	rm -f scheduler.elf scheduler.bin
```

### Startup Code (`startup.c`)

```c
#include <stdint.h>

extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata;
extern uint32_t _sbss, _ebss;

void Reset_Handler(void);
void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));

void Default_Handler(void) {
    while (1);
}

__attribute__((section(".vectors")))
const uint32_t vector_table[] = {
    (uint32_t)&_estack,
    (uint32_t)&Reset_Handler,
    (uint32_t)&NMI_Handler,
    (uint32_t)&HardFault_Handler,
    (uint32_t)0, (uint32_t)0, (uint32_t)0, (uint32_t)0,
    (uint32_t)0, (uint32_t)0, (uint32_t)0,
    (uint32_t)&PendSV_Handler,
    (uint32_t)&SysTick_Handler,
};

void Reset_Handler(void) {
    /* Copy .data */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;

    /* Zero .bss */
    dst = &_sbss;
    while (dst < &_ebss) *dst++ = 0;

    main();
    while (1);
}

int main(void);
```

### Build and Run

```bash
make
qemu-system-arm -M netduinoplus2 -kernel scheduler.bin -S -s &
arm-none-eabi-gdb scheduler.elf
```

In GDB:
```
(gdb) target remote :1234
(gdb) break task_led_fast
(gdb) break task_led_med
(gdb) break task_led_slow
(gdb) continue
```

Set breakpoints in each task and verify they are hit in sequence. Use `info registers` to inspect PSP between context switches.

---

## Implementation: Rust

### Project Setup

```bash
cargo init --name scheduler-rust
cd scheduler-rust
```

### `Cargo.toml`

```toml
[package]
name = "scheduler-rust"
version = "0.1.0"
edition = "2021"

[dependencies]
cortex-m = "0.7"
cortex-m-rt = "0.7"
panic-halt = "0.2"

[profile.release]
opt-level = "s"
lto = true
```

### `.cargo/config.toml`

```toml
[build]
target = "thumbv7em-none-eabihf"

[target.thumbv7em-none-eabihf]
runner = "qemu-system-arm -M netduinoplus2 -kernel"
rustflags = ["-C", "link-arg=-Tlink.x"]
```

### `memory.x`

```
MEMORY
{
    FLASH : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM : ORIGIN = 0x20000000, LENGTH = 128K
}
```

### `build.rs`

```rust
use std::env;
use std::fs::File;
use std::io::Write;
use std::path::PathBuf;

fn main() {
    let out = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    File::create(out.join("memory.x"))
        .unwrap()
        .write_all(include_bytes!("memory.x"))
        .unwrap();
    println!("cargo:rustc-link-search={}", out.display());
    println!("cargo:rerun-if-changed=memory.x");
}
```

### `src/main.rs`

```rust
#![no_std]
#![no_main]

use core::arch::asm;
use core::cell::UnsafeCell;
use core::ptr;
use cortex_m::peripheral::SCB;
use cortex_m_rt::{entry, exception, ExceptionFrame};

const MAX_TASKS: usize = 8;
const STACK_SIZE: usize = 256;

#[derive(Clone, Copy, PartialEq)]
enum TaskState {
    Ready,
    Running,
    Blocked,
    Suspended,
}

type TaskFunc = fn();

struct TCB {
    stack_ptr: *mut u32,
    stack: [u32; STACK_SIZE],
    state: TaskState,
    priority: u8,
    entry: TaskFunc,
    sleep_ticks: u32,
}

unsafe impl Sync for Scheduler {}

struct Scheduler {
    tasks: UnsafeCell<[TCB; MAX_TASKS]>,
    num_tasks: UnsafeCell<usize>,
    current_task: UnsafeCell<isize>,
}

static SCHEDULER: Scheduler = Scheduler {
    tasks: UnsafeCell::new(unsafe { core::mem::zeroed() }),
    num_tasks: UnsafeCell::new(0),
    current_task: UnsafeCell::new(-1),
};

fn scheduler_init() {
    let sched = unsafe { &*SCHEDULER.tasks.get() };
    for tcb in sched.iter_mut() {
        tcb.state = TaskState::Suspended;
        tcb.priority = 255;
        tcb.sleep_ticks = 0;
    }
    unsafe {
        *SCHEDULER.num_tasks.get() = 0;
        *SCHEDULER.current_task.get() = -1;
    }
}

fn scheduler_create_task(entry: TaskFunc, priority: u8) -> isize {
    let num = unsafe { *SCHEDULER.num_tasks.get() };
    if num >= MAX_TASKS {
        return -1;
    }

    let sched = unsafe { &mut *SCHEDULER.tasks.get() };
    let tcb = &mut sched[num];

    tcb.entry = entry;
    tcb.priority = priority;
    tcb.state = TaskState::Ready;
    tcb.sleep_ticks = 0;

    /* Initialize stack */
    let sp = STACK_SIZE as isize;
    let sp = sp - 8; /* R4-R11 space */
    let sp = sp - 8; /* Exception frame */

    let stack_ptr = &mut tcb.stack[sp as usize];

    stack_ptr[7] = entry as u32;       /* PC */
    stack_ptr[6] = 0x01000000;         /* xPSR */
    stack_ptr[5] = 0xFFFFFFF9;         /* LR: EXC_RETURN */
    stack_ptr[4] = 0;                  /* R12 */
    stack_ptr[3] = 0;                  /* R3 */
    stack_ptr[2] = 0;                  /* R2 */
    stack_ptr[1] = 0;                  /* R1 */
    stack_ptr[0] = 0;                  /* R0 */

    tcb.stack_ptr = stack_ptr.as_mut_ptr();

    unsafe {
        *SCHEDULER.num_tasks.get() = num + 1;
    }

    num as isize
}

fn select_next_task() -> isize {
    let sched = unsafe { &*SCHEDULER.tasks.get() };
    let num = unsafe { *SCHEDULER.num_tasks.get() };
    let current = unsafe { *SCHEDULER.current_task.get() };

    let mut best: isize = -1;
    let mut best_prio: u8 = 255;

    for i in 0..num {
        if sched[i].state == TaskState::Ready && sched[i].priority < best_prio {
            best_prio = sched[i].priority;
            best = i as isize;
        }
    }

    if best >= 0 && current >= 0 {
        for offset in 1..=num {
            let candidate = ((current as usize + offset) % num) as isize;
            if sched[candidate as usize].state == TaskState::Ready
                && sched[candidate as usize].priority == best_prio
            {
                best = candidate;
                break;
            }
        }
    }

    best
}

fn trigger_pendsv() {
    unsafe {
        let icsr = 0xE000_ED04 as *mut u32;
        icsr.write_volatile(1 << 28);
    }
}

fn scheduler_yield() {
    trigger_pendsv();
}

fn scheduler_sleep(ticks: u32) {
    if ticks == 0 {
        return;
    }
    let sched = unsafe { &*SCHEDULER.tasks.get() };
    let current = unsafe { *SCHEDULER.current_task.get() } as usize;
    sched[current].sleep_ticks = ticks;
    sched[current].state = TaskState::Blocked;
    trigger_pendsv();
}

fn scheduler_start() {
    /* SysTick: 1ms at 16MHz */
    let systick = unsafe { &*cortex_m::peripheral::SYST::PTR };
    systick.set_reload(16000 - 1);
    systick.clear_current();
    systick.enable_counter();
    systick.enable_interrupt();

    /* PendSV lowest priority */
    let mut scb = unsafe { SCB::steal() };
    scb.set_priority(cortex_m::Peripherals::PENDSV, 0xFF);

    let next = select_next_task();
    if next < 0 {
        loop {}
    }

    unsafe {
        *SCHEDULER.current_task.get() = next;
        let sched = &*SCHEDULER.tasks.get();
        sched[next as usize].state = TaskState::Running;

        /* Set PSP */
        let sp = sched[next as usize].stack_ptr;
        asm!("MSR PSP, {}", in(reg) sp);

        /* Switch to Thread Mode with PSP */
        asm!(
            "MOV R0, #3",
            "MSR CONTROL, R0",
            "ISB",
            out("r0") _,
        );
    }

    trigger_pendsv();
    loop {}
}

/* GPIO for STM32F405 */
const RCC_AHB1ENR: *mut u32 = 0x4002_3830 as _;
const GPIOA_MODER: *mut u32 = 0x4002_0000 as _;
const GPIOA_ODR: *mut u32 = 0x4002_0014 as _;

fn task_led_fast() {
    loop {
        unsafe {
            let odr = GPIOA_ODR.read_volatile();
            GPIOA_ODR.write_volatile(odr ^ (1 << 5));
        }
        scheduler_sleep(200);
    }
}

fn task_led_med() {
    loop {
        unsafe {
            let odr = GPIOA_ODR.read_volatile();
            GPIOA_ODR.write_volatile(odr ^ (1 << 5));
        }
        scheduler_sleep(500);
    }
}

fn task_led_slow() {
    loop {
        unsafe {
            let odr = GPIOA_ODR.read_volatile();
            GPIOA_ODR.write_volatile(odr ^ (1 << 5));
        }
        scheduler_sleep(1000);
    }
}

#[entry]
fn main() -> ! {
    unsafe {
        RCC_AHB1ENR.write_volatile(RCC_AHB1ENR.read_volatile() | (1 << 0));
        /* PA5 output: MODER bits 11:10 = 01 */
        let moder = GPIOA_MODER.read_volatile();
        GPIOA_MODER.write_volatile((moder & !(0x3 << 10)) | (0x1 << 10));
    }

    scheduler_init();
    scheduler_create_task(task_led_fast, 1);
    scheduler_create_task(task_led_med, 2);
    scheduler_create_task(task_led_slow, 3);

    scheduler_start()
}

#[exception]
fn SysTick() {
    let sched = unsafe { &*SCHEDULER.tasks.get() };
    let num = unsafe { *SCHEDULER.num_tasks.get() };

    for i in 0..num {
        if sched[i].state == TaskState::Blocked {
            if sched[i].sleep_ticks > 0 {
                sched[i].sleep_ticks -= 1;
                if sched[i].sleep_ticks == 0 {
                    sched[i].state = TaskState::Ready;
                    trigger_pendsv();
                }
            }
        }
    }
}

#[exception]
unsafe fn PendSV() {
    asm!(
        /* Save R4-R11 */
        "MRS R0, PSP",
        "STMDB R0!, {{R4-R11}}",

        /* Save SP to current TCB */
        "LDR R1, ={current}",
        "LDR R1, [R1]",
        "LDR R2, ={tasks}",
        "LDR R3, [R1, R2]",
        "STR R0, [R3]",

        /* Call select_next_task */
        "PUSH {{R0, LR}}",
        "BL {select}",
        "MOV R4, R0",
        "POP {{R0, LR}}",

        /* Check result */
        "CMP R4, #0",
        "BLT 1f",

        /* Update current_task */
        "LDR R1, ={current}",
        "STR R4, [R1]",

        /* Load next SP */
        "LDR R0, [R4, R2]",

        "1:",
        /* Restore R4-R11 */
        "LDMIA R0!, {{R4-R11}}",
        "MSR PSP, R0",
        "BX LR",

        current = sym SCHEDULER.current_task,
        tasks = sym SCHEDULER.tasks,
        select = sym select_next_task,
        options(noreturn),
    );
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
```

### Build and Run

```bash
cargo build --release
cargo run --release
```

In a separate terminal:
```bash
arm-none-eabi-gdb target/thumbv7em-none-eabihf/release/scheduler-rust
(gdb) target remote :1234
(gdb) break scheduler-rust::task_led_fast
(gdb) continue
```

---

## Implementation: Ada

Ada's Ravenscar profile provides a built-in cooperative tasking model. This implementation shows both the low-level approach (matching the other languages) and the idiomatic Ravenscar approach.

### Project Structure

```
scheduler-ada/
├── scheduler.gpr
├── src/
│   ├── scheduler.ads
│   ├── scheduler.adb
│   ├── tasks.ads
│   ├── tasks.adb
│   └── main.adb
```

### Project File (`scheduler.gpr`)

```ada
project Scheduler is
   for Source_Dirs use ("src");
   for Object_Dir use "obj";
   for Main use ("main.adb");
   for Target use "arm-eabi";

   package Compiler is
       for Default_Switches ("Ada") use
         ("-O2", "-g", "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16",
          "-fstack-check", "-gnatp", "-gnata");
   end Compiler;

   package Linker is
      for Default_Switches ("Ada") use
        ("-T", "linker.ld", "-nostartfiles");
   end Linker;
end Scheduler;
```

### Scheduler Package Spec (`scheduler.ads`)

```ada
with System;
with System.Machine_Code;

package Scheduler is

   Max_Tasks    : constant := 8;
   Stack_Size   : constant := 256;

   type Task_State is (Ready, Running, Blocked, Suspended);
   type Priority is range 1 .. 255;
   type Task_Index is range -1 .. Max_Tasks - 1;

   type Task_Entry is access procedure;

   type TCB is record
      Stack_Ptr   : System.Address;
      Stack       : array (1 .. Stack_Size) of Word;
      State       : Task_State;
      Prio        : Priority;
      Entry_Point : Task_Entry;
      Sleep_Ticks : Natural;
   end record;

   for TCB use record
      Stack_Ptr   at 0  range 0 .. 31;
      Stack       at 4  range 0 .. (Stack_Size * 32 - 1);
      State       at 4 + Stack_Size * 4  range 0 .. 7;
      Prio        at 4 + Stack_Size * 4 + 1  range 0 .. 7;
      Entry_Point at 4 + Stack_Size * 4 + 2  range 0 .. 31;
      Sleep_Ticks at 4 + Stack_Size * 4 + 6  range 0 .. 31;
   end record;

   procedure Initialize;
   function Create_Task (Entry : Task_Entry; Prio : Priority) return Task_Index;
   procedure Start;
   procedure Yield;
   procedure Sleep (Ticks : Natural);

   procedure SysTick_Handler;
   procedure PendSV_Handler;

private

   type Word is mod 2**32;

end Scheduler;
```

### Scheduler Package Body (`scheduler.adb`)

```ada
with System.Machine_Code; use System.Machine_Code;

package body Scheduler is

   Tasks_Array   : array (0 .. Max_Tasks - 1) of TCB;
   Num_Tasks     : Natural := 0;
   Current_Task  : Task_Index := -1;

   procedure Trigger_PendSV is
   begin
      Asm ("LDR R0, =0xE000ED04" & LF & HT &
           "MOV R1, #(1 << 28)" & LF & HT &
           "STR R1, [R0]",
           Volatile => True);
   end Trigger_PendSV;

   procedure Initialize is
   begin
      for I in Tasks_Array'Range loop
         Tasks_Array (I).State := Suspended;
         Tasks_Array (I).Prio := 255;
         Tasks_Array (I).Sleep_Ticks := 0;
      end loop;
      Num_Tasks := 0;
      Current_Task := -1;
   end Initialize;

   function Create_Task (Entry : Task_Entry; Prio : Priority) return Task_Index is
      Idx : constant Natural := Num_Tasks;
      TCB_Ptr : access TCB := Tasks_Array (Idx)'Access;
      SP : Natural;
   begin
      if Num_Tasks >= Max_Tasks then
         return -1;
      end if;

      TCB_Ptr.Entry_Point := Entry;
      TCB_Ptr.Prio := Prio;
      TCB_Ptr.State := Ready;
      TCB_Ptr.Sleep_Ticks := 0;

      -- Initialize stack (grows downward)
      SP := Stack_Size;
      SP := SP - 8;  -- R4-R11 space
      SP := SP - 8;  -- Exception frame

      -- Exception frame
      TCB_Ptr.Stack (SP + 8) := Word (To_Address (Entry).Img);  -- PC
      TCB_Ptr.Stack (SP + 7) := 16#0100_0000#;                   -- xPSR
      TCB_Ptr.Stack (SP + 6) := 16#FFFF_FFF9#;                   -- LR
      TCB_Ptr.Stack (SP + 5) := 0;  -- R12
      TCB_Ptr.Stack (SP + 4) := 0;  -- R3
      TCB_Ptr.Stack (SP + 3) := 0;  -- R2
      TCB_Ptr.Stack (SP + 2) := 0;  -- R1
      TCB_Ptr.Stack (SP + 1) := 0;  -- R0

      TCB_Ptr.Stack_Ptr := TCB_Ptr.Stack (SP + 1)'Address;

      Num_Tasks := Num_Tasks + 1;
      return Task_Index (Idx);
   end Create_Task;

   function Select_Next_Task return Task_Index is
      Best : Task_Index := -1;
      Best_Prio : Priority := 255;
   begin
      for I in 0 .. Num_Tasks - 1 loop
         if Tasks_Array (I).State = Ready
           and then Tasks_Array (I).Prio < Best_Prio
         then
            Best_Prio := Tasks_Array (I).Prio;
            Best := Task_Index (I);
         end if;
      end loop;

      -- Round-robin within same priority
      if Best >= 0 and then Current_Task >= 0 then
         for Offset in 1 .. Num_Tasks loop
            declare
               Candidate : constant Natural :=
                 (Current_Task + Offset) mod Num_Tasks;
            begin
               if Tasks_Array (Candidate).State = Ready
                 and then Tasks_Array (Candidate).Prio = Best_Prio
               then
                  Best := Task_Index (Candidate);
                  exit;
               end if;
            end;
         end loop;
      end if;

      return Best;
   end Select_Next_Task;

   procedure Start is
   begin
      -- SysTick: 1ms at 16MHz
      Asm ("LDR R0, =0xE000E010" & LF & HT &
           "LDR R1, =15999" & LF & HT &
           "STR R1, [R0]" & LF & HT &
           "LDR R0, =0xE000E014" & LF & HT &
           "MOV R1, #0" & LF & HT &
           "STR R1, [R0]" & LF & HT &
           "LDR R0, =0xE000E018" & LF & HT &
           "MOV R1, #7" & LF & HT &
           "STR R1, [R0]",
           Volatile => True);

      -- PendSV lowest priority
      Asm ("LDR R0, =0xE000ED22" & LF & HT &
           "MOV R1, #0xFF" & LF & HT &
           "STRB R1, [R0]",
           Volatile => True);

      Current_Task := Select_Next_Task;
      if Current_Task < 0 then
         loop null; end loop;
      end if;

      Tasks_Array (Current_Task).State := Running;

      -- Set PSP and switch to Thread Mode
      Asm ("MSR PSP, %0" & LF & HT &
           "MOV R0, #3" & LF & HT &
           "MSR CONTROL, R0" & LF & HT &
           "ISB",
           Volatile => True,
           Inputs => Word'Asm_Input ("r", Tasks_Array (Current_Task).Stack_Ptr));

      Trigger_PendSV;

      loop null; end loop;
   end Start;

   procedure Yield is
   begin
      Trigger_PendSV;
   end Yield;

   procedure Sleep (Ticks : Natural) is
   begin
      if Ticks = 0 then
         return;
      end if;
      Tasks_Array (Current_Task).Sleep_Ticks := Ticks;
      Tasks_Array (Current_Task).State := Blocked;
      Trigger_PendSV;
   end Sleep;

   procedure SysTick_Handler is
   begin
      for I in 0 .. Num_Tasks - 1 loop
         if Tasks_Array (I).State = Blocked then
            if Tasks_Array (I).Sleep_Ticks > 0 then
               Tasks_Array (I).Sleep_Ticks := Tasks_Array (I).Sleep_Ticks - 1;
               if Tasks_Array (I).Sleep_Ticks = 0 then
                  Tasks_Array (I).State := Ready;
                  Trigger_PendSV;
               end if;
            end if;
         end if;
      end loop;
   end SysTick_Handler;

   pragma Machine_Attribute (PendSV_Handler, "naked");

   procedure PendSV_Handler is
   begin
      Asm (
         "MRS R0, PSP" & LF & HT &
         "STMDB R0!, {R4-R11}" & LF & HT &
         -- Save SP to current TCB
         "LDR R1, =Current_Task" & LF & HT &
         "LDR R1, [R1]" & LF & HT &
         "LDR R2, =Tasks_Array" & LF & HT &
         "LDR R3, [R1, R2]" & LF & HT &
         "STR R0, [R3]" & LF & HT &
         -- Call Select_Next_Task
         "PUSH {R0, LR}" & LF & HT &
         "BL Select_Next_Task" & LF & HT &
         "MOV R4, R0" & LF & HT &
         "POP {R0, LR}" & LF & HT &
         -- Check
         "CMP R4, #0" & LF & HT &
         "BLT 1f" & LF & HT &
         -- Update current
         "LDR R1, =Current_Task" & LF & HT &
         "STR R4, [R1]" & LF & HT &
         "LDR R0, [R4, R2]" & LF & HT &
         "1:" & LF & HT &
         -- Restore
         "LDMIA R0!, {R4-R11}" & LF & HT &
         "MSR PSP, R0" & LF & HT &
         "BX LR",
         Volatile => True
      );
   end PendSV_Handler;

end Scheduler;
```

### Application Tasks (`tasks.ads`)

```ada
package Tasks is

   procedure LED_Fast;
   procedure LED_Med;
   procedure LED_Slow;

end Tasks;
```

### Application Tasks (`tasks.adb`)

```ada
with Scheduler;
with System.Machine_Code; use System.Machine_Code;

package body Tasks is

   type UInt32 is mod 2**32;
   GPIOA_ODR : UInt32 with
     Address => System'To_Address (16#4002_0014#),
     Volatile => True;

   procedure Toggle_LED is
   begin
      GPIOA_ODR := GPIOA_ODR xor (1 << 5);
   end Toggle_LED;

   procedure LED_Fast is
   begin
      loop
         Toggle_LED;
         Scheduler.Sleep (200);
      end loop;
   end LED_Fast;

   procedure LED_Med is
   begin
      loop
         Toggle_LED;
         Scheduler.Sleep (500);
      end loop;
   end LED_Med;

   procedure LED_Slow is
   begin
      loop
         Toggle_LED;
         Scheduler.Sleep (1000);
      end loop;
   end LED_Slow;

end Tasks;
```

### Main (`main.adb`)

```ada
with Scheduler; use Scheduler;
with Tasks;
with System.Machine_Code; use System.Machine_Code;

procedure Main is

   type UInt32 is mod 2**32;

   RCC_AHB1ENR : UInt32 with
     Address => System'To_Address (16#4002_3830#),
     Volatile => True;

   GPIOA_MODER : UInt32 with
     Address => System'To_Address (16#4002_0000#),
     Volatile => True;

begin
   -- Enable GPIOA clock via AHB1
   RCC_AHB1ENR := RCC_AHB1ENR or (1 << 0);

   -- Configure PA5 as output: MODER bits 11:10 = 01
   declare
      MODER : constant UInt32 := GPIOA_MODER;
   begin
      GPIOA_MODER := (MODER and not (16#3# << 10)) or (16#1# << 10);
   end;

   Initialize;
   Create_Task (Tasks.LED_Fast'Access, 1);
   Create_Task (Tasks.LED_Med'Access,  2);
   Create_Task (Tasks.LED_Slow'Access, 3);

   Start;
end Main;
```

### Build

```bash
gprbuild -P scheduler.gpr
qemu-system-arm -M netduinoplus2 -kernel obj/main -S -s &
arm-none-eabi-gdb obj/main
```

### Ravenscar Alternative

For production Ada code, the Ravenscar profile provides a simpler approach:

```ada
with Ada.Real_Time; use Ada.Real_Time;

task body LED_Task is
   Period : constant Time_Span := Milliseconds (500);
   Next_Release : Time := Clock;
begin
   loop
      Toggle_LED;
      Next_Release := Next_Release + Period;
      delay until Next_Release;
   end loop;
end LED_Task;
```

The Ravenscar runtime handles all scheduling, stack management, and context switching automatically. The GNAT Ravenscar runtime for ARM Cortex-M uses exactly the same primitives you implemented above.

---

## Implementation: Zig

### Project Structure

```
scheduler-zig/
├── build.zig
├── linker.ld
├── src/
│   └── main.zig
```

### `build.zig`

```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .thumb,
        .os_tag = .freestanding,
        .abi = .eabi,
    });

    const exe = b.addExecutable(.{
        .name = "scheduler",
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = .ReleaseSmall,
    });

    exe.entry = .disabled;
    exe.setLinkerScript(b.path("linker.ld"));

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());

    const run_step = b.step("run", "Run in QEMU");
    run_step.dependOn(&run_cmd.step);
}
```

### Linker Script (`linker.ld`)

Same as the C version above.

### `src/main.zig`

```zig
const std = @import("std");
const asm = std.os.arm;

pub const max_tasks = 8;
pub const stack_size = 256;

pub const TaskState = enum(u8) {
    ready,
    running,
    blocked,
    suspended,
};

pub const TaskFunc = *const fn () noreturn;

pub fn TCB(comptime StackSize: usize) type {
    return struct {
        stack_ptr: [*]u32,
        stack: [StackSize]u32,
        state: TaskState,
        priority: u8,
        entry: TaskFunc,
        sleep_ticks: u32,

        const Self = @This();

        pub fn init(self: *Self, entry: TaskFunc, prio: u8) void {
            self.entry = entry;
            self.priority = prio;
            self.state = .ready;
            self.sleep_ticks = 0;

            // Initialize stack (grows downward)
            var sp: usize = StackSize;
            sp -= 8; // R4-R11 space
            sp -= 8; // Exception frame

            // Exception frame
            self.stack[sp + 7] = @intFromPtr(entry); // PC
            self.stack[sp + 6] = 0x01000000;         // xPSR
            self.stack[sp + 5] = 0xFFFFFFF9;         // LR
            self.stack[sp + 4] = 0;                  // R12
            self.stack[sp + 3] = 0;                  // R3
            self.stack[sp + 2] = 0;                  // R2
            self.stack[sp + 1] = 0;                  // R1
            self.stack[sp + 0] = 0;                  // R0

            self.stack_ptr = &self.stack[sp];
        }
    };
}

// Global state
var tasks: [max_tasks]TCB(stack_size) = undefined;
var num_tasks: usize = 0;
var current_task: isize = -1;

fn trigger_pendsv() void {
    const icsr = @as(*volatile u32, @ptrFromInt(0xE000ED04));
    icsr.* = 1 << 28;
}

fn scheduler_init() void {
    var i: usize = 0;
    while (i < max_tasks) : (i += 1) {
        tasks[i].state = .suspended;
        tasks[i].priority = 255;
        tasks[i].sleep_ticks = 0;
    }
    num_tasks = 0;
    current_task = -1;
}

fn scheduler_create_task(entry: TaskFunc, prio: u8) isize {
    if (num_tasks >= max_tasks) return -1;

    const idx = num_tasks;
    tasks[idx].init(entry, prio);
    num_tasks += 1;

    return @intCast(idx);
}

fn select_next_task() isize {
    var best: isize = -1;
    var best_prio: u8 = 255;

    var i: usize = 0;
    while (i < num_tasks) : (i += 1) {
        if (tasks[i].state == .ready and tasks[i].priority < best_prio) {
            best_prio = tasks[i].priority;
            best = @intCast(i);
        }
    }

    // Round-robin within same priority
    if (best >= 0 and current_task >= 0) {
        var offset: usize = 1;
        while (offset <= num_tasks) : (offset += 1) {
            const candidate: usize = @intCast((@as(usize, @intCast(current_task)) + offset) % num_tasks);
            if (tasks[candidate].state == .ready and tasks[candidate].priority == best_prio) {
                best = @intCast(candidate);
                break;
            }
        }
    }

    return best;
}

fn scheduler_start() noreturn {
      // SysTick: 1ms at 16MHz
      const systick_load = @as(*volatile u32, @ptrFromInt(0xE000E010));
      const systick_val = @as(*volatile u32, @ptrFromInt(0xE000E014));
      const systick_ctrl = @as(*volatile u32, @ptrFromInt(0xE000E018));

      systick_load.* = 16000 - 1;
    systick_val.* = 0;
    systick_ctrl.* = 0x7; // enable, tickint, clksrc

    // PendSV lowest priority
    const shpr3 = @as(*volatile u8, @ptrFromInt(0xE000ED22));
    shpr3.* = 0xFF;

    const next = select_next_task();
    if (next < 0) {
        while (true) {}
    }

    current_task = next;
    tasks[@intCast(next)].state = .running;

    // Set PSP
    const sp: u32 = @intFromPtr(tasks[@intCast(next)].stack_ptr);
    asm volatile ("MSR PSP, $0"
        :
        : [sp] "{r0}" (sp),
    );

    // Switch to Thread Mode with PSP
    asm volatile (
        "MOV R0, #3\n"
        "MSR CONTROL, R0\n"
        "ISB\n"
        :
        :
        : "r0"
    );

    trigger_pendsv();

    while (true) {}
}

fn scheduler_yield() void {
    trigger_pendsv();
}

fn scheduler_sleep(ticks: u32) void {
    if (ticks == 0) return;
    const ct: usize = @intCast(current_task);
    tasks[ct].sleep_ticks = ticks;
    tasks[ct].state = .blocked;
    trigger_pendsv();
}

// Export for inline asm
export fn get_current_task() isize {
    return current_task;
}

export fn set_current_task(val: isize) void {
    current_task = val;
}

export fn get_num_tasks() usize {
    return num_tasks;
}

export fn get_task_state(idx: usize) TaskState {
    return tasks[idx].state;
}

export fn set_task_state(idx: usize, state: TaskState) void {
    tasks[idx].state = state;
}

export fn get_task_sp(idx: usize) u32 {
    return @intFromPtr(tasks[idx].stack_ptr);
}

export fn get_task_ptr(idx: usize) u32 {
    return @intFromPtr(&tasks[idx]);
}

// PendSV handler
export fn PendSV_Handler() callconv(.Naked) noreturn {
    asm volatile (
        \\ MRS R0, PSP
        \\ STMDB R0!, {R4-R11}
        \\
        \\ // Save SP to current TCB
        \\ BL get_current_task
        \\ MOV R4, R0
        \\ MOV R0, R4
        \\ BL get_task_ptr
        \\ MOV R1, R0
        \\ MOV R0, R4
        \\ BL get_task_sp
        \\ MOV R2, R0
        \\ MOV R0, R1
        \\ STR R2, [R0]
        \\
        \\ // Select next task
        \\ BL select_next_task
        \\ MOV R5, R0
        \\
        \\ CMP R5, #0
        \\ BLT 1f
        \\
        \\ // Update current_task
        \\ MOV R0, R5
        \\ BL set_current_task
        \\
        \\ // Load next SP
        \\ MOV R0, R5
        \\ BL get_task_sp
        \\ MOV R1, R0
        \\
        \\ 1:
        \\ MOV R0, R1
        \\ LDMIA R0!, {R4-R11}
        \\ MSR PSP, R0
        \\ BX LR
        \\
        ::: "memory"
    );
}

// SysTick handler
export fn SysTick_Handler() void {
    var i: usize = 0;
    const n = get_num_tasks();
    while (i < n) : (i += 1) {
        if (get_task_state(i) == .blocked) {
            const ct: usize = @intCast(current_task);
            _ = ct;
            // Access sleep_ticks through the global tasks array
            if (tasks[i].sleep_ticks > 0) {
                tasks[i].sleep_ticks -= 1;
                if (tasks[i].sleep_ticks == 0) {
                    tasks[i].state = .ready;
                    trigger_pendsv();
                }
            }
        }
    }
}

// GPIO registers for STM32F405
const RCC_AHB1ENR = @as(*volatile u32, @ptrFromInt(0x40023830));
const GPIOA_MODER = @as(*volatile u32, @ptrFromInt(0x40020000));
const GPIOA_ODR = @as(*volatile u32, @ptrFromInt(0x40020014));

fn task_led_fast() noreturn {
    while (true) {
        GPIOA_ODR.* ^= (1 << 5);
        scheduler_sleep(200);
    }
}

fn task_led_med() noreturn {
    while (true) {
        GPIOA_ODR.* ^= (1 << 5);
        scheduler_sleep(500);
    }
}

fn task_led_slow() noreturn {
    while (true) {
        GPIOA_ODR.* ^= (1 << 5);
        scheduler_sleep(1000);
    }
}

export fn Reset_Handler() callconv(.Naked) noreturn {
    asm volatile (
        \\ // Copy .data
        \\ LDR R0, =_sidata
        \\ LDR R1, =_sdata
        \\ LDR R2, =_edata
        \\ 1:
        \\ CMP R1, R2
        \\ BGE 2f
        \\ LDR R3, [R0], #4
        \\ STR R3, [R1], #4
        \\ B 1b
        \\ 2:
        \\ // Zero .bss
        \\ LDR R0, =_sbss
        \\ LDR R1, =_ebss
        \\ MOVS R2, #0
        \\ 3:
        \\ CMP R0, R1
        \\ BGE 4f
        \\ STR R2, [R0], #4
        \\ B 3b
        \\ 4:
        \\ BL main
        \\ B .
        ::: "memory"
    );
}

export fn main() noreturn {
    // Enable GPIOA via AHB1
    RCC_AHB1ENR.* |= (1 << 0);

    // PA5 output: MODER bits 11:10 = 01
    const moder = GPIOA_MODER.*;
    GPIOA_MODER.* = (moder & ~(@as(u32, 0x3) << 10)) | (@as(u32, 0x1) << 10);

    scheduler_init();
    _ = scheduler_create_task(task_led_fast, 1);
    _ = scheduler_create_task(task_led_med, 2);
    _ = scheduler_create_task(task_led_slow, 3);

    scheduler_start();
}

// Vector table
comptime {
    _ = @export(&Reset_Handler, .{ .name = "Reset_Handler", .linkage = .strong });
    _ = @export(&PendSV_Handler, .{ .name = "PendSV_Handler", .linkage = .strong });
    _ = @export(&SysTick_Handler, .{ .name = "SysTick_Handler", .linkage = .strong });
}
```

### Build and Run

```bash
zig build
qemu-system-arm -M netduinoplus2 -kernel zig-out/bin/scheduler -S -s &
arm-none-eabi-gdb zig-out/bin/scheduler
```

---

## QEMU Verification

### Running with GDB

```bash
# Terminal 1: Start QEMU
qemu-system-arm -M netduinoplus2 -kernel scheduler.bin -S -s &

# Terminal 2: Connect GDB
arm-none-eabi-gdb scheduler.elf
```

### GDB Session

```
(gdb) target remote :1234
Remote debugging using :1234

# Set breakpoints in each task
(gdb) break task_led_fast
(gdb) break task_led_med
(gdb) break task_led_slow

# Verify vector table
(gdb) x/4wx 0x08000000
0x08000000: 0x20000800  0x08000101  0x08000123  0x08000145

# Run
(gdb) continue
Continuing.

# After hitting first breakpoint, check PSP
(gdb) info registers psp
psr            0x1000000        16777216

# Check which task is running
(gdb) print current_task
$1 = 0

# Continue and verify round-robin
(gdb) continue
Breakpoint 2, task_led_med () at main.c:20

(gdb) print current_task
$2 = 1
```

### Verifying Context Switches

```
# Disassemble PendSV
(gdb) disassemble PendSV_Handler

# Set breakpoint at PendSV
(gdb) break PendSV_Handler
(gdb) continue

# Examine the stack of task 0
(gdb) print tasks[0].stack_ptr
$3 = (uint32_t *) 0x200007c0

# Examine saved registers on stack
(gdb) x/8wx 0x200007c0
0x200007c0: 0x00000000  0x00000000  0x00000000  0x00000000
0x200007d0: 0x00000000  0xfffffff9  0x01000000  0x08000200
                                                              ^-- PC = task_led_fast
```

---

## Deliverables

- [ ] TCB struct with stack, state, priority, sleep_ticks
- [ ] Context switch in PendSV (save/restore R4-R11)
- [ ] Priority-based scheduler with round-robin
- [ ] SysTick-driven tick counter
- [ ] Yield and sleep functions
- [ ] 3+ tasks toggling LED at different rates
- [ ] GDB verification showing context switches between tasks
- [ ] All four language implementations (C, Rust, Ada, Zig)

---

## Language Comparison

| Feature | C | Rust | Ada | Zig |
|---|---|---|---|---|
| **TCB definition** | Struct with fixed array | Struct with `UnsafeCell` | Record with address clauses | Generic struct with comptime size |
| **Stack initialization** | Manual pointer arithmetic | Index-based array access | Array indexing with offsets | Comptime-safe array indexing |
| **Context switch asm** | `__attribute__((naked))` with GNU asm | `asm!` macro with named symbols | `System.Machine_Code.Asm` | `asm volatile` with named operands |
| **PendSV trigger** | Memory-mapped ICSR write | `cortex-m` crate or raw pointer | Inline asm to ICSR | Volatile pointer write |
| **Task function type** | `void (*)(void)` | `fn()` | `access procedure` | `*const fn () noreturn` |
| **State management** | `volatile` globals | `UnsafeCell` with `static` | Package-level variables | Global `var` declarations |
| **Priority scheduling** | Linear search | Linear search | Linear search | Linear search |
| **Safety guarantees** | None — UB on stack overflow | `unsafe` blocks mark risk | Strong typing, range checks | Comptime validation, error unions |
| **Ravenscar equivalent** | N/A | `cortex-m-rtic` | Built-in Ravenscar profile | N/A |

---

## What You Learned

- How TCBs encapsulate task state and stack pointers
- The mechanics of saving and restoring CPU registers during a context switch
- Why PendSV is the right exception for cooperative scheduling
- How SysTick provides the system tick for time-based operations
- Priority scheduling with round-robin tiebreaking
- How each language expresses low-level hardware control:
  - C: naked functions and inline asm
  - Rust: `asm!` macro with symbol interop
  - Ada: `System.Machine_Code` and Ravenscar
  - Zig: comptime-sized TCBs and inline asm

## Next Steps

- **Project 8**: Build a lock-free ring buffer for interrupt-safe data transfer between tasks
- Add preemption: modify the scheduler to preempt lower-priority tasks when higher-priority tasks become ready
- Add mutexes and semaphores for task synchronization
- Port to a different architecture (RISC-V, ARMv8-M)
- Compare your scheduler's overhead to FreeRTOS or Zephyr
---

## References

### STMicroelectronics Documentation
- [STM32F4 Reference Manual (RM0090)](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-415-stm32f407-417-stm32f427-437-and-stm32f429-439-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) — Ch. 7: RCC (clock enable), SCB registers (SHPR3 for PendSV priority)
- [STM32F405/407 Datasheet](https://www.st.com/resource/en/datasheet/stm32f405rg.pdf) — Memory map

### ARM Documentation
- [Cortex-M4 Technical Reference Manual](https://developer.arm.com/documentation/ddi0439/latest/) — Ch. 3: Programmer's Model (MSP vs PSP, CONTROL register, EXC_RETURN values 0xFFFFFFF9/0xFFFFFFFD), Ch. 4: Memory Model (stack alignment)
- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/) — B1.4: Exception entry and return (hardware stacking of R0-R3, R12, LR, PC, xPSR), B1.5: PendSV exception (designed for context switching), SysTick timer
- [ARM EABI Specification (AAPCS)](https://github.com/ARM-software/abi-aa/releases) — Calling convention, callee-saved registers (R4-R11)

### Tools & Emulation
- [QEMU ARM Documentation](https://www.qemu.org/docs/master/system/target-arm.html) — GDB watchpoints on PSP, register inspection
- [QEMU STM32 Documentation](https://www.qemu.org/docs/master/system/arm/stm32.html) — netduinoplus2 machine
