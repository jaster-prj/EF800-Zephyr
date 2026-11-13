/* SPDX-License-Identifier: Apache-2.0 */

SECTION_DATA_PROLOGUE(.plugin_text, ORIGIN(EF800_EXT_SRAM),)
{
	__plugin_text_start = ALIGN(1024);
	KEEP(*(.plugin_text.*))
	. = __plugin_text_start + 1024;
	__plugin_text_end = .;
} GROUP_DATA_LINK_IN(EF800_EXT_SRAM, ROMABLE_REGION)

__plugin_text_load_start = LOADADDR(.plugin_text);
