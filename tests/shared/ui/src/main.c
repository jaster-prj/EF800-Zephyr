/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>

#include "ef800_ui.h"
#include "keyboard_host.h"
#include "ef800_plugin_session.h"

struct render_capture {
	struct ef800_ui_state state;
	unsigned int calls;
};

static size_t menu_count(void *context, enum ef800_ui_menu menu)
{
	ARG_UNUSED(context);
	return menu == EF800_UI_MENU_MAIN ? 5U : 2U;
}

static void capture_render(void *context, const struct ef800_ui_state *state)
{
	struct render_capture *capture = context;
	capture->state = *state;
	++capture->calls;
}

ZTEST(shared_ui, test_main_menu_wraps_and_renders)
{
	struct ef800_ui_state state;
	struct render_capture capture = {0};
	const struct ef800_ui_render renderer = {
		.render = capture_render,
		.context = &capture,
	};

	ef800_ui_init(&state);
	ef800_ui_move(&state, -1, menu_count, NULL, &renderer);
	zassert_equal(state.selection, 4U, NULL);
	ef800_ui_move(&state, 1, menu_count, NULL, &renderer);
	zassert_equal(state.selection, 0U, NULL);
	zassert_equal(capture.calls, 2U, NULL);
}

ZTEST(shared_ui, test_parent_navigation_preserves_expected_settings_entry)
{
	struct ef800_ui_state state;

	ef800_ui_init(&state);
	ef800_ui_enter(&state, EF800_UI_MENU_PORTS, NULL);
	ef800_ui_back(&state, NULL);
	zassert_equal(state.menu, EF800_UI_MENU_SETTINGS, NULL);
	zassert_equal(state.selection, 1U, NULL);
	ef800_ui_back(&state, NULL);
	zassert_equal(state.menu, EF800_UI_MENU_MAIN, NULL);
}

ZTEST(shared_ui, test_workspace_view_returns_to_workspace_menu)
{
	struct ef800_ui_state state;

	ef800_ui_init(&state);
	ef800_ui_enter(&state, EF800_UI_MENU_WORKSPACE_VIEW, NULL);
	ef800_ui_back(&state, NULL);
	zassert_equal(state.menu, EF800_UI_MENU_WORKSPACE, NULL);
}

ZTEST(shared_ui, test_workspace_management_returns_to_settings)
{
	struct ef800_ui_state state;

	ef800_ui_init(&state);
	ef800_ui_enter(&state, EF800_UI_MENU_WORKSPACE_MANAGEMENT, NULL);
	ef800_ui_back(&state, NULL);
	zassert_equal(state.menu, EF800_UI_MENU_SETTINGS, NULL);
}

ZTEST(shared_ui, test_plugin_and_editor_state_is_host_owned)
{
	struct ef800_ui_state state;

	ef800_ui_init(&state);
	state.plugin_session_active = true;
	state.editor_active = true;
	ef800_ui_enter(&state, EF800_UI_MENU_MEASUREMENT_EDITOR, NULL);
	ef800_ui_back(&state, NULL);
	zassert_false(state.editor_active, NULL);
	zassert_equal(state.menu, EF800_UI_MENU_MEASUREMENT_ACTIONS, NULL);
	state.plugin_session_active = true;
	ef800_ui_back(&state, NULL);
	zassert_false(state.plugin_session_active, NULL);
	zassert_equal(state.menu, EF800_UI_MENU_MEASUREMENTS, NULL);
}

ZTEST(shared_ui, test_empty_menu_selection_is_safe)
{
	struct ef800_ui_state state;

	ef800_ui_init(&state);
	state.selection = 3U;
	ef800_ui_move(&state, 1, NULL, NULL, NULL);
	zassert_equal(state.selection, 3U, NULL);
}

ZTEST(shared_ui, test_keyboard_host_characterization_preserves_normal_events)
{
	struct ef800_keyboard_host host;
	uint32_t event;

	ef800_keyboard_host_init(&host);
	zassert_equal(ef800_keyboard_host_feed(&host, CMD_INITIALIZED),
		EF800_HOST_FEED_READY, NULL);
	zassert_equal(ef800_keyboard_host_feed(&host, KB_SCANCODE_PREFIX_E0),
		EF800_HOST_FEED_NONE, NULL);
	zassert_equal(ef800_keyboard_host_feed(&host, 0x4DU),
		EF800_HOST_FEED_EVENT, NULL);
	zassert_ok(ef800_keyboard_host_get_event(&host, &event), NULL);
	zassert_equal(event, KB_EVENT_ARROWRIGHT, NULL);
}

static int characterization_render(void *instance,
					struct ef800_plugin_screen *screen)
{
	ARG_UNUSED(instance);
	screen->row_count = 1U;
	strcpy(screen->rows[0], "characterized");
	return 0;
}

static int characterization_key(void *instance,
				const struct ef800_plugin_key_event *event,
				enum ef800_plugin_key_result *result)
{
	ARG_UNUSED(instance);
	*result = event->code == EF800_PLUGIN_KEY_BACK ?
		EF800_PLUGIN_KEY_REQUEST_BACK : EF800_PLUGIN_KEY_CONSUMED;
	return 0;
}

ZTEST(shared_ui, test_plugin_session_close_is_recovery_boundary)
{
	struct ef800_plugin_session session;
	struct ef800_plugin_screen screen;
	enum ef800_plugin_key_result result;
	const struct ef800_plugin_ui_api api = {
		.abi_version = 1U,
		.struct_size = sizeof(api),
		.render = characterization_render,
		.handle_key = characterization_key,
	};

	ef800_plugin_session_init(&session);
	zassert_ok(ef800_plugin_session_open(&session, &session, &api), NULL);
	ef800_plugin_screen_init(&screen);
	zassert_ok(ef800_plugin_session_render(&session, &screen), NULL);
	zassert_ok(ef800_plugin_session_key(&session,
		&(struct ef800_plugin_key_event){.code = EF800_PLUGIN_KEY_BACK},
		&result), NULL);
	zassert_equal(result, EF800_PLUGIN_KEY_REQUEST_BACK, NULL);
	ef800_plugin_session_close(&session);
	zassert_equal(ef800_plugin_session_render(&session, &screen), -EINVAL, NULL);
}

ZTEST(shared_ui, test_failure_diagnostic_closes_plugin_session)
{
	struct ef800_ui_state state;
	struct render_capture capture = {0};
	const struct ef800_ui_render renderer = {
		.render = capture_render,
		.context = &capture,
	};

	ef800_ui_init(&state);
	state.plugin_session_active = true;
	ef800_ui_show_diagnostic(&state, EF800_UI_DIAGNOSTIC_TIMEOUT, &renderer);
	zassert_equal(state.diagnostic, EF800_UI_DIAGNOSTIC_TIMEOUT, NULL);
	zassert_false(state.plugin_session_active, NULL);
	zassert_equal(capture.calls, 1U, NULL);
}

ZTEST_SUITE(shared_ui, NULL, NULL, NULL, NULL, NULL);
