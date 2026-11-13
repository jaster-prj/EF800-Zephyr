/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Manage menu navigation and emit UI state render callbacks. */
#ifndef EF800_UI_H
#define EF800_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum ef800_ui_menu {
	EF800_UI_MENU_MAIN = 0,
	EF800_UI_MENU_SETTINGS,
	EF800_UI_MENU_WORKSPACE,
	EF800_UI_MENU_WORKSPACE_MANAGEMENT,
	EF800_UI_MENU_WORKSPACE_VIEW,
	EF800_UI_MENU_WORKSPACE_CREATE,
	EF800_UI_MENU_WORKSPACE_SELECT,
	EF800_UI_MENU_WORKSPACE_LIST,
	EF800_UI_MENU_WORKSPACE_DELETE_CONFIRMATION,
	EF800_UI_MENU_POSITION,
	EF800_UI_MENU_PORTS,
	EF800_UI_MENU_PORT_DEVICES,
	EF800_UI_MENU_PORT_CONNECTIONS,
	EF800_UI_MENU_PORT_DEBUG_CONFIRMATION,
	EF800_UI_MENU_MEASUREMENTS,
	EF800_UI_MENU_MEASUREMENT_ACTIONS,
	EF800_UI_MENU_MEASUREMENT_EDITOR,
	EF800_UI_MENU_CALCULATIONS,
};

enum ef800_ui_diagnostic {
	EF800_UI_DIAGNOSTIC_NONE = 0,
	EF800_UI_DIAGNOSTIC_FAILURE,
	EF800_UI_DIAGNOSTIC_TIMEOUT,
	EF800_UI_DIAGNOSTIC_UNAVAILABLE,
};

struct ef800_ui_state {
	enum ef800_ui_menu menu;
	uint8_t selection;
	bool plugin_session_active;
	bool editor_active;
	enum ef800_ui_diagnostic diagnostic;
};

struct ef800_ui_render {
	void (*render)(void *context, const struct ef800_ui_state *state);
	void *context;
};

typedef size_t (*ef800_ui_count_fn)(void *context,
					 enum ef800_ui_menu menu);

/** @brief Initialize the UI at its default menu and selection.
 * @param state UI state to initialize.
 * @return None.
 */
void ef800_ui_init(struct ef800_ui_state *state);

/** @brief Move the current selection, clamped to the menu's item count.
 * @param state UI state to update.
 * @param direction Negative moves up, positive moves down.
 * @param count Callback returning the number of items for a menu.
 * @param context Opaque argument passed to count.
 * @param render Optional renderer called after state changes.
 * @return None.
 */
void ef800_ui_move(struct ef800_ui_state *state, int direction,
		   ef800_ui_count_fn count, void *context,
		   const struct ef800_ui_render *render);

/** @brief Enter a menu, resetting its selection and notifying the renderer.
 * @param state UI state to update.
 * @param menu Destination menu.
 * @param render Optional renderer called after the transition.
 * @return None.
 */
void ef800_ui_enter(struct ef800_ui_state *state,
		    enum ef800_ui_menu menu,
		    const struct ef800_ui_render *render);

/** @brief Navigate to the parent menu and render the new state.
 * @param state UI state to update.
 * @param render Optional renderer called after the transition.
 * @return None.
 */
void ef800_ui_back(struct ef800_ui_state *state,
		   const struct ef800_ui_render *render);

/** @brief Clear active plugin-session and editor state.
 * @param state UI state to update.
 * @return None.
 */
void ef800_ui_close_plugin_session(struct ef800_ui_state *state);

/** @brief Set a diagnostic status and render the updated UI.
 * @param state UI state to update.
 * @param diagnostic Diagnostic condition to show.
 * @param render Optional renderer called after the update.
 * @return None.
 */
void ef800_ui_show_diagnostic(struct ef800_ui_state *state,
			      enum ef800_ui_diagnostic diagnostic,
			      const struct ef800_ui_render *render);

#endif
