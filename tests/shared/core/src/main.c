/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>

#include <errno.h>

#include "ef800_boot_decision.h"
#include "ef800_storage_owner.h"
#include "ef800_update.h"

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

ZTEST(shared_core, test_storage_owner_transitions)
{
	zassert_equal(ef800_storage_owner_action(EF800_STORAGE_OWNER_FAT, false),
			      EF800_STORAGE_ACTION_NONE, NULL);
	zassert_equal(ef800_storage_owner_action(EF800_STORAGE_OWNER_FAT, true),
			      EF800_STORAGE_ACTION_FAT_TO_MSC, NULL);
	zassert_equal(ef800_storage_owner_action(EF800_STORAGE_OWNER_MSC, true),
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

ZTEST_SUITE(shared_core, NULL, NULL, NULL, NULL, NULL);
