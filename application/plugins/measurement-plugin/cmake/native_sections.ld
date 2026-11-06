SECTIONS
{
    .text 0x00000000 : ALIGN(1024) { *(.text*) }
    .rodata 0x00002000 : ALIGN(1024) {
        KEEP(*(.rodata.pm_descriptor))
        *(.rodata*)
    }
    .data 0x20010000 : ALIGN(1024) { *(.data*) }
    .bss (NOLOAD) : ALIGN(1024) { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.init) *(.fini) *(.eh_frame*) }
}
