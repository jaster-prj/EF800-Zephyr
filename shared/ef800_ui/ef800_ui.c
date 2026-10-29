/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_ui.h"

#include <string.h>

static void render(const struct ef800_ui_render *renderer,
		   const struct ef800_ui_state *state)
{
	if (renderer != NULL && renderer->render != NULL) {
		renderer->render(renderer->context, state);
	}
}

void ef800_ui_init(struct ef800_ui_state *state)
{
	if (state == NULL) {
		return;
	}
	memset(state, 0, sizeof(*state));
	state->menu = EF800_UI_MENU_MAIN;
}

void ef800_ui_move(struct ef800_ui_state *state, int direction,
		   ef800_ui_count_fn count, void *context,
		   const struct ef800_ui_render *renderer)
{
	size_t item_count;

	if (state == NULL || count == NULL || direction == 0) {
		return;
	}
	item_count = count(context, state->menu);
	if (item_count == 0U) {
		state->selection = 0U;
	} else if (direction < 0) {
		state->selection = state->selection == 0U ?
			(uint8_t)(item_count - 1U) : (uint8_t)(state->selection - 1U);
	} else if ((size_t)state->selection + 1U >= item_count) {
		state->selection = 0U;
	} else {
		++state->selection;
	}
	render(renderer, state);
}

void ef800_ui_enter(struct ef800_ui_state *state,
		    enum ef800_ui_menu menu,
		    const struct ef800_ui_render *renderer)
{
	if (state == NULL) {
		return;
	}
	state->menu = menu;
	state->selection = 0U;
	render(renderer, state);
}

void ef800_ui_back(struct ef800_ui_state *state,
		   const struct ef800_ui_render *renderer)
{
	if (state == NULL) {
		return;
	}

	switch (state->menu) {
	case EF800_UI_MENU_MAIN:
		break;
	case EF800_UI_MENU_SETTINGS:
	case EF800_UI_MENU_WORKSPACE:
	case EF800_UI_MENU_MEASUREMENTS:
		state->menu = EF800_UI_MENU_MAIN;
		state->selection = 0U;
		break;
	case EF800_UI_MENU_WORKSPACE_MANAGEMENT:
		state->menu = EF800_UI_MENU_SETTINGS;
		state->selection = 2U;
		break;
	case EF800_UI_MENU_WORKSPACE_VIEW:
		state->menu = EF800_UI_MENU_WORKSPACE;
		state->selection = 0U;
		break;
	case EF800_UI_MENU_PORTS:
		state->menu = EF800_UI_MENU_SETTINGS;
		state->selection = 1U;
		break;
	case EF800_UI_MENU_PORT_DEVICES:
		state->menu = EF800_UI_MENU_PORTS;
		state->selection = 0U;
		break;
	case EF800_UI_MENU_PORT_CONNECTIONS:
	case EF800_UI_MENU_PORT_DEBUG_CONFIRMATION:
		state->menu = EF800_UI_MENU_PORT_DEVICES;
		state->selection = 0U;
		break;
	case EF800_UI_MENU_MEASUREMENT_ACTIONS:
		ef800_ui_close_plugin_session(state);
		state->menu = EF800_UI_MENU_MEASUREMENTS;
		state->selection = 0U;
		break;
	case EF800_UI_MENU_MEASUREMENT_EDITOR:
		state->editor_active = false;
		state->menu = EF800_UI_MENU_MEASUREMENT_ACTIONS;
		state->selection = 0U;
		break;
	case EF800_UI_MENU_WORKSPACE_CREATE:
	case EF800_UI_MENU_WORKSPACE_SELECT:
	case EF800_UI_MENU_WORKSPACE_LIST:
	case EF800_UI_MENU_WORKSPACE_DELETE_CONFIRMATION:
		state->menu = EF800_UI_MENU_WORKSPACE_MANAGEMENT;
		state->selection = 4U;
		break;
	case EF800_UI_MENU_POSITION:
	case EF800_UI_MENU_CALCULATIONS:
	default:
		state->menu = EF800_UI_MENU_WORKSPACE;
		state->selection = 0U;
		break;
	}
	render(renderer, state);
}

void ef800_ui_close_plugin_session(struct ef800_ui_state *state)
{
	if (state != NULL) {
		state->plugin_session_active = false;
	}
}

void ef800_ui_show_diagnostic(struct ef800_ui_state *state,
			      enum ef800_ui_diagnostic diagnostic,
			      const struct ef800_ui_render *renderer)
{
	if (state == NULL) {
		return;
	}
	state->diagnostic = diagnostic;
	ef800_ui_close_plugin_session(state);
	render(renderer, state);
}
