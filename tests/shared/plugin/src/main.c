/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include "keyboard_protocol.h"

#include <zephyr/ztest.h>

#include "ef800_plugin_registry.h"
#include "ef800_plugin_recovery_state.h"
#include "ef800_calculation_service.h"
#include "ef800_workspace.h"
#include "ef800_persistence_fake.h"
#include "ef800_plugin_screen.h"
#include "ef800_plugin_display.h"
#include "ef800_keyboard_events.h"
#include "ef800_plugin_session.h"
#include "ef800_plugin_worker_abi.h"
#include "ef800_measurement_submenu.h"

struct calculation_fixture {
	const char *snapshot;
	int snapshot_status;
	int transform_status;
};

static int fixture_snapshot(void *context, char *destination, size_t capacity,
	size_t *length)
{
	struct calculation_fixture *fixture = context;
	size_t size = strlen(fixture->snapshot);

	if (fixture->snapshot_status != 0) {
		return fixture->snapshot_status;
	}
	if (size > capacity) {
		return -EFBIG;
	}
	memcpy(destination, fixture->snapshot, size);
	*length = size;
	return 0;
}

static int fixture_transform(void *context, const char *snapshot,
	size_t snapshot_length, char *output, size_t capacity, size_t *length)
{
	(void)context;
	struct calculation_fixture *fixture = context;
	if (fixture->transform_status != 0) {
		return fixture->transform_status;
	}
	if (snapshot_length > capacity) {
		return -EFBIG;
	}
	memcpy(output, snapshot, snapshot_length);
	*length = snapshot_length;
	return 0;
}

ZTEST(ef800_plugin, test_bounded_calculation_service)
{
	struct calculation_fixture fixture = {.snapshot = "workspace"};
	struct ef800_calculation_service service = {
		.snapshot = fixture_snapshot,
		.transform = fixture_transform,
		.context = &fixture,
	};
	char snapshot[32];
	char output[32];
	size_t length;

	zassert_ok(ef800_calculation_service_run(&service, snapshot,
		sizeof(snapshot), output, sizeof(output), &length), NULL);
	zassert_equal(length, 9, NULL);
	zassert_mem_equal(output, "workspace", 9, NULL);
	fixture.snapshot_status = -EIO;
	zassert_equal(ef800_calculation_service_run(&service, snapshot,
		sizeof(snapshot), output, sizeof(output), &length), -EIO, NULL);
	zassert_equal(ef800_calculation_service_run(&service, NULL,
		sizeof(snapshot), output, sizeof(output), &length), -EINVAL, NULL);
	/* cppcheck-suppress redundantAssignment -- callback observes this on the following run */
	fixture.snapshot_status = 0;
	fixture.snapshot = "0123456789";
	zassert_equal(ef800_calculation_service_run(&service, snapshot, 4,
		output, sizeof(output), &length), -EFBIG, NULL);
	/* cppcheck-suppress redundantAssignment -- callback observes the shorter input next */
	fixture.snapshot = "ok";
	fixture.transform_status = -ERANGE;
	zassert_equal(ef800_calculation_service_run(&service, snapshot,
		sizeof(snapshot), output, sizeof(output), &length), -ERANGE, NULL);
}

static struct ef800_plugin_worker_message service_message(uint32_t operation)
{
	return (struct ef800_plugin_worker_message){
		.abi_version = EF800_PLUGIN_WORKER_ABI_VERSION,
		.struct_size = sizeof(struct ef800_plugin_worker_message),
		.generation = 7U,
		.sequence = 1U,
		.kind = EF800_PLUGIN_WORKER_HOST_SERVICE_REQUEST,
		.operation = operation,
		.capability = 0x1234U,
		.deadline_ms = 250U,
	};
}

ZTEST(ef800_plugin, test_worker_mailbox_service_bounds_and_generation)
{
	struct ef800_plugin_worker_message message =
		service_message(EF800_PLUGIN_WORKER_PORT_WRITE);

	message.kind = EF800_PLUGIN_WORKER_HOST_INVOKE;
	message.operation = EF800_PLUGIN_WORKER_MEASURE;
	message.payload_offset = 0U;
	message.payload_length = 16U;
	message.response_offset = 32U;
	message.response_capacity = 32U;
	zassert_ok(ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE, 7U, 100U), NULL);

	message = service_message(EF800_PLUGIN_WORKER_PORT_WRITE);

	message.payload_offset = 16U;
	message.payload_length = 4U;
	zassert_ok(ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE, 7U, 100U), NULL);
	message.struct_size--;
	zassert_equal(ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE, 7U, 100U), -EINVAL, NULL);
	message = service_message(EF800_PLUGIN_WORKER_PORT_WRITE);
	message.reserved = 1U;
	zassert_equal(ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE, 7U, 100U), -EINVAL, NULL);
	message = service_message(EF800_PLUGIN_WORKER_PORT_WRITE);
	message.deadline_padding = 1U;
	zassert_equal(ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE, 7U, 100U), -EINVAL, NULL);
	message = service_message(EF800_PLUGIN_WORKER_PORT_WRITE);
	message.response_length = 1U;
	zassert_equal(ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE, 7U, 100U), -EINVAL, NULL);
	message = service_message(EF800_PLUGIN_WORKER_PORT_WRITE);
	message.operation = 0x7fffU;
	zassert_equal(ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE, 7U, 100U), -EINVAL, NULL);
	message = service_message(EF800_PLUGIN_WORKER_PORT_WRITE);
	zassert_equal(ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE, 8U, 100U), -EINVAL, NULL);

	message.capability = 0U;
	zassert_equal(ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE, 7U, 100U), -EINVAL, NULL);
	message.capability = 0x1234U;
	message.payload_offset = EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE - 2U;
	message.payload_length = 4U;
	zassert_equal(ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE, 7U, 100U), -EFAULT, NULL);

	message = service_message(EF800_PLUGIN_WORKER_PORT_READ);
	message.response_offset = 32U;
	message.response_capacity = EF800_PLUGIN_WORKER_MAX_PORT_TRANSFER;
	message.timeout_ms = 100U;
	zassert_ok(ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE, 7U, 100U), NULL);
	message.deadline_ms = 150U;
	zassert_equal(ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE, 7U, 100U),
		-ETIMEDOUT, NULL);

	message = service_message(EF800_PLUGIN_WORKER_RENDER);
	message.kind = EF800_PLUGIN_WORKER_HOST_INVOKE;
	message.payload_offset = 8U;
	message.payload_length = 8U;
	message.response_offset = 12U;
	message.response_capacity = 8U;
	zassert_equal(ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE, 7U, 100U), -EINVAL, NULL);

	message.kind = EF800_PLUGIN_WORKER_RESULT;
	zassert_equal(ef800_plugin_worker_message_validate(&message,
		EF800_PLUGIN_WORKER_MAILBOX_PAYLOAD_SIZE, 7U, 100U), -EINVAL, NULL);
}

ZTEST(ef800_plugin, test_workspace_snapshot_is_bounded)
{
	struct ef800_persistence_fake fake;
	struct ef800_persistence storage;
	struct ef800_workspace workspace;
	char snapshot[16];
	char small_snapshot[8];
	size_t length;

	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&storage, &fake.backend), NULL);
	zassert_ok(ef800_workspace_init(&workspace, &storage), NULL);
	zassert_ok(ef800_workspace_create(&workspace, "calc"), NULL);
	zassert_ok(ef800_workspace_append(&workspace, "123456789", 9), NULL);
	zassert_equal(ef800_workspace_snapshot(&workspace, small_snapshot,
		sizeof(small_snapshot), &length), -EFBIG, NULL);
	zassert_equal(length, 0, NULL);
	zassert_ok(ef800_workspace_snapshot(&workspace, snapshot,
		12, &length), NULL);
	zassert_equal(length, 9, NULL);
	zassert_mem_equal(snapshot, "123456789", 9, NULL);
}

ZTEST(ef800_plugin, test_bounded_plugin_screen)
{
	struct ef800_plugin_screen screen;

	ef800_plugin_screen_init(&screen);
	strcpy(screen.title, "Plugin");
	screen.row_count = 2U;
	strcpy(screen.rows[0], "first");
	strcpy(screen.rows[1], "second");
	strcpy(screen.footer, "ENTER select");
	zassert_ok(ef800_plugin_screen_validate(&screen), NULL);
	screen.row_count = EF800_PLUGIN_SCREEN_ROWS + 1U;
	zassert_equal(ef800_plugin_screen_validate(&screen), -EINVAL, NULL);
	 screen.row_count = 1U;
	memset(screen.rows[0], 'x', sizeof(screen.rows[0]));
	zassert_equal(ef800_plugin_screen_validate(&screen), -EOVERFLOW, NULL);
}

static int display_line_test(void *context, uint8_t row, const char *text)
{
	uint8_t *last_row = context;
	(void)text;
	*last_row = row;
	return 0;
}

ZTEST(ef800_plugin, test_plugin_display_keeps_host_rows)
{
	struct ef800_plugin_screen screen;
	uint8_t last_row = 0U;
	struct ef800_plugin_display display = {
		.context = &last_row,
		.write_line = display_line_test,
	};

	ef800_plugin_screen_init(&screen);
	screen.row_count = 1U;
	strcpy(screen.rows[0], "body");
	zassert_ok(ef800_plugin_display_render(&display, &screen), NULL);
	zassert_equal(last_row, 6U, NULL);
	 screen.row_count = EF800_PLUGIN_SCREEN_ROWS + 1U;
	zassert_equal(ef800_plugin_display_render(&display, &screen), -EINVAL, NULL);
}

ZTEST(ef800_plugin, test_logical_keyboard_conversion)
{
	struct ef800_plugin_key_event event;

	zassert_ok(ef800_keyboard_key_event('a', &event), NULL);
	zassert_equal(event.code, EF800_PLUGIN_KEY_CHARACTER, NULL);
	zassert_equal(event.character, 'a', NULL);
	zassert_ok(ef800_keyboard_event_key(KB_EVENT_ARROWLEFT, &event), NULL);
	zassert_equal(event.code, EF800_PLUGIN_KEY_LEFT, NULL);
	zassert_equal(ef800_keyboard_event_key(KB_EVENT_OFF, &event),
		-ENOTSUP, NULL);
}

static int session_render(void *instance, struct ef800_plugin_screen *screen)
{
	(void)instance;
	screen->row_count = 1U;
	strcpy(screen->rows[0], "plugin");
	return 0;
}

static int session_key(void *instance, const struct ef800_plugin_key_event *event,
	enum ef800_plugin_key_result *result)
{
	(void)instance;
	*result = event->code == EF800_PLUGIN_KEY_CHARACTER ?
		EF800_PLUGIN_KEY_CONSUMED : EF800_PLUGIN_KEY_NOT_CONSUMED;
	return 0;
}

ZTEST(ef800_plugin, test_plugin_session_lifecycle_and_key_policy)
{
	struct ef800_plugin_session session;
	struct ef800_plugin_screen screen;
	struct ef800_plugin_key_event event;
	enum ef800_plugin_key_result result;
	const struct ef800_plugin_ui_api api = {
		.abi_version = 1U,
		.struct_size = sizeof(api),
		.render = session_render,
		.handle_key = session_key,
	};

	ef800_plugin_session_init(&session);
	zassert_equal(ef800_plugin_session_open(&session, &session, &api), 0, NULL);
	ef800_plugin_screen_init(&screen);
	zassert_ok(ef800_plugin_session_render(&session, &screen), NULL);
	zassert_equal(ef800_plugin_session_key(&session,
		&(struct ef800_plugin_key_event){.code = EF800_PLUGIN_KEY_CHARACTER},
		&result), 0, NULL);
	zassert_equal(result, EF800_PLUGIN_KEY_CONSUMED, NULL);
	memset(&event, 0, sizeof(event));
	event.code = EF800_PLUGIN_KEY_BACK;
	zassert_ok(ef800_plugin_session_key(&session, &event, &result), NULL);
	zassert_equal(result, EF800_PLUGIN_KEY_REQUEST_BACK, NULL);
	ef800_plugin_session_close(&session);
	zassert_equal(ef800_plugin_session_render(&session, &screen), -EINVAL, NULL);
}

ZTEST(ef800_plugin, test_builtin_and_external_entries)
{
	struct ef800_plugin_registry registry;
	struct ef800_plugin_descriptor_info info = {
		.struct_size = sizeof(info),
		.plugin_id = 100u,
		.interface_version = 1u,
		.kind = EF800_PLUGIN_KIND_CALCULATION,
		.menu_category = EF800_PLUGIN_MENU_CALCULATION,
	};
	const struct ef800_plugin_registry_entry *entry;

	strcpy(info.name, "Offset calculation");
	strcpy(info.version, "1.0.0");
	ef800_plugin_registry_init(&registry);
	zassert_equal(ef800_plugin_registry_register_builtins(&registry), 0, NULL);
	zassert_equal(ef800_plugin_registry_count(&registry), 0, NULL);
	zassert_equal(ef800_plugin_registry_register_external(&registry, &info), 0,
		NULL);
	entry = ef800_plugin_registry_find(&registry, 100u);
	zassert_not_null(entry, NULL);
	zassert_equal(entry->state, EF800_PLUGIN_REGISTRY_READY, NULL);
	zassert_equal(entry->info.kind, EF800_PLUGIN_KIND_CALCULATION, NULL);
	zassert_equal(ef800_plugin_registry_register_external(&registry, &info),
		-EEXIST, NULL);
}

ZTEST(ef800_plugin, test_measurement_submenu_actions_and_navigation)
{
	struct ef800_measurement_submenu menu;
	uint32_t action;

	ef800_measurement_submenu_init(&menu,
		EF800_MEASUREMENT_ACTION_MEASURE | EF800_MEASUREMENT_ACTION_UP |
		EF800_MEASUREMENT_ACTION_DOWN | EF800_MEASUREMENT_ACTION_ENTER);
	zassert_equal(ef800_measurement_submenu_count(&menu), 4, NULL);
	zassert_equal(ef800_measurement_submenu_action(&menu),
		EF800_MEASUREMENT_ACTION_MEASURE, NULL);
	ef800_measurement_submenu_move(&menu, 1);
	zassert_equal(ef800_measurement_submenu_action(&menu),
		EF800_MEASUREMENT_ACTION_UP, NULL);
	zassert_true(ef800_measurement_submenu_key('d', &action), NULL);
	zassert_equal(action, EF800_MEASUREMENT_ACTION_DOWN, NULL);
	ef800_measurement_submenu_init(&menu, EF800_MEASUREMENT_ACTION_MEASURE);
	zassert_equal(ef800_measurement_submenu_count(&menu), 1, NULL);
	zassert_equal(ef800_measurement_submenu_action(&menu),
		EF800_MEASUREMENT_ACTION_MEASURE, NULL);
	zassert_true(ef800_measurement_submenu_key('u', &action), NULL);
	zassert_equal(action, EF800_MEASUREMENT_ACTION_UP, NULL);
	zassert_equal(menu.actions & action, 0U, NULL);
}

ZTEST(ef800_plugin, test_measurement_submenu_lifecycle_and_unsupported_action)
{
	struct ef800_measurement_submenu menu;
	uint32_t action = 0U;

	ef800_measurement_submenu_init(&menu, 0U);
	zassert_false(menu.active, NULL);
	zassert_equal(ef800_measurement_submenu_count(&menu), 0, NULL);
	zassert_equal(ef800_measurement_submenu_action(&menu), 0U, NULL);
	ef800_measurement_submenu_move(&menu, 1);
	zassert_false(ef800_measurement_submenu_key('x', &action), NULL);
	ef800_measurement_submenu_init(&menu, EF800_MEASUREMENT_ACTION_MEASURE);
	zassert_false((menu.actions & EF800_MEASUREMENT_ACTION_UP) != 0U, NULL);
}

ZTEST(ef800_plugin, test_rejects_invalid_and_records_diagnostics)
{
	struct ef800_plugin_registry registry;
	struct ef800_plugin_descriptor_info invalid = {
		.struct_size = sizeof(invalid),
		.plugin_id = 101u,
	};

	ef800_plugin_registry_init(&registry);
	zassert_equal(ef800_plugin_registry_register_external(&registry, &invalid),
		-EINVAL, NULL);
	zassert_equal(ef800_plugin_registry_record_diagnostic(&registry, 102u,
		EF800_PLUGIN_REGISTRY_MISSING), 0, NULL);
	zassert_equal(ef800_plugin_registry_state_for(&registry, 102u),
		EF800_PLUGIN_REGISTRY_MISSING, NULL);
	zassert_equal(ef800_plugin_registry_record_diagnostic(&registry, 103u,
		EF800_PLUGIN_REGISTRY_INCOMPATIBLE), 0, NULL);
	zassert_equal(ef800_plugin_registry_state_for(&registry, 103u),
		EF800_PLUGIN_REGISTRY_INCOMPATIBLE, NULL);
	zassert_equal(ef800_plugin_registry_record_diagnostic(&registry, 104u,
		EF800_PLUGIN_REGISTRY_UNAVAILABLE), 0, NULL);
	zassert_equal(ef800_plugin_registry_state_for(&registry, 104u),
		EF800_PLUGIN_REGISTRY_UNAVAILABLE, NULL);
	zassert_equal(ef800_plugin_registry_state_for(&registry, 999u),
		EF800_PLUGIN_REGISTRY_MISSING, NULL);
}

ZTEST(ef800_plugin, test_fault_and_timeout_quarantine_is_boot_scoped)
{
	struct ef800_plugin_registry registry;
	struct ef800_plugin_descriptor_info faulted = {
		.struct_size = sizeof(faulted),
		.plugin_id = 105u,
		.interface_version = 1u,
		.kind = EF800_PLUGIN_KIND_MEASUREMENT,
		.menu_category = EF800_PLUGIN_MENU_MEASUREMENT,
	};
	struct ef800_plugin_descriptor_info timed_out = faulted;
	struct ef800_plugin_descriptor_info healthy = faulted;

	strcpy(faulted.name, "faulted measurement");
	strcpy(faulted.version, "1.0.0");
	timed_out.plugin_id = 106u;
	strcpy(timed_out.name, "timed measurement");
	healthy.plugin_id = 107u;
	healthy.kind = EF800_PLUGIN_KIND_CALCULATION;
	healthy.menu_category = EF800_PLUGIN_MENU_CALCULATION;
	strcpy(healthy.name, "healthy calculation");
	ef800_plugin_registry_init(&registry);
	zassert_ok(ef800_plugin_registry_register_external(&registry, &faulted),
		NULL);
	zassert_ok(ef800_plugin_registry_register_external(&registry, &timed_out),
		NULL);
	zassert_ok(ef800_plugin_registry_register_external(&registry, &healthy),
		NULL);
	zassert_equal(ef800_plugin_registry_set_state(&registry, faulted.plugin_id,
		EF800_PLUGIN_REGISTRY_FAULTED), 0, NULL);
	zassert_equal(ef800_plugin_registry_state_for(&registry, faulted.plugin_id),
		EF800_PLUGIN_REGISTRY_FAULTED, NULL);
	zassert_equal(ef800_plugin_registry_set_state(&registry, timed_out.plugin_id,
		EF800_PLUGIN_REGISTRY_TIMED_OUT), 0, NULL);
	zassert_equal(ef800_plugin_registry_state_for(&registry, timed_out.plugin_id),
		EF800_PLUGIN_REGISTRY_TIMED_OUT, NULL);
	zassert_equal(ef800_plugin_registry_state_for(&registry, healthy.plugin_id),
		EF800_PLUGIN_REGISTRY_READY, NULL);
	zassert_equal(ef800_plugin_registry_set_state(&registry, 999u,
		EF800_PLUGIN_REGISTRY_FAULTED), -ENOENT, NULL);
	zassert_equal(ef800_plugin_registry_set_state(&registry, faulted.plugin_id,
		EF800_PLUGIN_REGISTRY_READY), -EINVAL, NULL);

	/* Runtime quarantine lives only in this registry instance. */
	ef800_plugin_registry_init(&registry);
	zassert_ok(ef800_plugin_registry_register_external(&registry, &faulted),
		NULL);
	zassert_ok(ef800_plugin_registry_register_external(&registry, &timed_out),
		NULL);
	zassert_ok(ef800_plugin_registry_register_external(&registry, &healthy),
		NULL);
	zassert_equal(ef800_plugin_registry_state_for(&registry, faulted.plugin_id),
		EF800_PLUGIN_REGISTRY_READY, NULL);
	zassert_equal(ef800_plugin_registry_state_for(&registry, timed_out.plugin_id),
		EF800_PLUGIN_REGISTRY_READY, NULL);
	zassert_equal(ef800_plugin_registry_state_for(&registry, healthy.plugin_id),
		EF800_PLUGIN_REGISTRY_READY, NULL);
}

ZTEST(ef800_plugin, test_recovery_ledger_clears_only_detached_generation)
{
	uint32_t generation = 9U;
	bool worker_mapped = true;

	zassert_true(ef800_plugin_recovery_domain_detached(&generation,
		&worker_mapped, 9U), NULL);
	zassert_false(worker_mapped, NULL);
	zassert_equal(generation, 0U, NULL);

	generation = 10U;
	worker_mapped = true;
	zassert_false(ef800_plugin_recovery_domain_detached(&generation,
		&worker_mapped, 9U), NULL);
	zassert_true(worker_mapped, NULL);
	zassert_equal(generation, 10U, NULL);
}

ZTEST_SUITE(ef800_plugin, NULL, NULL, NULL, NULL, NULL);
