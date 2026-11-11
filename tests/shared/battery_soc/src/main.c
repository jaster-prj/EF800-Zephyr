/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/ztest.h>

#include "ef800_battery_soc.h"
#include "ef800_battery_adc_ring.h"
#include "ef800_battery_soc_gauge.h"
#include "ef800_battery_soc_store.h"
#include "ef800_max17043.h"
#include "ef800_persistence_fake.h"
#include "ef800_runtime_state.h"
#include "ef800_virtual_profile.h"

int ef800_main_rtc_get_local_time(struct rtc_time *local)
{
	ARG_UNUSED(local);
	return -ENOTSUP;
}

int ef800_virtual_serial_peer_set_profile(uint8_t port, uint8_t profile)
{
	ARG_UNUSED(port);
	ARG_UNUSED(profile);
	return 0;
}

int ef800_virtual_serial_peer_set(uint8_t port, const uint8_t *response,
	size_t length, int32_t delay_ms, int32_t error, bool available)
{
	ARG_UNUSED(port);
	ARG_UNUSED(response);
	ARG_UNUSED(length);
	ARG_UNUSED(delay_ms);
	ARG_UNUSED(error);
	ARG_UNUSED(available);
	return 0;
}

static struct ef800_battery_soc_table sample_table(void)
{
	return (struct ef800_battery_soc_table){
		.version = EF800_BATTERY_SOC_TABLE_VERSION,
		.chemistry = EF800_BATTERY_SOC_CHEMISTRY_LIFEPO4,
		.point_count = 3U,
		.points = {
			{ .pin_mv = 1000U, .soc_percent = 0U },
			{ .pin_mv = 2000U, .soc_percent = 50U },
			{ .pin_mv = 3000U, .soc_percent = 100U },
		},
	};
}

static const struct adc_dt_spec test_battery_adc =
	ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), battery);

ZTEST(battery_soc_adc, test_adc_ring_warmup_averages_and_wraps)
{
	struct ef800_battery_adc_ring ring = {0};
	uint16_t average;

	zassert_ok(ef800_battery_adc_ring_push(&ring, 1000U, &average), NULL);
	zassert_equal(average, 1000U, "warm-up should use available samples");
	zassert_ok(ef800_battery_adc_ring_push(&ring, 1201U, &average), NULL);
	zassert_equal(average, 1101U, "mean should round to nearest millivolt");

	for (uint16_t sample = 2000U; sample < 2000U +
		EF800_BATTERY_ADC_RING_CAPACITY - 2U; ++sample) {
		zassert_ok(ef800_battery_adc_ring_push(&ring, sample, &average), NULL);
	}
	zassert_equal(ring.count, EF800_BATTERY_ADC_RING_CAPACITY, NULL);
	zassert_ok(ef800_battery_adc_ring_push(&ring, 3000U, &average), NULL);
	zassert_equal(ring.count, EF800_BATTERY_ADC_RING_CAPACITY, NULL);
	zassert_equal(average, 2027U, "full ring should discard oldest sample");
}

ZTEST(battery_soc_table, test_hex_encode_decode_round_trip)
{
	struct ef800_battery_soc_table original = sample_table();
	struct ef800_battery_soc_table decoded;
	char hex[EF800_BATTERY_SOC_TABLE_MAX_HEX_LENGTH + 1U];
	size_t hex_length;

	zassert_ok(ef800_battery_soc_table_encode_hex(&original, hex, sizeof(hex),
		&hex_length), NULL);
	zassert_true(hex_length > 0U, NULL);
	zassert_ok(ef800_battery_soc_table_decode_hex(hex, hex_length, &decoded), NULL);
	zassert_equal(decoded.version, original.version, NULL);
	zassert_equal(decoded.chemistry, original.chemistry, NULL);
	zassert_equal(decoded.point_count, original.point_count, NULL);
	for (size_t index = 0U; index < original.point_count; ++index) {
		zassert_equal(decoded.points[index].pin_mv,
			original.points[index].pin_mv, NULL);
		zassert_equal(decoded.points[index].soc_percent,
			original.points[index].soc_percent, NULL);
	}
}

ZTEST(battery_soc_table, test_documented_approximate_lifepo4_table)
{
	static const char hex[] =
		"01020b080100099b0a0a3f140a4f1e0a68280a70320a783c0a91460aa1500aba5a0ae36473f7f18d";
	static const uint16_t expected_pin_mv[] = {
		2049U, 2459U, 2623U, 2639U, 2664U, 2672U,
		2680U, 2705U, 2721U, 2746U, 2787U,
	};
	struct ef800_battery_soc_table table;

	zassert_ok(ef800_battery_soc_table_decode_hex(hex, strlen(hex), &table), NULL);
	zassert_equal(table.chemistry, EF800_BATTERY_SOC_CHEMISTRY_LIFEPO4, NULL);
	zassert_equal(table.point_count, ARRAY_SIZE(expected_pin_mv), NULL);
	for (size_t index = 0U; index < ARRAY_SIZE(expected_pin_mv); ++index) {
		zassert_equal(table.points[index].pin_mv, expected_pin_mv[index], NULL);
		zassert_equal(table.points[index].soc_percent, index * 10U, NULL);
	}
}

ZTEST(battery_soc_table, test_interpolation_clamps_and_rounds)
{
	struct ef800_battery_soc_table table = sample_table();
	uint8_t soc;
	uint16_t pin_mv;

	zassert_ok(ef800_battery_soc_from_pin_mv(&table, 500U, &soc), NULL);
	zassert_equal(soc, 0U, NULL);
	zassert_ok(ef800_battery_soc_from_pin_mv(&table, 1500U, &soc), NULL);
	zassert_equal(soc, 25U, NULL);
	zassert_ok(ef800_battery_soc_from_pin_mv(&table, 3500U, &soc), NULL);
	zassert_equal(soc, 100U, NULL);

	zassert_ok(ef800_battery_pin_mv_from_soc(&table, 0U, &pin_mv), NULL);
	zassert_equal(pin_mv, 1000U, NULL);
	zassert_ok(ef800_battery_pin_mv_from_soc(&table, 25U, &pin_mv), NULL);
	zassert_equal(pin_mv, 1500U, NULL);
	zassert_ok(ef800_battery_pin_mv_from_soc(&table, 100U, &pin_mv), NULL);
	zassert_equal(pin_mv, 3000U, NULL);
}

ZTEST(battery_soc_table, test_decode_rejects_bad_hex_crc_and_length)
{
	struct ef800_battery_soc_table table = sample_table();
	struct ef800_battery_soc_table decoded;
	char hex[EF800_BATTERY_SOC_TABLE_MAX_HEX_LENGTH + 1U];
	size_t hex_length;

	zassert_ok(ef800_battery_soc_table_encode_hex(&table, hex, sizeof(hex),
		&hex_length), NULL);
	zassert_equal(ef800_battery_soc_table_decode_hex(hex, hex_length - 1U,
		&decoded), -EINVAL, NULL);
	hex[0] = 'Z';
	zassert_equal(ef800_battery_soc_table_decode_hex(hex, hex_length,
		&decoded), -EINVAL, NULL);
	zassert_ok(ef800_battery_soc_table_encode_hex(&table, hex, sizeof(hex),
		&hex_length), NULL);
	hex[hex_length - 1U] = hex[hex_length - 1U] == '0' ? '1' : '0';
	zassert_equal(ef800_battery_soc_table_decode_hex(hex, hex_length,
		&decoded), -EBADMSG, NULL);
}

ZTEST(battery_soc_table, test_validation_rejects_invalid_points_and_capacity)
{
	struct ef800_battery_soc_table table = sample_table();
	char hex[EF800_BATTERY_SOC_TABLE_MAX_HEX_LENGTH + 1U];
	size_t hex_length;

	table.points[1].pin_mv = table.points[0].pin_mv;
	zassert_equal(ef800_battery_soc_table_validate(&table), -EINVAL, NULL);
	table = sample_table();
	table.points[1].soc_percent = table.points[0].soc_percent;
	zassert_equal(ef800_battery_soc_table_validate(&table), -EINVAL, NULL);
	table = sample_table();
	table.points[2].pin_mv = EF800_BATTERY_SOC_ADC_MAX_MV + 1U;
	zassert_equal(ef800_battery_soc_table_validate(&table), -ERANGE, NULL);
	table = sample_table();
	zassert_equal(ef800_battery_soc_table_encode_hex(&table, hex, 1U,
		&hex_length), -ENOMEM, NULL);
}

ZTEST(battery_soc_adc, test_native_adc_path_converts_millivolts_and_soc_bounds)
{
	struct ef800_battery_soc_table table = sample_table();
	struct adc_sequence sequence;
	int16_t raw_sample;
	int32_t measured_mv;
	uint16_t requested_mv;
	uint8_t actual_soc;
	uint8_t battery_control[3] = { 4U, 6U, 0U };
	static const uint8_t requested_values[] = { 0U, 50U, 100U };

	zassert_true(device_is_ready(test_battery_adc.dev), NULL);
	zassert_ok(adc_channel_setup_dt(&test_battery_adc), NULL);
	for (size_t index = 0U; index < ARRAY_SIZE(requested_values); ++index) {
		uint8_t requested_soc = requested_values[index];

		battery_control[2] = requested_soc;
		zassert_ok(ef800_virtual_profile_apply(battery_control,
			sizeof(battery_control)), NULL);
		requested_soc = ef800_virtual_profile_battery_soc();
		zassert_ok(ef800_battery_pin_mv_from_soc(&table, requested_soc,
			&requested_mv), NULL);
		zassert_ok(adc_emul_const_value_set(test_battery_adc.dev,
			test_battery_adc.channel_id, requested_mv), NULL);
		zassert_ok(adc_sequence_init_dt(&test_battery_adc, &sequence), NULL);
		sequence.buffer = &raw_sample;
		sequence.buffer_size = sizeof(raw_sample);
		zassert_ok(adc_read_dt(&test_battery_adc, &sequence), NULL);
		measured_mv = raw_sample;
		zassert_ok(adc_raw_to_millivolts_dt(&test_battery_adc,
			&measured_mv), NULL);
		zassert_ok(ef800_battery_soc_from_pin_mv(&table,
			(uint16_t)measured_mv, &actual_soc), NULL);
		zassert_equal(actual_soc, requested_soc, "requested=%u measured=%d",
			requested_soc, measured_mv);
	}
}

ZTEST(battery_soc_gauge_emul, test_max17048_register_path_uses_virtual_soc)
{
	static const struct i2c_dt_spec gauge_spec =
		I2C_DT_SPEC_GET(DT_NODELABEL(battery_gauge_emul));
	struct ef800_max17043 gauge;
	struct ef800_max17043_status status;
	uint8_t set_zero[] = { 4U, 6U, 0U };
	uint8_t set_full[] = { 4U, 6U, 100U };
	uint8_t set_unavailable[] = { 4U, 1U, 0U };
	uint8_t set_available[] = { 4U, 1U, 1U };

	zassert_ok(ef800_virtual_profile_apply(set_zero, sizeof(set_zero)), NULL);
	zassert_true(i2c_is_ready_dt(&gauge_spec), NULL);
	zassert_ok(ef800_max17043_init(&gauge, &gauge_spec), NULL);
	zassert_ok(ef800_max17043_read(&gauge, &status), NULL);
	zassert_equal(status.soc_percent, 0U, "zero SOC must remain a valid sample");
	zassert_equal(status.voltage_uv, 3800000U, NULL);

	zassert_ok(ef800_virtual_profile_apply(set_full, sizeof(set_full)), NULL);
	zassert_ok(ef800_max17043_read(&gauge, &status), NULL);
	zassert_equal(status.soc_percent, 100U, NULL);
	zassert_equal(status.voltage_uv, 3800000U, NULL);

	zassert_ok(ef800_virtual_profile_apply(set_unavailable,
		sizeof(set_unavailable)), NULL);
	zassert_equal(ef800_max17043_read(&gauge, &status), -ENODEV, NULL);
	zassert_equal(ef800_max17043_init(&gauge, &gauge_spec), -ENODEV, NULL);
	zassert_ok(ef800_virtual_profile_apply(set_available,
		sizeof(set_available)), NULL);
}

ZTEST(battery_soc_store, test_table_persists_and_reloads)
{
	struct ef800_persistence_fake fake;
	struct ef800_persistence persistence;
	struct ef800_battery_soc_store store;
	struct ef800_battery_soc_store restarted;
	struct ef800_battery_soc_table table = sample_table();
	struct ef800_battery_soc_table loaded;
	char hex[EF800_BATTERY_SOC_TABLE_MAX_HEX_LENGTH + 1U];
	size_t hex_length;

	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&persistence, &fake.backend), NULL);
	zassert_ok(ef800_battery_soc_store_init(&store, &persistence), NULL);
	zassert_false(ef800_battery_soc_store_has_table(&store), NULL);
	zassert_ok(ef800_battery_soc_table_encode_hex(&table, hex, sizeof(hex),
		&hex_length), NULL);
	zassert_ok(ef800_battery_soc_store_set_hex(&store, hex, hex_length), NULL);
	zassert_true(ef800_battery_soc_store_has_table(&store), NULL);
	zassert_ok(ef800_battery_soc_store_init(&restarted, &persistence), NULL);
	zassert_ok(ef800_battery_soc_store_get_table(&restarted, &loaded), NULL);
	zassert_equal(loaded.point_count, table.point_count, NULL);
	zassert_equal(loaded.points[1].pin_mv, table.points[1].pin_mv, NULL);
	zassert_ok(ef800_battery_soc_store_get_hex(&restarted, hex, sizeof(hex),
		&hex_length), NULL);
	zassert_true(hex_length > 0U, NULL);
}

ZTEST(battery_soc_store, test_invalid_and_corrupt_updates_keep_previous_table)
{
	struct ef800_persistence_fake fake;
	struct ef800_persistence persistence;
	struct ef800_battery_soc_store store;
	struct ef800_battery_soc_store restarted;
	struct ef800_battery_soc_table first = sample_table();
	struct ef800_battery_soc_table second = sample_table();
	struct ef800_battery_soc_table loaded;
	char first_hex[EF800_BATTERY_SOC_TABLE_MAX_HEX_LENGTH + 1U];
	char second_hex[EF800_BATTERY_SOC_TABLE_MAX_HEX_LENGTH + 1U];
	size_t first_length;
	size_t second_length;

	second.points[0].pin_mv = 900U;
	second.points[1].pin_mv = 1900U;
	second.points[2].pin_mv = 2900U;
	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&persistence, &fake.backend), NULL);
	zassert_ok(ef800_battery_soc_store_init(&store, &persistence), NULL);
	zassert_ok(ef800_battery_soc_table_encode_hex(&first, first_hex,
		sizeof(first_hex), &first_length), NULL);
	zassert_ok(ef800_battery_soc_table_encode_hex(&second, second_hex,
		sizeof(second_hex), &second_length), NULL);
	zassert_ok(ef800_battery_soc_store_set_hex(&store, first_hex, first_length), NULL);
	zassert_equal(ef800_battery_soc_store_set_hex(&store, "not-hex", 7U),
		-EINVAL, NULL);
	zassert_ok(ef800_battery_soc_store_get_table(&store, &loaded), NULL);
	zassert_equal(loaded.points[0].pin_mv, first.points[0].pin_mv, NULL);

	zassert_ok(ef800_battery_soc_store_set_hex(&store, second_hex, second_length), NULL);
	strcpy(fake.corrupt_on_sync_name, EF800_BATTERY_SOC_STORE_SLOT_A);
	fake.corrupt_on_sync_enabled = true;
	zassert_true(ef800_battery_soc_store_set_hex(&store, first_hex, first_length) < 0,
		NULL);
	zassert_ok(ef800_battery_soc_store_init(&restarted, &persistence), NULL);
	zassert_ok(ef800_battery_soc_store_get_table(&restarted, &loaded), NULL);
	zassert_equal(loaded.points[0].pin_mv, second.points[0].pin_mv, NULL);
}

struct battery_reader_context {
	uint8_t soc_percent;
	uint8_t failures_remaining;
};

static int fake_battery_reader(void *context,
	struct ef800_max17043_status *status)
{
	struct battery_reader_context *reader = context;

	if (reader->failures_remaining > 0U) {
		--reader->failures_remaining;
		return -EIO;
	}
	status->soc_percent = reader->soc_percent;
	status->voltage_uv = 2800000U;
	return 0;
}

ZTEST(battery_runtime, test_soc_stays_visible_until_third_failure)
{
	struct battery_reader_context reader = { .soc_percent = 73U };
	struct ef800_runtime_state state;
	struct ef800_runtime_snapshot snapshot;

	ef800_runtime_state_init(&state, fake_battery_reader, &reader);
	ef800_runtime_state_refresh_battery(&state);
	zassert_ok(ef800_runtime_state_snapshot(&state, &snapshot), NULL);
	zassert_true(snapshot.battery_has_value, NULL);
	zassert_equal(snapshot.battery_soc_percent, 73U, NULL);

	reader.failures_remaining = 2U;
	ef800_runtime_state_refresh_battery(&state);
	zassert_ok(ef800_runtime_state_snapshot(&state, &snapshot), NULL);
	zassert_true(snapshot.battery_has_value, NULL);
	zassert_equal(snapshot.battery_soc_percent, 73U, NULL);
	ef800_runtime_state_refresh_battery(&state);
	zassert_ok(ef800_runtime_state_snapshot(&state, &snapshot), NULL);
	zassert_true(snapshot.battery_has_value, NULL);
	zassert_equal(snapshot.battery_soc_percent, 73U, NULL);
	reader.failures_remaining = 1U;
	ef800_runtime_state_refresh_battery(&state);
	zassert_ok(ef800_runtime_state_snapshot(&state, &snapshot), NULL);
	zassert_false(snapshot.battery_has_value, NULL);
	zassert_equal(snapshot.battery_status, EF800_RUNTIME_SAMPLE_UNAVAILABLE, NULL);

	reader.soc_percent = 64U;
	ef800_runtime_state_refresh_battery(&state);
	zassert_ok(ef800_runtime_state_snapshot(&state, &snapshot), NULL);
	zassert_true(snapshot.battery_has_value, NULL);
	zassert_equal(snapshot.battery_soc_percent, 64U, NULL);
}

ZTEST(battery_runtime, test_zero_soc_is_not_source_unavailable)
{
	struct battery_reader_context reader = { .soc_percent = 0U };
	struct ef800_runtime_state state;
	struct ef800_runtime_snapshot snapshot;

	ef800_runtime_state_init(&state, fake_battery_reader, &reader);
	ef800_runtime_state_refresh_battery(&state);
	zassert_ok(ef800_runtime_state_snapshot(&state, &snapshot), NULL);
	zassert_true(snapshot.battery_has_value, NULL);
	zassert_equal(snapshot.battery_soc_percent, 0U, NULL);

	ef800_runtime_state_init(&state, NULL, NULL);
	ef800_runtime_state_refresh_battery(&state);
	zassert_ok(ef800_runtime_state_snapshot(&state, &snapshot), NULL);
	zassert_false(snapshot.battery_has_value, NULL);
	zassert_equal(snapshot.battery_status, EF800_RUNTIME_SAMPLE_UNAVAILABLE, NULL);
}

struct fake_gauge {
	bool present;
	int read_result;
	uint8_t probe_calls;
	uint8_t read_calls;
};

static int fake_gauge_probe(void *context)
{
	struct fake_gauge *fake = context;

	++fake->probe_calls;
	return fake->present ? 0 : -ENODEV;
}

static int fake_gauge_read(void *context,
	struct ef800_max17043_status *status)
{
	struct fake_gauge *fake = context;

	++fake->read_calls;
	if (fake->read_result != 0) {
		return fake->read_result;
	}
	status->soc_percent = 62U;
	status->voltage_uv = 3700000U;
	return 0;
}

ZTEST(battery_gauge, test_absent_gauge_probe_is_one_shot)
{
	struct fake_gauge fake = { .present = false };
	struct ef800_battery_soc_gauge gauge;
	struct ef800_max17043_status status;

	zassert_ok(ef800_battery_soc_gauge_init(&gauge, fake_gauge_probe,
		fake_gauge_read, &fake), NULL);
	zassert_equal(ef800_battery_soc_gauge_read(&gauge, &status), -ENODEV, NULL);
	zassert_equal(ef800_battery_soc_gauge_read(&gauge, &status), -ENODEV, NULL);
	zassert_equal(fake.probe_calls, 1U, NULL);
	zassert_equal(fake.read_calls, 0U, NULL);
}

ZTEST(battery_gauge, test_responding_gauge_is_probed_once_and_sampled)
{
	struct fake_gauge fake = { .present = true };
	struct ef800_battery_soc_gauge gauge;
	struct ef800_max17043_status status;

	zassert_ok(ef800_battery_soc_gauge_init(&gauge, fake_gauge_probe,
		fake_gauge_read, &fake), NULL);
	zassert_ok(ef800_battery_soc_gauge_read(&gauge, &status), NULL);
	zassert_equal(status.soc_percent, 62U, NULL);
	fake.read_result = -EIO;
	zassert_equal(ef800_battery_soc_gauge_read(&gauge, &status), -EIO, NULL);
	zassert_equal(fake.probe_calls, 1U, NULL);
	zassert_equal(fake.read_calls, 2U, NULL);
}

ZTEST_SUITE(battery_soc_table, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(battery_soc_adc, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(battery_soc_gauge_emul, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(battery_soc_store, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(battery_runtime, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(battery_gauge, NULL, NULL, NULL, NULL, NULL);
