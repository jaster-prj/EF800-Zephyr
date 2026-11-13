/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <errno.h>

#include "virtual_ef800_storage.h"

ZTEST(shared_virtual_storage, test_disposable_storage_is_safe)
{
	struct virtual_ef800_storage storage;

	zassert_ok(virtual_ef800_storage_init(&storage,
		VIRTUAL_EF800_STORAGE_DISPOSABLE, "/tmp/ef800-virtual"), NULL);
	zassert_true(virtual_ef800_storage_is_safe(&storage), NULL);
	zassert_equal(storage.mode, VIRTUAL_EF800_STORAGE_DISPOSABLE, NULL);
	zassert_equal(virtual_ef800_storage_init(&storage,
		VIRTUAL_EF800_STORAGE_PERSISTENT, "/EF800:"), -EINVAL, NULL);
}

ZTEST_SUITE(shared_virtual_storage, NULL, NULL, NULL, NULL, NULL);
