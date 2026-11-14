/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_plugin_host.h"
#include "ef800_application_display.h"
#include "ef800_workspace_browser.h"
#include "ef800_workspace_row_editor.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/sys/util.h>

void ef800_application_display_init(struct ef800_application_display *display,
			     struct ef800_t6963 *lcd, bool ready)
{
	if (display == NULL) {
		return;
	}
	memset(display, 0, sizeof(*display));
	display->lcd = lcd;
	display->ready = ready;
	display->last_battery_soc = UINT8_MAX;
}

void ef800_application_display_set_ready(struct ef800_application_display *display,
				  bool ready)
{
	if (display != NULL) {
		display->ready = ready;
	}
}

/** @brief Write a padded body row directly to the T6963 text plane.
 * @param display Display and LCD state.
 * @param row Text row to write.
 * @param text Source text, or empty text when NULL.
 * @return None; unavailable displays are ignored.
 */
static void display_raw_line(struct ef800_application_display *display, uint8_t row,
			     const char *text)
{
	char line[EF800_APPLICATION_DISPLAY_COLUMNS + 1U];
	int length;

	if (display == NULL || !display->ready || display->lcd == NULL) {
		return;
	}
	length = snprintf(line, sizeof(line), "%-40.40s", text == NULL ? "" : text);
	if (length < 0) {
		return;
	}
	(void)ef800_t6963_text_cursor(display->lcd, 0U, row);
	(void)ef800_t6963_write_string(display->lcd, line);
}

/** @brief Compose and draw the retained title and optional clock header.
 * @param display Display state containing the retained title.
 * @param title New title, or NULL to retain the current title.
 * @param clock_text Optional clock string.
 * @return None.
 */
static void display_header(struct ef800_application_display *display,
			   const char *title, const char *clock_text)
{
	char line[EF800_APPLICATION_DISPLAY_COLUMNS + 1U];
	size_t clock_length;

	if (display == NULL) {
		return;
	}

	memset(line, ' ', sizeof(line) - 1U);
	line[sizeof(line) - 1U] = '\0';
	if (title != NULL) {
		(void)snprintf(display->header_title, sizeof(display->header_title),
			"%.16s", title);
	}
	memcpy(line, display->header_title, strlen(display->header_title));
	clock_length = clock_text == NULL ? 0U : strlen(clock_text);
	if (clock_length > 5U) {
		clock_length = 5U;
	}
	if (clock_length != 0U) {
		memcpy(&line[17], clock_text, clock_length);
	}
	display_raw_line(display, 0U, line);
}

void ef800_application_display_line(struct ef800_application_display *display, uint8_t row,
			     const char *text, const char *clock_text)
{
	if (display == NULL) {
		return;
	}
	if (row == 0U) {
		display_header(display, text, clock_text);
	} else {
		display_raw_line(display, row, text);
	}
}

void ef800_application_display_clear(struct ef800_application_display *display,
			      const char *clock_text)
{
	for (uint8_t row = 0U; row < 8U; ++row) {
		ef800_application_display_line(display, row, "", clock_text);
	}
}

int ef800_application_display_draw_header_separator(
	struct ef800_application_display *display)
{
	if (display == NULL || !display->ready || display->lcd == NULL) {
		return -ENODEV;
	}
	return ef800_t6963_draw_horizontal_line(display->lcd, 8U);
}

/** @brief Draw the battery outline and fill level for the supplied SOC.
 * @param display Ready display state.
 * @param soc Battery state of charge in percent.
 * @return None.
 */
static void draw_battery_icon(struct ef800_application_display *display, uint8_t soc)
{
	static const uint16_t outline[][2] = {
		{228U, 1U}, {229U, 1U}, {230U, 1U}, {231U, 1U},
		{232U, 1U}, {233U, 1U}, {228U, 2U}, {233U, 2U},
		{234U, 2U}, {228U, 3U}, {234U, 3U}, {228U, 4U},
		{233U, 4U}, {234U, 4U}, {228U, 5U}, {229U, 5U},
		{230U, 5U}, {231U, 5U}, {232U, 5U}, {233U, 5U},
	};

	for (size_t index = 0U; index < ARRAY_SIZE(outline); ++index) {
		(void)ef800_t6963_write_pixel(display->lcd, outline[index][0],
			outline[index][1], true);
	}
	for (uint16_t x = 229U; x <= 233U; ++x) {
		bool filled = soc > (uint8_t)((x - 228U) * 20U);

		for (uint16_t y = 2U; y <= 4U; ++y) {
			(void)ef800_t6963_write_pixel(display->lcd, x, y, filled);
		}
	}
	for (uint16_t y = 2U; y <= 4U; ++y) {
		(void)ef800_t6963_write_pixel(display->lcd, 233U, y, soc > 95U);
	}
}

/** @brief Clear all pixels occupied by the battery icon.
 * @param display Ready display state.
 * @return None.
 */
static void clear_battery_icon(struct ef800_application_display *display)
{
	for (uint16_t x = 228U; x <= 234U; ++x) {
		for (uint16_t y = 1U; y <= 5U; ++y) {
			(void)ef800_t6963_write_pixel(display->lcd, x, y, false);
		}
	}
}

void ef800_application_display_battery_status(struct ef800_application_display *display,
	bool has_value, uint8_t soc_percent)
{
	if (display == NULL || !display->ready || display->lcd == NULL) {
		return;
	}
	if (has_value && display->last_battery_soc != soc_percent) {
		draw_battery_icon(display, soc_percent);
		display->last_battery_soc = soc_percent;
	} else if (!has_value && display->last_battery_soc != UINT8_MAX) {
		clear_battery_icon(display);
		display->last_battery_soc = UINT8_MAX;
	}
}

void ef800_application_display_menu(struct ef800_application_display *display,
			    const char *title, const char *workspace_label,
			    const char *const *items,
			    const char *const *shortcuts, size_t count,
			    size_t selection, const char *clock_text)
{
	char line[EF800_APPLICATION_DISPLAY_COLUMNS + 1U];

	if (display == NULL || items == NULL || shortcuts == NULL) {
		return;
	}
	ef800_application_display_clear(display, clock_text);
	ef800_application_display_line(display, 0U, title, clock_text);
	ef800_application_display_line(display, 1U,
		workspace_label == NULL ? "Workspace: none" : workspace_label,
		clock_text);
	ef800_application_display_line(display, 2U, "", clock_text);
	for (size_t index = 0U; index < 5U; ++index) {
		if (index < count) {
			(void)snprintf(line, sizeof(line), "%c %s [%s]",
				index == selection ? '>' : ' ', items[index],
				shortcuts[index]);
		} else {
			line[0] = '\0';
		}
		ef800_application_display_line(display, (uint8_t)(3U + index), line,
			clock_text);
	}
}

void ef800_application_display_list_page(struct ef800_application_display *display,
	const char *title, const char *subtitle,
	const char *const rows[4], size_t row_count, const char *footer,
	const char *clock_text)
{
	if (display == NULL || rows == NULL) {
		return;
	}
	ef800_application_display_clear(display, clock_text);
	ef800_application_display_line(display, 0U, title, clock_text);
	ef800_application_display_line(display, 1U, subtitle, clock_text);
	for (size_t row = 0U; row < 4U; ++row) {
		ef800_application_display_line(display, (uint8_t)(3U + row),
			row < row_count ? rows[row] : "", clock_text);
	}
	ef800_application_display_line(display, 7U, footer, clock_text);
}

void ef800_application_display_plugin_menu(struct ef800_application_display *display,
	const struct ef800_measurement_display *model, uint8_t port_id,
	const char *device_title, const char *clock_text)
{
	char line[EF800_APPLICATION_DISPLAY_COLUMNS + 1U];

	if (display == NULL || model == NULL) {
		return;
	}
	ef800_application_display_clear(display, clock_text);
	ef800_application_display_line(display, 0U, model->title, clock_text);
	(void)snprintf(line, sizeof(line), "Port %u: %s", port_id,
		device_title == NULL ? "assigned device" : device_title);
	ef800_application_display_line(display, 1U, line, clock_text);
	ef800_application_display_line(display, 2U, "Select action", clock_text);
	for (size_t index = 0U; index < model->row_count && index < 4U; ++index) {
		(void)snprintf(line, sizeof(line), "%c %s",
			index == model->selected_action ? '>' : ' ', model->rows[index]);
		ef800_application_display_line(display, (uint8_t)(3U + index), line,
			clock_text);
	}
	ef800_application_display_line(display, 7U, model->footer, clock_text);
}

void ef800_application_display_plugin_unavailable(struct ef800_application_display *display,
	bool show_header, bool working, const char *clock_text)
{
	if (display == NULL) {
		return;
	}
	ef800_application_display_clear(display, clock_text);
	if (show_header) {
		ef800_application_display_line(display, 0U, "Measurement plugin",
			clock_text);
	}
	ef800_application_display_line(display, 6U,
		working ? "Plugin working..." : "Plugin display unavailable",
		clock_text);
}

void ef800_application_display_workspace_view(struct ef800_application_display *display,
	const struct ef800_workspace_browser *browser, bool workspace_selected,
	const char *workspace_name, bool position_only, const char *clock_text)
{
	char line[EF800_APPLICATION_DISPLAY_COLUMNS + 1U];

	if (display == NULL || browser == NULL) {
		return;
	}
	ef800_application_display_clear(display, clock_text);
	ef800_application_display_line(display, 0U, position_only ? "Current Position" :
		"Show Measurements", clock_text);
	if (workspace_selected && workspace_name != NULL) {
		(void)snprintf(line, sizeof(line), "Workspace: %s", workspace_name);
	} else {
		(void)snprintf(line, sizeof(line), "Workspace: none");
	}
	ef800_application_display_line(display, 1U, line, clock_text);
	if (!browser->active) {
		ef800_application_display_line(display, 3U, browser->file_size == 0U ?
			"No workspace data" : "Workspace unavailable", clock_text);
		ef800_application_display_line(display, 7U, "ESC back", clock_text);
		return;
	}
	if (position_only) {
		ef800_application_display_line(display, 3U, browser->line_count == 0U ?
			"No position" : browser->lines[0], clock_text);
		ef800_application_display_line(display, 7U, "ESC back", clock_text);
		return;
	}
	for (size_t row = 0U; row < EF800_WORKSPACE_BROWSER_ROWS; ++row) {
		if (row >= browser->line_count) {
			ef800_application_display_line(display, (uint8_t)(3U + row), "",
				clock_text);
			continue;
		}
		line[0] = row == browser->selected_row ? '>' : ' ';
		line[1] = ' ';
		(void)snprintf(&line[2], sizeof(line) - 2U, "%.*s",
			(int)(EF800_WORKSPACE_BROWSER_COLUMNS - 2U),
			browser->lines[row]);
		ef800_application_display_line(display, (uint8_t)(3U + row), line,
			clock_text);
	}
	ef800_application_display_line(display, 7U,
		"UP/DOWN select ENTER actions", clock_text);
}

void ef800_application_display_workspace_row_confirmation(
	struct ef800_application_display *display, const char *selected_row,
	const char *clock_text)
{
	if (display == NULL) {
		return;
	}
	ef800_application_display_clear(display, clock_text);
	ef800_application_display_line(display, 0U, "Delete selected row?", clock_text);
	if (selected_row != NULL) {
		ef800_application_display_line(display, 2U, selected_row, clock_text);
	}
	ef800_application_display_line(display, 5U, "ENTER confirm delete", clock_text);
	ef800_application_display_line(display, 6U, "ESC cancel", clock_text);
}

void ef800_application_display_workspace_row_editor(struct ef800_application_display *display,
	struct ef800_workspace_row_editor *editor, const char *workspace_name,
	const char *clock_text)
{
	char line[EF800_APPLICATION_DISPLAY_COLUMNS + 1U];
	char caret_line[EF800_APPLICATION_DISPLAY_COLUMNS];
	const size_t view_width = EF800_APPLICATION_DISPLAY_COLUMNS - 1U;
	size_t visible_length;
	size_t caret_column;

	if (display == NULL || editor == NULL) {
		return;
	}
	if (editor->caret < editor->viewport_start) {
		editor->viewport_start = editor->caret;
	} else if (editor->caret > editor->viewport_start + view_width) {
		editor->viewport_start = editor->caret - view_width;
	}
	ef800_application_display_clear(display, clock_text);
	ef800_application_display_line(display, 0U, "Modify workspace row", clock_text);
	(void)snprintf(line, sizeof(line), "Workspace: %s",
		workspace_name == NULL ? "" : workspace_name);
	ef800_application_display_line(display, 1U, line, clock_text);
	ef800_application_display_line(display, 2U, editor->insert_mode ?
		"INSERT (INS toggles)" : "OVERWRITE (INS toggles)", clock_text);
	visible_length = editor->length > editor->viewport_start ?
		MIN(view_width, editor->length - editor->viewport_start) : 0U;
	if (visible_length != 0U) {
		memcpy(line, &editor->text[editor->viewport_start], visible_length);
	}
	line[visible_length] = '\0';
	ef800_application_display_line(display, 3U, line, clock_text);
	memset(caret_line, ' ', view_width);
	caret_column = editor->caret - editor->viewport_start;
	if (caret_column > view_width) {
		caret_column = view_width;
	}
	caret_line[caret_column] = '^';
	caret_line[view_width] = '\0';
	ef800_application_display_line(display, 4U, caret_line, clock_text);
	ef800_application_display_line(display, 6U, "BACKSPACE/DEL remove", clock_text);
	ef800_application_display_line(display, 7U, "ENTER save  ESC cancel",
		clock_text);
}
