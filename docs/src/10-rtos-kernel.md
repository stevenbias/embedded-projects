---
title: "Project 10: RTOS Kernel (Minimal)"
phase: 4
project: 10
---

# Project 10: RTOS Kernel (Minimal)

In this project you will build a **preemptive RTOS kernel** from scratch for ARM Cortex-M4F microcontrollers (STM32F405). Unlike Project 7's cooperative scheduler — where tasks voluntarily yielded — this kernel will forcibly preempt lower-priority tasks when higher-priority ones become ready. You will implement mutexes with **priority inheritance**, binary and counting semaphores, message queues with ring-buffer storage, and a SysTick-driven time-slicing tick, in **C, Rust, Ada, and Zig**.

This is the capstone concurrency project. Every production RTOS you will ever use (FreeRTOS, Zephyr, ThreadX, embOS) is built on exactly these primitives. By implementing them yourself, you will understand priority inversion, priority inheritance protocols, the difference between mutexes and semaphores, and how message queues avoid shared-memory races.

## What You'll Learn

- Preemptive vs cooperative scheduling: when and why the kernel interrupts a running task
- PendSV-based context switching for Cortex-M with full register save/restore
- Mutexes with priority inheritance to prevent priority inversion
- Binary semaphores (two-state) and counting semaphores (N-state)
- Message queues: ring buffer + semaphore signaling for producer/consumer
- SysTick as the periodic tick source for time slicing and timeout tracking
- Task states: Ready, Running, Blocked, Suspended — and transitions between them
- Language-specific approaches: C naked functions, Rust `Mutex<T>` with priority ceiling, Ada protected objects, Zig comptime configuration

## Prerequisites

- ARM GCC toolchain (`arm-none-eabi-gcc`)
- Rust: `cargo`, `cortex-m` crate, `cortex-m-rt` crate, `bare-metal` crate
- Ada: GNAT ARM toolchain with Ravenscar runtime
- Zig: Zig 0.11+ with ARM cross-compilation support
- QEMU with ARM support (`qemu-system-arm`)
- GDB with ARM support (`arm-none-eabi-gdb`)
- Completion of Project 7 (Cooperative Task Scheduler)

---

## Preemptive vs Cooperative Scheduling

In Project 7, tasks ran until they called `scheduler_sleep()` or `scheduler_yield()`. This is **cooperative** scheduling — a misbehaving task (infinite loop without yield) starves all others.

**Preemptive** scheduling changes this: the kernel can forcibly remove a running task from the CPU. This happens in two scenarios:

1. **Time slice expiration**: the SysTick interrupt fires, and if round-robin is enabled for equal-priority tasks, the current task is preempted.
2. **Higher-priority task becomes ready**: an ISR or another task unblocks a higher-priority task, triggering an immediate context switch.

The key difference is in the SysTick handler:

```
Cooperative (Project 7):
    SysTick → decrement sleep counters → wake tasks → pend PendSV

Preemptive (this project):
    SysTick → decrement sleep counters → wake tasks
            → check if any woken task has higher priority than current
            → if yes, pend PendSV immediately (preemption)
```

---

## PendSV-Based Context Switching

The PendSV exception is the cornerstone of Cortex-M context switching. It is designed to run at the lowest possible priority, ensuring it never blocks any other interrupt. When pended, it executes after all higher-priority ISRs complete.

### Full Context Switch Sequence

```
PendSV_Handler:
    1. Hardware pushes: R0-R3, R12, LR, PC, xPSR (automatic on exception entry)
    2. Software pushes: R4-R11 (callee-saved, not saved by hardware)
    3. Save PSP into current TCB
    4. Call scheduler to select next task
    5. Load next task's PSP
    6. Pop R4-R11
    7. BX LR → hardware pops R0-R3, R12, LR, PC, xPSR
```

The hardware handles the "exception frame" (R0-R3, R12, LR, PC, xPSR) automatically on exception entry and exit. Software only needs to save and restore the callee-saved registers (R4-R11).

### Stack Layout

```
High address
  +------------------+
  |     Stack        |
  |     Base         |
  +------------------+
  |   (used space)   |
  +------------------+  ← PSP after context switch
  |   R4             |
  |   R5             |
  |   R6             |
  |   R7             |
  |   R8             |
  |   R9             |
  |   R10            |
  |   R11            |
  +------------------+
  |   R0             |  ← pushed by hardware
  |   R1             |
  |   R2             |
  |   R3             |
  |   R12            |
  |   LR (EXC_RET)   |
  |   PC (return)    |
  |   xPSR           |
  +------------------+
Low address
```

> **Cortex-M4F FPU Note:** The STM32F405 has a hardware FPU (FPv4-SP-D16). The basic context switch above saves/restores R0-R11 only, which is sufficient for integer-only tasks. If any task uses floating-point operations, the FPU registers (S0-S15, FPSCR) must also be saved and restored. The FPU status register (FPCCR) controls lazy stacking — when enabled, the hardware automatically pushes S0-S15 and FPSCR on exception entry. For a production FPU-aware context switch, you would:
> 1. Check the EXC_RETURN[4] bit (FPCA) to determine if the task used the FPU
> 2. If set, save/restore S0-S15 and FPSCR in addition to the integer registers
> 3. Alternatively, disable lazy stacking and always save/restore FPU state
> 
> The basic context switch shown here works correctly for tasks that do not use floating point. For FPU-aware context switching, the stack layout expands to include 17 additional words (S0-S15 + FPSCR).

---

## Task States

Each task transitions between four states:

```
                    +-----------+
          create    |           |
        +---------> |  READY    | <--- unblock / wake
        |           |           |        from blocked
        |           +-----+-----+
        |                 |
        |   scheduler     | dispatch
        |   picks task    v
        |           +-----------+
        |           |           |
        |           |  RUNNING  |
        |           |           |
        |           +-----+-----+
        |          /      |      \
        |         /       |       \
        |  sleep /  block  |  yield /
        |       /  on sync |  preempt
        |      v           v       v
        |  +-----------+ +-----------+
        |  |           | |           |
        +--|  BLOCKED  | | SUSPENDED |
           |           | |           |
           +-----+-----+ +-----+-----+
                 |             |
            wake /             | suspend
            timeout            |
                 v             v
              READY         (stays here
                            until resume)
```

| State | Description |
|---|---|
| **Ready** | Task is eligible to run, waiting for the scheduler to dispatch it |
| **Running** | Task is currently executing on the CPU (only one task at a time) |
| **Blocked** | Task is waiting for an event: semaphore, mutex, queue, or timeout |
| **Suspended** | Task is explicitly paused by another task or the kernel — does not consume CPU |

---

## Mutexes with Priority Inheritance

### The Priority Inversion Problem

Priority inversion occurs when a high-priority task is blocked waiting for a resource held by a low-priority task, while a medium-priority task preempts the low-priority task, preventing it from releasing the resource.

```
Timeline without priority inheritance:

  High Prio:  |               |---- BLOCKED on mutex ----|  RUN  |
  Med  Prio:  |               |------- RUNNING (preempts low!) -------|
  Low  Prio:  |--- LOCK mutex |--- preempted ---|  RUN  | UNLOCK |
              t0              t1                 t2     t3      t4

  High waits from t1 to t4 — blocked by Med, not just Low!
```

### Priority Inheritance Protocol

When a high-priority task blocks on a mutex held by a lower-priority task, the holder **temporarily inherits** the higher priority. This prevents medium-priority tasks from preempting it.

```
Timeline with priority inheritance:

  High Prio:  |               |---- BLOCKED ----|  RUN (inherits low) |
  Med  Prio:  |               | BLOCKED (can't preempt inherited)     |
  Low  Prio:  |--- LOCK mutex |--- RUNNING (inherited HIGH prio) ---| UNLOCK |
              t0              t1                 t2                  t3

  High waits from t1 to t3 — only blocked by Low's critical section.
```

### Implementation

```c
typedef struct {
    volatile int locked;       /* 0 = free, 1 = held */
    TCB *owner;                /* Task currently holding the mutex */
    uint8_t original_prio;     /* Owner's original priority */
    TCB *wait_list;            /* Tasks waiting for this mutex */
} Mutex;

void mutex_lock(Mutex *m) {
    TCB *current = get_current_task();

    if (m->locked == 0) {
        /* Mutex is free — take it */
        m->locked = 1;
        m->owner = current;
        m->original_prio = current->priority;
        return;
    }

    if (m->owner == current) {
        return; /* Already held by current task (recursive) */
    }

    /* Mutex is held — block current task */
    current->state = TASK_BLOCKED;
    current->blocked_on = m;
    add_to_wait_list(m, current);

    /* Priority inheritance: boost owner if current has higher priority */
    if (current->priority < m->owner->priority) {
        m->owner->priority = current->priority;
    }

    trigger_pendsv(); /* Switch to another task */
}

void mutex_unlock(Mutex *m) {
    TCB *current = get_current_task();

    if (m->owner != current) return; /* Not the owner */

    /* Restore owner's original priority */
    current->priority = m->original_prio;

    m->locked = 0;
    m->owner = NULL;

    /* Wake highest-priority waiter */
    TCB *waiter = remove_highest_prio_from_wait_list(m);
    if (waiter) {
        waiter->state = TASK_READY;
        waiter->blocked_on = NULL;

        /* New owner inherits from next waiter if any */
        if (m->wait_list) {
            TCB *next = highest_prio_in_wait_list(m);
            if (next->priority < waiter->priority) {
                waiter->priority = next->priority;
            }
        }
    }

    trigger_pendsv();
}
```

---

## Semaphores

### Binary Semaphores

A binary semaphore has two states: taken (0) and available (1). It is used for task synchronization and interrupt-to-task signaling.

```c
typedef struct {
    volatile int count;      /* 0 or 1 */
    TCB *wait_list;
} BinarySemaphore;

void sem_take(BinarySemaphore *s) {
    TCB *current = get_current_task();

    if (s->count > 0) {
        s->count = 0;
        return;
    }

    current->state = TASK_BLOCKED;
    current->blocked_on = s;
    add_to_wait_list(s, current);
    trigger_pendsv();
}

void sem_give(BinarySemaphore *s) {
    TCB *waiter = remove_highest_prio_from_wait_list(s);
    if (waiter) {
        waiter->state = TASK_READY;
    } else {
        s->count = 1;
    }
    trigger_pendsv();
}
```

### Counting Semaphores

A counting semaphore tracks a resource count from 0 to N. Used for managing pools of identical resources.

```c
typedef struct {
    volatile int count;      /* 0 to MAX_COUNT */
    int max_count;
    TCB *wait_list;
} CountingSemaphore;

void sem_take(CountingSemaphore *s) {
    TCB *current = get_current_task();

    if (s->count > 0) {
        s->count--;
        return;
    }

    current->state = TASK_BLOCKED;
    current->blocked_on = s;
    add_to_wait_list(s, current);
    trigger_pendsv();
}

void sem_give(CountingSemaphore *s) {
    TCB *waiter = remove_highest_prio_from_wait_list(s);
    if (waiter) {
        waiter->state = TASK_READY;
    } else if (s->count < s->max_count) {
        s->count++;
    }
    trigger_pendsv();
}
```

---

## Message Queues

A message queue combines a ring buffer for data storage with a counting semaphore for signaling. Producers write to the ring buffer and signal the semaphore; consumers wait on the semaphore and read from the buffer.

```
  +---+---+---+---+---+---+
  | D |   |   |   |   |   |  Ring buffer (6 slots)
  +---+---+---+---+---+---+
    ^                   ^
   read                write
   idx=1               idx=2

  Semaphore count = 1 (one message available)
```

```c
#define MSG_QUEUE_SIZE 16
#define MSG_MAX_SIZE   32

typedef struct {
    uint8_t buffer[MSG_QUEUE_SIZE][MSG_MAX_SIZE];
    volatile int head;       /* Write index */
    volatile int tail;       /* Read index */
    volatile int count;      /* Messages in queue */
    CountingSemaphore sem;   /* Signals available messages */
} MessageQueue;

int queue_send(MessageQueue *q, const void *data, size_t len) {
    if (len > MSG_MAX_SIZE || q->count >= MSG_QUEUE_SIZE) {
        return -1; /* Full */
    }

    memcpy(q->buffer[q->head], data, len);
    q->head = (q->head + 1) % MSG_QUEUE_SIZE;
    q->count++;

    sem_give(&q->sem);
    return 0;
}

int queue_receive(MessageQueue *q, void *data, size_t max_len) {
    sem_take(&q->sem); /* Blocks if empty */

    size_t len = max_len < MSG_MAX_SIZE ? max_len : MSG_MAX_SIZE;
    memcpy(data, q->buffer[q->tail], len);
    q->tail = (q->tail + 1) % MSG_QUEUE_SIZE;
    q->count--;

    return (int)len;
}
```

---

## Implementation: C

### Project Structure

```
rtos-c/
├── linker.ld
├── startup.c
├── rtos.h
├── rtos.c
├── main.c
└── Makefile
```

### Linker Script (`linker.ld`)

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
        . = . + 0x1000;
        _estack = .;
    } > RAM
}
```

### RTOS Header (`rtos.h`)

```c
#ifndef RTOS_H
#define RTOS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_TASKS       16
#define STACK_SIZE      512
#define MSG_QUEUE_SIZE  16
#define MSG_MAX_SIZE    32

/* Task states */
typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SUSPENDED
} TaskState;

typedef void (*TaskFunc)(void *arg);

/* Mutex with priority inheritance */
typedef struct Mutex {
    volatile int locked;
    struct TCB *owner;
    uint8_t original_prio;
    struct TCB *wait_list;
    struct Mutex *next;
} Mutex;

/* Counting semaphore */
typedef struct Semaphore {
    volatile int count;
    int max_count;
    struct TCB *wait_list;
    struct Semaphore *next;
} Semaphore;

/* Message queue */
typedef struct {
    uint8_t buffer[MSG_QUEUE_SIZE][MSG_MAX_SIZE];
    volatile int head;
    volatile int tail;
    volatile int count;
    Semaphore sem;
} MessageQueue;

/* Task Control Block */
typedef struct TCB {
    uint32_t *stack_ptr;
    uint32_t stack[STACK_SIZE];
    TaskState state;
    uint8_t priority;
    uint8_t base_priority;
    TaskFunc entry;
    void *arg;
    uint32_t sleep_ticks;
    void *blocked_on;
    struct TCB *next;
    struct TCB *wait_next;
} TCB;

/* Kernel functions */
void rtos_init(void);
int  rtos_create_task(TaskFunc entry, void *arg, uint8_t priority);
void rtos_start(void) __attribute__((noreturn));
void rtos_yield(void);
void rtos_sleep(uint32_t ticks);
void rtos_suspend(int task_id);
void rtos_resume(int task_id);

/* Synchronization */
void mutex_init(Mutex *m);
void mutex_lock(Mutex *m);
void mutex_unlock(Mutex *m);

void sem_init(Semaphore *s, int initial, int max);
void sem_take(Semaphore *s);
void sem_give(Semaphore *s);

void queue_init(MessageQueue *q);
int  queue_send(MessageQueue *q, const void *data, size_t len);
int  queue_receive(MessageQueue *q, void *data, size_t max_len);

/* Accessors */
TCB *rtos_current_task(void);
uint32_t rtos_tick_count(void);

/* Exception handlers */
void SysTick_Handler(void);
void PendSV_Handler(void);

#endif
```

### RTOS Implementation (`rtos.c`)

```c
#include "rtos.h"
#include <string.h>

static TCB tasks[MAX_TASKS];
static int num_tasks = 0;
static int current_task = -1;
static volatile uint32_t system_ticks = 0;

/* ICSR register for PendSV */
#define ICSR    (*(volatile uint32_t *)0xE000ED04)
#define ICSR_PENDSVSET (1U << 28)

static inline void trigger_pendsv(void) {
    ICSR = ICSR_PENDSVSET;
}

TCB *rtos_current_task(void) {
    if (current_task < 0) return NULL;
    return &tasks[current_task];
}

uint32_t rtos_tick_count(void) {
    return system_ticks;
}

/* Initialize the RTOS kernel */
void rtos_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_SUSPENDED;
        tasks[i].priority = 255;
        tasks[i].base_priority = 255;
        tasks[i].sleep_ticks = 0;
        tasks[i].blocked_on = NULL;
        tasks[i].next = NULL;
        tasks[i].wait_next = NULL;
    }
    num_tasks = 0;
    current_task = -1;
}

/* Create a new task */
int rtos_create_task(TaskFunc entry, void *arg, uint8_t priority) {
    if (num_tasks >= MAX_TASKS) return -1;

    int idx = num_tasks++;
    TCB *tcb = &tasks[idx];

    tcb->entry = entry;
    tcb->arg = arg;
    tcb->base_priority = priority;
    tcb->priority = priority;
    tcb->state = TASK_READY;
    tcb->sleep_ticks = 0;
    tcb->blocked_on = NULL;
    tcb->next = NULL;
    tcb->wait_next = NULL;

    /* Initialize stack — grows downward */
    uint32_t *sp = &tcb->stack[STACK_SIZE];

    /* Space for R4-R11 (saved by PendSV) */
    sp -= 8;

    /* Exception frame (pushed by hardware) */
    sp -= 8;

    sp[7] = (uint32_t)entry;        /* PC */
    sp[6] = 0x01000000;             /* xPSR: Thumb bit */
    sp[5] = 0xFFFFFFFD;             /* LR: EXC_RETURN (thread mode, PSP) */
    sp[4] = (uint32_t)arg;          /* R0: task argument */
    sp[3] = 0;                      /* R1 */
    sp[2] = 0;                      /* R2 */
    sp[1] = 0;                      /* R3 */
    sp[0] = 0;                      /* R12 */

    tcb->stack_ptr = sp;

    return idx;
}

/* Select highest-priority ready task */
static int select_next_task(void) {
    int best = -1;
    uint8_t best_prio = 255;

    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].state == TASK_READY && tasks[i].priority < best_prio) {
            best_prio = tasks[i].priority;
            best = i;
        }
    }

    return best;
}

/* Check if a higher-priority task is ready */
static int should_preempt(void) {
    if (current_task < 0) return 0;
    uint8_t current_prio = tasks[current_task].priority;

    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].state == TASK_READY && tasks[i].priority < current_prio) {
            return 1;
        }
    }
    return 0;
}

/* Start the scheduler */
void rtos_start(void) {
    /* SysTick: 1ms tick at 16MHz */
    *(volatile uint32_t *)0xE000E010 = 16000 - 1;  /* LOAD */
    *(volatile uint32_t *)0xE000E014 = 0;          /* VAL */
    *(volatile uint32_t *)0xE000E018 = 0x7;        /* CTRL */

    /* PendSV at lowest priority */
    *(volatile uint8_t *)0xE000ED22 = 0xFF;

    /* Select first task */
    current_task = select_next_task();
    if (current_task < 0) {
        while (1);
    }
    tasks[current_task].state = TASK_RUNNING;

    /* Set PSP and switch to Thread Mode with PSP */
    __asm volatile (
        "MSR PSP, %0\n"
        "MOV R0, #3\n"
        "MSR CONTROL, R0\n"
        "ISB\n"
        :
        : "r" (tasks[current_task].stack_ptr)
        : "r0", "memory"
    );

    trigger_pendsv();
    while (1);
}

void rtos_yield(void) {
    trigger_pendsv();
}

void rtos_sleep(uint32_t ticks) {
    if (ticks == 0 || current_task < 0) return;
    tasks[current_task].sleep_ticks = ticks;
    tasks[current_task].state = TASK_BLOCKED;
    tasks[current_task].blocked_on = NULL;
    trigger_pendsv();
}

void rtos_suspend(int task_id) {
    if (task_id < 0 || task_id >= num_tasks) return;
    tasks[task_id].state = TASK_SUSPENDED;
    if (task_id == current_task) trigger_pendsv();
}

void rtos_resume(int task_id) {
    if (task_id < 0 || task_id >= num_tasks) return;
    if (tasks[task_id].state == TASK_SUSPENDED) {
        tasks[task_id].state = TASK_READY;
        if (should_preempt()) trigger_pendsv();
    }
}

/* Mutex operations */
void mutex_init(Mutex *m) {
    m->locked = 0;
    m->owner = NULL;
    m->original_prio = 0;
    m->wait_list = NULL;
    m->next = NULL;
}

void mutex_lock(Mutex *m) {
    TCB *cur = rtos_current_task();
    if (!cur) return;

    if (m->locked == 0) {
        m->locked = 1;
        m->owner = cur;
        m->original_prio = cur->base_priority;
        return;
    }

    if (m->owner == cur) return; /* Already held */

    /* Block on mutex */
    cur->state = TASK_BLOCKED;
    cur->blocked_on = m;

    /* Insert into wait list ordered by priority */
    TCB **pp = &m->wait_list;
    while (*pp && (*pp)->priority <= cur->priority) {
        pp = &(*pp)->wait_next;
    }
    cur->wait_next = *pp;
    *pp = cur;

    /* Priority inheritance */
    if (cur->base_priority < m->owner->priority) {
        m->owner->priority = cur->base_priority;
    }

    trigger_pendsv();
}

void mutex_unlock(Mutex *m) {
    TCB *cur = rtos_current_task();
    if (!cur || m->owner != cur) return;

    /* Restore original priority */
    cur->priority = m->original_prio;
    m->locked = 0;
    m->owner = NULL;

    /* Wake highest-priority waiter */
    TCB *waiter = m->wait_list;
    if (waiter) {
        m->wait_list = waiter->wait_next;
        waiter->wait_next = NULL;
        waiter->state = TASK_READY;
        waiter->blocked_on = NULL;
        m->owner = waiter;
        m->locked = 1;
        m->original_prio = waiter->base_priority;

        /* Inherit from next waiter if any */
        if (m->wait_list && m->wait_list->base_priority < waiter->base_priority) {
            waiter->priority = m->wait_list->base_priority;
        }
    }

    trigger_pendsv();
}

/* Semaphore operations */
void sem_init(Semaphore *s, int initial, int max) {
    s->count = initial;
    s->max_count = max;
    s->wait_list = NULL;
    s->next = NULL;
}

void sem_take(Semaphore *s) {
    TCB *cur = rtos_current_task();
    if (!cur) return;

    if (s->count > 0) {
        s->count--;
        return;
    }

    cur->state = TASK_BLOCKED;
    cur->blocked_on = s;

    TCB **pp = &s->wait_list;
    while (*pp && (*pp)->priority <= cur->priority) {
        pp = &(*pp)->wait_next;
    }
    cur->wait_next = *pp;
    *pp = cur;

    trigger_pendsv();
}

void sem_give(Semaphore *s) {
    TCB *waiter = s->wait_list;
    if (waiter) {
        s->wait_list = waiter->wait_next;
        waiter->wait_next = NULL;
        waiter->state = TASK_READY;
        waiter->blocked_on = NULL;
    } else if (s->count < s->max_count) {
        s->count++;
    }

    if (should_preempt()) trigger_pendsv();
}

/* Message queue operations */
void queue_init(MessageQueue *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    sem_init(&q->sem, 0, MSG_QUEUE_SIZE);
}

int queue_send(MessageQueue *q, const void *data, size_t len) {
    if (len > MSG_MAX_SIZE || q->count >= MSG_QUEUE_SIZE) {
        return -1;
    }

    memcpy(q->buffer[q->head], data, len);
    q->head = (q->head + 1) % MSG_QUEUE_SIZE;
    q->count++;

    sem_give(&q->sem);
    return (int)len;
}

int queue_receive(MessageQueue *q, void *data, size_t max_len) {
    sem_take(&q->sem);

    size_t len = max_len < MSG_MAX_SIZE ? max_len : MSG_MAX_SIZE;
    memcpy(data, q->buffer[q->tail], len);
    q->tail = (q->tail + 1) % MSG_QUEUE_SIZE;
    q->count--;

    return (int)len;
}

/* SysTick handler — 1ms tick */
void SysTick_Handler(void) {
    system_ticks++;

    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].state == TASK_BLOCKED && tasks[i].blocked_on == NULL) {
            if (tasks[i].sleep_ticks > 0) {
                tasks[i].sleep_ticks--;
                if (tasks[i].sleep_ticks == 0) {
                    tasks[i].state = TASK_READY;
                }
            }
        }
    }

    if (should_preempt()) trigger_pendsv();
}

/* PendSV handler — context switch */
__attribute__((naked)) void PendSV_Handler(void) {
    __asm volatile (
        /* Save R4-R11 onto current task's stack */
        "MRS R0, PSP\n"
        "STMDB R0!, {R4-R11}\n"

        /* Save PSP into current TCB */
        "LDR R1, =current_task\n"
        "LDR R1, [R1]\n"
        "LDR R2, =tasks\n"
        "LDR R3, [R1, R2]\n"
        "STR R0, [R3]\n"

        /* Mark current task as ready (if it was running) */
        "CMP R1, #-1\n"
        "BLT 1f\n"
        "LDRB R4, [R3, #16]\n"   /* Load state byte */
        "CMP R4, #1\n"            /* Was it RUNNING? */
        "BNE 1f\n"
        "MOVS R4, #0\n"           /* Set to READY */
        "STRB R4, [R3, #16]\n"

        "1:\n"
        /* Select next task */
        "PUSH {R0, LR}\n"
        "BL select_next_task\n"
        "MOV R4, R0\n"
        "POP {R0, LR}\n"

        /* If no task, stay on current */
        "CMP R4, #-1\n"
        "BEQ 2f\n"

        /* Update current_task */
        "LDR R1, =current_task\n"
        "STR R4, [R1]\n"

        /* Mark new task as RUNNING */
        "LDR R1, =tasks\n"
        "LDR R2, [R4, R1]\n"
        "MOVS R3, #1\n"
        "STRB R3, [R2, #16]\n"

        /* Load new SP */
        "LDR R0, [R4, R1]\n"

        "2:\n"
        /* Restore R4-R11 */
        "LDMIA R0!, {R4-R11}\n"
        "MSR PSP, R0\n"
        "BX LR\n"
    );
}
```

### Main Application (`main.c`)

```c
#include "rtos.h"
#include <stdio.h>

/* GPIO for STM32F405 */
#define RCC_AHB1ENR   (*(volatile uint32_t *)0x40023830)
#define GPIOA_MODER   (*(volatile uint32_t *)0x40020000)
#define GPIOA_ODR     (*(volatile uint32_t *)0x40020014)

/* Shared state for demonstration */
static Mutex shared_mutex;
static MessageQueue sensor_queue;
static Semaphore data_ready_sem;
static volatile int sensor_value = 0;
static volatile int processed_count = 0;

/* Task 1: High-priority sensor reader (priority 1) */
void task_sensor_reader(void *arg) {
    (void)arg;
    while (1) {
        /* Simulate reading a sensor */
        mutex_lock(&shared_mutex);
        sensor_value = (int)(rtos_tick_count() % 1000);
        mutex_unlock(&shared_mutex);

        /* Send to queue */
        int val = sensor_value;
        queue_send(&sensor_queue, &val, sizeof(val));

        /* Signal that data is ready */
        sem_give(&data_ready_sem);

        rtos_sleep(100); /* Read every 100ms */
    }
}

/* Task 2: Medium-priority processor (priority 2) */
void task_processor(void *arg) {
    (void)arg;
    while (1) {
        /* Wait for data */
        sem_take(&data_ready_sem);

        int val;
        int len = queue_receive(&sensor_queue, &val, sizeof(val));
        if (len > 0) {
            mutex_lock(&shared_mutex);
            processed_count++;
            mutex_unlock(&shared_mutex);
        }

        rtos_sleep(10);
    }
}

/* Task 3: Low-priority logger (priority 3) */
void task_logger(void *arg) {
    (void)arg;
    while (1) {
        mutex_lock(&shared_mutex);
        /* Log the current values */
        volatile int sv = sensor_value;
        volatile int pc = processed_count;
        (void)sv;
        (void)pc;
        mutex_unlock(&shared_mutex);

        rtos_sleep(500);
    }
}

/* Task 4: Priority inversion demonstrator (priority 4) */
void task_low_worker(void *arg) {
    (void)arg;
    while (1) {
        mutex_lock(&shared_mutex);
        /* Hold mutex for a while — this simulates a long critical section */
        for (volatile int i = 0; i < 10000; i++);
        mutex_unlock(&shared_mutex);
        rtos_sleep(200);
    }
}

int main(void) {
    /* Enable GPIOA */
    RCC_AHB1ENR |= (1 << 0);
    /* PA5 as output (MODER bits 11:10 = 01) */
    GPIOA_MODER &= ~(0x3 << 10);
    GPIOA_MODER |= (0x1 << 10);

    /* Initialize kernel */
    rtos_init();

    /* Initialize synchronization objects */
    mutex_init(&shared_mutex);
    queue_init(&sensor_queue);
    sem_init(&data_ready_sem, 0, MSG_QUEUE_SIZE);

    /* Create 4 tasks with different priorities */
    rtos_create_task(task_sensor_reader, NULL, 1);  /* Highest */
    rtos_create_task(task_processor,     NULL, 2);
    rtos_create_task(task_logger,        NULL, 3);
    rtos_create_task(task_low_worker,    NULL, 4);  /* Lowest */

    /* Start the scheduler — never returns */
    rtos_start();

    return 0;
}
```

### Makefile

```makefile
CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
CFLAGS = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os -g -Wall -Wextra -nostdlib -ffreestanding
LDFLAGS = -T linker.ld

all: rtos.elf rtos.bin

rtos.elf: startup.c rtos.c main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

rtos.bin: rtos.elf
	$(OBJCOPY) -O binary $< $@

run: rtos.bin
	qemu-system-arm -M netduinoplus2 -kernel rtos.bin -S -s &

clean:
	rm -f rtos.elf rtos.bin
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
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;

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
qemu-system-arm -M netduinoplus2 -kernel rtos.bin -S -s &
arm-none-eabi-gdb rtos.elf
```

In GDB:
```
(gdb) target remote :1234
(gdb) break task_sensor_reader
(gdb) break task_processor
(gdb) break task_logger
(gdb) continue
```

---

## Implementation: Rust

### Project Setup

```bash
cargo init --name rtos-rust
cd rtos-rust
```

### `Cargo.toml`

```toml
[package]
name = "rtos-rust"
version = "0.1.0"
edition = "2021"

[dependencies]
cortex-m = "0.7"
cortex-m-rt = "0.7"
panic-halt = "0.2"
bare-metal = "1.0"

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
use cortex_m_rt::{entry, exception};

const MAX_TASKS: usize = 16;
const STACK_SIZE: usize = 512;
const MSG_QUEUE_SIZE: usize = 16;
const MSG_MAX_SIZE: usize = 32;

#[derive(Clone, Copy, PartialEq)]
enum TaskState {
    Ready,
    Running,
    Blocked,
    Suspended,
}

type TaskFunc = fn(*mut ());

struct TCB {
    stack_ptr: *mut u32,
    stack: [u32; STACK_SIZE],
    state: TaskState,
    priority: u8,
    base_priority: u8,
    entry: TaskFunc,
    arg: *mut (),
    sleep_ticks: u32,
    blocked_on: *mut (),
    wait_next: *mut TCB,
}

unsafe impl Sync for Kernel {}

struct Mutex {
    locked: bool,
    owner: *mut TCB,
    original_prio: u8,
    wait_list: *mut TCB,
}

struct Semaphore {
    count: i32,
    max_count: i32,
    wait_list: *mut TCB,
}

struct MessageQueue {
    buffer: UnsafeCell<[[u8; MSG_MAX_SIZE]; MSG_QUEUE_SIZE]>,
    head: UnsafeCell<usize>,
    tail: UnsafeCell<usize>,
    count: UnsafeCell<usize>,
    sem: UnsafeCell<Semaphore>,
}

struct Kernel {
    tasks: UnsafeCell<[TCB; MAX_TASKS]>,
    num_tasks: UnsafeCell<usize>,
    current_task: UnsafeCell<isize>,
    system_ticks: UnsafeCell<u32>,
}

static KERNEL: Kernel = Kernel {
    tasks: UnsafeCell::new(unsafe { core::mem::zeroed() }),
    num_tasks: UnsafeCell::new(0),
    current_task: UnsafeCell::new(-1),
    system_ticks: UnsafeCell::new(0),
};

fn trigger_pendsv() {
    unsafe {
        let icsr = 0xE000_ED04 as *mut u32;
        icsr.write_volatile(1 << 28);
    }
}

fn rtos_init() {
    let tasks = unsafe { &mut *KERNEL.tasks.get() };
    for tcb in tasks.iter_mut() {
        tcb.state = TaskState::Suspended;
        tcb.priority = 255;
        tcb.base_priority = 255;
        tcb.sleep_ticks = 0;
        tcb.blocked_on = ptr::null_mut();
        tcb.wait_next = ptr::null_mut();
    }
    unsafe {
        *KERNEL.num_tasks.get() = 0;
        *KERNEL.current_task.get() = -1;
        *KERNEL.system_ticks.get() = 0;
    }
}

fn rtos_create_task(entry: TaskFunc, arg: *mut (), priority: u8) -> isize {
    let num = unsafe { *KERNEL.num_tasks.get() };
    if num >= MAX_TASKS {
        return -1;
    }

    let tasks = unsafe { &mut *KERNEL.tasks.get() };
    let tcb = &mut tasks[num];

    tcb.entry = entry;
    tcb.arg = arg;
    tcb.base_priority = priority;
    tcb.priority = priority;
    tcb.state = TaskState::Ready;
    tcb.sleep_ticks = 0;
    tcb.blocked_on = ptr::null_mut();
    tcb.wait_next = ptr::null_mut();

    /* Initialize stack */
    let mut sp: isize = STACK_SIZE as isize;
    sp -= 8; /* R4-R11 */
    sp -= 8; /* Exception frame */

    let stack_ptr = &mut tcb.stack[sp as usize];
    stack_ptr[7] = entry as u32;        /* PC */
    stack_ptr[6] = 0x01000000;          /* xPSR */
    stack_ptr[5] = 0xFFFFFFFD;          /* LR: EXC_RETURN */
    stack_ptr[4] = arg as u32;          /* R0 */
    stack_ptr[3] = 0;                   /* R1 */
    stack_ptr[2] = 0;                   /* R2 */
    stack_ptr[1] = 0;                   /* R3 */
    stack_ptr[0] = 0;                   /* R12 */

    tcb.stack_ptr = stack_ptr.as_mut_ptr();

    unsafe {
        *KERNEL.num_tasks.get() = num + 1;
    }

    num as isize
}

fn select_next_task() -> isize {
    let tasks = unsafe { &*KERNEL.tasks.get() };
    let num = unsafe { *KERNEL.num_tasks.get() };
    let mut best: isize = -1;
    let mut best_prio: u8 = 255;

    for i in 0..num {
        if tasks[i].state == TaskState::Ready && tasks[i].priority < best_prio {
            best_prio = tasks[i].priority;
            best = i as isize;
        }
    }

    best
}

fn should_preempt() -> bool {
    let tasks = unsafe { &*KERNEL.tasks.get() };
    let num = unsafe { *KERNEL.num_tasks.get() };
    let current = unsafe { *KERNEL.current_task.get() };
    if current < 0 {
        return false;
    }
    let current_prio = tasks[current as usize].priority;

    for i in 0..num {
        if tasks[i].state == TaskState::Ready && tasks[i].priority < current_prio {
            return true;
        }
    }
    false
}

fn rtos_start() -> ! {
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
        *KERNEL.current_task.get() = next;
        let tasks = &*KERNEL.tasks.get();
        tasks[next as usize].state = TaskState::Running;

        let sp = tasks[next as usize].stack_ptr;
        asm!("MSR PSP, {}", in(reg) sp);
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

fn rtos_sleep(ticks: u32) {
    if ticks == 0 {
        return;
    }
    let tasks = unsafe { &*KERNEL.tasks.get() };
    let current = unsafe { *KERNEL.current_task.get() } as usize;
    tasks[current].sleep_ticks = ticks;
    tasks[current].state = TaskState::Blocked;
    tasks[current].blocked_on = ptr::null_mut();
    trigger_pendsv();
}

fn rtos_yield() {
    trigger_pendsv();
}

/* Mutex with priority inheritance — type-safe wrapper */
struct PriorityMutex {
    inner: UnsafeCell<Mutex>,
}

unsafe impl Sync for PriorityMutex {}

impl PriorityMutex {
    const fn new() -> Self {
        Self {
            inner: UnsafeCell::new(Mutex {
                locked: false,
                owner: ptr::null_mut(),
                original_prio: 0,
                wait_list: ptr::null_mut(),
            }),
        }
    }

    fn lock(&self) {
        let current = unsafe { *KERNEL.current_task.get() } as usize;
        let tasks = unsafe { &*KERNEL.tasks.get() };
        let m = unsafe { &mut *self.inner.get() };

        if !m.locked {
            m.locked = true;
            m.owner = unsafe { KERNEL.tasks.get().cast::<TCB>().add(current) };
            m.original_prio = tasks[current].base_priority;
            return;
        }

        if m.owner as usize == current {
            return;
        }

        /* Block */
        let tcb = unsafe { &mut *KERNEL.tasks.get().cast::<TCB>().add(current) };
        tcb.state = TaskState::Blocked;
        tcb.blocked_on = self.inner.get().cast();

        /* Insert into wait list by priority */
        let mut pp = &mut m.wait_list;
        while !(*pp).is_null() {
            let waiter_prio = unsafe { (*(*pp)).priority };
            if waiter_prio > tcb.priority {
                break;
            }
            pp = unsafe { &mut (*(*pp)).wait_next };
        }
        tcb.wait_next = *pp;
        *pp = tcb;

        /* Priority inheritance */
        if tcb.base_priority < unsafe { (*m.owner).priority } {
            unsafe { (*m.owner).priority = tcb.base_priority };
        }

        trigger_pendsv();
    }

    fn unlock(&self) {
        let current = unsafe { *KERNEL.current_task.get() } as usize;
        let m = unsafe { &mut *self.inner.get() };
        let owner_ptr = m.owner as usize;
        let tasks_ptr = unsafe { KERNEL.tasks.get() };

        if owner_ptr == 0 {
            return;
        }

        let owner_idx = (owner_ptr - tasks_ptr as usize) / core::mem::size_of::<TCB>();

        /* Restore priority */
        let owner = unsafe { &mut *tasks_ptr.cast::<TCB>().add(owner_idx) };
        owner.priority = m.original_prio;

        m.locked = false;
        m.owner = ptr::null_mut();

        /* Wake highest-priority waiter */
        let waiter = m.wait_list;
        if !waiter.is_null() {
            m.wait_list = unsafe { (*waiter).wait_next };
            unsafe { (*waiter).wait_next = ptr::null_mut() };
            unsafe { (*waiter).state = TaskState::Ready };
            unsafe { (*waiter).blocked_on = ptr::null_mut() };

            m.owner = waiter;
            m.locked = true;
            let waiter_idx = (waiter as usize - tasks_ptr as usize)
                / core::mem::size_of::<TCB>();
            let waiter_tcb = unsafe { &mut *tasks_ptr.cast::<TCB>().add(waiter_idx) };
            m.original_prio = waiter_tcb.base_priority;

            /* Inherit from next waiter */
            if !m.wait_list.is_null() {
                let next_prio = unsafe { (*m.wait_list).base_priority };
                if next_prio < waiter_tcb.base_priority {
                    waiter_tcb.priority = next_prio;
                }
            }
        }

        if should_preempt() {
            trigger_pendsv();
        }
    }
}

/* Message queue — type-safe generic */
struct TypedQueue<T: Copy> {
    buffer: UnsafeCell<[T; MSG_QUEUE_SIZE]>,
    head: UnsafeCell<usize>,
    tail: UnsafeCell<usize>,
    count: UnsafeCell<usize>,
    sem: UnsafeCell<Semaphore>,
}

unsafe impl<T: Copy> Sync for TypedQueue<T> {}

impl<T: Copy> TypedQueue<T> {
    const fn new() -> Self {
        Self {
            buffer: UnsafeCell::new(unsafe { core::mem::zeroed() }),
            head: UnsafeCell::new(0),
            tail: UnsafeCell::new(0),
            count: UnsafeCell::new(0),
            sem: UnsafeCell::new(Semaphore {
                count: 0,
                max_count: MSG_QUEUE_SIZE as i32,
                wait_list: ptr::null_mut(),
            }),
        }
    }

    fn send(&self, data: T) -> Result<(), ()> {
        let count = unsafe { *self.count.get() };
        if count >= MSG_QUEUE_SIZE {
            return Err(());
        }

        let buffer = unsafe { &mut *self.buffer.get() };
        let head = unsafe { *self.head.get() };
        buffer[head] = data;
        unsafe {
            *self.head.get() = (head + 1) % MSG_QUEUE_SIZE;
            *self.count.get() = count + 1;
        }

        sem_give_internal(unsafe { &mut *self.sem.get() });
        Ok(())
    }

    fn receive(&self) -> T {
        sem_take_internal(unsafe { &mut *self.sem.get() });

        let buffer = unsafe { &mut *self.buffer.get() };
        let tail = unsafe { *self.tail.get() };
        let data = buffer[tail];
        unsafe {
            *self.tail.get() = (tail + 1) % MSG_QUEUE_SIZE;
            *self.count.get() -= 1;
        }

        data
    }
}

fn sem_take_internal(s: &mut Semaphore) {
    let current = unsafe { *KERNEL.current_task.get() } as usize;

    if s.count > 0 {
        s.count -= 1;
        return;
    }

    let tcb = unsafe { &mut *KERNEL.tasks.get().cast::<TCB>().add(current) };
    tcb.state = TaskState::Blocked;
    tcb.blocked_on = s as *mut Semaphore as *mut ();

    let mut pp = &mut s.wait_list;
    while !(*pp).is_null() {
        let waiter_prio = unsafe { (*(*pp)).priority };
        if waiter_prio > tcb.priority {
            break;
        }
        pp = unsafe { &mut (*(*pp)).wait_next };
    }
    tcb.wait_next = *pp;
    *pp = tcb;

    trigger_pendsv();
}

fn sem_give_internal(s: &mut Semaphore) {
    let waiter = s.wait_list;
    if !waiter.is_null() {
        s.wait_list = unsafe { (*waiter).wait_next };
        unsafe { (*waiter).wait_next = ptr::null_mut() };
        unsafe { (*waiter).state = TaskState::Ready };
        unsafe { (*waiter).blocked_on = ptr::null_mut() };
    } else if s.count < s.max_count {
        s.count += 1;
    }

    if should_preempt() {
        trigger_pendsv();
    }
}

/* GPIO */
const RCC_AHB1ENR: *mut u32 = 0x4002_3830 as _;
const GPIOA_MODER: *mut u32 = 0x4002_0000 as _;
const GPIOA_ODR: *mut u32 = 0x4002_0014 as _;

/* Shared state */
static SHARED_MUTEX: PriorityMutex = PriorityMutex::new();
static SENSOR_QUEUE: TypedQueue<i32> = TypedQueue::new();
static DATA_READY_SEM: UnsafeCell<Semaphore> = UnsafeCell::new(Semaphore {
    count: 0,
    max_count: MSG_QUEUE_SIZE as i32,
    wait_list: ptr::null_mut(),
});

static mut SENSOR_VALUE: i32 = 0;
static mut PROCESSED_COUNT: i32 = 0;

fn task_sensor_reader(_arg: *mut ()) {
    loop {
        SHARED_MUTEX.lock();
        unsafe {
            SENSOR_VALUE = (KERNEL.system_ticks.get().read_volatile() % 1000) as i32;
        }
        SHARED_MUTEX.unlock();

        let val = unsafe { SENSOR_VALUE };
        let _ = SENSOR_QUEUE.send(val);
        sem_give_internal(unsafe { &mut *DATA_READY_SEM.get() });

        rtos_sleep(100);
    }
}

fn task_processor(_arg: *mut ()) {
    loop {
        sem_take_internal(unsafe { &mut *DATA_READY_SEM.get() });

        let val = SENSOR_QUEUE.receive();
        SHARED_MUTEX.lock();
        unsafe {
            PROCESSED_COUNT += 1;
            let _ = val;
        }
        SHARED_MUTEX.unlock();

        rtos_sleep(10);
    }
}

fn task_logger(_arg: *mut ()) {
    loop {
        SHARED_MUTEX.lock();
        unsafe {
            let _sv = SENSOR_VALUE;
            let _pc = PROCESSED_COUNT;
        }
        SHARED_MUTEX.unlock();

        rtos_sleep(500);
    }
}

fn task_low_worker(_arg: *mut ()) {
    loop {
        SHARED_MUTEX.lock();
        for _ in 0..10000 {
            core::hint::spin_loop();
        }
        SHARED_MUTEX.unlock();
        rtos_sleep(200);
    }
}

#[entry]
fn main() -> ! {
    unsafe {
        (*RCC_AHB1ENR) |= 1 << 0;
        let moder = (*GPIOA_MODER).read_volatile();
        (*GPIOA_MODER).write_volatile((moder & !(0x3 << 10)) | (0x1 << 10));
    }

    rtos_init();
    rtos_create_task(task_sensor_reader, ptr::null_mut(), 1);
    rtos_create_task(task_processor, ptr::null_mut(), 2);
    rtos_create_task(task_logger, ptr::null_mut(), 3);
    rtos_create_task(task_low_worker, ptr::null_mut(), 4);

    rtos_start()
}

#[exception]
fn SysTick() {
    unsafe {
        *KERNEL.system_ticks.get() += 1;
    }

    let tasks = unsafe { &*KERNEL.tasks.get() };
    let num = unsafe { *KERNEL.num_tasks.get() };

    for i in 0..num {
        if tasks[i].state == TaskState::Blocked && tasks[i].blocked_on.is_null() {
            if tasks[i].sleep_ticks > 0 {
                let tcb = unsafe { &mut *KERNEL.tasks.get().cast::<TCB>().add(i) };
                tcb.sleep_ticks -= 1;
                if tcb.sleep_ticks == 0 {
                    tcb.state = TaskState::Ready;
                }
            }
        }
    }

    if should_preempt() {
        trigger_pendsv();
    }
}

#[exception]
unsafe fn PendSV() {
    asm!(
        "MRS R0, PSP",
        "STMDB R0!, {{R4-R11}}",
        "LDR R1, ={current}",
        "LDR R1, [R1]",
        "LDR R2, ={tasks}",
        "LDR R3, [R1, R2]",
        "STR R0, [R3]",
        "CMP R1, #-1",
        "BLT 1f",
        "LDRB R4, [R3, #16]",
        "CMP R4, #1",
        "BNE 1f",
        "MOVS R4, #0",
        "STRB R4, [R3, #16]",
        "1:",
        "PUSH {{R0, LR}}",
        "BL {select}",
        "MOV R4, R0",
        "POP {{R0, LR}}",
        "CMP R4, #-1",
        "BEQ 2f",
        "LDR R1, ={current}",
        "STR R4, [R1]",
        "LDR R1, ={tasks}",
        "LDR R2, [R4, R1]",
        "MOVS R3, #1",
        "STRB R3, [R2, #16]",
        "LDR R0, [R4, R1]",
        "2:",
        "LDMIA R0!, {{R4-R11}}",
        "MSR PSP, R0",
        "BX LR",
        current = sym KERNEL.current_task,
        tasks = sym KERNEL.tasks,
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
arm-none-eabi-gdb target/thumbv7em-none-eabihf/release/rtos-rust
(gdb) target remote :1234
(gdb) break rtos_rust::task_sensor_reader
(gdb) continue
```

---

## Implementation: Ada

Ada's approach to concurrency is fundamentally different from C, Rust, and Zig. Instead of manually implementing context switches and synchronization primitives, Ada provides **tasks** and **protected objects** as first-class language constructs. Under the Ravenscar profile, the compiler and runtime generate all the RTOS machinery automatically.

### Project Structure

```
rtos-ada/
├── rtos.gpr
├── src/
│   ├── sensors.ads
│   ├── sensors.adb
│   ├── shared_data.ads
│   ├── shared_data.adb
│   └── main.adb
```

### Project File (`rtos.gpr`)

```ada
project RTOS is
   for Source_Dirs use ("src");
   for Object_Dir use "obj";
   for Main use ("main.adb");
   for Target use "arm-eabi";

   package Compiler is
       for Default_Switches ("Ada") use
         ("-O2", "-g", "-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16",
          "-fstack-check", "-gnatp", "-gnata",
          "-gnatR", "-gnatw.e");
   end Compiler;

   package Linker is
      for Default_Switches ("Ada") use
        ("-T", "linker.ld", "-nostartfiles");
   end Linker;
end RTOS;
```

### Shared Data Package (`shared_data.ads`)

Protected objects in Ada are the idiomatic replacement for mutexes. They provide mutual exclusion automatically — only one task can execute a protected procedure at a time. Priority ceiling is enforced by the Ravenscar runtime.

```ada
with Interfaces; use Interfaces;

package Shared_Data is

   -- Protected object replaces mutex + shared variables
   protected Sensor_Data is
      procedure Set_Value (Val : Integer_32);
      function  Get_Value return Integer_32;
      procedure Increment_Processed;
      function  Get_Processed return Integer_32;
   private
      Current_Value    : Integer_32 := 0;
      Processed_Count  : Integer_32 := 0;
   end Sensor_Data;

end Shared_Data;
```

### Shared Data Body (`shared_data.adb`)

```ada
package body Shared_Data is

   protected body Sensor_Data is

      procedure Set_Value (Val : Integer_32) is
      begin
         Current_Value := Val;
      end Set_Value;

      function Get_Value return Integer_32 is
      begin
         return Current_Value;
      end Get_Value;

      procedure Increment_Processed is
      begin
         Processed_Count := Processed_Count + 1;
      end Increment_Processed;

      function Get_Processed return Integer_32 is
      begin
         return Processed_Count;
      end Get_Processed;

   end Sensor_Data;

end Shared_Data;
```

### Sensor Package (`sensors.ads`)

Message queues in Ada are implemented as protected objects with bounded entry queues. The `entry` keyword declares a rendezvous point — callers block until the entry is open (guarded by `when`).

```ada
with Interfaces; use Interfaces;

package Sensors is

   Max_Queue_Size : constant := 16;

   -- Message queue as a protected object with bounded buffer
   protected type Sensor_Queue is
      entry Send (Val : Integer_32);
      entry Receive (Val : out Integer_32);
   private
      Buffer : array (1 .. Max_Queue_Size) of Integer_32;
      Head   : Positive := 1;
      Tail   : Positive := 1;
      Count  : Natural := 0;
   end Sensor_Queue;

   -- Shared queue instance
   Sensor_Data_Queue : Sensor_Queue;

end Sensors;
```

### Sensor Package Body (`sensors.adb`)

```ada
package body Sensors is

   protected body Sensor_Queue is

      entry Send (Val : Integer_32)
        when Count < Max_Queue_Size is
      begin
         Buffer (Head) := Val;
         Head := (Head mod Max_Queue_Size) + 1;
         Count := Count + 1;
      end Send;

      entry Receive (Val : out Integer_32)
        when Count > 0 is
      begin
         Val := Buffer (Tail);
         Tail := (Tail mod Max_Queue_Size) + 1;
         Count := Count - 1;
      end Receive;

   end Sensor_Queue;

end Sensors;
```

### Main Application (`main.adb`)

```ada
with Ada.Real_Time; use Ada.Real_Time;
with System.Machine_Code; use System.Machine_Code;
with Shared_Data; use Shared_Data;
with Sensors; use Sensors;

procedure Main is

   type UInt32 is mod 2**32;

    RCC_AHB1ENR : UInt32 with
      Address => System'To_Address (16#4002_3830#),
      Volatile => True;

    GPIOA_MODER : UInt32 with
      Address => System'To_Address (16#4002_0000#),
      Volatile => True;

    GPIOA_ODR : UInt32 with
      Address => System'To_Address (16#4002_0014#),
      Volatile => True;

   procedure Toggle_LED is
    begin
       GPIOA_ODR := GPIOA_ODR xor (1 << 5);
   end Toggle_LED;

   -- Task 1: High-priority sensor reader (priority 10)
   task type Sensor_Reader is
      pragma Priority (10);
   end Sensor_Reader;

   task body Sensor_Reader is
      Period : constant Time_Span := Milliseconds (100);
      Next_Release : Time := Clock;
      Tick_Count : Integer_32 := 0;
   begin
      loop
         -- Simulate sensor read
         Tick_Count := Tick_Count + 1;
         Sensor_Data.Set_Value (Tick_Count mod 1000);

         -- Send to queue
         Sensors.Sensor_Data_Queue.Send (Sensor_Data.Get_Value);

         Toggle_LED;

         Next_Release := Next_Release + Period;
         delay until Next_Release;
      end loop;
   end Sensor_Reader;

   -- Task 2: Medium-priority processor (priority 20)
   task type Data_Processor is
      pragma Priority (20);
   end Data_Processor;

   task body Data_Processor is
      Period : constant Time_Span := Milliseconds (50);
      Next_Release : Time := Clock;
      Val : Integer_32;
   begin
      loop
         -- Receive from queue (blocks if empty)
         Sensors.Sensor_Data_Queue.Receive (Val);

         -- Process
         Sensor_Data.Increment_Processed;

         Next_Release := Next_Release + Period;
         delay until Next_Release;
      end loop;
   end Data_Processor;

   -- Task 3: Low-priority logger (priority 30)
   task type Data_Logger is
      pragma Priority (30);
   end Data_Logger;

   task body Data_Logger is
      Period : constant Time_Span := Milliseconds (500);
      Next_Release : Time := Clock;
      Val : Integer_32;
      Count : Integer_32;
   begin
      loop
         -- Read shared data under mutual exclusion
         Val := Sensor_Data.Get_Value;
         Count := Sensor_Data.Get_Processed;

         -- Suppress unused warnings
         pragma Unreferenced (Val, Count);

         Toggle_LED;

         Next_Release := Next_Release + Period;
         delay until Next_Release;
      end loop;
   end Data_Logger;

   -- Task 4: Low-priority worker (priority 40)
   task type Low_Worker is
      pragma Priority (40);
   end Low_Worker;

   task body Low_Worker is
      Period : constant Time_Span := Milliseconds (200);
      Next_Release : Time := Clock;
      Val : Integer_32;
   begin
      loop
         -- Access shared data (simulates long critical section)
         Sensor_Data.Set_Value (Val);
         for I in 1 .. 10_000 loop
            null;
         end loop;

         Next_Release := Next_Release + Period;
         delay until Next_Release;
      end loop;
   end Low_Worker;

   -- Instantiate tasks
   Reader : Sensor_Reader;
   Proc   : Data_Processor;
   Logger : Data_Logger;
   Worker : Low_Worker;

begin
   -- Enable GPIOA
    RCC_AHB1ENR := RCC_AHB1ENR or (1 << 0);

    -- Configure PA5 as output
    declare
       MODER : constant UInt32 := GPIOA_MODER;
    begin
       GPIOA_MODER := (MODER and not (16#3# << 10)) or (16#1# << 10);
    end;

   -- Main task suspends forever — worker tasks run independently
   loop
      delay until Time_Last;
   end loop;

end Main;
```

### Build

```bash
gprbuild -P rtos.gpr
qemu-system-arm -M netduinoplus2 -kernel obj/main -S -s &
arm-none-eabi-gdb obj/main
```

### How Ada Handles This Differently

| Concept | C/Rust/Zig | Ada |
|---|---|---|
| Task creation | Manual TCB allocation, stack init | `task type` declaration |
| Context switch | Hand-coded PendSV assembly | Generated by Ravenscar runtime |
| Mutex | Manual lock/unlock + priority inheritance | Protected object (automatic mutual exclusion) |
| Message queue | Ring buffer + semaphore | Entry with `when` guard |
| Sleep/delay | `rtos_sleep(ticks)` | `delay until Next_Release` |
| Priority | Manual field in TCB | `pragma Priority (N)` |
| Priority inversion | Manual inheritance protocol | Priority ceiling (Ravenscar) |

> **Tip:** The Ravenscar profile (`pragma Restrictions (No_Dynamic_Priorities, No_Dynamic_Task_Hierarchy, Max_Task_Entries => 0)`) guarantees that all tasks and priorities are known at compile time. This eliminates runtime allocation failures and makes the system analyzable for worst-case execution time.

---

## Implementation: Zig

### Project Structure

```
rtos-zig/
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
        .name = "rtos",
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
        . = . + 0x1000;
        _estack = .;
    } > RAM
}
```

### `src/main.zig`

```zig
const std = @import("std");

// Comptime configuration
pub const config = struct {
    pub const max_tasks: usize = 16;
    pub const stack_size: usize = 512;
    pub const msg_queue_size: usize = 16;
    pub const msg_max_size: usize = 32;
    pub const systick_reload: u32 = 16000; // 1ms at 16MHz
};

pub const TaskState = enum(u8) {
    ready,
    running,
    blocked,
    suspended,
};

pub const TaskFunc = *const fn (*anyopaque) callconv(.C) noreturn;

pub fn TCB(comptime StackSize: usize) type {
    return struct {
        stack_ptr: [*]u32,
        stack: [StackSize]u32,
        state: TaskState,
        priority: u8,
        base_priority: u8,
        entry: TaskFunc,
        arg: *anyopaque,
        sleep_ticks: u32,
        blocked_on: ?*anyopaque,
        wait_next: ?*TCB(StackSize),

        const Self = @This();

        pub fn init(self: *Self, entry: TaskFunc, arg: *anyopaque, prio: u8) void {
            self.entry = entry;
            self.arg = arg;
            self.base_priority = prio;
            self.priority = prio;
            self.state = .ready;
            self.sleep_ticks = 0;
            self.blocked_on = null;
            self.wait_next = null;

            // Initialize stack (grows downward)
            var sp: usize = StackSize;
            sp -= 8; // R4-R11
            sp -= 8; // Exception frame

            self.stack[sp + 7] = @intFromPtr(entry); // PC
            self.stack[sp + 6] = 0x01000000;         // xPSR
            self.stack[sp + 5] = 0xFFFFFFFD;         // LR: EXC_RETURN
            self.stack[sp + 4] = @intFromPtr(arg);   // R0
            self.stack[sp + 3] = 0;                  // R1
            self.stack[sp + 2] = 0;                  // R2
            self.stack[sp + 1] = 0;                  // R3
            self.stack[sp + 0] = 0;                  // R12

            self.stack_ptr = &self.stack[sp];
        }
    };
}

// Mutex with priority inheritance
pub fn Mutex(comptime TcbType: type) type {
    return struct {
        locked: bool,
        owner: ?*TcbType,
        original_prio: u8,
        wait_list: ?*TcbType,

        const Self = @This();

        pub fn init(self: *Self) void {
            self.locked = false;
            self.owner = null;
            self.original_prio = 0;
            self.wait_list = null;
        }

        pub fn lock(self: *Self, cur: *TcbType, tasks: []TcbType, trigger_pendsv_fn: fn () void) void {
            if (!self.locked) {
                self.locked = true;
                self.owner = cur;
                self.original_prio = cur.base_priority;
                return;
            }

            if (self.owner == cur) return;

            // Block
            cur.state = .blocked;
            cur.blocked_on = self;

            // Insert into wait list ordered by priority
            var pp: *?*TcbType = &self.wait_list;
            while (pp.*) |waiter| {
                if (waiter.priority > cur.priority) break;
                pp = &waiter.wait_next;
            }
            cur.wait_next = pp.*;
            pp.* = cur;

            // Priority inheritance
            if (cur.base_priority < self.owner.?.priority) {
                self.owner.?.priority = cur.base_priority;
            }

            trigger_pendsv_fn();
        }

        pub fn unlock(self: *Self, cur: *TcbType, tasks: []TcbType, trigger_pendsv_fn: fn () void) void {
            if (self.owner != cur) return;

            cur.priority = cur.base_priority;
            self.locked = false;
            self.owner = null;

            // Wake highest-priority waiter
            if (self.wait_list) |waiter| {
                self.wait_list = waiter.wait_next;
                waiter.wait_next = null;
                waiter.state = .ready;
                waiter.blocked_on = null;

                self.owner = waiter;
                self.locked = true;
                self.original_prio = waiter.base_priority;

                // Inherit from next waiter
                if (self.wait_list) |next| {
                    if (next.base_priority < waiter.base_priority) {
                        waiter.priority = next.base_priority;
                    }
                }
            }

            trigger_pendsv_fn();
        }
    };
}

// Semaphore
pub fn Semaphore(comptime TcbType: type) type {
    return struct {
        count: i32,
        max_count: i32,
        wait_list: ?*TcbType,

        const Self = @This();

        pub fn init(self: *Self, initial: i32, max: i32) void {
            self.count = initial;
            self.max_count = max;
            self.wait_list = null;
        }

        pub fn take(self: *Self, cur: *TcbType, trigger_pendsv_fn: fn () void) void {
            if (self.count > 0) {
                self.count -= 1;
                return;
            }

            cur.state = .blocked;
            cur.blocked_on = self;

            var pp: *?*TcbType = &self.wait_list;
            while (pp.*) |waiter| {
                if (waiter.priority > cur.priority) break;
                pp = &waiter.wait_next;
            }
            cur.wait_next = pp.*;
            pp.* = cur;

            trigger_pendsv_fn();
        }

        pub fn give(self: *Self, should_preempt_fn: fn () bool, trigger_pendsv_fn: fn () void) void {
            if (self.wait_list) |waiter| {
                self.wait_list = waiter.wait_next;
                waiter.wait_next = null;
                waiter.state = .ready;
                waiter.blocked_on = null;
            } else if (self.count < self.max_count) {
                self.count += 1;
            }

            if (should_preempt_fn()) trigger_pendsv_fn();
        }
    };
}

// Message queue
pub fn MessageQueue(comptime TcbType: type, comptime QueueSize: usize, comptime MsgSize: usize) type {
    return struct {
        buffer: [QueueSize][MsgSize]u8,
        head: usize,
        tail: usize,
        count: usize,
        sem: Semaphore(TcbType),

        const Self = @This();

        pub fn init(self: *Self) void {
            @memset(&self.buffer, 0);
            self.head = 0;
            self.tail = 0;
            self.count = 0;
            self.sem.init(0, QueueSize);
        }

        pub fn send(
            self: *Self,
            cur: *TcbType,
            data: []const u8,
            should_preempt_fn: fn () bool,
            trigger_pendsv_fn: fn () void,
        ) bool {
            if (data.len > MsgSize or self.count >= QueueSize) return false;

            @memcpy(self.buffer[self.head][0..data.len], data);
            self.head = (self.head + 1) % QueueSize;
            self.count += 1;

            self.sem.give(should_preempt_fn, trigger_pendsv_fn);
            return true;
        }

        pub fn receive(
            self: *Self,
            cur: *TcbType,
            out: []u8,
            trigger_pendsv_fn: fn () void,
        ) usize {
            self.sem.take(cur, trigger_pendsv_fn);

            const len = if (out.len < MsgSize) out.len else MsgSize;
            @memcpy(out[0..len], self.buffer[self.tail][0..len]);
            self.tail = (self.tail + 1) % QueueSize;
            self.count -= 1;

            return len;
        }
    };
}

// Comptime validation
comptime {
    std.debug.assert(std.math.isPowerOfTwo(config.stack_size));
    std.debug.assert(config.max_tasks > 0);
    std.debug.assert(config.msg_queue_size > 0);
}

// Global state
const TcbType = TCB(config.stack_size);
var tasks: [config.max_tasks]TcbType = undefined;
var num_tasks: usize = 0;
var current_task: isize = -1;
var system_ticks: u32 = 0;

// Synchronization objects
var shared_mutex: Mutex(TcbType) = undefined;
var sensor_queue: MessageQueue(TcbType, config.msg_queue_size, config.msg_max_size) = undefined;
var data_ready_sem: Semaphore(TcbType) = undefined;

var sensor_value: i32 = 0;
var processed_count: i32 = 0;

fn trigger_pendsv() void {
    const icsr = @as(*volatile u32, @ptrFromInt(0xE000ED04));
    icsr.* = 1 << 28;
}

fn should_preempt() bool {
    const ct: usize = @intCast(current_task);
    if (current_task < 0) return false;
    const current_prio = tasks[ct].priority;

    var i: usize = 0;
    while (i < num_tasks) : (i += 1) {
        if (tasks[i].state == .ready and tasks[i].priority < current_prio) {
            return true;
        }
    }
    return false;
}

fn rtos_init() void {
    var i: usize = 0;
    while (i < config.max_tasks) : (i += 1) {
        tasks[i].state = .suspended;
        tasks[i].priority = 255;
        tasks[i].base_priority = 255;
        tasks[i].sleep_ticks = 0;
        tasks[i].blocked_on = null;
        tasks[i].wait_next = null;
    }
    num_tasks = 0;
    current_task = -1;
    system_ticks = 0;

    shared_mutex.init();
    sensor_queue.init();
    data_ready_sem.init(0, config.msg_queue_size);
}

fn rtos_create_task(entry: TaskFunc, arg: *anyopaque, prio: u8) isize {
    if (num_tasks >= config.max_tasks) return -1;

    const idx = num_tasks;
    tasks[idx].init(entry, arg, prio);
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

    return best;
}

fn rtos_sleep(ticks: u32) void {
    if (ticks == 0) return;
    const ct: usize = @intCast(current_task);
    tasks[ct].sleep_ticks = ticks;
    tasks[ct].state = .blocked;
    tasks[ct].blocked_on = null;
    trigger_pendsv();
}

fn rtos_start() noreturn {
    // SysTick: 1ms at 16MHz
    const systick_load = @as(*volatile u32, @ptrFromInt(0xE000E010));
    const systick_val = @as(*volatile u32, @ptrFromInt(0xE000E014));
    const systick_ctrl = @as(*volatile u32, @ptrFromInt(0xE000E018));

    systick_load.* = config.systick_reload - 1;
    systick_val.* = 0;
    systick_ctrl.* = 0x7;

    // PendSV lowest priority
    const shpr3 = @as(*volatile u8, @ptrFromInt(0xE000ED22));
    shpr3.* = 0xFF;

    const next = select_next_task();
    if (next < 0) while (true) {}

    current_task = next;
    tasks[@intCast(next)].state = .running;

    const sp: u32 = @intFromPtr(tasks[@intCast(next)].stack_ptr);
    asm volatile ("MSR PSP, $0"
        :
        : [sp] "{r0}" (sp),
    );

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

// GPIO
const RCC_AHB1ENR = @as(*volatile u32, @ptrFromInt(0x40023830));
const GPIOA_MODER = @as(*volatile u32, @ptrFromInt(0x40020000));
const GPIOA_ODR = @as(*volatile u32, @ptrFromInt(0x40020014));

fn task_sensor_reader(arg: *anyopaque) callconv(.C) noreturn {
    _ = arg;
    while (true) {
        shared_mutex.lock(
            &tasks[@intCast(current_task)],
            &tasks,
            trigger_pendsv,
        );
        sensor_value = @intCast(system_ticks % 1000);
        shared_mutex.unlock(
            &tasks[@intCast(current_task)],
            &tasks,
            trigger_pendsv,
        );

        const val: i32 = sensor_value;
        const bytes = std.mem.asBytes(&val);
        _ = sensor_queue.send(
            &tasks[@intCast(current_task)],
            bytes,
            should_preempt,
            trigger_pendsv,
        );

        data_ready_sem.give(should_preempt, trigger_pendsv);

        rtos_sleep(100);
    }
}

fn task_processor(arg: *anyopaque) callconv(.C) noreturn {
    _ = arg;
    while (true) {
        data_ready_sem.take(&tasks[@intCast(current_task)], trigger_pendsv);

        var val_bytes: [config.msg_max_size]u8 = undefined;
        const len = sensor_queue.receive(
            &tasks[@intCast(current_task)],
            &val_bytes,
            trigger_pendsv,
        );
        if (len > 0) {
            shared_mutex.lock(
                &tasks[@intCast(current_task)],
                &tasks,
                trigger_pendsv,
            );
            processed_count += 1;
            shared_mutex.unlock(
                &tasks[@intCast(current_task)],
                &tasks,
                trigger_pendsv,
            );
        }

        rtos_sleep(10);
    }
}

fn task_logger(arg: *anyopaque) callconv(.C) noreturn {
    _ = arg;
    while (true) {
        shared_mutex.lock(
            &tasks[@intCast(current_task)],
            &tasks,
            trigger_pendsv,
        );
        const sv = sensor_value;
        const pc = processed_count;
        _ = sv;
        _ = pc;
        shared_mutex.unlock(
            &tasks[@intCast(current_task)],
            &tasks,
            trigger_pendsv,
        );

        rtos_sleep(500);
    }
}

fn task_low_worker(arg: *anyopaque) callconv(.C) noreturn {
    _ = arg;
    while (true) {
        shared_mutex.lock(
            &tasks[@intCast(current_task)],
            &tasks,
            trigger_pendsv,
        );
        var i: usize = 0;
        while (i < 10000) : (i += 1) {
            _ = &i;
        }
        shared_mutex.unlock(
            &tasks[@intCast(current_task)],
            &tasks,
            trigger_pendsv,
        );
        rtos_sleep(200);
    }
}

// PendSV handler
export fn PendSV_Handler() callconv(.Naked) noreturn {
    asm volatile (
        \\ MRS R0, PSP
        \\ STMDB R0!, {R4-R11}
        \\
        \\ // Save SP to current TCB
        \\ LDR R1, =current_task
        \\ LDR R1, [R1]
        \\ LDR R2, =tasks
        \\ LDR R3, [R1, R2]
        \\ STR R0, [R3]
        \\
        \\ // Mark current as ready if running
        \\ CMP R1, #-1
        \\ BLT 1f
        \\ LDRB R4, [R3, #16]
        \\ CMP R4, #1
        \\ BNE 1f
        \\ MOVS R4, #0
        \\ STRB R4, [R3, #16]
        \\
        \\ 1:
        \\ // Select next task
        \\ PUSH {R0, LR}
        \\ BL select_next_task
        \\ MOV R4, R0
        \\ POP {R0, LR}
        \\
        \\ CMP R4, #-1
        \\ BEQ 2f
        \\
        \\ LDR R1, =current_task
        \\ STR R4, [R1]
        \\
        \\ LDR R1, =tasks
        \\ LDR R2, [R4, R1]
        \\ MOVS R3, #1
        \\ STRB R3, [R2, #16]
        \\
        \\ LDR R0, [R4, R1]
        \\
        \\ 2:
        \\ LDMIA R0!, {R4-R11}
        \\ MSR PSP, R0
        \\ BX LR
        \\
        ::: "memory"
    );
}

// SysTick handler
export fn SysTick_Handler() void {
    system_ticks += 1;

    var i: usize = 0;
    while (i < num_tasks) : (i += 1) {
        if (tasks[i].state == .blocked and tasks[i].blocked_on == null) {
            if (tasks[i].sleep_ticks > 0) {
                tasks[i].sleep_ticks -= 1;
                if (tasks[i].sleep_ticks == 0) {
                    tasks[i].state = .ready;
                }
            }
        }
    }

    if (should_preempt()) trigger_pendsv();
}

// Reset handler
export fn Reset_Handler() callconv(.Naked) noreturn {
    asm volatile (
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
    // Enable GPIOA
    RCC_AHB1ENR.* |= (1 << 0);

    // PA5 output (MODER bits 11:10 = 01)
    const moder = GPIOA_MODER.*;
    GPIOA_MODER.* = (moder & ~(@as(u32, 0x3) << 10)) | (@as(u32, 0x1) << 10);

    rtos_init();
    _ = rtos_create_task(task_sensor_reader, undefined, 1);
    _ = rtos_create_task(task_processor, undefined, 2);
    _ = rtos_create_task(task_logger, undefined, 3);
    _ = rtos_create_task(task_low_worker, undefined, 4);

    rtos_start();
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
qemu-system-arm -M netduinoplus2 -kernel zig-out/bin/rtos -S -s &
arm-none-eabi-gdb zig-out/bin/rtos
```

---

## QEMU Verification

### Running with GDB

```bash
# Terminal 1: Start QEMU
qemu-system-arm -M netduinoplus2 -kernel rtos.bin -S -s &

# Terminal 2: Connect GDB
arm-none-eabi-gdb rtos.elf
```

### GDB Session

```
(gdb) target remote :1234
Remote debugging using :1234

# Set breakpoints in each task
(gdb) break task_sensor_reader
(gdb) break task_processor
(gdb) break task_logger
(gdb) break task_low_worker

# Run
(gdb) continue
Continuing.

# Verify preemption — high-priority task should run first
(gdb) info registers psp
psr            0x1000000        16777216

# Check which task is running
(gdb) print current_task
$1 = 0

# Verify priority inheritance
# Set a breakpoint in mutex_lock
(gdb) break mutex_lock
(gdb) continue

# When task_sensor_reader (prio 1) blocks on mutex held by task_low_worker (prio 4),
# check that task_low_worker's priority was boosted to 1
(gdb) print tasks[3].priority
$2 = 1 '\001'    <-- boosted from 4 to 1!
(gdb) print tasks[3].base_priority
$3 = 4 '\004'    <-- original priority preserved

# Verify message queue
(gdb) print sensor_queue.count
$4 = 3

# Check task states
(gdb) print tasks[0].state
$5 = TASK_RUNNING
(gdb) print tasks[1].state
$6 = TASK_BLOCKED
(gdb) print tasks[3].state
$7 = TASK_READY
```

### Verifying Priority Inversion Prevention

```
# Scenario: task_low_worker holds mutex, task_sensor_reader wants it

(gdb) break mutex_lock
(gdb) continue
# Hit: task_low_worker locks mutex

(gdb) continue
# Hit: task_sensor_reader tries to lock — should block

(gdb) print current_task
$8 = 0    # task_sensor_reader is current (just blocked)

(gdb) print tasks[3].priority
$9 = 1    # task_low_worker boosted to priority 1

(gdb) print tasks[3].base_priority
$10 = 4   # original priority preserved

(gdb) continue
# task_low_worker runs at priority 1, finishes critical section, unlocks

(gdb) print tasks[3].priority
$11 = 4   # priority restored to original
```

---

## Deliverables

- [ ] TCB with stack, state, base priority, effective priority, blocked_on
- [ ] PendSV context switch (save/restore R4-R11, switch PSP)
- [ ] Preemptive scheduler: SysTick triggers preemption when higher-priority task becomes ready
- [ ] Mutex with priority inheritance (owner boosted, original saved, restored on unlock)
- [ ] Binary semaphore (take/give with wait list)
- [ ] Counting semaphore (bounded count, wait list)
- [ ] Message queue (ring buffer + semaphore signaling)
- [ ] 4+ tasks with different priorities communicating via queues
- [ ] GDB verification showing priority inheritance in action
- [ ] All four language implementations (C, Rust, Ada, Zig)

---

## Language Comparison

| Feature | C | Rust | Ada | Zig |
|---|---|---|---|---|
| **Task definition** | Function pointer + manual TCB init | `fn(*mut ())` + stack init in `UnsafeCell` | `task type` with `pragma Priority` | Generic `TCB(StackSize)` with `init()` |
| **Context switch** | `__attribute__((naked))` GNU asm | `asm!` macro with symbol interop | Ravenscar runtime (auto-generated) | `callconv(.Naked)` with inline asm |
| **Mutex** | Manual lock/unlock + priority inheritance chain | `PriorityMutex` wrapper around `UnsafeCell<Mutex>` | Protected object (automatic mutual exclusion) | Generic `Mutex(TcbType)` with comptime methods |
| **Priority inheritance** | Manual: boost owner, save original, restore on unlock | Same as C, wrapped in type-safe struct | Priority ceiling (Ravenscar, compile-time) | Same as C, generic over TCB type |
| **Semaphore** | Struct with count + wait list | Same as C, wrapped in `UnsafeCell` | N/A (use protected object entries) | Generic `Semaphore(TcbType)` |
| **Message queue** | Ring buffer array + counting semaphore | `TypedQueue<T>` with generic payload | Protected object with `entry Send/Receive` | Generic `MessageQueue(TcbType, Size, MsgSize)` |
| **Sleep/delay** | `rtos_sleep(ticks)` | `rtos_sleep(ticks)` | `delay until Next_Release` | `rtos_sleep(ticks)` |
| **Preemption trigger** | `should_preempt()` in SysTick, pend PendSV | Same as C | Automatic (Ravenscar runtime) | Same as C |
| **Type safety** | None — raw pointers, manual offsets | `UnsafeCell` marks unsafety, generic types | Strong typing, protected object guarantees | Comptime generics, explicit `*anyopaque` |
| **Runtime overhead** | Minimal — direct struct access | Minimal — same as C after monomorphization | Higher — Ravenscar runtime abstraction | Minimal — comptime resolves generics |
| **Analyzability** | Manual WCET analysis required | Manual WCET analysis required | Ravenscar enables formal schedulability analysis | Comptime config enables static analysis |

---

## What You Learned

- How preemptive scheduling differs from cooperative: the kernel can forcibly remove a running task
- PendSV as the mechanism for deferred context switching at the lowest interrupt priority
- The priority inversion problem and how priority inheritance resolves it
- The difference between mutexes (ownership, priority inheritance) and semaphores (signaling, no ownership)
- How message queues combine ring buffers with semaphore signaling for safe producer/consumer communication
- How each language approaches RTOS primitives:
  - C: Manual implementation with naked functions and raw pointers
  - Rust: Type-safe wrappers (`PriorityMutex`, `TypedQueue<T>`) around unsafe primitives
  - Ada: Language-level tasks and protected objects — the compiler generates the RTOS
  - Zig: Comptime-generic primitives (`Mutex(TcbType)`, `MessageQueue(TcbType, N, M)`)

## Next Steps

- **Project 11**: Build a CAN bus node with OBD-II protocol support
- Add deadline-based scheduling (Earliest Deadline First)
- Implement a tickless idle mode to save power when all tasks are blocked
- Add memory pools for zero-allocation message passing
- Port to a different architecture (RISC-V with CLINT/MTIME)
- Compare your kernel's context switch overhead to FreeRTOS or Zephyr
- Add tracing/instrumentation to visualize task state transitions
---

## References

### STMicroelectronics Documentation
- [STM32F4 Reference Manual (RM0090)](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-415-stm32f407-417-stm32f427-437-and-stm32f429-439-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf) — Ch. 14: SysTick (periodic tick source), Ch. 7: RCC (clock configuration)

### ARM Documentation
- [Cortex-M4 Technical Reference Manual](https://developer.arm.com/documentation/ddi0439/latest/) — Ch. 3: PendSV exception (designed for context switching, lowest priority), FPU context (FPCCR lazy stacking, FPCA bit in EXC_RETURN, S0-S15 + FPSCR save/restore), Ch. 8: NVIC (priority grouping)
- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/) — B1.4: Exception entry/return (full context switch sequence), B1.5: PendSV (pended while other ISRs run), B3.2: Stack alignment (8-byte alignment requirement)
- [ARM EABI Specification (AAPCS)](https://github.com/ARM-software/abi-aa/releases) — Register preservation rules for context switch

### Tools & Emulation
- [QEMU ARM Documentation](https://www.qemu.org/docs/master/system/target-arm.html) — GDB debugging of context switches, PSP inspection
- [QEMU STM32 Documentation](https://www.qemu.org/docs/master/system/arm/stm32.html) — netduinoplus2 machine
