/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_POSITION_H
#define EF800_POSITION_H

#include <stddef.h>
#include <stdint.h>

#include "ef800_workspace.h"

#define EF800_POSITION_TEXT_MAX 48U

struct ef800_position_entry {
	char stp[EF800_POSITION_TEXT_MAX + 1U];
	char code[EF800_POSITION_TEXT_MAX + 1U];
	int32_t height_millimeters;
};

/* These characters match the legacy Position input alphabet, excluding space. */
int ef800_position_text_char_valid(char character);
int ef800_position_text_valid(const char *text);
int ef800_position_parse_height(const char *text, int32_t *height_millimeters);
int ef800_position_format_record(const struct ef800_position_entry *entry,
				 char *buffer, size_t buffer_size);
int ef800_position_append(struct ef800_workspace *workspace,
				 const struct ef800_position_entry *entry);

#endif /* EF800_POSITION_H */
