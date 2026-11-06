SECTIONS
{
    .text : ALIGN(1024) { *(.text*) } > PM_PLUGIN_TEXT
    .rodata : ALIGN(1024) {
        KEEP(*(.rodata.pm_descriptor))
        *(.rodata*)
    } > PM_PLUGIN_RODATA
    .data : ALIGN(1024) { *(.data*) } > PM_PLUGIN_DATA AT> PM_PLUGIN_TEXT
    .bss (NOLOAD) : ALIGN(1024) { *(.bss*) *(COMMON) } > PM_PLUGIN_DATA
    /DISCARD/ : { *(.isr_vector) *(.init) *(.fini) *(.ARM.exidx*) *(.eh_frame*) }
}
