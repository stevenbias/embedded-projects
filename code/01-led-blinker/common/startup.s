/* startup.s — Cortex-M4F startup for STM32F405 */

    .syntax unified
    .cpu cortex-m4
    .thumb

/* External symbols defined by the linker */
    .extern _data_start
    .extern _data_end
    .extern _data_loadaddr
    .extern _bss_start
    .extern _bss_end

    .global Reset_Handler
    .global Default_Handler

    .section .text.Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    /* Copy .data from flash to RAM */
    ldr  r0, =_data_start
    ldr  r1, =_data_end
    ldr  r2, =_data_loadaddr
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
    ldr  r0, =_bss_start
    ldr  r1, =_bss_end
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
    .section .text.Default_Handler
    .type Default_Handler, %function
Default_Handler:
    b    .
    .size Default_Handler, . - Default_Handler
