/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Configure the EF800 console route and output destination. */
#ifndef EF800_CONSOLE_H
#define EF800_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_measurement.h"

/* Install the output hook while the application port assignments are loaded. */
int ef800_console_prepare(void);

/** @brief Clear the selected console route and queued target state.
 * @return None.
 */
void ef800_console_clear_target(void);

/* Select and configure a console after the application assignments are known. */

/** @brief Initialize console output after port assignments are loaded.
 * @param usb_debug_available True when USB CDC debug output is available.
 * @return 0 on setup; negative errno if no usable transport can be initialized.
 */
int ef800_console_init(bool usb_debug_available);

/** @brief Select a port as the console output/input route.
 * @param port_id Target physical port.
 * @return 0 on selection; negative errno if route is unavailable or busy.
 */
int ef800_console_set_target(uint8_t port_id);

/** @brief Select a console route and apply its serial configuration.
 * @param port_id Target physical port.
 * @param config UART configuration for that route.
 * @return 0 on selection; negative errno if route/configuration fails.
 */
int ef800_console_set_target_with_config(
	uint8_t port_id, const struct ef800_measurement_serial_config *config);

/** @brief Get the currently selected console port.
 * @return Port ID, or the no-target sentinel when unset.
 */
uint8_t ef800_console_get_target(void);

/** @brief Transmit console bytes or queue them until a route is ready.
 * @param data Bytes to send.
 * @param length Byte count.
 * @return 0 when accepted; negative errno if output cannot be queued/sent.
 */
int ef800_console_write(const uint8_t *data, size_t length);

/** @brief Read one byte from the selected console route.
 * @param data Receives one byte.
 * @return 0 on read; negative errno when no byte is available or route fails.
 */
int ef800_console_read(uint8_t *data);

/** @brief Start the USB CDC console transport.
 * @return 0 on start; negative errno on device/USB failure.
 */
int ef800_console_usb_start(void);

/** @brief Stop the USB CDC console transport.
 * @return 0 on stop; negative errno on shutdown failure.
 */
int ef800_console_usb_stop(void);

/** @brief Set whether the selected console transport can currently transmit.
 * @param ready True when transport is configured and ready.
 * @return None.
 */
void ef800_console_set_transport_ready(bool ready);

/* Queue output until a usable DebugConsole transport is selected. */

/** @brief Get bytes currently waiting in the console output queue.
 * @return Number of queued bytes.
 */
size_t ef800_console_pending(void);

/** @brief Get cumulative bytes dropped because the output queue was full.
 * @return Dropped-byte count.
 */
uint32_t ef800_console_dropped(void);

#endif /* EF800_CONSOLE_H */
