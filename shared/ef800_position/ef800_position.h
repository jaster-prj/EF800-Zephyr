/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Validate, parse, format, and persist position records. */
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

/** @brief Check whether one character is allowed in position text.
 * @param character Character to check.
 * @return Nonzero when accepted by the legacy input alphabet.
 */
int ef800_position_text_char_valid(char character);

/** @brief Validate a bounded position text field.
 * @param text NUL-terminated field text.
 * @return Nonzero when nonempty and every character is supported.
 */
int ef800_position_text_valid(const char *text);

/** @brief Parse a signed height measurement from text.
 * @param text NUL-terminated numeric text.
 * @param height_millimeters Receives parsed millimeters.
 * @return 0 on parse; negative errno for malformed or out-of-range values.
 */
int ef800_position_parse_height(const char *text, int32_t *height_millimeters);

/** @brief Format a position entry as one workspace record.
 * @param entry Position fields to encode.
 * @param buffer Destination text buffer.
 * @param buffer_size Buffer capacity.
 * @return Formatted byte count, or negative errno if invalid/too small.
 */
int ef800_position_format_record(const struct ef800_position_entry *entry,
				 char *buffer, size_t buffer_size);

/** @brief Append a formatted position record to the selected workspace.
 * @param workspace Destination workspace.
 * @param entry Position values to append.
 * @return 0 on append; negative errno for invalid data or workspace failure.
 */
int ef800_position_append(struct ef800_workspace *workspace,
				 const struct ef800_position_entry *entry);

#endif /* EF800_POSITION_H */
