/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>

#include <errno.h>

#include "ef800_boot_decision.h"
#include "ef800_storage_owner.h"
#include "ef800_update.h"
#include "ef800_keyboard_boot.h"
#include "ef800_keyboard_package.h"

ZTEST(shared_core, test_boot_decision_priority)
{
	struct ef800_boot_decision_input input = {
		.main_valid = true,
		.backup_valid = true,
		.failure_limit_reached = false,
	};

	zassert_equal(ef800_boot_decide(&input), EF800_BOOT_DECISION_MAIN, NULL);
	input.failure_limit_reached = true;
	zassert_equal(ef800_boot_decide(&input), EF800_BOOT_DECISION_BACKUP, NULL);
	input.main_valid = false;
	zassert_equal(ef800_boot_decide(&input), EF800_BOOT_DECISION_BACKUP, NULL);
	input.backup_valid = false;
	zassert_equal(ef800_boot_decide(&input), EF800_BOOT_DECISION_NONE, NULL);
	zassert_equal(ef800_boot_decide(NULL), EF800_BOOT_DECISION_NONE, NULL);
}

ZTEST(shared_core, test_main_msd_ownership_requires_composition_request)
{
	bool stale_msd_assignment = true;
	bool vbus_present = true;
	bool usbmsd0_effective = true;
	bool requested = stale_msd_assignment && vbus_present && !usbmsd0_effective;

	zassert_equal(ef800_storage_owner_action(EF800_STORAGE_OWNER_FAT, requested),
		      EF800_STORAGE_ACTION_NONE, NULL);
	usbmsd0_effective = false;
	requested = stale_msd_assignment && vbus_present && !usbmsd0_effective;
	zassert_equal(ef800_storage_owner_action(EF800_STORAGE_OWNER_FAT, requested),
			      EF800_STORAGE_ACTION_FAT_TO_MSC, NULL);
	zassert_equal(ef800_storage_owner_action(EF800_STORAGE_OWNER_MSC, requested),
			      EF800_STORAGE_ACTION_NONE, NULL);
	zassert_equal(ef800_storage_owner_action(EF800_STORAGE_OWNER_MSC, false),
			      EF800_STORAGE_ACTION_MSC_TO_FAT, NULL);
}

ZTEST(shared_core, test_metadata_round_trip_and_corruption)
{
	struct ef800_boot_metadata metadata;
	struct ef800_boot_metadata decoded;
	uint8_t buffer[EF800_BOOT_METADATA_SIZE];

	ef800_boot_metadata_init(&metadata);
	zassert_true(ef800_boot_metadata_valid(&metadata), NULL);
	zassert_ok(ef800_boot_metadata_encode(&metadata, buffer, sizeof(buffer)), NULL);
	zassert_ok(ef800_boot_metadata_decode(buffer, sizeof(buffer), &decoded), NULL);
	zassert_mem_equal(&metadata, &decoded, sizeof(metadata), NULL);

	buffer[0] ^= 0x01U;
	zassert_ok(ef800_boot_metadata_decode(buffer, sizeof(buffer), &decoded), NULL);
	zassert_false(ef800_boot_metadata_valid(&decoded), NULL);
}

ZTEST(shared_core, test_metadata_invalid_arguments)
{
	struct ef800_boot_metadata metadata;
	uint8_t buffer[EF800_BOOT_METADATA_SIZE];

	ef800_boot_metadata_init(&metadata);
	zassert_equal(ef800_boot_metadata_encode(NULL, buffer, sizeof(buffer)),
			      -EINVAL, NULL);
	zassert_equal(ef800_boot_metadata_encode(&metadata, NULL, sizeof(buffer)),
			      -EINVAL, NULL);
	zassert_equal(ef800_boot_metadata_decode(buffer, sizeof(buffer) - 1U,
				      &metadata), -EINVAL, NULL);
}

ZTEST(shared_core, test_keyboard_watchdog_attempt_limit)
{
	struct ef800_keyboard_boot_state state;

	ef800_keyboard_boot_state_initialize(&state);
	zassert_false(ef800_keyboard_boot_prepare(&state, false, 0U), NULL);
	zassert_equal(state.watchdog_starts, 0U, NULL);
	zassert_false(ef800_keyboard_boot_prepare(&state, true, 1U), NULL);
	zassert_equal(state.watchdog_starts, 1U, NULL);
	zassert_false(ef800_keyboard_boot_prepare(&state, true, 1U), NULL);
	zassert_equal(state.watchdog_starts, 2U, NULL);
	zassert_true(ef800_keyboard_boot_prepare(&state, true, 1U), NULL);
	zassert_equal(state.watchdog_starts,
		EF800_KEYBOARD_BOOT_MAX_WATCHDOG_STARTS, NULL);
}

ZTEST(shared_core, test_keyboard_watchdog_health_clears_attempts)
{
	struct ef800_keyboard_boot_state state;

	ef800_keyboard_boot_state_initialize(&state);
	(void)ef800_keyboard_boot_prepare(&state, false, 0U);
	(void)ef800_keyboard_boot_prepare(&state, true, 1U);
	ef800_keyboard_boot_confirm(&state);
	zassert_true(ef800_keyboard_boot_state_valid(&state), NULL);
	zassert_equal(state.phase, EF800_KEYBOARD_BOOT_HEALTHY, NULL);
	zassert_equal(state.watchdog_starts, 0U, NULL);
}

ZTEST(shared_core, test_keyboard_system_request)
{
	struct ef800_keyboard_boot_state state;

	ef800_keyboard_boot_state_initialize(&state);
	ef800_keyboard_boot_request_system(&state);
	zassert_true(ef800_keyboard_boot_consume_system_request(&state), NULL);
	zassert_false(ef800_keyboard_boot_consume_system_request(&state), NULL);
	zassert_true(ef800_keyboard_boot_state_valid(&state), NULL);
}

ZTEST(shared_core, test_keyboard_address_only_update_probe)
{
	zassert_true(ef800_keyboard_boot_i2c_probe(true, false), NULL);
	zassert_false(ef800_keyboard_boot_i2c_probe(false, false), NULL);
	zassert_false(ef800_keyboard_boot_i2c_probe(true, true), NULL);
	zassert_false(ef800_keyboard_boot_updater_window_expired(10U,
		10U + EF800_KEYBOARD_BOOT_UPDATER_WINDOW_MS - 1U), NULL);
	zassert_true(ef800_keyboard_boot_updater_window_expired(10U,
		10U + EF800_KEYBOARD_BOOT_UPDATER_WINDOW_MS), NULL);
	zassert_true(ef800_keyboard_boot_updater_window_expired(UINT32_MAX - 10U,
		EF800_KEYBOARD_BOOT_UPDATER_WINDOW_MS - 11U), NULL);
	zassert_false(ef800_keyboard_boot_should_enter_updater(false, false, true), NULL);
	zassert_true(ef800_keyboard_boot_should_enter_updater(true, false, true), NULL);
	zassert_true(ef800_keyboard_boot_should_enter_updater(false, true, true), NULL);
	zassert_true(ef800_keyboard_boot_should_enter_updater(false, false, false), NULL);
}

ZTEST(shared_core, test_keyboard_package_validation)
{
	struct ef800_keyboard_package_header header = {
		.magic = EF800_KEYBOARD_PACKAGE_MAGIC,
		.format = EF800_KEYBOARD_PACKAGE_FORMAT,
		.header_size = EF800_KEYBOARD_PACKAGE_HEADER_SIZE,
		.target_id = EF800_KEYBOARD_PACKAGE_TARGET,
		.image_base = EF800_KEYBOARD_PACKAGE_BASE,
		.image_size = 128U,
		.image_kind = EF800_KEYBOARD_IMAGE_APPLICATION,
	};

	zassert_true(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
	header.target_id = 0x0461U;
	zassert_false(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
	header.target_id = EF800_KEYBOARD_PACKAGE_TARGET;
	header.image_kind = EF800_KEYBOARD_IMAGE_BOOTLOADER;
	header.image_base = EF800_KEYBOARD_BOOT_BASE;
	header.image_size = EF800_KEYBOARD_BOOT_LIMIT;
	zassert_true(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
	header.image_base = EF800_KEYBOARD_PACKAGE_BASE;
	zassert_false(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
	header.image_kind = EF800_KEYBOARD_IMAGE_UPDATER;
	header.image_base = EF800_KEYBOARD_UPDATER_BASE;
	header.image_size = EF800_KEYBOARD_UPDATER_LIMIT;
	zassert_true(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
	header.image_base = EF800_KEYBOARD_PACKAGE_BASE;
	zassert_false(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
	header.image_kind = 0xffU;
	zassert_false(ef800_keyboard_package_valid(&header,
		EF800_KEYBOARD_PACKAGE_HEADER_SIZE + header.image_size), NULL);
}

ZTEST_SUITE(shared_core, NULL, NULL, NULL, NULL, NULL);
