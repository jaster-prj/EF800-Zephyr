/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <errno.h>

#include "virtual_ef800.h"

ZTEST(shared_virtual_startup, test_virtual_status_initializes_without_hardware)
{
	struct virtual_ef800_status status;

	virtual_ef800_status_init(&status);
	zassert_true(status.initialized, NULL);
	zassert_false(status.front_panel_connected, NULL);
	zassert_true(status.storage_available, NULL);
	zassert_true(status.rtc_available, NULL);
	zassert_false(status.battery_available, NULL);
	zassert_true(status.serial_peers_available, NULL);
	zassert_not_null(virtual_ef800_capabilities(), NULL);
}

ZTEST(shared_virtual_startup, test_virtual_peripheral_controls_are_deterministic)
{
	struct virtual_ef800_status status;

	virtual_ef800_status_init(&status);
	virtual_ef800_status_set_battery(&status, 73, 3875000U, true);
	zassert_true(status.battery_available, NULL);
	zassert_equal(status.battery_soc_percent, 73, NULL);
	zassert_equal(status.battery_voltage_uv, 3875000U, NULL);
	virtual_ef800_status_set_serial_fault(&status, 25, -EIO, false);
	zassert_false(status.serial_peers_available, NULL);
	zassert_equal(status.serial_delay_ms, 25, NULL);
	zassert_equal(status.serial_error, -EIO, NULL);
}

ZTEST_SUITE(shared_virtual_startup, NULL, NULL, NULL, NULL, NULL);
