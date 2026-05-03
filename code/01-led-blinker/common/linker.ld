/* linker.ld — STM32F405 memory layout */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

/* Top of stack — grows downward from end of RAM */
_stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{

    /* Vector table — defined in startup.s */
    .isr_vector : ALIGN(4)
    {
        _svtor = .;
        KEEP(*(.isr_vector))
        . = ALIGN(4);
        _evtor = .;
    } > FLASH

    .text : ALIGN(4)
    {
        . = _evtor; /* Place .text after the vector table */
        *(.text*)
        . = ALIGN(4);
    } > FLASH

    .rodata : ALIGN(4)
    {
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* .data section — lives in RAM, initialized from flash */
    .data : ALIGN(4)
    {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    /* .bss section — zeroed at startup */
    .bss : ALIGN(4)
    {
        _sbss = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM
}

/* Load addresses for copying from flash to RAM at startup */
_lma_sdata = LOADADDR(.data);
_lma_edata = _lma_sdata + SIZEOF(.data);
_lma_stext = LOADADDR(.text);
_lma_etext = _lma_stext + SIZEOF(.text);
_lma_svtor = LOADADDR(.isr_vector);
_lma_evtor = _lma_svtor + SIZEOF(.isr_vector);
