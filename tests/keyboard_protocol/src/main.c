/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>

#include "keyboard_host.h"
#include "keyboard_protocol.h"
#include "keyboard_translation.h"
#include "ef800_time.h"
#include "ef800_persistence.h"
#include "ef800_persistence_fake.h"
#include "ef800_persistence_legacy.h"
#include "ef800_position.h"
#include "ef800_ports.h"
#include "ef800_port_uart.h"
#include "ef800_workspace.h"
#include "ef800_update.h"
#include "ef800_measurement.h"

ZTEST(keyboard_protocol, test_raw_key_map)
{
	struct kb_wire_scancode scancode;

	zassert_equal(kb_scancode_for_raw(KB_MATRIX_RAW(0, 0), &scancode), 0,
			      NULL);
	zassert_equal(scancode.prefix, 0U, NULL);
	zassert_equal(scancode.code, 0x2FU, NULL);
	zassert_true(!scancode.modifier, NULL);

	zassert_equal(kb_scancode_for_raw(KB_MATRIX_RAW(7, 6), &scancode), 0,
			      NULL);
	zassert_equal(scancode.code, 0x0EU, NULL);
	zassert_equal(kb_scancode_for_raw(KB_MATRIX_RAW(6, 5), &scancode), -1,
			      NULL);
	zassert_equal(kb_scancode_for_raw(KB_SPECIAL_RAW(4), &scancode), 0,
			      NULL);
	zassert_equal(scancode.prefix, KB_SCANCODE_PREFIX_E0, NULL);
	zassert_equal(scancode.code, 0x5EU, NULL);
	zassert_true(scancode.modifier, NULL);
}

ZTEST(keyboard_protocol, test_translation_modifiers)
{
	struct kb_translator translator;
	struct kb_translation output;

	kb_translator_init(&translator);
	zassert_equal(kb_translate_scancode(&translator, 0U, 0x2FU, false,
					   &output), 1, NULL);
	zassert_equal(output.type, KB_TRANSLATION_CHAR, NULL);
	zassert_equal(output.value, 'v', NULL);
	zassert_equal(kb_translate_scancode(&translator, 0U, 0x2AU, false,
					   &output), 0, NULL);
	zassert_equal(kb_translate_scancode(&translator, 0U, 0x2FU, false,
					   &output), 1, NULL);
	zassert_equal(output.value, 'V', NULL);
	zassert_equal(kb_translate_scancode(&translator, 0U, 0x2AU, true,
					   &output), 0, NULL);
	zassert_equal(kb_translate_scancode(&translator, 0U, 0x3AU, false,
					   &output), 0, NULL);
	zassert_equal(kb_translate_scancode(&translator, 0U, 0x2FU, false,
					   &output), 1, NULL);
	zassert_equal(output.value, 'V', NULL);
}

ZTEST(keyboard_protocol, test_host_streaming_parser)
{
	struct ef800_keyboard_host host;
	uint8_t key;
	uint32_t event;
	uint8_t id[2];

	ef800_keyboard_host_init(&host);
	zassert_equal(ef800_keyboard_host_feed(&host, CMD_INITIALIZED),
			      EF800_HOST_FEED_READY, NULL);
	zassert_true(ef800_keyboard_host_is_ready(&host), NULL);

	zassert_equal(ef800_keyboard_host_feed(&host, 0x2FU),
			      EF800_HOST_FEED_KEY, NULL);
	zassert_equal(ef800_keyboard_host_get_key(&host, &key), 0, NULL);
	zassert_equal(key, 'v', NULL);

	zassert_equal(ef800_keyboard_host_feed(&host, KB_SCANCODE_PREFIX_E0),
			      EF800_HOST_FEED_NONE, NULL);
	zassert_equal(ef800_keyboard_host_feed(&host, 0x52U),
			      EF800_HOST_FEED_EVENT, NULL);
	zassert_equal(ef800_keyboard_host_get_event(&host, &event), 0, NULL);
	zassert_equal(event, KB_EVENT_INSERT, NULL);

	zassert_equal(ef800_keyboard_host_feed(&host, CMD_RETURN),
			      EF800_HOST_FEED_NONE, NULL);
	zassert_equal(ef800_keyboard_host_feed(&host, CMD_KEYBOARD_ID),
			      EF800_HOST_FEED_NONE, NULL);
	zassert_equal(ef800_keyboard_host_feed(&host, 0xABU),
			      EF800_HOST_FEED_NONE, NULL);
	zassert_equal(ef800_keyboard_host_feed(&host, 0x83U),
			      EF800_HOST_FEED_COMMAND, NULL);
	zassert_true(ef800_keyboard_host_get_id(&host, id), NULL);
	zassert_equal(id[0], 0xABU, NULL);
	zassert_equal(id[1], 0x83U, NULL);

	/* A stale partial frame must be dropped before the next key. */
	zassert_equal(ef800_keyboard_host_feed_at(&host, CMD_RETURN, 100U),
			      EF800_HOST_FEED_NONE, NULL);
	zassert_equal(ef800_keyboard_host_feed_at(&host, CMD_KEYBOARD_ID, 100U),
			      EF800_HOST_FEED_NONE, NULL);
	zassert_equal(ef800_keyboard_host_feed_at(&host, 0xAAU, 101U),
			      EF800_HOST_FEED_NONE, NULL);
	zassert_equal(ef800_keyboard_host_feed_at(&host, 0x2FU, 400U),
			      EF800_HOST_FEED_KEY, NULL);
	zassert_equal(ef800_keyboard_host_get_key(&host, &key), 0, NULL);
	zassert_equal(key, 'v', NULL);

	zassert_equal(ef800_keyboard_host_feed(&host, CMD_RETURN),
			      EF800_HOST_FEED_NONE, NULL);
	zassert_equal(ef800_keyboard_host_feed(&host, CMD_KEYBOARD_TYPEMATIC),
			      EF800_HOST_FEED_NONE, NULL);
	zassert_equal(ef800_keyboard_host_feed(&host, CMD_RESEND),
			      EF800_HOST_FEED_COMMAND, NULL);
}

ZTEST(keyboard_protocol, test_rtc_local_time_conversion)
{
	const struct ef800_time_settings settings = {
		.utc_offset_minutes = 120,
		.daylight_saving = true,
	};
	struct rtc_time local = {
		.tm_year = 124,
		.tm_mon = 2,
		.tm_mday = 31,
		.tm_hour = 0,
		.tm_min = 30,
		.tm_sec = 0,
		.tm_wday = 0,
		.tm_yday = -1,
		.tm_isdst = -1,
	};
	struct rtc_time utc;
	struct rtc_time round_trip;

	zassert_true(ef800_rtc_time_valid(&local), NULL);
	zassert_ok(ef800_local_time_to_utc(&local, &settings, &utc), NULL);
	zassert_equal(utc.tm_year, 124, NULL);
	zassert_equal(utc.tm_mon, 2, NULL);
	zassert_equal(utc.tm_mday, 30, NULL);
	zassert_equal(utc.tm_hour, 21, NULL);
	zassert_equal(utc.tm_min, 30, NULL);
	zassert_ok(ef800_utc_time_to_local(&utc, &settings, &round_trip), NULL);
	zassert_equal(round_trip.tm_year, local.tm_year, NULL);
	zassert_equal(round_trip.tm_mon, local.tm_mon, NULL);
	zassert_equal(round_trip.tm_mday, local.tm_mday, NULL);
	zassert_equal(round_trip.tm_hour, local.tm_hour, NULL);
	zassert_equal(round_trip.tm_min, local.tm_min, NULL);
}

ZTEST(keyboard_protocol, test_rtc_date_validation)
{
	struct rtc_time leap_day = {
		.tm_year = 124,
		.tm_mon = 1,
		.tm_mday = 29,
		.tm_wday = 4,
	};
	struct rtc_time invalid_day = leap_day;
	struct rtc_time invalid_weekday = leap_day;

	invalid_day.tm_year = 123;
	invalid_weekday.tm_wday = 7;
	zassert_true(ef800_rtc_time_valid(&leap_day), NULL);
	zassert_false(ef800_rtc_time_valid(&invalid_day), NULL);
	zassert_false(ef800_rtc_time_valid(&invalid_weekday), NULL);
}

ZTEST(keyboard_protocol, test_rtc_offset_parsing)
{
	int32_t offset;

	zassert_ok(ef800_parse_utc_offset("+02:30", &offset), NULL);
	zassert_equal(offset, 150, NULL);
	zassert_ok(ef800_parse_utc_offset("-02:30", &offset), NULL);
	zassert_equal(offset, -150, NULL);
	zassert_ok(ef800_parse_utc_offset("-2", &offset), NULL);
	zassert_equal(offset, -120, NULL);
	zassert_equal(ef800_parse_utc_offset("+24:01", &offset), -EINVAL, NULL);
	zassert_equal(ef800_parse_utc_offset("02:30", &offset), -EINVAL, NULL);
}

struct workspace_names {
	char values[4][EF800_WORKSPACE_NAME_MAX + 1U];
	size_t count;
};

static int collect_workspace_name(const char *name, void *context)
{
	struct workspace_names *names = context;

	if (names->count >= ARRAY_SIZE(names->values)) {
		return -ENOMEM;
	}
	strcpy(names->values[names->count++], name);
	return 0;
}

static int ignore_persistence_entry(
	const struct ef800_persistence_entry *entry, void *context)
{
	ARG_UNUSED(entry);
	ARG_UNUSED(context);
	return 0;
}

ZTEST(keyboard_protocol, test_persistence_name_validation)
{
	zassert_true(ef800_persistence_valid_name("settings/time"), NULL);
	zassert_true(ef800_persistence_valid_name("workspace.dat"), NULL);
	zassert_false(ef800_persistence_valid_name(NULL), NULL);
	zassert_false(ef800_persistence_valid_name(""), NULL);
	zassert_false(ef800_persistence_valid_name("/absolute"), NULL);
	zassert_false(ef800_persistence_valid_name("../escape"), NULL);
	zassert_false(ef800_persistence_valid_name("safe/../escape"), NULL);
	zassert_false(ef800_persistence_valid_name("safe//name"), NULL);
	zassert_false(ef800_persistence_valid_name("not safe"), NULL);
	zassert_false(ef800_persistence_valid_name("trailing/"), NULL);
}

ZTEST(keyboard_protocol, test_disabled_zephyr_persistence)
{
	struct ef800_persistence storage;
	const struct ef800_persistence_backend *backend =
		ef800_persistence_zephyr_backend();
	char buffer[8];
	size_t length;

	zassert_not_null(backend, NULL);
	zassert_ok(ef800_persistence_init(&storage, backend), NULL);
	zassert_equal(ef800_persistence_read(&storage,
					EF800_PERSISTENCE_DATA, "alpha.dat",
					buffer, sizeof(buffer), &length), -ENOTSUP, NULL);
	zassert_equal(ef800_persistence_write(&storage,
					 EF800_PERSISTENCE_DATA, "alpha.dat",
					 "x", 1U), -ENOTSUP, NULL);
	zassert_equal(ef800_persistence_list(&storage,
					 EF800_PERSISTENCE_DATA, ignore_persistence_entry,
					 NULL), -ENOTSUP, NULL);
}

ZTEST(keyboard_protocol, test_workspace_persistence)
{
	struct ef800_persistence_fake fake;
	struct ef800_persistence storage;
	struct ef800_workspace workspace;
	struct ef800_workspace restored;
	struct ef800_persistence_entry entry;
	struct workspace_names names = {0};
	char content[32] = {0};
	size_t length;
	int ret;

	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&storage, &fake.backend), NULL);
	zassert_ok(ef800_workspace_init(&workspace, &storage), NULL);

	zassert_ok(ef800_workspace_create(&workspace, "alpha"), NULL);
	zassert_true(ef800_workspace_is_selected(&workspace), NULL);
	zassert_str_equal(ef800_workspace_get_name(&workspace), "alpha");
	zassert_str_equal(ef800_workspace_get_filename(&workspace), "alpha.dat");
	zassert_equal(ef800_workspace_create(&workspace, "alpha"), -EEXIST, NULL);

	zassert_ok(ef800_workspace_append(&workspace, "P1\n", 3U), NULL);
	zassert_ok(ef800_persistence_read(&storage, EF800_PERSISTENCE_DATA,
					 "alpha.dat", content, sizeof(content), &length), NULL);
	zassert_equal(length, 3U, NULL);
	zassert_mem_equal(content, "P1\n", 3U, NULL);

	zassert_ok(ef800_workspace_create(&workspace, "beta"), NULL);
	zassert_str_equal(ef800_workspace_get_name(&workspace), "beta");
	zassert_ok(ef800_workspace_select(&workspace, "alpha"), NULL);
	zassert_str_equal(ef800_workspace_get_name(&workspace), "alpha");

	zassert_ok(ef800_persistence_write(&storage,
					EF800_PERSISTENCE_CONFIGURATION,
					EF800_WORKSPACE_CONFIGURATION_FILE,
					"alpha.dat\r\n", 10U), NULL);
	zassert_ok(ef800_workspace_init(&restored, &storage), NULL);
	zassert_ok(ef800_workspace_load_current(&restored), NULL);
	zassert_str_equal(ef800_workspace_get_name(&restored), "alpha");
	zassert_str_equal(ef800_workspace_get_filename(&restored), "alpha.dat");

	zassert_ok(ef800_persistence_write(&storage, EF800_PERSISTENCE_DATA,
					"readme.txt", "ignored", 7U), NULL);
	zassert_ok(ef800_workspace_list(&restored, collect_workspace_name, &names),
			   NULL);
	zassert_equal(names.count, 2U, NULL);
	zassert_str_equal(names.values[0], "alpha");
	zassert_str_equal(names.values[1], "beta");

	zassert_ok(ef800_persistence_stat(&storage, EF800_PERSISTENCE_DATA,
					 "alpha.dat", &entry), NULL);
	zassert_equal(entry.type, EF800_PERSISTENCE_FILE, NULL);
	zassert_equal(entry.size, 3U, NULL);
	zassert_ok(ef800_persistence_remove(&storage, EF800_PERSISTENCE_DATA,
					"beta.dat"), NULL);
	zassert_equal(ef800_persistence_stat(&storage, EF800_PERSISTENCE_DATA,
						"beta.dat", &entry), -ENOENT, NULL);
	ret = ef800_workspace_init(&restored, &storage);
	zassert_ok(ret, NULL);
	zassert_equal(ef800_workspace_select(&restored, "missing"), -ENOENT, NULL);
}

ZTEST(keyboard_protocol, test_position_record_format)
{
	struct ef800_position_entry entry = {
		.stp = "STP1",
		.code = "P001",
		.height_millimeters = 1234,
	};
	char record[96];
	int32_t height;

	zassert_ok(ef800_position_parse_height("1.234", &height), NULL);
	zassert_equal(height, 1234, NULL);
	zassert_ok(ef800_position_parse_height("-0.5", &height), NULL);
	zassert_equal(height, -500, NULL);
	zassert_ok(ef800_position_parse_height("+2", &height), NULL);
	zassert_equal(height, 2000, NULL);
	zassert_equal(ef800_position_parse_height("1.2345", &height), -EINVAL,
		      NULL);
	zassert_equal(ef800_position_parse_height("1.", &height), -EINVAL, NULL);
	zassert_equal(ef800_position_parse_height("abc", &height), -EINVAL, NULL);
	zassert_true(ef800_position_text_valid("P-001;A"), NULL);
	zassert_false(ef800_position_text_valid("P 001"), NULL);

	zassert_true(ef800_position_format_record(&entry, record, sizeof(record)) > 0,
		     NULL);
	zassert_str_equal(record, "-STP1 1.234 P001 \r");

	entry.height_millimeters = -500;
	zassert_true(ef800_position_format_record(&entry, record, sizeof(record)) > 0,
		     NULL);
	zassert_str_equal(record, "-STP1 -0.500 P001 \r");
}

ZTEST(keyboard_protocol, test_position_append_to_workspace)
{
	struct ef800_persistence_fake fake;
	struct ef800_persistence storage;
	struct ef800_workspace workspace;
	struct ef800_position_entry position = {
		.stp = "STP1",
		.code = "P001",
		.height_millimeters = 2500,
	};
	char content[64] = {0};
	size_t length;

	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&storage, &fake.backend), NULL);
	zassert_ok(ef800_workspace_init(&workspace, &storage), NULL);
	zassert_ok(ef800_workspace_create(&workspace, "survey"), NULL);
	zassert_ok(ef800_position_append(&workspace, &position), NULL);
	zassert_ok(ef800_persistence_read(&storage, EF800_PERSISTENCE_DATA,
					 "survey.dat", content, sizeof(content), &length), NULL);
	zassert_equal(length, strlen("-STP1 2.500 P001 \r"), NULL);
	zassert_mem_equal(content, "-STP1 2.500 P001 \r", length, NULL);
}

ZTEST(keyboard_protocol, test_port_descriptors_and_compatibility)
{
	const struct ef800_port_descriptor *rs232 = ef800_port_get(1U);
	const struct ef800_port_descriptor *serial = ef800_port_get(2U);
	const struct ef800_port_descriptor *multifunction = ef800_port_get(3U);
	const struct ef800_port_descriptor *usb = ef800_port_get(4U);
	const struct ef800_port_descriptor *bluenrg_port = ef800_port_get(5U);
	const struct ef800_port_device_descriptor *debug =
		ef800_port_device_get(EF800_PORT_BUILDER_DEBUG);
	const struct ef800_port_device_descriptor *elta3 =
		ef800_port_device_get(EF800_PORT_BUILDER_ELTA3);
	const struct ef800_port_device_descriptor *msd =
		ef800_port_device_get(EF800_PORT_BUILDER_MSD);

	zassert_equal(ef800_port_count(), EF800_PORT_COUNT, NULL);
	zassert_equal(ef800_port_device_count(), EF800_PORT_DEVICE_COUNT, NULL);
	zassert_not_null(rs232, NULL);
	zassert_not_null(serial, NULL);
	zassert_not_null(multifunction, NULL);
	zassert_not_null(usb, NULL);
	zassert_not_null(bluenrg_port, NULL);
	zassert_false((rs232->capabilities & EF800_PORT_CAP_PARALLEL) != 0U, NULL);
	zassert_true((multifunction->capabilities & EF800_PORT_CAP_PARALLEL) != 0U, NULL);
	zassert_false((multifunction->capabilities & EF800_PORT_CAP_SERIAL_FLOW_CONTROL) != 0U, NULL);
	zassert_true(ef800_port_device_is_compatible(rs232, debug), NULL);
	zassert_true(ef800_port_device_is_compatible(serial, elta3), NULL);
	zassert_false(ef800_port_device_is_compatible(serial, msd), NULL);
	zassert_true(ef800_port_device_is_compatible(usb, debug), NULL);
	zassert_true(ef800_port_device_is_compatible(usb, msd), NULL);
	zassert_true(bluenrg_port->fixed, NULL);
	zassert_true(ef800_port_device_supports_connection(
		EF800_PORT_BUILDER_BLUENRG, EF800_PORT_CONNECTION_SPI), NULL);
	zassert_false(ef800_port_device_supports_connection(
		EF800_PORT_BUILDER_BLUENRG, EF800_PORT_CONNECTION_SERIAL), NULL);
	zassert_true(ef800_port_builder_connection_valid(
		EF800_PORT_BUILDER_DEBUG, EF800_PORT_CONNECTION_USB_CDC), NULL);
	zassert_false(ef800_port_builder_connection_valid(
		EF800_PORT_BUILDER_MSD, EF800_PORT_CONNECTION_USB_CDC), NULL);
	zassert_is_null(ef800_port_get(0U), NULL);
	zassert_is_null(ef800_port_device_get(99U), NULL);
}

ZTEST(keyboard_protocol, test_port_assignment_record_validation)
{
	struct ef800_port_assignment_state state = {
		.port_id = 1U,
		.builder_id = EF800_PORT_BUILDER_DEBUG,
		.connection = EF800_PORT_CONNECTION_SERIAL,
		.assigned = true,
	};
	struct ef800_port_assignment_state decoded;
	uint8_t record[EF800_PORT_ASSIGNMENT_RECORD_SIZE];
	uint8_t invalid[EF800_PORT_ASSIGNMENT_RECORD_SIZE];
	size_t length;

	zassert_ok(ef800_port_assignment_encode(&state, record, sizeof(record),
						&length), NULL);
	zassert_equal(length, EF800_PORT_ASSIGNMENT_RECORD_SIZE, NULL);
	zassert_ok(ef800_port_assignment_decode(record, length, &decoded), NULL);
	zassert_equal(decoded.port_id, state.port_id, NULL);
	zassert_equal(decoded.builder_id, state.builder_id, NULL);
	zassert_equal(decoded.connection, state.connection, NULL);
	zassert_true(decoded.assigned, NULL);

	memcpy(invalid, record, sizeof(invalid));
	invalid[0] = 'X';
	zassert_equal(ef800_port_assignment_decode(invalid, sizeof(invalid),
							  &decoded), -EINVAL, NULL);
	memcpy(invalid, record, sizeof(invalid));
	invalid[2]++;
	zassert_equal(ef800_port_assignment_decode(invalid, sizeof(invalid),
							  &decoded), -EINVAL, NULL);
	memcpy(invalid, record, sizeof(invalid));
	invalid[4] = 4U;
	zassert_equal(ef800_port_assignment_decode(invalid, sizeof(invalid),
							  &decoded), -EINVAL, NULL);
	memcpy(invalid, record, sizeof(invalid));
	invalid[5] = EF800_PORT_BUILDER_MSD;
	zassert_equal(ef800_port_assignment_decode(invalid, sizeof(invalid),
							  &decoded), -EINVAL, NULL);
	memcpy(invalid, record, sizeof(invalid));
	invalid[8]++;
	zassert_equal(ef800_port_assignment_decode(invalid, sizeof(invalid),
							  &decoded), -EINVAL, NULL);
	zassert_equal(ef800_port_assignment_decode(record, length - 1U, &decoded),
				      -EINVAL, NULL);
	state.assigned = false;
	zassert_equal(ef800_port_assignment_encode(&state, record, sizeof(record),
						&length), -EINVAL, NULL);
}

ZTEST(keyboard_protocol, test_port_assignment_persistence)
{
	struct ef800_persistence_fake fake;
	struct ef800_persistence storage;
	struct ef800_port_assignment_service service;
	struct ef800_port_assignment_service restored;
	uint8_t record[EF800_PORT_ASSIGNMENT_RECORD_SIZE];
	char name[] = "port_assignment_1";
	size_t length;

	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&storage, &fake.backend), NULL);
	zassert_ok(ef800_port_assignment_init(&service, &storage), NULL);
	zassert_false(ef800_port_assignment_is_assigned(&service, 1U), NULL);
	zassert_ok(ef800_port_assignment_set(&service, 1U,
						 EF800_PORT_BUILDER_DEBUG), NULL);
	zassert_true(ef800_port_assignment_is_assigned(&service, 1U), NULL);
	zassert_equal(ef800_port_assignment_get_builder(&service, 1U),
			      EF800_PORT_BUILDER_DEBUG, NULL);
	zassert_equal(ef800_port_assignment_set(&service, 4U,
						 EF800_PORT_BUILDER_ELTA3), -EINVAL, NULL);
	zassert_equal(ef800_port_assignment_get_builder(&service, 4U),
			      EF800_PORT_BUILDER_NONE, NULL);

	zassert_ok(ef800_persistence_read(&storage,
					 EF800_PERSISTENCE_CONFIGURATION, name, record,
					 sizeof(record), &length), NULL);
	zassert_equal(ef800_port_assignment_decode(record, length, NULL), -EINVAL,
			      NULL);
	zassert_ok(ef800_port_assignment_init(&restored, &storage), NULL);
	zassert_ok(ef800_port_assignment_load(&restored, 1U), NULL);
	zassert_equal(ef800_port_assignment_get_builder(&restored, 1U),
			      EF800_PORT_BUILDER_DEBUG, NULL);
	zassert_equal(ef800_port_assignment_get_connection(&restored, 1U),
			      EF800_PORT_CONNECTION_SERIAL, NULL);
	zassert_ok(ef800_port_assignment_remove(&restored, 1U), NULL);
	zassert_false(ef800_port_assignment_is_assigned(&restored, 1U), NULL);
	zassert_ok(ef800_port_assignment_remove(&restored, 1U), NULL);
	zassert_equal(ef800_port_assignment_load(&restored, 4U), 0, NULL);
	zassert_true(ef800_port_assignment_is_assigned(&restored, 5U), NULL);
	zassert_equal(ef800_port_assignment_get_builder(&restored, 5U),
			      EF800_PORT_BUILDER_BLUENRG, NULL);
	zassert_equal(ef800_port_assignment_remove(&restored, 5U), -EPERM, NULL);
}

ZTEST(keyboard_protocol, test_disabled_assignment_uses_runtime_model)
{
	struct ef800_persistence storage;
	struct ef800_port_assignment_service service;

	zassert_ok(ef800_persistence_init(&storage,
						 ef800_persistence_zephyr_backend()), NULL);
	zassert_ok(ef800_port_assignment_init(&service, &storage), NULL);
	zassert_equal(ef800_port_assignment_set_with_connection(&service, 1U,
									 EF800_PORT_BUILDER_ELTA3,
									 EF800_PORT_CONNECTION_SERIAL), -ENOTSUP, NULL);
	zassert_ok(ef800_port_assignment_import_with_connection(&service, 1U,
									 EF800_PORT_BUILDER_ELTA3,
									 EF800_PORT_CONNECTION_SERIAL), NULL);
	zassert_true(ef800_port_assignment_is_assigned(&service, 1U), NULL);
	zassert_equal(ef800_port_assignment_get_builder(&service, 1U),
				      EF800_PORT_BUILDER_ELTA3, NULL);
}

ZTEST(keyboard_protocol, test_disabled_port_assignment_storage)
{
	struct ef800_persistence storage;
	struct ef800_port_assignment_service service;

	zassert_ok(ef800_persistence_init(&storage,
					 ef800_persistence_zephyr_backend()), NULL);
	zassert_ok(ef800_port_assignment_init(&service, &storage), NULL);
	zassert_equal(ef800_port_assignment_set(&service, 1U,
						 EF800_PORT_BUILDER_DEBUG), -ENOTSUP, NULL);
	zassert_false(ef800_port_assignment_is_assigned(&service, 1U), NULL);
	zassert_equal(ef800_port_assignment_load(&service, 1U), -ENOTSUP, NULL);
	zassert_false(ef800_port_assignment_is_assigned(&service, 1U), NULL);
}

ZTEST(keyboard_protocol, test_legacy_persistence_parsers)
{
	uint8_t time_record[16] = {0};
	uint8_t serial_records[52] = {0};
	struct ef800_time_settings time_settings;
	struct ef800_measurement_serial_config serial_config;
	uint8_t builder_id;

	sys_put_le32(1U, &time_record[0]);
	sys_put_le32(0U, &time_record[4]);
	sys_put_le32((uint32_t)-2, &time_record[8]);
	time_record[12] = 1U;
	time_record[13] = 0xA5U; /* Native C++ padding is not a data field. */
	zassert_ok(ef800_legacy_parse_time(time_record, sizeof(time_record),
							&time_settings), NULL);
	zassert_equal(time_settings.utc_offset_minutes, -120, NULL);
	zassert_true(time_settings.daylight_saving, NULL);

	zassert_ok(ef800_legacy_parse_port_assignment((const uint8_t *)"3\r\n", 3U,
								&builder_id), NULL);
	zassert_equal(builder_id, EF800_PORT_BUILDER_FTS3XX, NULL);
	zassert_equal(ef800_legacy_parse_port_assignment((const uint8_t *)"5", 1U,
								&builder_id), -EINVAL, NULL);
	{
		const uint8_t embedded_nul[] = {'1', '\0', 'x'};

		zassert_equal(ef800_legacy_parse_port_assignment(embedded_nul,
									 sizeof(embedded_nul), &builder_id), -EINVAL, NULL);
	}

	sys_put_le32(2U, &serial_records[0]);
	sys_put_le32(1U, &serial_records[4]);
	sys_put_le32(1200U, &serial_records[8]);
	sys_put_le32(7U, &serial_records[12]);
	sys_put_le32(EF800_MEASUREMENT_PARITY_ODD, &serial_records[16]);
	sys_put_le32(2U, &serial_records[20]);
	sys_put_le32(0U, &serial_records[24]);
	sys_put_le32(3U, &serial_records[28]);
	sys_put_le32(9600U, &serial_records[32]);
	sys_put_le32(8U, &serial_records[36]);
	sys_put_le32(EF800_MEASUREMENT_PARITY_NONE, &serial_records[40]);
	sys_put_le32(1U, &serial_records[44]);
	sys_put_le32(0U, &serial_records[48]);
	zassert_ok(ef800_legacy_parse_serial_config(serial_records,
								sizeof(serial_records),
								EF800_LEGACY_SERIAL_ELTA3, 1U,
								&serial_config), NULL);
	zassert_equal(serial_config.baudrate, 1200U, NULL);
	zassert_equal(serial_config.data_bits, 7U, NULL);
	zassert_equal(serial_config.parity, EF800_MEASUREMENT_PARITY_ODD, NULL);
	zassert_equal(serial_config.stop_bits, 2U, NULL);
	zassert_equal(serial_config.flow_control, EF800_MEASUREMENT_FLOW_NONE, NULL);
	sys_put_le32(5U, &serial_records[12]);
	sys_put_le32(EF800_MEASUREMENT_PARITY_EVEN, &serial_records[16]);
	sys_put_le32(EF800_MEASUREMENT_FLOW_RTS_CTS, &serial_records[24]);
	zassert_ok(ef800_legacy_parse_serial_config(serial_records,
								sizeof(serial_records),
								EF800_LEGACY_SERIAL_ELTA3, 1U,
								&serial_config), NULL);
	zassert_equal(serial_config.data_bits, 5U, NULL);
	zassert_equal(serial_config.parity, EF800_MEASUREMENT_PARITY_EVEN, NULL);
	zassert_equal(serial_config.flow_control, EF800_MEASUREMENT_FLOW_RTS_CTS, NULL);
	zassert_equal(ef800_legacy_parse_serial_config(serial_records, 51U,
								EF800_LEGACY_SERIAL_ELTA3, 1U,
								&serial_config), -EINVAL, NULL);
		sys_put_le32(2U, &serial_records[0]);
		sys_put_le32(1U, &serial_records[28]);
	zassert_equal(ef800_legacy_parse_serial_config(serial_records,
								sizeof(serial_records),
								EF800_LEGACY_SERIAL_ELTA3, 1U,
								&serial_config), -EINVAL, NULL);
		sys_put_le32(0U, &serial_records[0]);
	zassert_equal(ef800_legacy_parse_serial_config(serial_records, 4U,
								EF800_LEGACY_SERIAL_ELTA3, 1U,
								&serial_config), -ENOENT, NULL);
	time_record[12] = 2U;
	zassert_equal(ef800_legacy_parse_time(time_record, sizeof(time_record),
							&time_settings), -EINVAL, NULL);
}

ZTEST(keyboard_protocol, test_legacy_persistence_migration)
{
	struct ef800_persistence_fake fake;
	struct ef800_persistence storage;
	struct ef800_time_settings time_settings;
	struct ef800_time_settings decoded_time;
	struct ef800_measurement_serial_config serial_config = {
		.baudrate = 9600U,
		.data_bits = 8U,
		.parity = EF800_MEASUREMENT_PARITY_NONE,
		.stop_bits = 1U,
		.flow_control = EF800_MEASUREMENT_FLOW_NONE,
	};
	struct ef800_measurement_serial_config decoded_serial;
	struct ef800_port_assignment_state assignment;
	uint8_t legacy_time[16] = {0};
	uint8_t legacy_serial[28] = {0};
	uint8_t record[EF800_PORT_ASSIGNMENT_RECORD_SIZE];
	uint8_t buffer[64];
	size_t length;

	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&storage, &fake.backend), NULL);
	sys_put_le32(1U, &legacy_time[0]);
	sys_put_le32(0U, &legacy_time[4]);
	sys_put_le32(1U, &legacy_time[8]);
	legacy_time[12] = 0U;
	zassert_ok(ef800_persistence_write(&storage,
						EF800_PERSISTENCE_CONFIGURATION,
						EF800_LEGACY_TIME_FILE, legacy_time,
						sizeof(legacy_time)), NULL);
	zassert_ok(ef800_persistence_write(&storage,
						EF800_PERSISTENCE_CONFIGURATION, "port_1",
						"1\r\n", 3U), NULL);
	sys_put_le32(1U, &legacy_serial[0]);
	sys_put_le32(2U, &legacy_serial[4]);
	sys_put_le32(9600U, &legacy_serial[8]);
	sys_put_le32(8U, &legacy_serial[12]);
	sys_put_le32(EF800_MEASUREMENT_PARITY_NONE, &legacy_serial[16]);
	sys_put_le32(1U, &legacy_serial[20]);
	sys_put_le32(0U, &legacy_serial[24]);
	zassert_ok(ef800_persistence_write(&storage,
						EF800_PERSISTENCE_CONFIGURATION,
						EF800_LEGACY_ELTA3_FILE, legacy_serial,
						sizeof(legacy_serial)), NULL);
	zassert_ok(ef800_legacy_migrate_all(&storage), NULL);

	zassert_equal(ef800_persistence_read(&storage,
						EF800_PERSISTENCE_CONFIGURATION,
						EF800_LEGACY_TIME_FILE, buffer, sizeof(buffer),
						&length), -ENOENT, NULL);
	zassert_ok(ef800_persistence_read(&storage,
						EF800_PERSISTENCE_CONFIGURATION,
						EF800_TIME_SETTINGS_FILE, buffer, sizeof(buffer),
						&length), NULL);
	zassert_ok(ef800_time_settings_decode(buffer, length, &decoded_time), NULL);
	time_settings.utc_offset_minutes = 60;
	time_settings.daylight_saving = false;
	zassert_equal(decoded_time.utc_offset_minutes, time_settings.utc_offset_minutes, NULL);
	zassert_equal(decoded_time.daylight_saving, time_settings.daylight_saving, NULL);

	zassert_ok(ef800_persistence_read(&storage,
						EF800_PERSISTENCE_CONFIGURATION, "port_assignment_1", record,
						sizeof(record), &length), NULL);
	zassert_ok(ef800_port_assignment_decode(record, length, &assignment), NULL);
	zassert_equal(assignment.port_id, 1U, NULL);
	zassert_equal(assignment.builder_id, EF800_PORT_BUILDER_DEBUG, NULL);
	zassert_equal(assignment.connection, EF800_PORT_CONNECTION_SERIAL, NULL);
	zassert_equal(ef800_persistence_read(&storage,
						EF800_PERSISTENCE_CONFIGURATION, "port_1", buffer,
						sizeof(buffer), &length), -ENOENT, NULL);

	zassert_ok(ef800_persistence_read(&storage,
						EF800_PERSISTENCE_CONFIGURATION, "elta3_2", buffer,
						sizeof(buffer), &length), NULL);
	zassert_ok(ef800_serial_config_decode(buffer, length, &decoded_serial), NULL);
	zassert_equal(decoded_serial.baudrate, serial_config.baudrate, NULL);
	zassert_equal(decoded_serial.data_bits, serial_config.data_bits, NULL);
	zassert_equal(decoded_serial.parity, serial_config.parity, NULL);
	zassert_equal(decoded_serial.stop_bits, serial_config.stop_bits, NULL);
	zassert_equal(ef800_persistence_read(&storage,
						EF800_PERSISTENCE_CONFIGURATION,
						EF800_LEGACY_ELTA3_FILE, buffer, sizeof(buffer),
						&length), -ENOENT, NULL);
	zassert_ok(ef800_legacy_migrate_all(&storage), NULL);
}

ZTEST(keyboard_protocol, test_legacy_migration_preflight_rejects_duplicate_debug)
{
	struct ef800_persistence_fake fake;
	struct ef800_persistence storage;
	char buffer[4] = {0};
	size_t length;

	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&storage, &fake.backend), NULL);
	zassert_ok(ef800_persistence_write(&storage,
						EF800_PERSISTENCE_CONFIGURATION, "port_1",
						"1", 1U), NULL);
	zassert_ok(ef800_persistence_write(&storage,
						EF800_PERSISTENCE_CONFIGURATION, "port_2",
						"1", 1U), NULL);
	zassert_equal(ef800_legacy_migrate_all(&storage), -EADDRINUSE, NULL);
	zassert_equal(ef800_persistence_read(&storage,
						EF800_PERSISTENCE_CONFIGURATION, "port_1", buffer,
						sizeof(buffer), &length), 0, NULL);
	zassert_equal(length, 1U, NULL);
	zassert_mem_equal(buffer, "1", 1U, NULL);
}

ZTEST(keyboard_protocol, test_port_uart_routes_and_ownership)
{
	const struct ef800_port_uart_route *port1 =
		ef800_port_uart_for_port(1U);
	const struct ef800_port_uart_route *port2 =
		ef800_port_uart_for_port(2U);
	const struct ef800_port_uart_route *port3 =
		ef800_port_uart_for_port(3U);
	struct ef800_port_uart_registry registry;

	zassert_not_null(port1, NULL);
	zassert_not_null(port2, NULL);
	zassert_not_null(port3, NULL);
	zassert_equal(port1->uart_id, EF800_PORT_UART3, NULL);
	zassert_str_equal(port1->tx_pins, "PD8");
	zassert_str_equal(port1->rx_pins, "PD9");
	zassert_equal(port2->uart_id, EF800_PORT_UART1, NULL);
	zassert_false(port2->has_rts, NULL);
	zassert_false(port2->has_cts, NULL);
	zassert_equal(port3->uart_id, EF800_PORT_UART4, NULL);
	zassert_false(port3->has_rts, NULL);
	zassert_false(port3->has_cts, NULL);
	zassert_not_null(ef800_port_uart_for_connection(2U,
								 EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL), NULL);
	zassert_true(ef800_port_uart_for_connection(2U,
								 EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL)->has_rts,
			     NULL);
	zassert_equal(ef800_port_connection_count(2U), 2U, NULL);
	zassert_equal(ef800_port_connection_count(3U), 4U, NULL);
	zassert_true(ef800_port_connection_at(3U, 2U)->capability ==
			     EF800_PORT_CONNECTION_I2C, NULL);
	zassert_is_null(ef800_port_uart_for_port(4U), NULL);
	zassert_is_null(ef800_port_uart_for_id(EF800_PORT_UART_NONE), NULL);

	zassert_ok(ef800_port_uart_registry_init(&registry, EF800_PORT_UART_NONE), NULL);
	zassert_ok(ef800_port_uart_can_claim(&registry, 1U,
						 EF800_PORT_BUILDER_DEBUG,
						 EF800_PORT_CONNECTION_SERIAL), NULL);
	zassert_ok(ef800_port_uart_can_claim(&registry, 2U,
							 EF800_PORT_BUILDER_ELTA3,
							 EF800_PORT_CONNECTION_SERIAL), NULL);
	zassert_ok(ef800_port_uart_can_claim(&registry, 2U,
						 EF800_PORT_BUILDER_ELTA3,
						 EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL), NULL);
	zassert_ok(ef800_port_uart_claim(&registry, 2U,
						 EF800_PORT_BUILDER_ELTA3,
						 EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL), NULL);
	zassert_true(ef800_port_uart_is_claimed(&registry, 2U), NULL);
	zassert_equal(ef800_port_uart_can_claim(&registry, 3U,
						       EF800_PORT_BUILDER_FTS3XX,
						       EF800_PORT_CONNECTION_SERIAL), 0, NULL);
	zassert_equal(ef800_port_uart_can_claim(&registry, 2U,
							       EF800_PORT_BUILDER_MSD,
							       EF800_PORT_CONNECTION_SERIAL), -EINVAL, NULL);
	{
		struct ef800_port_uart_registry console_registry;

		zassert_ok(ef800_port_uart_registry_init(&console_registry,
								 EF800_PORT_UART1), NULL);
		zassert_equal(ef800_port_uart_can_claim(&console_registry, 2U,
								 EF800_PORT_BUILDER_ELTA3,
								 EF800_PORT_CONNECTION_SERIAL), -EBUSY, NULL);
	}
	zassert_equal(ef800_port_uart_release(&registry, 2U,
						   EF800_PORT_BUILDER_DEBUG,
						   EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL), -EINVAL, NULL);
	zassert_ok(ef800_port_uart_release(&registry, 2U,
						 EF800_PORT_BUILDER_ELTA3,
						 EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL), NULL);
	zassert_false(ef800_port_uart_is_claimed(&registry, 2U), NULL);
	zassert_ok(ef800_port_uart_claim(&registry, 4U,
						 EF800_PORT_BUILDER_MSD,
						 EF800_PORT_CONNECTION_USB_MSD), NULL);
	zassert_false(ef800_port_uart_is_claimed(&registry, 4U), NULL);
}

ZTEST(keyboard_protocol, test_boot_metadata_assignments_and_console_fallback)
{
	struct ef800_boot_metadata metadata;
	struct ef800_boot_metadata decoded;
	uint8_t encoded[EF800_BOOT_METADATA_SIZE];

	ef800_boot_metadata_init(&metadata);
	zassert_equal(sizeof(metadata), EF800_BOOT_METADATA_SIZE, NULL);
	zassert_true(ef800_boot_metadata_valid(&metadata), NULL);
	zassert_equal(ef800_port_assignment_builder(metadata.port_assignments, 5U),
			      EF800_PORT_BUILDER_BLUENRG, NULL);
	zassert_equal(ef800_port_assignment_connection(metadata.port_assignments, 5U),
			      EF800_PORT_CONNECTION_SPI, NULL);
	zassert_equal(ef800_console_port_select(&metadata, false),
			      EF800_CONSOLE_FALLBACK_PORT, NULL);

	zassert_ok(ef800_port_assignment_set_record(&metadata.port_assignments, 1U,
								 EF800_PORT_BUILDER_DEBUG,
								 EF800_PORT_CONNECTION_SERIAL), NULL);
	zassert_ok(ef800_port_assignment_set_record(&metadata.port_assignments, 2U,
								 EF800_PORT_BUILDER_ELTA3,
								 EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL), NULL);
	zassert_ok(ef800_port_assignment_set_record(&metadata.port_assignments, 3U,
								 EF800_PORT_BUILDER_FTS3XX,
								 EF800_PORT_CONNECTION_SERIAL), NULL);
	zassert_ok(ef800_port_assignment_set_record(&metadata.port_assignments, 4U,
								 EF800_PORT_BUILDER_MSD,
								 EF800_PORT_CONNECTION_USB_MSD), NULL);
	zassert_true(ef800_port_assignments_valid(metadata.port_assignments), NULL);
	zassert_equal(ef800_port_assignment_builder(metadata.port_assignments, 2U),
			      EF800_PORT_BUILDER_ELTA3, NULL);
	zassert_equal(ef800_port_assignment_connection(metadata.port_assignments, 2U),
			      EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL, NULL);
	zassert_equal(ef800_console_port_select(&metadata, false),
			      EF800_CONSOLE_SAFE_PORT, NULL);
	zassert_ok(ef800_boot_metadata_set_console(&metadata, 1U), NULL);
	zassert_equal(ef800_console_port_select(&metadata, false), 1U, NULL);
	metadata.crc32 = ef800_boot_metadata_crc(&metadata);
	zassert_true(ef800_boot_metadata_valid(&metadata), NULL);
	zassert_ok(ef800_boot_metadata_encode(&metadata, encoded, sizeof(encoded)), NULL);
	zassert_equal(encoded[0], 0x42U, NULL);
	zassert_equal(encoded[1], 0x38U, NULL);
	zassert_equal(encoded[4], EF800_BOOT_METADATA_VERSION, NULL);
	zassert_ok(ef800_boot_metadata_decode(encoded, sizeof(encoded), &decoded), NULL);
	zassert_mem_equal(&decoded, &metadata, sizeof(metadata), NULL);
	zassert_ok(ef800_boot_metadata_set_console(&metadata, 2U), NULL);
	metadata.crc32 = ef800_boot_metadata_crc(&metadata);
	zassert_false(ef800_boot_metadata_valid(&metadata), NULL);
	zassert_ok(ef800_boot_metadata_set_console(&metadata, 1U), NULL);
	metadata.crc32 = ef800_boot_metadata_crc(&metadata);
	zassert_true(ef800_boot_metadata_valid(&metadata), NULL);
	zassert_equal(ef800_port_assignment_set_record(&metadata.port_assignments, 3U,
								 EF800_PORT_BUILDER_ELTA3,
								 EF800_PORT_CONNECTION_PARALLEL), -EINVAL, NULL);
	metadata.port_assignments |= 0x80ULL;
	zassert_false(ef800_boot_metadata_valid(&metadata), NULL);
	metadata.port_assignments &= ~0x80ULL;
	metadata.port_assignments |= 1ULL << 40;
	zassert_false(ef800_boot_metadata_valid(&metadata), NULL);
	metadata.port_assignments &= ~(1ULL << 40);
	zassert_ok(ef800_port_assignment_set_record(&metadata.port_assignments, 2U,
								 EF800_PORT_BUILDER_DEBUG,
								 EF800_PORT_CONNECTION_SERIAL), NULL);
	metadata.crc32 = ef800_boot_metadata_crc(&metadata);
	zassert_false(ef800_boot_metadata_valid(&metadata), NULL);
}

struct measurement_fake_transport {
	const uint8_t *response;
	size_t response_length;
	size_t response_offset;
	uint8_t writes[64];
	size_t write_length;
};

static int measurement_fake_write(void *context, const uint8_t *data,
					  size_t length)
{
	struct measurement_fake_transport *fake = context;

	if (fake == NULL || data == NULL || length > sizeof(fake->writes) -
		fake->write_length) {
		return -EINVAL;
	}
	memcpy(&fake->writes[fake->write_length], data, length);
	fake->write_length += length;
	return (int)length;
}

static int measurement_fake_read(void *context, uint8_t *data,
					 int32_t timeout_ms)
{
	struct measurement_fake_transport *fake = context;

	ARG_UNUSED(timeout_ms);
	if (fake == NULL || data == NULL) {
		return -EINVAL;
	}
	if (fake->response_offset >= fake->response_length) {
		return 0;
	}
	*data = fake->response[fake->response_offset++];
	return 1;
}

ZTEST(keyboard_protocol, test_measurement_protocols)
{
	struct ef800_measurement_serial_config config;
	struct ef800_measurement_data measurement;
	struct measurement_fake_transport elta_fake = {
		.response = (const uint8_t *)"A\r\n#48 0 1.234; 100.0000; 50.5000\r\n",
		.response_length = strlen("A\r\n#48 0 1.234; 100.0000; 50.5000\r\n"),
	};
	struct measurement_fake_transport fts_fake = {
		.response = (const uint8_t *)"1234 0900000 1800000\r\n",
		.response_length = strlen("1234 0900000 1800000\r\n"),
	};
	const struct ef800_measurement_transport elta_transport = {
		.write = measurement_fake_write,
		.read = measurement_fake_read,
		.context = &elta_fake,
	};
	const struct ef800_measurement_transport fts_transport = {
		.write = measurement_fake_write,
		.read = measurement_fake_read,
		.context = &fts_fake,
	};
	struct ef800_measurement_record record = {
		.number = 7,
		.data = {
			.distance_mm = 1234,
			.horizontal_gon_1e4 = 1000000,
			.vertical_gon_1e4 = 505000,
		},
		.staff_height_mm = 1500,
		.code = "P001",
	};
	char formatted[96];

	ef800_elta3_default_serial_config(&config);
	zassert_equal(config.baudrate, 1200U, NULL);
	zassert_equal(config.data_bits, 7U, NULL);
	zassert_equal(config.parity, EF800_MEASUREMENT_PARITY_ODD, NULL);
	zassert_equal(config.stop_bits, 2U, NULL);
	zassert_ok(ef800_measurement_serial_config_for_builder(
		EF800_PORT_BUILDER_ELTA3, EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL,
		&config), NULL);
	zassert_equal(config.flow_control, EF800_MEASUREMENT_FLOW_RTS_CTS, NULL);
	zassert_equal(ef800_measurement_serial_config_for_builder(
		EF800_PORT_BUILDER_DEBUG, EF800_PORT_CONNECTION_SERIAL, &config),
		-EINVAL, NULL);
	zassert_ok(ef800_elta3_measure(&elta_transport, &measurement), NULL);
	zassert_equal(measurement.control, 48U, NULL);
	zassert_equal(measurement.distance_mm, 1234, NULL);
	zassert_equal(measurement.horizontal_gon_1e4, 1000000, NULL);
	zassert_equal(measurement.vertical_gon_1e4, 505000, NULL);
	zassert_mem_equal(elta_fake.writes, "S14\r\nB\r\nQ\r\n",
				 strlen("S14\r\nB\r\nQ\r\n"), NULL);

	ef800_fts3xx_default_serial_config(&config);
	zassert_equal(config.baudrate, 9600U, NULL);
	zassert_equal(config.data_bits, 8U, NULL);
	zassert_equal(config.parity, EF800_MEASUREMENT_PARITY_NONE, NULL);
	zassert_equal(config.stop_bits, 1U, NULL);
	zassert_ok(ef800_fts3xx_measure(&fts_transport, &measurement), NULL);
	zassert_equal(measurement.distance_mm, 1234, NULL);
	zassert_equal(measurement.vertical_gon_1e4, 1000000, NULL);
	zassert_equal(measurement.horizontal_gon_1e4, 2000000, NULL);
	zassert_mem_equal(fts_fake.writes, "\x11\r\n", 3U, NULL);

	zassert_true(ef800_measurement_format_record(&record, formatted,
								 sizeof(formatted)) > 0, NULL);
	zassert_str_equal(formatted, "7 100.0000 1.234 50.5000 1.500 P001\r", NULL);

	zassert_ok(ef800_elta3_up(&elta_transport), NULL);
	zassert_ok(ef800_elta3_down(&elta_transport), NULL);
	zassert_ok(ef800_elta3_enter(&elta_transport), NULL);
	zassert_mem_equal(&elta_fake.writes[11], "S12\r\nS11\r\nS14\r\n", 15U, NULL);
}

ZTEST(keyboard_protocol, test_measurement_protocol_rejects_invalid_data)
{
	struct ef800_measurement_data measurement;
	char record[64];
	struct ef800_measurement_record invalid_record = {
		.number = 1,
		.code = "bad code",
	};

	zassert_equal(ef800_elta3_parse_response("#49 1 2", &measurement), -EINVAL,
			      NULL);
	zassert_equal(ef800_elta3_parse_response("#48 0 1.2", &measurement), -EINVAL,
			      NULL);
	zassert_ok(ef800_elta3_parse_response("#33 100.0000; 50.5000;",
								 &measurement), NULL);
	zassert_equal(measurement.control, 33U, NULL);
	zassert_equal(measurement.distance_mm, 0, NULL);
	zassert_equal(measurement.horizontal_gon_1e4, 1000000, NULL);
	zassert_equal(measurement.vertical_gon_1e4, 505000, NULL);
	zassert_equal(ef800_fts3xx_parse_response("1234 0966000 1800000",
								 &measurement), -EINVAL, NULL);
	{
		struct measurement_fake_transport fake = {
			.response = (const uint8_t *)"N\r\n",
			.response_length = 3U,
		};
		const struct ef800_measurement_transport transport = {
			.write = measurement_fake_write,
			.read = measurement_fake_read,
			.context = &fake,
		};

		zassert_equal(ef800_elta3_measure(&transport, &measurement), -EIO, NULL);
	}
	zassert_equal(ef800_measurement_format_record(&invalid_record, record,
									 sizeof(record)), -EINVAL, NULL);
}

ZTEST(keyboard_protocol, test_measurement_append_to_workspace)
{
	struct ef800_persistence_fake fake;
	struct ef800_persistence storage;
	struct ef800_workspace workspace;
	struct ef800_measurement_record record = {
		.number = 7,
		.data = {
			.distance_mm = 1234,
			.horizontal_gon_1e4 = 1000000,
			.vertical_gon_1e4 = 505000,
		},
		.staff_height_mm = 1500,
		.code = "P001",
	};
	char content[128] = {0};
	size_t length;

	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&storage, &fake.backend), NULL);
	zassert_ok(ef800_workspace_init(&workspace, &storage), NULL);
	zassert_ok(ef800_workspace_create(&workspace, "survey"), NULL);
	zassert_ok(ef800_measurement_append(&workspace, &record), NULL);
	zassert_ok(ef800_persistence_read(&storage, EF800_PERSISTENCE_DATA,
						 "survey.dat", content, sizeof(content), &length), NULL);
	zassert_str_equal(content, "7 100.0000 1.234 50.5000 1.500 P001\r", NULL);
	zassert_equal(length, strlen(content), NULL);
}

ZTEST_SUITE(keyboard_protocol, NULL, NULL, NULL, NULL, NULL);
