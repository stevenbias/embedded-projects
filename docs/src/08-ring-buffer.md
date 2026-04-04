---
title: "Project 8: Ring Buffer Library (Lock-Free)"
phase: 3
project: 8
---

# Project 8: Ring Buffer Library (Lock-Free)

In this project you will implement a lock-free Single-Producer Single-Consumer (SPSC) ring buffer in **C, Rust, Ada, and Zig**. Ring buffers are the most common data structure in embedded systems — they connect interrupt handlers to main-loop processing, DMA engines to application code, and communication peripherals to protocol stacks.

You will learn why `volatile` is not enough for concurrent access, how memory ordering prevents data races on weakly-ordered architectures, and how each language's atomic primitives express the same synchronization guarantees.

This project builds on Project 7 (Cooperative Scheduler). The ring buffer you build here is the primary mechanism tasks use to communicate without blocking.

## What You'll Learn

- SPSC (Single-Producer Single-Consumer) lock-free design
- Memory ordering: acquire, release, and acquire-release semantics
- Why `volatile` does not provide synchronization
- Cache line considerations and false sharing
- Generic programming patterns across four languages
- Integration with UART interrupt-driven I/O
- Host unit testing for lock-free data structures
- Formal reasoning about concurrent correctness

## Prerequisites

- ARM GCC toolchain (`arm-none-eabi-gcc`)
- Rust: `cargo`, `cortex-m` crate
- Ada: GNAT ARM toolchain
- Zig: Zig 0.11+
- QEMU with UART support
- Host compiler for unit tests (gcc, rustc, gnat, zig)

---

## Lock-Free SPSC Design

A ring buffer is a circular array with two indices: a head (write position) and a tail (read position). In the SPSC case, only one thread writes (advances head) and only one thread reads (advances tail).

```
        tail                    head
         v                       v
    +----+----+----+----+----+----+
    | D3 | D4 |    |    | D1 | D2 |
    +----+----+----+----+----+----+
         ^                       ^
      consumed               produced
```

The buffer is:
- **Empty** when `head == tail`
- **Full** when `(head + 1) % capacity == tail`

We waste one slot to distinguish empty from full without a separate count variable.

### Why Lock-Free?

In an embedded system, the producer is often an interrupt handler. Interrupt handlers cannot block. A mutex-based ring buffer would deadlock if the interrupt tried to acquire a lock held by the main loop. A lock-free SPSC ring buffer requires no locks because each index is owned by exactly one thread:

- The **producer** owns `head` — only the producer writes it
- The **consumer** owns `tail` — only the consumer writes it
- Each thread reads the other's index to check capacity

### Memory Ordering

On Cortex-M (ARMv7-M), the processor is **weakly ordered**. Reads and writes can be reordered by the CPU and the memory system. Without proper barriers, the consumer might see an updated `head` before it sees the data written to the buffer.

The correct ordering for SPSC is:

**Producer (push):**
1. Write data to `buffer[head]`
2. **Release barrier** — ensures the data write is visible before the head update
3. Update `head`

**Consumer (pop):**
1. Read `head` with **acquire barrier** — ensures we see all writes the producer made before updating head
2. Read data from `buffer[tail]`
3. Update `tail`

On Cortex-M, `__DMB()` (Data Memory Barrier) provides the necessary ordering. The ARM architecture guarantees that all memory accesses before the DMB complete before any access after it.

---

## Implementation: C

### Header (`ringbuf.h`)

```c
#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Capacity must be a power of 2 for efficient masking */
typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t mask;
    volatile uint32_t head;  /* Written by producer only */
    volatile uint32_t tail;  /* Written by consumer only */
} ringbuf_t;

/* Initialize a ring buffer. Buffer memory must be provided by caller. */
void ringbuf_init(ringbuf_t *rb, uint8_t *buf, size_t capacity);

/* Push a single byte. Returns true on success, false if full. */
bool ringbuf_push(ringbuf_t *rb, uint8_t data);

/* Pop a single byte. Returns true on success, false if empty. */
bool ringbuf_pop(ringbuf_t *rb, uint8_t *data);

/* Push multiple bytes. Returns number of bytes pushed. */
size_t ringbuf_push_n(ringbuf_t *rb, const uint8_t *data, size_t len);

/* Pop multiple bytes. Returns number of bytes popped. */
size_t ringbuf_pop_n(ringbuf_t *rb, uint8_t *data, size_t len);

/* Query functions (safe to call from either thread) */
size_t ringbuf_count(const ringbuf_t *rb);
size_t ringbuf_free(const ringbuf_t *rb);
bool ringbuf_is_empty(const ringbuf_t *rb);
bool ringbuf_is_full(const ringbuf_t *rb);

/* Generic (void*) ring buffer for arbitrary-sized elements */
typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t elem_size;
    size_t mask;
    volatile uint32_t head;
    volatile uint32_t tail;
} ringbuf_generic_t;

void ringbuf_generic_init(ringbuf_generic_t *rb, uint8_t *buf,
                          size_t capacity, size_t elem_size);
bool ringbuf_generic_push(ringbuf_generic_t *rb, const void *data);
bool ringbuf_generic_pop(ringbuf_generic_t *rb, void *data);
size_t ringbuf_generic_count(const ringbuf_generic_t *rb);

#endif
```

### Implementation (`ringbuf.c`)

```c
#include "ringbuf.h"
#include <string.h>

/* ARM Cortex-M memory barrier */
static inline void acquire_barrier(void) {
    __asm volatile ("dmb ish" ::: "memory");
}

static inline void release_barrier(void) {
    __asm volatile ("dmb ish" ::: "memory");
}

void ringbuf_init(ringbuf_t *rb, uint8_t *buf, size_t capacity) {
    rb->buffer = buf;
    rb->capacity = capacity;
    rb->mask = capacity - 1;
    rb->head = 0;
    rb->tail = 0;
}

bool ringbuf_push(ringbuf_t *rb, uint8_t data) {
    uint32_t head = rb->head;
    uint32_t next = (head + 1) & rb->mask;

    if (next == rb->tail) {
        return false; /* Buffer full */
    }

    rb->buffer[head] = data;

    /* Release barrier: ensure data write is visible before head update */
    release_barrier();

    rb->head = next;
    return true;
}

bool ringbuf_pop(ringbuf_t *rb, uint8_t *data) {
    uint32_t tail = rb->tail;

    if (tail == rb->head) {
        return false; /* Buffer empty */
    }

    /* Acquire barrier: ensure we see the producer's data write */
    acquire_barrier();

    *data = rb->buffer[tail];
    rb->tail = (tail + 1) & rb->mask;
    return true;
}

size_t ringbuf_push_n(ringbuf_t *rb, const uint8_t *data, size_t len) {
    size_t pushed = 0;
    while (pushed < len && ringbuf_push(rb, data[pushed])) {
        pushed++;
    }
    return pushed;
}

size_t ringbuf_pop_n(ringbuf_t *rb, uint8_t *data, size_t len) {
    size_t popped = 0;
    while (popped < len && ringbuf_pop(rb, &data[popped])) {
        popped++;
    }
    return popped;
}

size_t ringbuf_count(const ringbuf_t *rb) {
    uint32_t head = rb->head;
    uint32_t tail = rb->tail;
    return (head - tail) & rb->mask;
}

size_t ringbuf_free(const ringbuf_t *rb) {
    return rb->capacity - ringbuf_count(rb) - 1;
}

bool ringbuf_is_empty(const ringbuf_t *rb) {
    return rb->head == rb->tail;
}

bool ringbuf_is_full(const ringbuf_t *rb) {
    return ((rb->head + 1) & rb->mask) == rb->tail;
}

/* Generic implementation */
void ringbuf_generic_init(ringbuf_generic_t *rb, uint8_t *buf,
                          size_t capacity, size_t elem_size) {
    rb->buffer = buf;
    rb->capacity = capacity;
    rb->elem_size = elem_size;
    rb->mask = capacity - 1;
    rb->head = 0;
    rb->tail = 0;
}

bool ringbuf_generic_push(ringbuf_generic_t *rb, const void *data) {
    uint32_t head = rb->head;
    uint32_t next = (head + 1) & rb->mask;

    if (next == rb->tail) {
        return false;
    }

    memcpy(&rb->buffer[head * rb->elem_size], data, rb->elem_size);

    release_barrier();

    rb->head = next;
    return true;
}

bool ringbuf_generic_pop(ringbuf_generic_t *rb, void *data) {
    uint32_t tail = rb->tail;

    if (tail == rb->head) {
        return false;
    }

    acquire_barrier();

    memcpy(data, &rb->buffer[tail * rb->elem_size], rb->elem_size);

    rb->tail = (tail + 1) & rb->mask;
    return true;
}

size_t ringbuf_generic_count(const ringbuf_generic_t *rb) {
    uint32_t head = rb->head;
    uint32_t tail = rb->tail;
    return (head - tail) & rb->mask;
}
```

> **Warning:** `volatile` on `head` and `tail` prevents the compiler from caching the values in registers, but it does **not** provide any memory ordering guarantees. The `dmb ish` barriers are essential for correctness. On Cortex-M, `__DMB()` is sufficient because the processor executes instructions in order — the barrier ensures the memory system observes writes in the correct order.

### Host Unit Test (`test_ringbuf.c`)

```c
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include "ringbuf.h"

#define BUF_SIZE 256
#define NUM_ITEMS 10000

static ringbuf_t rb;
static uint8_t buffer[BUF_SIZE];

static void *producer_thread(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < NUM_ITEMS; i++) {
        while (!ringbuf_push(&rb, (uint8_t)(i & 0xFF))) {
            /* Spin until space available */
            __asm volatile ("yield" ::: "memory");
        }
    }
    return NULL;
}

static void *consumer_thread(void *arg) {
    (void)arg;
    uint32_t received = 0;
    uint8_t data;
    uint8_t expected = 0;

    while (received < NUM_ITEMS) {
        if (ringbuf_pop(&rb, &data)) {
            assert(data == expected);
            expected++;
            received++;
        }
    }
    return NULL;
}

int main(void) {
    ringbuf_init(&rb, buffer, BUF_SIZE);

    /* Basic functionality tests */
    assert(ringbuf_is_empty(&rb));
    assert(!ringbuf_is_full(&rb));

    /* Push and pop single items */
    assert(ringbuf_push(&rb, 42));
    assert(!ringbuf_is_empty(&rb));
    uint8_t val;
    assert(ringbuf_pop(&rb, &val));
    assert(val == 42);
    assert(ringbuf_is_empty(&rb));

    /* Fill and check full */
    for (size_t i = 0; i < BUF_SIZE - 1; i++) {
        assert(ringbuf_push(&rb, (uint8_t)i));
    }
    assert(ringbuf_is_full(&rb));
    assert(!ringbuf_push(&rb, 0xFF));

    /* Wrap-around test */
    for (size_t i = 0; i < BUF_SIZE - 1; i++) {
        assert(ringbuf_pop(&rb, &val));
        assert(val == (uint8_t)i);
    }
    assert(ringbuf_is_empty(&rb));

    /* Concurrent test */
    pthread_t prod, cons;
    pthread_create(&prod, NULL, producer_thread, NULL);
    pthread_create(&cons, NULL, consumer_thread, NULL);
    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    printf("All tests passed!\n");
    return 0;
}
```

Build and run:
```bash
gcc -O2 -pthread -o test_ringbuf test_ringbuf.c ringbuf.c
./test_ringbuf
```

---

## Implementation: Rust

### Project Setup

```bash
cargo init --lib --name ringbuf-rust
```

### `Cargo.toml`

```toml
[package]
name = "ringbuf-rust"
version = "0.1.0"
edition = "2021"

[dependencies]
cortex-m = { version = "0.7", optional = true }

[features]
default = []
embedded = ["cortex-m"]
```

### `src/lib.rs`

```rust
#![no_std]

use core::sync::atomic::{AtomicUsize, Ordering};

/// A lock-free SPSC ring buffer with compile-time capacity.
///
/// # Safety
/// This buffer is safe for concurrent access with exactly one producer
/// and one consumer. Multiple producers or multiple consumers will
/// cause data races.
pub struct RingBuffer<T, const CAP: usize> {
    buffer: [core::mem::MaybeUninit<T>; CAP],
    head: AtomicUsize,
    tail: AtomicUsize,
}

// SAFETY: RingBuffer can be shared between threads. The SPSC contract
// ensures that head is only written by the producer and tail only by
// the consumer.
unsafe impl<T: Send, const CAP: usize> Sync for RingBuffer<T, CAP> {}

impl<T, const CAP: usize> RingBuffer<T, CAP> {
    /// Create a new ring buffer. Capacity must be a power of 2.
    pub const fn new() -> Self {
        assert!(CAP.is_power_of_two(), "Capacity must be a power of 2");
        assert!(CAP > 0, "Capacity must be greater than 0");

        Self {
            buffer: [const { core::mem::MaybeUninit::uninit() }; CAP],
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        }
    }

    const MASK: usize = CAP - 1;

    /// Push an item into the buffer. Returns Ok(()) on success,
    /// or Err(item) if the buffer is full.
    pub fn push(&self, item: T) -> Result<(), T> {
        let head = self.head.load(Ordering::Relaxed);
        let next = (head + 1) & Self::MASK;

        let tail = self.tail.load(Ordering::Acquire);
        if next == tail {
            return Err(item);
        }

        // SAFETY: head is within bounds because of the mask
        unsafe {
            self.buffer[head]
                .as_mut_ptr()
                .write(item);
        }

        // Release ordering: ensures the data write above is visible
        // before the head update is visible to the consumer.
        self.head.store(next, Ordering::Release);
        Ok(())
    }

    /// Pop an item from the buffer. Returns Some(item) on success,
    /// or None if the buffer is empty.
    pub fn pop(&self) -> Option<T> {
        let tail = self.tail.load(Ordering::Relaxed);

        let head = self.head.load(Ordering::Acquire);
        if tail == head {
            return None;
        }

        // Acquire ordering: ensures we see the producer's data write
        // before we read the data.

        // SAFETY: tail is within bounds because of the mask
        let item = unsafe { self.buffer[tail].as_ptr().read() };

        self.tail.store((tail + 1) & Self::MASK, Ordering::Release);
        Some(item)
    }

    /// Returns the number of items currently in the buffer.
    /// Note: this is a snapshot and may be stale immediately.
    pub fn len(&self) -> usize {
        let head = self.head.load(Ordering::Acquire);
        let tail = self.tail.load(Ordering::Acquire);
        (head.wrapping_sub(tail)) & Self::MASK
    }

    /// Returns true if the buffer is empty.
    pub fn is_empty(&self) -> bool {
        self.head.load(Ordering::Acquire) == self.tail.load(Ordering::Acquire)
    }

    /// Returns the number of free slots.
    pub fn free(&self) -> usize {
        CAP - self.len() - 1
    }
}

impl<T, const CAP: usize> Drop for RingBuffer<T, CAP> {
    fn drop(&mut self) {
        while self.pop().is_some() {}
    }
}

/* Byte-optimized version for UART use */
pub type ByteRingBuffer<const CAP: usize> = RingBuffer<u8, CAP>;

#[cfg(feature = "embedded")]
mod embedded_impl {
    use super::*;
    use core::sync::atomic::compiler_fence;

    impl<const CAP: usize> RingBuffer<u8, CAP> {
        /// Push using Cortex-M DMB barrier explicitly.
        /// This is equivalent to the Release ordering above but
        /// makes the barrier visible for educational purposes.
        pub fn push_with_barrier(&self, item: u8) -> Result<(), u8> {
            let head = self.head.load(Ordering::Relaxed);
            let next = (head + 1) & Self::MASK;

            let tail = self.tail.load(Ordering::Acquire);
            if next == tail {
                return Err(item);
            }

            unsafe {
                self.buffer[head].as_mut_ptr().write(item);
            }

            // On Cortex-M, Release ordering implies a DMB
            // This explicit fence is redundant but educational
            compiler_fence(Ordering::Release);
            cortex_m::asm::dmb();

            self.head.store(next, Ordering::Release);
            Ok(())
        }
    }
}
```

### Host Unit Test (`tests/ringbuf_test.rs`)

```rust
use ringbuf_rust::RingBuffer;
use std::thread;
use std::sync::Arc;

const CAP: usize = 256;
const NUM_ITEMS: usize = 10000;

#[test]
fn test_empty_and_full() {
    let rb: RingBuffer<u8, CAP> = RingBuffer::new();
    assert!(rb.is_empty());
    assert_eq!(rb.len(), 0);
    assert_eq!(rb.free(), CAP - 1);

    // Fill the buffer
    for i in 0..CAP - 1 {
        assert!(rb.push(i as u8).is_ok());
    }
    assert!(!rb.is_empty());
    assert_eq!(rb.len(), CAP - 1);
    assert_eq!(rb.free(), 0);

    // Should fail when full
    assert!(rb.push(0xFF).is_err());
}

#[test]
fn test_push_pop() {
    let rb: RingBuffer<u8, CAP> = RingBuffer::new();

    assert!(rb.push(42).is_ok());
    assert_eq!(rb.pop(), Some(42));
    assert!(rb.is_empty());
}

#[test]
fn test_wrap_around() {
    let rb: RingBuffer<u8, 4> = RingBuffer::new(); // Small buffer for wrap test

    // Push 3, pop 3, push 3 again (wraps around)
    for i in 0..3u8 {
        assert!(rb.push(i).is_ok());
    }
    for i in 0..3u8 {
        assert_eq!(rb.pop(), Some(i));
    }

    // Now push again — should wrap
    for i in 10..13u8 {
        assert!(rb.push(i).is_ok());
    }
    for i in 10..13u8 {
        assert_eq!(rb.pop(), Some(i));
    }
}

#[test]
fn test_concurrent_spsc() {
    let rb = Arc::new(RingBuffer::<u8, CAP>::new());

    let producer = {
        let rb = Arc::clone(&rb);
        thread::spawn(move || {
            for i in 0..NUM_ITEMS {
                loop {
                    if rb.push((i & 0xFF) as u8).is_ok() {
                        break;
                    }
                    thread::yield_now();
                }
            }
        })
    };

    let consumer = {
        let rb = Arc::clone(&rb);
        thread::spawn(move || {
            let mut received = 0;
            let mut expected: u8 = 0;

            while received < NUM_ITEMS {
                if let Some(val) = rb.pop() {
                    assert_eq!(val, expected, "Mismatch at item {}", received);
                    expected = expected.wrapping_add(1);
                    received += 1;
                }
            }
        })
    };

    producer.join().unwrap();
    consumer.join().unwrap();
}

#[test]
fn test_generic_types() {
    let rb: RingBuffer<u32, 16> = RingBuffer::new();

    assert!(rb.push(0xDEADBEEF).is_ok());
    assert!(rb.push(0xCAFEBABE).is_ok());
    assert_eq!(rb.pop(), Some(0xDEADBEEF));
    assert_eq!(rb.pop(), Some(0xCAFEBABE));
}
```

Run tests:
```bash
cargo test
```

### Embedded Integration: UART with Ring Buffer

```rust
// In your embedded application:
use ringbuf_rust::ByteRingBuffer;

static UART_RX_BUF: ByteRingBuffer<256> = ByteRingBuffer::new();

// UART interrupt handler (producer)
#[interrupt]
fn USART1() {
    // Read data register
    let data = read_uart_dr();
    // Push to ring buffer (non-blocking)
    let _ = UART_RX_BUF.push(data);
}

// Main loop (consumer)
fn main() -> ! {
    loop {
        if let Some(byte) = UART_RX_BUF.pop() {
            process_uart_byte(byte);
        }
    }
}
```

---

## Implementation: Ada

### Package Spec (`lock_free_buffer.ads`)

```ada
with System;
with System.Atomic_Operations;

generic
   type Element_Type is private;
   Capacity : Positive;
package Lock_Free_Buffer is

   pragma Precondition (Capacity > 1 and then
     (Capacity and (Capacity - 1)) = 0,
     "Capacity must be a power of 2 greater than 1");

   type Buffer is limited private;

   procedure Initialize (B : in out Buffer);

   function Push (B : in out Buffer; Item : Element_Type) return Boolean;
   function Pop  (B : in out Buffer; Item : out Element_Type) return Boolean;

   function Count (B : Buffer) return Natural;
   function Is_Empty (B : Buffer) return Boolean;
   function Is_Full  (B : Buffer) return Boolean;

private

   use System.Atomic_Operations;

   type Index_Type is mod Capacity;

   type Buffer is record
      Data    : array (Index_Type) of Element_Type;
      Head    : aliased Index_Type := 0;
      Tail    : aliased Index_Type := 0;
   end record;

   pragma Atomic_Components (Buffer.Data);
   pragma Atomic (Buffer.Head);
   pragma Atomic (Buffer.Tail);

end Lock_Free_Buffer;
```

### Package Body (`lock_free_buffer.adb`)

```ada
with System.Memory_Barriers;

package body Lock_Free_Buffer is

   use System.Memory_Barriers;

   Mask : constant Index_Type := Index_Type (Capacity - 1);

   procedure Initialize (B : in out Buffer) is
   begin
      B.Head := 0;
      B.Tail := 0;
   end Initialize;

   function Push (B : in out Buffer; Item : Element_Type) return Boolean is
      Head : constant Index_Type := B.Head;
      Next : constant Index_Type := (Head + 1) and Mask;
   begin
      if Next = B.Tail then
         return False;  -- Buffer full
      end if;

      B.Data (Head) := Item;

      -- Release barrier: data write must be visible before head update
      Memory_Barrier_Release;

      B.Head := Next;
      return True;
   end Push;

   function Pop (B : in out Buffer; Item : out Element_Type) return Boolean is
      Tail : constant Index_Type := B.Tail;
   begin
      if Tail = B.Head then
         return False;  -- Buffer empty
      end if;

      -- Acquire barrier: must see producer's data write
      Memory_Barrier_Acquire;

      Item := B.Data (Tail);
      B.Tail := (Tail + 1) and Mask;
      return True;
   end Pop;

   function Count (B : Buffer) return Natural is
      Head : constant Index_Type := B.Head;
      Tail : constant Index_Type := B.Tail;
   begin
      return Natural ((Head - Tail) and Mask);
   end Count;

   function Is_Empty (B : Buffer) return Boolean is
   begin
      return B.Head = B.Tail;
   end Is_Empty;

   function Is_Full (B : Buffer) return Boolean is
   begin
      return ((B.Head + 1) and Mask) = B.Tail;
   end Is_Full;

end Lock_Free_Buffer;
```

### Protected Entry Alternative (Idiomatic Ada)

For production Ada code, protected entries provide a cleaner interface:

```ada
generic
   type Element_Type is private;
   Capacity : Positive;
package Bounded_Buffer is

   protected type Buffer is
      entry Put (Item : in Element_Type);
      entry Get (Item : out Element_Type);
      function Count return Natural;
      function Is_Empty return Boolean;
      function Is_Full return Boolean;
   private
      Data  : array (1 .. Capacity) of Element_Type;
      Head  : Positive := 1;
      Tail  : Positive := 1;
      Used  : Natural := 0;
   end Buffer;

end Bounded_Buffer;

package body Bounded_Buffer is

   protected body Buffer is

      entry Put (Item : in Element_Type)
        when Used < Capacity
      is
      begin
         Data (Head) := Item;
         Head := (Head mod Capacity) + 1;
         Used := Used + 1;
      end Put;

      entry Get (Item : out Element_Type)
        when Used > 0
      is
      begin
         Item := Data (Tail);
         Tail := (Tail mod Capacity) + 1;
         Used := Used - 1;
      end Get;

      function Count return Natural is
      begin
         return Used;
      end Count;

      function Is_Empty return Boolean is
      begin
         return Used = 0;
      end Is_Empty;

      function Is_Full return Boolean is
      begin
         return Used = Capacity;
      end Is_Full;

   end Buffer;

end Bounded_Buffer;
```

The protected entry version is blocking (tasks wait when the buffer is full/empty), while the lock-free version returns immediately. Choose based on your use case: lock-free for interrupt handlers, protected entries for task-to-task communication.

### Host Test (`test_buffer.adb`)

```ada
with Ada.Text_IO; use Ada.Text_IO;
with Lock_Free_Buffer;

procedure Test_Buffer is

   package Byte_Buffer is new Lock_Free_Buffer
     (Element_Type => Character, Capacity => 256);

   Buf : Byte_Buffer.Buffer;

   -- Producer task
   task Producer;
   task body Producer is
   begin
      for I in Character'Val (0) .. Character'Val (255) loop
         while not Byte_Buffer.Push (Buf, Character'Val (I)) loop
            null;  -- Spin
         end loop;
      end loop;
   end Producer;

   -- Consumer task
   task Consumer;
   task body Consumer is
      Ch : Character;
      Count : Natural := 0;
   begin
      while Count < 256 loop
         if Byte_Buffer.Pop (Buf, Ch) then
            if Ch /= Character'Val (Count) then
               Put_Line ("ERROR: Expected " & Natural'Image (Count) &
                         " but got " & Character'Pos (Ch)'Image);
            end if;
            Count := Count + 1;
         end if;
      end loop;
      Put_Line ("All 256 bytes received correctly");
   end Consumer;

begin
   Byte_Buffer.Initialize (Buf);

   -- Wait for tasks to complete
   delay 1.0;

   -- Basic tests
   pragma Assert (Byte_Buffer.Is_Empty (Buf));

   pragma Assert (Byte_Buffer.Push (Buf, 'A'));
   pragma Assert (not Byte_Buffer.Is_Empty (Buf));

   declare
      Ch : Character;
   begin
      pragma Assert (Byte_Buffer.Pop (Buf, Ch));
      pragma Assert (Ch = 'A');
   end;

   Put_Line ("All assertions passed");
end Test_Buffer;
```

Build and run:
```bash
gnatmake test_buffer.adb
./test_buffer
```

---

## Implementation: Zig

### `src/ringbuf.zig`

```zig
const std = @import("std");
const Atomic = std.atomic.Value;
const fence = std.atomic.fence;

/// A lock-free SPSC ring buffer with comptime capacity.
pub fn RingBuffer(comptime T: type, comptime capacity: usize) type {
    comptime {
        std.debug.assert(std.math.isPowerOfTwo(capacity));
        std.debug.assert(capacity > 0);
    }

    const mask = capacity - 1;

    return struct {
        buffer: [capacity]T,
        head: Atomic(u32),
        tail: Atomic(u32),

        const Self = @This();

        pub fn init() Self {
            return Self{
                .buffer = undefined,
                .head = Atomic(u32).init(0),
                .tail = Atomic(u32).init(0),
            };
        }

        /// Push an item. Returns true on success, false if full.
        pub fn push(self: *Self, item: T) bool {
            const head = self.head.load(.Relaxed);
            const next = (head + 1) & mask;

            const tail = self.tail.load(.Acquire);
            if (next == tail) {
                return false;
            }

            self.buffer[@intCast(head)] = item;

            // Release fence: data write must be visible before head update
            fence(.Release);

            self.head.store(next, .Release);
            return true;
        }

        /// Pop an item. Returns the item or null if empty.
        pub fn pop(self: *Self) ?T {
            const tail = self.tail.load(.Relaxed);

            const head = self.head.load(.Acquire);
            if (tail == head) {
                return null;
            }

            // Acquire fence: must see producer's data write
            fence(.Acquire);

            const item = self.buffer[@intCast(tail)];

            self.tail.store((tail + 1) & mask, .Release);
            return item;
        }

        /// Number of items currently in the buffer.
        pub fn len(self: *const Self) usize {
            const head = self.head.load(.Acquire);
            const tail = self.tail.load(.Acquire);
            return @as(usize, @intCast((head -% tail) & mask));
        }

        pub fn isEmpty(self: *const Self) bool {
            return self.head.load(.Acquire) == self.tail.load(.Acquire);
        }

        pub fn free(self: *const Self) usize {
            return capacity - self.len() - 1;
        }
    };
}

/// Byte-optimized ring buffer for UART
pub const ByteRingBuffer = RingBuffer(u8, 256);
```

### Host Unit Test (`src/ringbuf_test.zig`)

```zig
const std = @import("std");
const RingBuffer = @import("ringbuf.zig").RingBuffer;
const testing = std.testing;

test "empty and full" {
    var rb = RingBuffer(u8, 4).init();
    try testing.expect(rb.isEmpty());
    try testing.expectEqual(@as(usize, 0), rb.len());
    try testing.expectEqual(@as(usize, 3), rb.free());

    // Fill
    try testing.expect(rb.push(0));
    try testing.expect(rb.push(1));
    try testing.expect(rb.push(2));
    try testing.expect(!rb.push(3)); // Full
    try testing.expectEqual(@as(usize, 3), rb.len());
}

test "push and pop" {
    var rb = RingBuffer(u8, 256).init();

    try testing.expect(rb.push(42));
    try testing.expect(!rb.isEmpty());
    const val = rb.pop();
    try testing.expect(val != null);
    try testing.expectEqual(@as(u8, 42), val.?);
    try testing.expect(rb.isEmpty());
}

test "wrap around" {
    var rb = RingBuffer(u8, 4).init();

    // Push 3, pop 3
    try testing.expect(rb.push(0));
    try testing.expect(rb.push(1));
    try testing.expect(rb.push(2));
    try testing.expectEqual(@as(?u8, 0), rb.pop());
    try testing.expectEqual(@as(?u8, 1), rb.pop());
    try testing.expectEqual(@as(?u8, 2), rb.pop());

    // Push again (wraps)
    try testing.expect(rb.push(10));
    try testing.expect(rb.push(11));
    try testing.expect(rb.push(12));
    try testing.expectEqual(@as(?u8, 10), rb.pop());
    try testing.expectEqual(@as(?u8, 11), rb.pop());
    try testing.expectEqual(@as(?u8, 12), rb.pop());
}

test "generic types" {
    var rb = RingBuffer(u32, 16).init();

    try testing.expect(rb.push(0xDEADBEEF));
    try testing.expect(rb.push(0xCAFEBABE));
    try testing.expectEqual(@as(?u32, 0xDEADBEEF), rb.pop());
    try testing.expectEqual(@as(?u32, 0xCAFEBABE), rb.pop());
}

test "concurrent SPSC" {
    var rb = RingBuffer(u8, 256).init();
    const num_items: u32 = 10000;

    var producer_thread = try std.Thread.spawn(.{}, struct {
        fn run(ringbuf: *RingBuffer(u8, 256)) void {
            var i: u32 = 0;
            while (i < num_items) {
                if (ringbuf.push(@intCast(i & 0xFF))) {
                    i += 1;
                } else {
                    std.Thread.yield();
                }
            }
        }
    }.run, .{&rb});

    var consumer_thread = try std.Thread.spawn(.{}, struct {
        fn run(ringbuf: *RingBuffer(u8, 256)) void {
            var received: u32 = 0;
            var expected: u8 = 0;
            while (received < num_items) {
                if (ringbuf.pop()) |val| {
                    std.debug.assert(val == expected);
                    expected +%= 1;
                    received += 1;
                }
            }
        }
    }.run, .{&rb});

    producer_thread.join();
    consumer_thread.join();
}
```

Run tests:
```bash
zig test src/ringbuf_test.zig
```

### Embedded Integration: UART Interrupt

```zig
const ByteRingBuffer = @import("ringbuf.zig").ByteRingBuffer;

var uart_rx_buf: ByteRingBuffer = ByteRingBuffer.init();

export fn USART1_IRQHandler() void {
    // Read data register (STM32F103)
    const usart1_dr = @as(*volatile u8, @ptrFromInt(0x40013804));
    const data = usart1_dr.*;

    // Push to ring buffer (non-blocking, safe in interrupt)
    _ = uart_rx_buf.push(data);
}

export fn main() noreturn {
    // Configure UART...

    while (true) {
        if (uart_rx_buf.pop()) |byte| {
            process_uart_byte(byte);
        }
    }
}
```

---

## Integration Test: UART Interrupt Producer + Main Loop Consumer

This test demonstrates the ring buffer in its most common embedded use case: UART receive interrupt feeds bytes into the buffer, and the main loop processes them.

### C Implementation

```c
#include "ringbuf.h"

static ringbuf_t uart_rx;
static uint8_t uart_buf[256];

void USART1_IRQHandler(void) {
    /* Check RXNE flag */
    if (USART1->SR & (1 << 5)) {
        uint8_t data = USART1->DR & 0xFF;
        ringbuf_push(&uart_rx, data);
    }
}

int main(void) {
    ringbuf_init(&uart_rx, uart_buf, 256);
    uart_init(115200);

    uint8_t cmd_buf[64];
    size_t cmd_len = 0;

    while (1) {
        uint8_t byte;
        while (ringbuf_pop(&uart_rx, &byte)) {
            if (byte == '\n') {
                /* Process complete command */
                process_command(cmd_buf, cmd_len);
                cmd_len = 0;
            } else if (cmd_len < sizeof(cmd_buf) - 1) {
                cmd_buf[cmd_len++] = byte;
            }
        }
    }
}
```

### QEMU UART Test

```bash
# Build the application
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -Os -g \
    -o uart_ringbuf.elf uart_ringbuf.c ringbuf.c startup.c

# Run QEMU with UART connected to stdio
qemu-system-arm -M stm32-f103 -kernel uart_ringbuf.bin \
    -serial stdio -S -s &

# In GDB, verify the ring buffer state
arm-none-eabi-gdb uart_ringbuf.elf
(gdb) target remote :1234
(gdb) break USART1_IRQHandler
(gdb) break process_command
(gdb) continue

# Send data via QEMU serial
# (type in the QEMU serial window)
(gdb) print uart_rx
$1 = {head = 5, tail = 0, capacity = 256}
```

---

## Deliverables

- [ ] Lock-free SPSC ring buffer with acquire/release memory ordering
- [ ] Push and pop operations that are safe for concurrent producer/consumer
- [ ] Generic element support (not just bytes)
- [ ] Host unit tests with concurrent producer/consumer threads
- [ ] UART interrupt integration test (producer = ISR, consumer = main loop)
- [ ] All four language implementations (C, Rust, Ada, Zig)
- [ ] Wrap-around test verifying correct behavior after multiple cycles

---

## Language Comparison

| Feature | C | Rust | Ada | Zig |
|---|---|---|---|---|
| **Atomic head/tail** | `volatile` + `__DMB()` | `AtomicUsize` with `Ordering` | `pragma Atomic` + barriers | `std.atomic.Value` + `fence` |
| **Memory ordering** | Manual `dmb ish` asm | `Ordering::Acquire`/`Release` | `Memory_Barrier_Acquire`/`Release` | `.Acquire`/`.Release` + `fence()` |
| **Generic elements** | `void*` + `memcpy` | `RingBuffer<T, const CAP>` | `generic type Element_Type` | `RingBuffer(comptime T: type)` |
| **Capacity check** | Runtime assert or comment | `const` assert at compile time | `pragma Precondition` | `comptime` assert |
| **Mask computation** | Runtime `capacity - 1` | `const MASK: usize` | `constant Index_Type` | `comptime const mask` |
| **Drop/cleanup** | Manual (caller's responsibility) | `Drop` impl drains buffer | Controlled by scope | Manual (caller's responsibility) |
| **Thread safety** | Convention (SPSC contract) | `unsafe impl Sync` | Protected types or atomics | Convention (SPSC contract) |
| **Blocking variant** | Semaphore-based | `Mutex` + `Condvar` | Protected entries | `std.Thread.Mutex` |
| **False sharing prevention** | Manual padding | `#[repr(align(64))]` | Representation clauses | `align(64)` |

---

## What You Learned

- Why SPSC is the simplest lock-free pattern: each index has a single writer
- How acquire/release ordering prevents the consumer from seeing stale data
- The difference between `volatile` (prevents compiler reordering) and memory barriers (prevents CPU/memory system reordering)
- How each language expresses atomic operations:
  - C: inline `dmb` + `volatile`
  - Rust: `AtomicUsize` with explicit `Ordering`
  - Ada: `pragma Atomic` with memory barrier intrinsics
  - Zig: `std.atomic.Value` with `fence()`
- Generic programming patterns for type-safe ring buffers
- Integration with UART interrupt-driven I/O

## Next Steps

- **Project 9**: Build a custom bootloader with UART firmware update protocol
- Extend the ring buffer to MPMC (Multiple Producer Multiple Consumer) using atomic CAS
- Add DMA integration: ring buffer as the DMA circular buffer
- Implement a lock-free queue with dynamic allocation
- Compare throughput and latency against a mutex-based buffer