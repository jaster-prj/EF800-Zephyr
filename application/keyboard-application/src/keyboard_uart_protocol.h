/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Parse keyboard UART commands and emit key, version, and updater responses.
 */

#ifndef EF800_KEYBOARD_UART_PROTOCOL_H
#define EF800_KEYBOARD_UART_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct keyboard_uart_protocol_ops {
	/* transmit sends bytes; callbacks use context as their opaque adapter. */
	void (*transmit)(void *context, const uint8_t *data, size_t length);
	int (*configure_baudrate)(void *context, uint32_t baudrate);
	void (*delay_ms)(void *context, uint32_t delay_ms);
	uint64_t (*uptime_ms)(void *context);
	void (*reset_scan_counts)(void *context);
	void (*reset_device)(void *context);
	void (*enter_updater)(void *context);
	void *context;
};

/** @brief Bind hardware and timing callbacks for protocol processing.
 * @param ops Complete callback table and opaque callback context.
 * @return 0 on initialization; negative errno if required callbacks are absent.
 */
int ef800_keyboard_uart_protocol_init(
	const struct keyboard_uart_protocol_ops *ops);

/** @brief Feed one received byte into the command parser.
 * @param byte Received UART byte.
 * @param now_ms Current monotonic time in milliseconds.
 * @return None; complete valid frames are dispatched through the adapter.
 */
void ef800_keyboard_uart_protocol_process(uint8_t byte, uint64_t now_ms);

/** @brief Expire a partially received command whose inter-byte deadline elapsed.
 * @param now_ms Current monotonic time in milliseconds.
 * @return None.
 */
void ef800_keyboard_uart_protocol_expire(uint64_t now_ms);

/** @brief Queue a physical key press/release event for transmission.
 * @param index Keyboard matrix key index.
 * @param pressed True for press, false for release.
 * @param now_ms Current monotonic time in milliseconds.
 * @return None.
 */
void ef800_keyboard_uart_protocol_key_change(uint8_t index, bool pressed,
						     uint64_t now_ms);

/** @brief Send due repeat events for currently held keys.
 * @param now_ms Current monotonic time in milliseconds.
 * @return None.
 */
void ef800_keyboard_uart_protocol_repeat(uint64_t now_ms);

/** @brief Query whether UART protocol processing is initialized and enabled.
 * @return True when the protocol is active.
 */
bool ef800_keyboard_uart_protocol_active(void);

#endif /* EF800_KEYBOARD_UART_PROTOCOL_H */
