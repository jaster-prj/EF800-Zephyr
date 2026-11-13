/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Bounded RTC editor state, field validation, and key-input policy.
 */
#ifndef EF800_MAIN_TIME_EDITOR_H
#define EF800_MAIN_TIME_EDITOR_H

#include <stdbool.h>
#include <stdint.h>

#include "ef800_time.h"

enum ef800_main_time_editor_field {
	EF800_TIME_EDITOR_WEEKDAY,
	EF800_TIME_EDITOR_DATE,
	EF800_TIME_EDITOR_MONTH,
	EF800_TIME_EDITOR_YEAR,
	EF800_TIME_EDITOR_HOUR,
	EF800_TIME_EDITOR_MINUTE,
	EF800_TIME_EDITOR_SECOND,
	EF800_TIME_EDITOR_OFFSET,
	EF800_TIME_EDITOR_DST,
};

enum ef800_main_time_editor_event {
	EF800_TIME_EDITOR_IGNORED,
	EF800_TIME_EDITOR_REDRAW,
	EF800_TIME_EDITOR_ADVANCED,
	EF800_TIME_EDITOR_COMPLETE,
	EF800_TIME_EDITOR_CANCELLED,
	EF800_TIME_EDITOR_INVALID,
};

struct ef800_main_time_editor {
	bool active;
	enum ef800_main_time_editor_field field;
	uint8_t length;
	char input[8];
	struct rtc_time local;
	struct ef800_time_settings settings;
};

/** @brief Start editing a local time and its timezone settings.
 * @param editor State to initialize.
 * @param local Initial local calendar value.
 * @param settings Initial UTC offset and DST settings.
 * @return None; invalid pointers leave state unchanged.
 */
void ef800_main_time_editor_init(struct ef800_main_time_editor *editor,
	const struct rtc_time *local, const struct ef800_time_settings *settings);
/** @brief Move the weekday selection forward or backward with wraparound.
 * @param editor Active editor in the weekday field.
 * @param direction Negative decrements, positive increments, zero does nothing.
 * @return None.
 */
void ef800_main_time_editor_adjust_weekday(
	struct ef800_main_time_editor *editor, int direction);
/** @brief Apply one byte of input and return the UI action required.
 * @param editor Active editor state to update.
 * @param key Input byte (text, Enter, Backspace/Delete, or Escape).
 * @return Event indicating ignored input, redraw, advance, completion,
 *         cancellation, or invalid field data.
 */
enum ef800_main_time_editor_event ef800_main_time_editor_handle_key(
	struct ef800_main_time_editor *editor, uint8_t key);

#endif /* EF800_MAIN_TIME_EDITOR_H */
