SECTIONS
{
    .text : ALIGN(4) { *(.text*) } > PM_PLUGIN_TEXT
    .rodata : ALIGN(4) {
        KEEP(*(.rodata.pm_descriptor))
        *(.rodata*)
    } > PM_PLUGIN_RODATA
    .data : ALIGN(4) { *(.data*) } > PM_PLUGIN_DATA AT> PM_PLUGIN_TEXT
    .bss (NOLOAD) : ALIGN(4) { *(.bss*) *(COMMON) } > PM_PLUGIN_DATA
    /DISCARD/ : { *(.isr_vector) *(.init) *(.fini) *(.ARM.exidx*) *(.eh_frame*) }
}
