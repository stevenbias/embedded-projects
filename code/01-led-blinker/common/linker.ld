/* linker.ld — STM32F405 memory layout */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

/* Top of stack — grows downward from end of RAM */
_stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    /* Vector table must be at the very start of flash */
    .vector_table :
    {
        LONG(_stack_top)          /* Initial MSP */
        LONG(Reset_Handler)       /* Reset handler address */
    } > FLASH

    .text :
    {
        *(.text*)
    } > FLASH

    .rodata :
    {
        *(.rodata*)
    } > FLASH

    /* .data section — lives in RAM, initialized from flash */
    _data_start = .;
    .data :
    {
        *(.data*)
    } > RAM AT > FLASH
    _data_end = .;
    _data_loadaddr = LOADADDR(.data);

    /* .bss section — zeroed at startup */
    .bss :
    {
        *(.bss*)
        *(COMMON)
    } > RAM
    _bss_start = .;
    _bss_end = .;

    /DISCARD/ : { *(.eh_frame*) }
}

ENTRY(Reset_Handler)
