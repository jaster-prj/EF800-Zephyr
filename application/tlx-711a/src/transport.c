/* SPDX-License-Identifier: Apache-2.0 */
#include "transport.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
static const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

int transport_init(void)
{
	return device_is_ready(uart) ? 0 : -ENODEV;
}

void transport_send_frame(const uint8_t *frame, uint16_t length)
{
	static const char prefix[] = "FB: [";
	static const char suffix[] = "]\r\n";
	for (size_t i = 0U; i < sizeof(prefix) - 1U; ++i)
		uart_poll_out(uart, prefix[i]);
	for (uint16_t i = 0U; i < length; ++i) uart_poll_out(uart, frame[i]);
	for (size_t i = 0U; i < sizeof(suffix) - 1U; ++i)
		uart_poll_out(uart, suffix[i]);
}

void transport_send_info(const char *message)
{
	static const char prefix[] = "INFO: ";
	if (message == NULL) return;
	for (size_t i = 0U; i < sizeof(prefix) - 1U; ++i)
		uart_poll_out(uart, prefix[i]);
	for (size_t i = 0U; i < strlen(message); ++i) uart_poll_out(uart, message[i]);
	uart_poll_out(uart, '\r');
	uart_poll_out(uart, '\n');
}
