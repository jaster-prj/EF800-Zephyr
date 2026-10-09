/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_position.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

#include <zephyr/kernel.h>

static const char position_punctuation[] =
	"_-.,;:=?\\#+*~'<>|$%&{}[]()";

int ef800_position_text_char_valid(char character)
{
	if ((character >= 'a' && character <= 'z') ||
	    (character >= 'A' && character <= 'Z') ||
	    (character >= '0' && character <= '9')) {
		return 1;
	}
	return strchr(position_punctuation, character) != NULL;
}

int ef800_position_text_valid(const char *text)
{
	size_t length;

	if (text == NULL || text[0] == '\0') {
		return 0;
	}
	length = strlen(text);
	if (length > EF800_POSITION_TEXT_MAX) {
		return 0;
	}
	for (size_t index = 0U; index < length; ++index) {
		if (!ef800_position_text_char_valid(text[index])) {
			return 0;
		}
	}
	return 1;
}

int ef800_position_parse_height(const char *text, int32_t *height_millimeters)
{
	const char *cursor;
	int64_t whole = 0;
	int64_t fraction = 0;
	int64_t total;
	int fraction_digits = 0;
	int sign = 1;

	if (text == NULL || height_millimeters == NULL || text[0] == '\0') {
		return -EINVAL;
	}
	cursor = text;
	if (*cursor == '+' || *cursor == '-') {
		sign = *cursor == '-' ? -1 : 1;
		++cursor;
	}
	if (*cursor < '0' || *cursor > '9') {
		return -EINVAL;
	}
	while (*cursor >= '0' && *cursor <= '9') {
		whole = whole * 10 + (*cursor - '0');
		if (whole > INT32_MAX / 1000) {
			return -ERANGE;
		}
		++cursor;
	}
	if (*cursor == '.') {
		++cursor;
		if (*cursor < '0' || *cursor > '9') {
			return -EINVAL;
		}
		while (*cursor >= '0' && *cursor <= '9') {
			if (fraction_digits == 3) {
				return -EINVAL;
			}
			fraction = fraction * 10 + (*cursor - '0');
			++fraction_digits;
			++cursor;
		}
	}
	if (*cursor != '\0') {
		return -EINVAL;
	}
	while (fraction_digits < 3) {
		fraction *= 10;
		++fraction_digits;
	}
	total = whole * 1000 + fraction;
	if ((sign < 0 && total > (int64_t)INT32_MAX + 1) ||
	    (sign > 0 && total > INT32_MAX)) {
		return -ERANGE;
	}
	*height_millimeters = (int32_t)(sign * total);
	return 0;
}

int ef800_position_format_record(const struct ef800_position_entry *entry,
				 char *buffer, size_t buffer_size)
{
	int length;
	int64_t absolute_height;
	int64_t signed_height;
	int64_t whole;
	int64_t fraction;
	const char *sign = "";

	if (entry == NULL || buffer == NULL || buffer_size == 0U ||
	    !ef800_position_text_valid(entry->stp) ||
	    !ef800_position_text_valid(entry->code)) {
		return -EINVAL;
	}
	signed_height = entry->height_millimeters;
	if (signed_height < 0) {
		sign = "-";
		absolute_height = -signed_height;
	} else {
		absolute_height = signed_height;
	}
	whole = absolute_height / 1000;
	fraction = absolute_height % 1000;
	length = snprintk(buffer, buffer_size, "-%s %s%lld.%03lld %s \r\n",
				  entry->stp, sign, (long long)whole, (long long)fraction,
				  entry->code);
	if (length < 0 || (size_t)length >= buffer_size) {
		return -ENAMETOOLONG;
	}
	return length;
}

int ef800_position_append(struct ef800_workspace *workspace,
				 const struct ef800_position_entry *entry)
{
	char record[2U * EF800_POSITION_TEXT_MAX + 32U];
	int length;

	if (workspace == NULL || entry == NULL) {
		return -EINVAL;
	}
	length = ef800_position_format_record(entry, record, sizeof(record));
	if (length < 0) {
		return length;
	}
	return ef800_workspace_append(workspace, record, (size_t)length);
}
