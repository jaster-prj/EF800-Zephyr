/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <errno.h>

#include "ef800_virtual_storage.h"

ZTEST(shared_virtual_storage, test_disposable_storage_is_safe)
{
	struct ef800_virtual_storage storage;

	zassert_ok(ef800_virtual_storage_init(&storage,
		EF800_VIRTUAL_STORAGE_DISPOSABLE, "/tmp/ef800-virtual"), NULL);
	zassert_true(ef800_virtual_storage_is_safe(&storage), NULL);
	zassert_equal(storage.mode, EF800_VIRTUAL_STORAGE_DISPOSABLE, NULL);
	zassert_equal(ef800_virtual_storage_init(&storage,
		EF800_VIRTUAL_STORAGE_PERSISTENT, "/EF800:"), -EINVAL, NULL);
}

ZTEST_SUITE(shared_virtual_storage, NULL, NULL, NULL, NULL, NULL);
