/* startup.s — Cortex-M4F startup for STM32F405 */

    .syntax unified
    .cpu cortex-m4
    .thumb

/* External symbols defined by the linker */
    .extern _sdata
    .extern _edata
    .extern _lma_sdata
    .extern _sbss
    .extern _ebss
    .extern _stack_top

    .global Reset_Handler
    .global Default_Handler

/* Vector table — defined here so assembler sets Thumb bit on Reset_Handler */
    .section .isr_vector, "a", %progbits
    .word _stack_top
    .word Reset_Handler

    .section .text.Reset_Handler, "ax", %progbits
    .thumb_func
Reset_Handler:
    /* Copy .data from flash to RAM */
    ldr  r0, =_sdata
    ldr  r1, =_edata
    ldr  r2, =_lma_sdata
    movs r3, #0
copy_data:
    cmp  r0, r1
    beq  zero_bss
    ldr  r4, [r2, r3]
    str  r4, [r0, r3]
    adds r3, r3, #4
    b    copy_data

    /* Zero .bss */
zero_bss:
    ldr  r0, =_sbss
    ldr  r1, =_ebss
    movs r2, #0
zero_loop:
    cmp  r0, r1
    beq  call_main
    str  r2, [r0]
    adds r0, r0, #4
    b    zero_loop

    /* Call main() */
call_main:
    bl   main

    /* If main returns, hang */
hang:
    b    hang

    .size Reset_Handler, . - Reset_Handler

/* Catch-all handler for unused exceptions */
    .section .text.Default_Handler, "ax", %progbits
    .type Default_Handler, %function
Default_Handler:
    b    .
    .size Default_Handler, . - Default_Handler
