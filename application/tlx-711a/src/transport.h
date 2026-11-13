/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Define the command transport between TLX host and LCD emulator. */
#ifndef TLX711A_TRANSPORT_H
#define TLX711A_TRANSPORT_H

#include <stdint.h>

/** @brief Initialize the host command transport.
 * @return 0 on setup; negative errno if the transport cannot start.
 */
int transport_init(void);

/** @brief Send one encoded response frame to the host.
 * @param frame Frame bytes.
 * @param length Frame length.
 * @return None.
 */
void transport_send_frame(const uint8_t *frame, uint16_t length);

/** @brief Send a textual information message using the protocol framing.
 * @param message NUL-terminated text.
 * @return None.
 */
void transport_send_info(const char *message);

#endif
