/* SPDX-License-Identifier: Apache-2.0 */

SECTION_DATA_PROLOGUE(.plugin_text, ALIGN(1024),)
{
	__plugin_text_start = ALIGN(1024);
	KEEP(*(.plugin_text.*))
	. = __plugin_text_start + 1024;
	__plugin_text_end = .;
} GROUP_DATA_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)

__plugin_text_load_start = LOADADDR(.plugin_text);
