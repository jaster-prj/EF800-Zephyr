/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>

#include "virtual_ef800_keyboard.h"

ZTEST(shared_virtual_keyboard, test_semantic_press_release_and_initialized)
{
	struct virtual_ef800_keyboard keyboard;
	uint8_t output[32];
	size_t length;

	virtual_ef800_keyboard_init(&keyboard);
	zassert_ok(virtual_ef800_keyboard_press(&keyboard, "right", 5U), NULL);
	zassert_ok(virtual_ef800_keyboard_release(&keyboard, "right", 5U), NULL);
	length = virtual_ef800_keyboard_read(&keyboard, output, sizeof(output));
	zassert_equal(output[0], CMD_INITIALIZED, NULL);
	zassert_true(length >= 5U, NULL);
	zassert_equal(output[1], KB_SCANCODE_PREFIX_E0, NULL);
	zassert_equal(output[2], 0x4DU, NULL);
	zassert_equal(output[3], KB_SCANCODE_PREFIX_E0, NULL);
	zassert_equal(output[4], KB_SCANCODE_BREAK, NULL);
}

ZTEST(shared_virtual_keyboard, test_duplicate_invalid_and_release_all)
{
	struct virtual_ef800_keyboard keyboard;
	uint8_t output[64];

	virtual_ef800_keyboard_init(&keyboard);
	zassert_ok(virtual_ef800_keyboard_press(&keyboard, "a", 1U), NULL);
	zassert_ok(virtual_ef800_keyboard_press(&keyboard, "a", 1U), NULL);
	zassert_equal(virtual_ef800_keyboard_press(&keyboard, "invalid", 7U),
		-EINVAL, NULL);
	virtual_ef800_keyboard_release_all(&keyboard);
	zassert_equal(keyboard.held_count, 0U, NULL);
	zassert_true(virtual_ef800_keyboard_read(&keyboard, output, sizeof(output)) > 0U,
		NULL);
}

ZTEST_SUITE(shared_virtual_keyboard, NULL, NULL, NULL, NULL, NULL);
