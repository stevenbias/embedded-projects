.syntax unified
.thumb

.extern _sdata
.extern _edata
.extern _lma_sdata
.extern _sbss
.extern _ebss

.global Reset_Handler

/* Vector table — placed in .isr_vector section */
.section .isr_vector, "a", %progbits
    .word _stack_top  /* Initial stack pointer (top of RAM) */
    .word Reset_Handler

.section .text.Reset_Handler, "ax", %progbits
.thumb_func
Reset_Handler:
    ldr r0, =_sdata             /* Start of .data in RAM */
    ldr r1, =_edata             /* End of .data in RAM */
    ldr r2, =_lma_sdata         /* Start of .data in Flash */
    movs r3, #0                 /* Offset for copying */

copy_data:
    /* If _sbss == _ebss, no bss to zero */
    cmp r0, r1
    beq zero_bss

    ldr r3, [r2], #4            /* Load from Flash, post-increment */
    str r3, [r0], #4            /* Store to RAM, post-increment */
    b copy_data

zero_bss:
    /* Zero .bss section */
    ldr r0, =_sbss              /* Start of .bss in RAM */
    ldr r1, =_ebss              /* End of .bss in RAM */
    movs r2, #0                 /* Zero value */

zero_loop:
    cmp  r0, r1
    beq  _main
    str r2, [r0], #4            /* Store zero, post-increment */
    b zero_loop

.section .text
_main:
    bl main

    /* If main returns, hang */
hang:
    b    hang
