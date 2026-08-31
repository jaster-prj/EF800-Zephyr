/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>

#include "keyboard_host.h"
#include "keyboard_protocol.h"
#include "keyboard_translation.h"
#include "ef800_time.h"

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

ZTEST_SUITE(keyboard_protocol, NULL, NULL, NULL, NULL, NULL);
