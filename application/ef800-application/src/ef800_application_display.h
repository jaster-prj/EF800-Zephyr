/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Render EF800 Application menus and domain views through the T6963 API.
 */
#ifndef EF800_APPLICATION_DISPLAY_H
#define EF800_APPLICATION_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "ef800_t6963.h"

struct ef800_measurement_display;
struct ef800_workspace_browser;
struct ef800_workspace_row_editor;

#define EF800_APPLICATION_DISPLAY_COLUMNS 40U

struct ef800_application_display {
	struct ef800_t6963 *lcd;
	bool ready;
	uint8_t last_battery_soc;
	char header_title[17];
};

/** @brief Initialize display state and bind the LCD device.
 * @param display State to initialize; ignored when NULL.
 * @param lcd T6963 device used for rendering.
 * @param ready Whether the LCD is initialized and usable.
 * @return None.
 */
void ef800_application_display_init(struct ef800_application_display *display,
			     struct ef800_t6963 *lcd, bool ready);

/** @brief Update whether subsequent drawing operations are permitted.
 * @param display Display state to update.
 * @param ready True when the LCD is available.
 * @return None.
 */
void ef800_application_display_set_ready(struct ef800_application_display *display,
				  bool ready);

/** @brief Render one padded text row, preserving the header clock/title layout.
 * @param display Display state and LCD binding.
 * @param row Text row index, from 0 through 7.
 * @param text Text to draw; NULL clears the body row or retains header title.
 * @param clock_text Optional clock text for the header row.
 * @return None; unavailable displays are ignored.
 */
void ef800_application_display_line(struct ef800_application_display *display, uint8_t row,
			     const char *text, const char *clock_text);

/** @brief Clear all text rows while retaining the supplied clock in the header.
 * @param display Display to clear.
 * @param clock_text Optional clock text for the header row.
 * @return None.
 */
void ef800_application_display_clear(struct ef800_application_display *display,
			      const char *clock_text);

/** @brief Draw the fixed horizontal separator below the display header.
 * @param display Display with an initialized LCD.
 * @return 0 on success or a negative errno if unavailable or drawing fails.
 */
int ef800_application_display_draw_header_separator(
	struct ef800_application_display *display);

/** @brief Draw or clear the battery icon only when its displayed state changes.
 * @param display Display state and LCD binding.
 * @param has_value True when a valid state-of-charge value is available.
 * @param soc_percent Battery state of charge in percent.
 * @return None.
 */
void ef800_application_display_battery_status(struct ef800_application_display *display,
	bool has_value, uint8_t soc_percent);

/** @brief Render a five-entry menu with selection and shortcut indicators.
 * @param display Display used for rendering.
 * @param title Menu title.
 * @param workspace_label Workspace line, or NULL for the default label.
 * @param items Menu item labels.
 * @param shortcuts Shortcut labels corresponding to items.
 * @param count Number of available entries.
 * @param selection Selected entry index.
 * @param clock_text Optional header clock text.
 * @return None.
 */
void ef800_application_display_menu(struct ef800_application_display *display,
			    const char *title, const char *workspace_label,
			    const char *const *items,
			    const char *const *shortcuts, size_t count,
			    size_t selection, const char *clock_text);

/** @brief Render a titled page with up to four rows and a footer.
 * @param display Display used for rendering.
 * @param title Page title.
 * @param subtitle Text shown below the title.
 * @param rows Four row strings; entries beyond row_count are blanked.
 * @param row_count Number of populated rows, up to four.
 * @param footer Instruction text shown on the last row.
 * @param clock_text Optional header clock text.
 * @return None.
 */
void ef800_application_display_list_page(struct ef800_application_display *display,
	const char *title, const char *subtitle,
	const char *const rows[4], size_t row_count, const char *footer,
	const char *clock_text);

/** @brief Render the bounded action model supplied by a measurement plugin.
 * @param display Display used for rendering.
 * @param model Bounded title, rows, selection, and footer model.
 * @param port_id Assigned measurement port identifier.
 * @param device_title Assigned device label, or a default when NULL.
 * @param clock_text Optional header clock text.
 * @return None.
 */
void ef800_application_display_plugin_menu(struct ef800_application_display *display,
	const struct ef800_measurement_display *model, uint8_t port_id,
	const char *device_title, const char *clock_text);

/** @brief Render plugin working/unavailable status.
 * @param display Display used for rendering.
 * @param show_header True to draw the plugin page header.
 * @param working True to show the in-progress message.
 * @param clock_text Optional header clock text.
 * @return None.
 */
void ef800_application_display_plugin_unavailable(struct ef800_application_display *display,
	bool show_header, bool working, const char *clock_text);

/** @brief Render the selected workspace rows or current-position view.
 * @param display Display used for rendering.
 * @param browser Current bounded browser state.
 * @param workspace_selected True when a workspace is selected.
 * @param workspace_name Selected workspace name, when available.
 * @param position_only True to render the one-row current-position view.
 * @param clock_text Optional header clock text.
 * @return None.
 */
void ef800_application_display_workspace_view(struct ef800_application_display *display,
	const struct ef800_workspace_browser *browser, bool workspace_selected,
	const char *workspace_name, bool position_only, const char *clock_text);

/** @brief Render confirmation instructions and the row being deleted.
 * @param display Display used for rendering.
 * @param selected_row Text of the selected row, or NULL.
 * @param clock_text Optional header clock text.
 * @return None.
 */
void ef800_application_display_workspace_row_confirmation(
	struct ef800_application_display *display, const char *selected_row,
	const char *clock_text);

/** @brief Render the row editor, caret, and bounded horizontal viewport.
 * @param display Display used for rendering.
 * @param editor Mutable editor state whose viewport may be adjusted.
 * @param workspace_name Name shown in the editor heading.
 * @param clock_text Optional header clock text.
 * @return None.
 */
void ef800_application_display_workspace_row_editor(struct ef800_application_display *display,
	struct ef800_workspace_row_editor *editor, const char *workspace_name,
	const char *clock_text);

#endif /* EF800_APPLICATION_DISPLAY_H */
