/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_VIRTUAL_SERIAL_PEERS_H
#define EF800_VIRTUAL_SERIAL_PEERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int ef800_virtual_serial_peer_set(uint8_t port, const uint8_t *response,
	size_t length, int32_t delay_ms, int32_t error, bool available);
int ef800_virtual_serial_peer_set_profile(uint8_t port, uint8_t profile);

#endif
