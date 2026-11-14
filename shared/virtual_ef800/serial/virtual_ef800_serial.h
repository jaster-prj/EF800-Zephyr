/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Model a virtual asynchronous serial endpoint and fault behavior. */
#ifndef VIRTUAL_EF800_SERIAL_H
#define VIRTUAL_EF800_SERIAL_H

#include <stddef.h>
#include <stdint.h>

#define VIRTUAL_EF800_SERIAL_MAX_SCRIPT 256U

struct virtual_ef800_serial_peer {
	uint8_t expected[VIRTUAL_EF800_SERIAL_MAX_SCRIPT];
	size_t expected_length;
	uint8_t response[VIRTUAL_EF800_SERIAL_MAX_SCRIPT];
	size_t response_length;
	size_t response_offset;
	int32_t delay_ms;
	int32_t error;
};

/** @brief Clear scripted expectations, responses, and injected faults.
 * @param peer Peer state to initialize.
 * @return None.
 */
void virtual_ef800_serial_peer_init(struct virtual_ef800_serial_peer *peer);

/** @brief Configure expected host bytes and the response to return.
 * @param peer Peer state.
 * @param expected Expected write bytes, or NULL for no expectation.
 * @param expected_length Expected byte count.
 * @param response Response bytes.
 * @param response_length Response byte count.
 * @return 0 on setup; negative errno when either script exceeds capacity.
 */
int virtual_ef800_serial_peer_script(struct virtual_ef800_serial_peer *peer,
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
int virtual_ef800_serial_peer_write(struct virtual_ef800_serial_peer *peer,
					    const uint8_t *data, size_t length);

/** @brief Read scripted response bytes subject to timeout/fault settings.
 * @param peer Peer state.
 * @param data Destination buffer.
 * @param capacity Buffer capacity.
 * @param timeout_ms Maximum wait time.
 * @return Bytes read, or negative errno for timeout/fault/invalid arguments.
 */
int virtual_ef800_serial_peer_read(struct virtual_ef800_serial_peer *peer,
					   uint8_t *data, size_t capacity,
					   int32_t timeout_ms);

#endif
