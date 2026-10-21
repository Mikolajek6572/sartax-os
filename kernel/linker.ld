ENTRY(_start)
SECTIONS
{
    . = 0x100000;
    .text : {
        *(.text.boot)
        *(.text .text.*)
    }
    .rodata : ALIGN(16) { *(.rodata .rodata.*) }
    .data : ALIGN(16) { *(.data .data.*) }
    . = ALIGN(16);
    _image_end = .;
    .bss : ALIGN(16) {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        _bss_end = .;
    }
    . = ALIGN(4096);
    _kernel_end = .;
    /DISCARD/ : { *(.eh_frame) *(.note .note.*) *(.comment) }
}
