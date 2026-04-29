/* linker.ld — STM32F405 linker script for bare-metal Rust */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
}

ENTRY(Reset)
EXTERN(RESET_VECTOR)

/* Top of stack — grows downward from end of RAM */
_stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .vector_table ORIGIN(FLASH) :
    {
      /* First entry: initial Stack Pointer value */
      LONG(_stack_top)          /* Initial MSP */
      /* Second entry: reset vector */
      KEEP(*(.vector_table.reset_vector))
    } > FLASH

    .text :
    {
        LONG(0x20020000);
        LONG(Reset);
        *(.text*)
    } > FLASH

    .rodata :
    {
        *(.rodata*)
    } > FLASH

    .init :
    {
        *(.init)
    } > FLASH

    .fini :
    {
        *(.fini)
    } > FLASH

    .data : ALIGN(4)
    {
        _sdata = .;
        *(.data*)
        _edata = .;
    } > RAM AT > FLASH

    _sidata = LOADADDR(.data);

    .bss : ALIGN(4)
    {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
    } > RAM

    /DISCARD/ : { *(.eh_frame*) }
}
