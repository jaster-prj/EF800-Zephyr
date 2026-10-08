/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_VIRTUAL_SERIAL_H
#define EF800_VIRTUAL_SERIAL_H

#include <stddef.h>
#include <stdint.h>

#define EF800_VIRTUAL_SERIAL_MAX_SCRIPT 256U

struct ef800_virtual_serial_peer {
	uint8_t expected[EF800_VIRTUAL_SERIAL_MAX_SCRIPT];
	size_t expected_length;
	uint8_t response[EF800_VIRTUAL_SERIAL_MAX_SCRIPT];
	size_t response_length;
	size_t response_offset;
	int32_t delay_ms;
	int32_t error;
};

void ef800_virtual_serial_peer_init(struct ef800_virtual_serial_peer *peer);
int ef800_virtual_serial_peer_script(struct ef800_virtual_serial_peer *peer,
					     const uint8_t *expected,
					     size_t expected_length,
					     const uint8_t *response,
					     size_t response_length);
int ef800_virtual_serial_peer_write(struct ef800_virtual_serial_peer *peer,
					    const uint8_t *data, size_t length);
int ef800_virtual_serial_peer_read(struct ef800_virtual_serial_peer *peer,
					   uint8_t *data, size_t capacity,
					   int32_t timeout_ms);

#endif
