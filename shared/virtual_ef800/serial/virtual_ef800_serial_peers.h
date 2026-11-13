/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Provide controllable virtual serial instrument peers. */
#ifndef VIRTUAL_EF800_SERIAL_PEERS_H
#define VIRTUAL_EF800_SERIAL_PEERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Configure a virtual instrument response and injected delay/error.
 * @param port Virtual port index.
 * @param response Response bytes to return to the instrument client.
 * @param length Response byte count.
 * @param delay_ms Injected response delay.
 * @param error Injected transport error, or zero for success.
 * @param available Whether the peer should respond.
 * @return 0 on update; negative errno for invalid port or response size.
 */
int virtual_ef800_serial_peer_set(uint8_t port, const uint8_t *response,
	size_t length, int32_t delay_ms, int32_t error, bool available);

/** @brief Select a predefined response profile for one virtual instrument.
 * @param port Virtual port index.
 * @param profile Predefined profile identifier.
 * @return 0 on selection; negative errno for unsupported port/profile.
 */
int virtual_ef800_serial_peer_set_profile(uint8_t port, uint8_t profile);

#endif
