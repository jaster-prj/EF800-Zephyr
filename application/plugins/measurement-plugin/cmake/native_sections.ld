SECTIONS
{
    .text : ALIGN(4) { *(.text*) }
    .rodata : ALIGN(4) {
        KEEP(*(.rodata.pm_descriptor))
        *(.rodata*)
    }
    .data : ALIGN(4) { *(.data*) }
    .bss (NOLOAD) : ALIGN(4) { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.init) *(.fini) *(.eh_frame*) }
}
