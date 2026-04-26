/* linker.ld — STM32F405 linker script for bare-metal Rust */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
}

ENTRY(Reset)

SECTIONS
{
    .text 0x08000000 :
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