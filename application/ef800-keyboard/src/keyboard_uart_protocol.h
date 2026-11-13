/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EF800_KEYBOARD_UART_PROTOCOL_H
#define EF800_KEYBOARD_UART_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct keyboard_uart_protocol_ops {
	void (*transmit)(void *context, const uint8_t *data, size_t length);
	int (*configure_baudrate)(void *context, uint32_t baudrate);
	void (*delay_ms)(void *context, uint32_t delay_ms);
	uint64_t (*uptime_ms)(void *context);
	void (*reset_scan_counts)(void *context);
	void (*reset_device)(void *context);
	void (*enter_updater)(void *context);
	void *context;
};

int ef800_keyboard_uart_protocol_init(
	const struct keyboard_uart_protocol_ops *ops);
void ef800_keyboard_uart_protocol_process(uint8_t byte, uint64_t now_ms);
void ef800_keyboard_uart_protocol_expire(uint64_t now_ms);
void ef800_keyboard_uart_protocol_key_change(uint8_t index, bool pressed,
					     uint64_t now_ms);
void ef800_keyboard_uart_protocol_repeat(uint64_t now_ms);
bool ef800_keyboard_uart_protocol_active(void);

#endif /* EF800_KEYBOARD_UART_PROTOCOL_H */
