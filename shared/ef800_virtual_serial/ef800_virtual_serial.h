/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Model a virtual asynchronous serial endpoint and fault behavior. */
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

/** @brief Clear scripted expectations, responses, and injected faults.
 * @param peer Peer state to initialize.
 * @return None.
 */
void ef800_virtual_serial_peer_init(struct ef800_virtual_serial_peer *peer);

/** @brief Configure expected host bytes and the response to return.
 * @param peer Peer state.
 * @param expected Expected write bytes, or NULL for no expectation.
 * @param expected_length Expected byte count.
 * @param response Response bytes.
 * @param response_length Response byte count.
 * @return 0 on setup; negative errno when either script exceeds capacity.
 */
int ef800_virtual_serial_peer_script(struct ef800_virtual_serial_peer *peer,
					     const uint8_t *expected,
					     size_t expected_length,
					     const uint8_t *response,
					     size_t response_length);

/** @brief Match a transmitted write against the peer script.
 * @param peer Peer state.
 * @param data Written bytes.
 * @param length Byte count.
 * @return 0 on match; negative errno on mismatch or configured fault.
 */
int ef800_virtual_serial_peer_write(struct ef800_virtual_serial_peer *peer,
					    const uint8_t *data, size_t length);

/** @brief Read scripted response bytes subject to timeout/fault settings.
 * @param peer Peer state.
 * @param data Destination buffer.
 * @param capacity Buffer capacity.
 * @param timeout_ms Maximum wait time.
 * @return Bytes read, or negative errno for timeout/fault/invalid arguments.
 */
int ef800_virtual_serial_peer_read(struct ef800_virtual_serial_peer *peer,
					   uint8_t *data, size_t capacity,
					   int32_t timeout_ms);

#endif
