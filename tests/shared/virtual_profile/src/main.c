/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <zephyr/ztest.h>

#include "ef800_virtual_profile.h"

static uint8_t last_peer[128];
static size_t last_peer_length;
static int32_t last_delay;
static int32_t last_error;
static bool last_available;

int ef800_virtual_serial_peer_set(uint8_t port, const uint8_t *response,
	size_t length, int32_t delay_ms, int32_t error, bool available)
{
	ARG_UNUSED(port);
	memcpy(last_peer, response, length);
	last_peer_length = length;
	last_delay = delay_ms;
	last_error = error;
	last_available = available;
	return 0;
}

ZTEST(shared_virtual_profile, test_device_availability_controls)
{
	uint8_t control[] = {3U, 1U, 0U};

	zassert_true(ef800_virtual_profile_available(3U), NULL);
	zassert_ok(ef800_virtual_profile_apply(control, sizeof(control)), NULL);
	zassert_false(ef800_virtual_profile_available(3U), NULL);
	control[2] = 1U;
	zassert_ok(ef800_virtual_profile_apply(control, sizeof(control)), NULL);
	zassert_true(ef800_virtual_profile_available(3U), NULL);
}

ZTEST(shared_virtual_profile, test_serial_profile_is_bounded)
{
	uint8_t control[] = {1U, 4U, 0xaa, 0x55};

	zassert_ok(ef800_virtual_profile_apply(control, sizeof(control)), NULL);
	zassert_equal(last_peer_length, 2U, NULL);
	zassert_equal(last_peer[0], 0xaa, NULL);
	zassert_equal(last_peer[1], 0x55, NULL);
	zassert_true(last_available, NULL);
	control[0] = 9U;
	zassert_equal(ef800_virtual_profile_apply(control, sizeof(control)), -ENODEV, NULL);
}

ZTEST(shared_virtual_profile, test_battery_values_are_bounded)
{
	uint8_t control[] = {4U, 5U, 95U, 0x80, 0x96, 0x3a, 0x00};

	zassert_ok(ef800_virtual_profile_apply(control, sizeof(control)), NULL);
	zassert_equal(ef800_virtual_profile_battery_soc(), 95U, NULL);
	zassert_true(ef800_virtual_profile_battery_voltage_uv() != 3800000U, NULL);
	control[2] = 101U;
	zassert_ok(ef800_virtual_profile_apply(control, sizeof(control)), NULL);
	zassert_equal(ef800_virtual_profile_battery_soc(), 100U, NULL);
	ARG_UNUSED(last_delay);
	ARG_UNUSED(last_error);
}

ZTEST_SUITE(shared_virtual_profile, NULL, NULL, NULL, NULL, NULL);
