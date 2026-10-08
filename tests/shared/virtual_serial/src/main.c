/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <errno.h>

#include "ef800_virtual_serial.h"

ZTEST(shared_virtual_serial, test_scripted_exchange_and_truncation)
{
	struct ef800_virtual_serial_peer peer;
	const uint8_t request[] = "S14\r\n";
	const uint8_t response[] = "123.4\r\n";
	uint8_t buffer[16];

	ef800_virtual_serial_peer_init(&peer);
	zassert_ok(ef800_virtual_serial_peer_script(&peer, request, sizeof(request) - 1U,
		response, sizeof(response) - 1U), NULL);
	zassert_ok(ef800_virtual_serial_peer_write(&peer, request, sizeof(request) - 1U), NULL);
	zassert_equal(ef800_virtual_serial_peer_read(&peer, buffer, 3U, 100), 3, NULL);
	zassert_mem_equal(buffer, "123", 3U, NULL);
	zassert_equal(ef800_virtual_serial_peer_read(&peer, buffer, sizeof(buffer), 100),
		4, NULL);
	zassert_equal(ef800_virtual_serial_peer_write(&peer, (const uint8_t *)"bad", 3U),
		-EINVAL, NULL);
}

ZTEST(shared_virtual_serial, test_error_injection)
{
	struct ef800_virtual_serial_peer peer;
	uint8_t value;

	ef800_virtual_serial_peer_init(&peer);
	peer.error = -EIO;
	zassert_equal(ef800_virtual_serial_peer_read(&peer, &value, 1U, 0), -EIO, NULL);
}

ZTEST_SUITE(shared_virtual_serial, NULL, NULL, NULL, NULL, NULL);
