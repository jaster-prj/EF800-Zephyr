/* SPDX-License-Identifier: Apache-2.0 */
#include "virtual_ef800_serial.h"

#include <errno.h>
#include <string.h>

void virtual_ef800_serial_peer_init(struct virtual_ef800_serial_peer *peer)
{
	if (peer != NULL) {
		memset(peer, 0, sizeof(*peer));
	}
}

int virtual_ef800_serial_peer_script(struct virtual_ef800_serial_peer *peer,
					     const uint8_t *expected,
					     size_t expected_length,
					     const uint8_t *response,
					     size_t response_length)
{
	if (peer == NULL || expected == NULL || response == NULL ||
		expected_length > sizeof(peer->expected) ||
		response_length > sizeof(peer->response)) {
		return -EINVAL;
	}
	memcpy(peer->expected, expected, expected_length);
	memcpy(peer->response, response, response_length);
	peer->expected_length = expected_length;
	peer->response_length = response_length;
	peer->response_offset = 0U;
	return 0;
}

int virtual_ef800_serial_peer_write(struct virtual_ef800_serial_peer *peer,
					    const uint8_t *data, size_t length)
{
	if (peer == NULL || data == NULL || length != peer->expected_length) {
		return -EINVAL;
	}
	if (peer->error != 0) {
		return peer->error;
	}
	return memcmp(peer->expected, data, length) == 0 ? 0 : -EPROTO;
}

int virtual_ef800_serial_peer_read(struct virtual_ef800_serial_peer *peer,
					   uint8_t *data, size_t capacity,
					   int32_t timeout_ms)
{
	size_t remaining;
	size_t count;

	if (peer == NULL || data == NULL || capacity == 0U || timeout_ms < 0) {
		return -EINVAL;
	}
	if (peer->error != 0) {
		return peer->error;
	}
	remaining = peer->response_length - peer->response_offset;
	count = remaining < capacity ? remaining : capacity;
	memcpy(data, &peer->response[peer->response_offset], count);
	peer->response_offset += count;
	return (int)count;
}
