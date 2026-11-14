/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_application_time_editor.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

void ef800_application_time_editor_init(struct ef800_application_time_editor *editor,
	const struct rtc_time *local, const struct ef800_time_settings *settings)
{
	if (editor == NULL || local == NULL || settings == NULL) {
		return;
	}
	*editor = (struct ef800_application_time_editor){
		.active = true,
		.field = EF800_TIME_EDITOR_WEEKDAY,
		.local = *local,
		.settings = *settings,
	};
}

void ef800_application_time_editor_adjust_weekday(
	struct ef800_application_time_editor *editor, int direction)
{
	int weekday;

	if (editor == NULL || !editor->active ||
		editor->field != EF800_TIME_EDITOR_WEEKDAY) {
		return;
	}
	weekday = (editor->local.tm_wday + direction + 7) % 7;
	editor->local.tm_wday = weekday;
}

/** @brief Parse the current field text as an integer or UTC offset.
 * @param editor Active editor containing field text.
 * @param value Receives parsed value, in minutes for offsets.
 * @return 0 on parse; negative errno for empty, malformed, or overflowing text.
 */
static int parse_value(const struct ef800_application_time_editor *editor, int *value)
{
	char *end;
	long parsed;

	if (editor->length == 0U) {
		return -EINVAL;
	}
	if (editor->field == EF800_TIME_EDITOR_OFFSET &&
		strchr(editor->input, ':') != NULL) {
		int32_t offset_minutes;

		if (ef800_parse_utc_offset(editor->input, &offset_minutes) != 0) {
			return -EINVAL;
		}
		*value = offset_minutes;
		return 0;
	}
	parsed = strtol(editor->input, &end, 10);
	if (*end != '\0' || parsed < INT_MIN || parsed > INT_MAX) {
		return -EINVAL;
	}
	*value = editor->field == EF800_TIME_EDITOR_OFFSET ? (int)parsed * 60 :
		(int)parsed;
	return 0;
}

/** @brief Validate and apply the current field text to editor state.
 * @param editor Active editor to update.
 * @return 0 when accepted; negative errno when parsing or range validation fails.
 */
static int commit_value(struct ef800_application_time_editor *editor)
{
	int value;
	int result;

	if (editor->field == EF800_TIME_EDITOR_WEEKDAY) {
		return editor->local.tm_wday >= 0 && editor->local.tm_wday <= 6 ?
			0 : -ERANGE;
	}
	result = parse_value(editor, &value);
	if (result != 0) {
		return result;
	}
	switch (editor->field) {
	case EF800_TIME_EDITOR_DATE:
		editor->local.tm_mday = value;
		break;
	case EF800_TIME_EDITOR_MONTH:
		if (value < 1 || value > 12) return -ERANGE;
		editor->local.tm_mon = value - 1;
		break;
	case EF800_TIME_EDITOR_YEAR:
		if (value < 2000 || value > 2099) return -ERANGE;
		editor->local.tm_year = value - 1900;
		break;
	case EF800_TIME_EDITOR_HOUR:
		if (value < 0 || value > 23) return -ERANGE;
		editor->local.tm_hour = value;
		break;
	case EF800_TIME_EDITOR_MINUTE:
		if (value < 0 || value > 59) return -ERANGE;
		editor->local.tm_min = value;
		break;
	case EF800_TIME_EDITOR_SECOND:
		if (value < 0 || value > 59) return -ERANGE;
		editor->local.tm_sec = value;
		break;
	case EF800_TIME_EDITOR_OFFSET:
		if (value < -24 * 60 || value > 24 * 60) return -ERANGE;
		editor->settings.utc_offset_minutes = value;
		break;
	case EF800_TIME_EDITOR_DST:
		if (value != 0 && value != 1) return -ERANGE;
		editor->settings.daylight_saving = value != 0;
		break;
	default:
		return -EINVAL;
	}
	/* Validate the complete date at the RTC adapter on final submission so
	 * month/day may be edited in either order. */
	return 0;
}

/** @brief Clear the field's bounded input buffer and length.
 * @param editor Editor state to clear.
 * @return None.
 */
static void clear_input(struct ef800_application_time_editor *editor)
{
	editor->length = 0U;
	editor->input[0] = '\0';
}

enum ef800_application_time_editor_event ef800_application_time_editor_handle_key(
	struct ef800_application_time_editor *editor, uint8_t key)
{
	int result;

	if (editor == NULL || !editor->active) {
		return EF800_TIME_EDITOR_IGNORED;
	}
	if (key == 0x1BU) {
		editor->active = false;
		return EF800_TIME_EDITOR_CANCELLED;
	}
	if (key == 0x08U || key == 0x7FU) {
		if (editor->length == 0U) {
			return EF800_TIME_EDITOR_IGNORED;
		}
		editor->input[--editor->length] = '\0';
		return EF800_TIME_EDITOR_REDRAW;
	}
	if (key == '\r' || key == '\n') {
		result = commit_value(editor);
		if (result != 0) {
			clear_input(editor);
			return EF800_TIME_EDITOR_INVALID;
		}
		if (editor->field == EF800_TIME_EDITOR_DST) {
			return EF800_TIME_EDITOR_COMPLETE;
		}
		++editor->field;
		clear_input(editor);
		return EF800_TIME_EDITOR_ADVANCED;
	}
	if (editor->field == EF800_TIME_EDITOR_WEEKDAY) {
		return EF800_TIME_EDITOR_IGNORED;
	}
	if ((key >= '0' && key <= '9') ||
		(editor->field == EF800_TIME_EDITOR_OFFSET &&
		 (key == '+' || key == '-') && editor->length == 0U) ||
		(editor->field == EF800_TIME_EDITOR_OFFSET && key == ':' &&
		 editor->length != 0U && strchr(editor->input, ':') == NULL)) {
		if ((size_t)editor->length + 1U < sizeof(editor->input)) {
			editor->input[editor->length++] = (char)key;
			editor->input[editor->length] = '\0';
			return EF800_TIME_EDITOR_REDRAW;
		}
	}
	return EF800_TIME_EDITOR_IGNORED;
}
