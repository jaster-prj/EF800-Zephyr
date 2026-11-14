/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <string.h>

#include "ef800_ui.h"
#include "keyboard_host.h"
#include "ef800_plugin_session.h"
#include "ef800_lcd_contrast.h"
#include "ef800_application_display.h"
#include "ef800_workspace_browser.h"
#include "ef800_workspace_row_editor.h"
#include "ef800_measurement_plugin.h"

static char rendered_rows[8U][EF800_APPLICATION_DISPLAY_COLUMNS + 1U];
static uint8_t rendered_row;
static unsigned int pixel_write_calls;
static bool last_pixel_value;
static bool first_pixel_value;

int ef800_t6963_text_cursor(struct ef800_t6963 *lcd, uint8_t column,
	uint8_t row)
{
	ARG_UNUSED(lcd);
	ARG_UNUSED(column);
	rendered_row = row;
	return 0;
}

int ef800_t6963_write_string(struct ef800_t6963 *lcd, const char *string)
{
	ARG_UNUSED(lcd);
	if (rendered_row < ARRAY_SIZE(rendered_rows)) {
		strncpy(rendered_rows[rendered_row], string,
			sizeof(rendered_rows[rendered_row]) - 1U);
		rendered_rows[rendered_row][EF800_APPLICATION_DISPLAY_COLUMNS] = '\0';
	}
	return 0;
}

int ef800_t6963_draw_horizontal_line(struct ef800_t6963 *lcd, uint16_t y)
{
	ARG_UNUSED(lcd);
	ARG_UNUSED(y);
	return 0;
}

int ef800_t6963_write_pixel(struct ef800_t6963 *lcd, uint16_t x, uint16_t y,
	bool value)
{
	ARG_UNUSED(lcd);
	ARG_UNUSED(x);
	ARG_UNUSED(y);
	if (pixel_write_calls == 0U) {
		first_pixel_value = value;
	}
	++pixel_write_calls;
	last_pixel_value = value;
	return 0;
}

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

ZTEST(shared_ui, test_plugin_model_render_keeps_host_context_and_bounds)
{
	struct ef800_t6963 lcd = {0};
	struct ef800_application_display display;
	struct ef800_measurement_display model = {
		.title = "Plugin title",
		.rows = {"Measure", "Back"},
		.footer = "ENTER select",
		.selected_action = 1U,
		.row_count = 2U,
	};

	memset(rendered_rows, 0, sizeof(rendered_rows));
	ef800_application_display_init(&display, &lcd, true);
	ef800_application_display_plugin_menu(&display, &model, 3U, "Instrument",
		"12:34");
	zassert_true(strncmp(rendered_rows[0], "Plugin title", 12U) == 0, NULL);
	zassert_true(strncmp(rendered_rows[0] + 17U, "12:34", 5U) == 0, NULL);
	zassert_true(strncmp(rendered_rows[1], "Port 3: Instrument", 18U) == 0,
		NULL);
	zassert_true(strncmp(rendered_rows[4], "> Back", 6U) == 0, NULL);
	zassert_true(strncmp(rendered_rows[7], "ENTER select", 12U) == 0, NULL);
	zassert_equal(strlen(rendered_rows[7]), EF800_APPLICATION_DISPLAY_COLUMNS, NULL);
}

ZTEST(shared_ui, test_paged_list_renderer_keeps_rows_and_footer_separate)
{
	struct ef800_t6963 lcd = {0};
	struct ef800_application_display display;
	const char *const rows[4] = {"> Port A", "  Port B", "  Port C", ""};

	memset(rendered_rows, 0, sizeof(rendered_rows));
	ef800_application_display_init(&display, &lcd, true);
	ef800_application_display_list_page(&display, "Port assignment", "Ports 1-4 of 5",
		rows, ARRAY_SIZE(rows), "ENTER edit  ESC back", "11:24");
	zassert_true(strncmp(rendered_rows[0], "Port assignment", 15U) == 0,
		NULL);
	zassert_true(strncmp(rendered_rows[1], "Ports 1-4 of 5", 14U) == 0,
		NULL);
	zassert_true(strncmp(rendered_rows[3], "> Port A", 8U) == 0, NULL);
	zassert_true(strncmp(rendered_rows[5], "  Port C", 8U) == 0, NULL);
	zassert_true(strncmp(rendered_rows[7], "ENTER edit  ESC back", 20U) == 0,
		NULL);
}

ZTEST(shared_ui, test_battery_icon_renderer_updates_only_on_state_change)
{
	struct ef800_t6963 lcd = {0};
	struct ef800_application_display display;

	pixel_write_calls = 0U;
	first_pixel_value = false;
	ef800_application_display_init(&display, &lcd, true);
	ef800_application_display_battery_status(&display, true, 50U);
	zassert_equal(pixel_write_calls, 38U, NULL);
	zassert_true(first_pixel_value, NULL);
	ef800_application_display_battery_status(&display, true, 50U);
	zassert_equal(pixel_write_calls, 38U, NULL);
	ef800_application_display_battery_status(&display, false, 0U);
	zassert_equal(pixel_write_calls, 73U, NULL);
	zassert_false(last_pixel_value, NULL);
}

ZTEST(shared_ui, test_workspace_renderer_preserves_selection_and_position_mode)
{
	struct ef800_t6963 lcd = {0};
	struct ef800_application_display display;
	struct ef800_workspace_browser browser = {
		.lines = {"first row", "selected row"},
		.line_count = 2U,
		.selected_row = 1U,
		.active = true,
	};

	memset(rendered_rows, 0, sizeof(rendered_rows));
	ef800_application_display_init(&display, &lcd, true);
	ef800_application_display_workspace_view(&display, &browser, true, "survey",
		false, "08:21");
	zassert_true(strncmp(rendered_rows[0], "Show", 4U) == 0,
		NULL);
	zassert_true(strncmp(rendered_rows[1], "Workspace: survey", 17U) == 0,
		NULL);
	zassert_true(strncmp(rendered_rows[3], "  first row", 11U) == 0, NULL);
	zassert_true(strncmp(rendered_rows[4], "> selected row", 14U) == 0,
		NULL);
	zassert_true(strncmp(rendered_rows[7], "UP/DOWN select ENTER actions",
		28U) == 0, NULL);

	memset(rendered_rows, 0, sizeof(rendered_rows));
	ef800_application_display_workspace_view(&display, &browser, true, "survey",
		true, "08:21");
	zassert_true(strncmp(rendered_rows[0], "Current Position", 16U) == 0,
		NULL);
	zassert_true(strncmp(rendered_rows[3], "first row", 9U) == 0, NULL);
	zassert_true(strncmp(rendered_rows[7], "ESC back", 8U) == 0, NULL);
}

ZTEST(shared_ui, test_workspace_row_editor_renderer_tracks_bounded_viewport)
{
	struct ef800_t6963 lcd = {0};
	struct ef800_application_display display;
	struct ef800_workspace_row_editor editor = {
		.text = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghij",
		.length = 46U,
		.caret = 40U,
		.insert_mode = true,
	};

	memset(rendered_rows, 0, sizeof(rendered_rows));
	ef800_application_display_init(&display, &lcd, true);
	ef800_application_display_workspace_row_editor(&display, &editor, "survey",
		"09:15");
	zassert_equal(editor.viewport_start, 1U, NULL);
	zassert_true(strncmp(rendered_rows[0], "Modify workspace", 16U) == 0,
		NULL);
	zassert_true(strncmp(rendered_rows[1], "Workspace: survey", 17U) == 0,
		NULL);
	zassert_true(strncmp(rendered_rows[2], "INSERT (INS toggles)", 20U) == 0,
		NULL);
	zassert_true(strncmp(rendered_rows[3], "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcd", 39U) == 0,
		NULL);
	zassert_equal(rendered_rows[4][38], ' ', NULL);
	zassert_true(strncmp(rendered_rows[7], "ENTER save  ESC cancel", 22U) == 0,
		NULL);
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

struct contrast_menu_context {
	enum ef800_lcd_contrast_mode mode;
};

static size_t contrast_menu_count(void *context, enum ef800_ui_menu menu)
{
	const struct contrast_menu_context *contrast = context;

	return menu == EF800_UI_MENU_SETTINGS ?
		ef800_lcd_contrast_settings_count(contrast->mode, 3U) : 0U;
}

ZTEST(shared_ui, test_settings_navigation_exposes_contrast_only_when_active)
{
	struct ef800_ui_state state;
	struct contrast_menu_context contrast = {
		.mode = EF800_LCD_CONTRAST_STATIC,
	};

	ef800_ui_init(&state);
	state.menu = EF800_UI_MENU_SETTINGS;
	state.selection = 2U;
	ef800_ui_move(&state, 1, contrast_menu_count, &contrast, NULL);
	zassert_equal(state.selection, 0U, NULL);

	contrast.mode = EF800_LCD_CONTRAST_PWM;
	state.selection = 2U;
	ef800_ui_move(&state, 1, contrast_menu_count, &contrast, NULL);
	zassert_equal(state.selection, 3U, NULL);
	ef800_ui_move(&state, -1, contrast_menu_count, &contrast, NULL);
	zassert_equal(state.selection, 2U, NULL);
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

ZTEST(shared_ui, test_workspace_delete_confirmation_returns_to_management)
{
	struct ef800_ui_state state;

	ef800_ui_init(&state);
	ef800_ui_enter(&state, EF800_UI_MENU_WORKSPACE_DELETE_CONFIRMATION, NULL);
	ef800_ui_back(&state, NULL);
	zassert_equal(state.menu, EF800_UI_MENU_WORKSPACE_MANAGEMENT, NULL);
	zassert_equal(state.selection, 4U, NULL);
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
