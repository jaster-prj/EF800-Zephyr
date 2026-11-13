/* SPDX-License-Identifier: Apache-2.0 */

#include "virtual_ef800_keyboard_controller.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "keyboard_protocol.h"

LOG_MODULE_REGISTER(virtual_ef800_keyboard_controller, LOG_LEVEL_INF);

#define KEYBOARD_UART_NODE DT_CHOSEN(zephyr_keyboard_uart)

static const struct device *const keyboard_uart = DEVICE_DT_GET(KEYBOARD_UART_NODE);

void virtual_ef800_keyboard_controller_reset_released(bool released)
{
	static const uint8_t initialized[] = {CMD_INITIALIZED};

	if (released) {
		(void)virtual_ef800_keyboard_controller_send(initialized, sizeof(initialized));
	}
}

static void tx_data_ready(const struct device *dev, size_t size, void *user_data)
{
	uint8_t buffer[32];
	uint32_t length;

	ARG_UNUSED(user_data);
	length = uart_emul_get_tx_data(dev, buffer, sizeof(buffer));
	if (length != 0U) {
		LOG_DBG("keyboard controller observed %u TX bytes", length);
	}
	ARG_UNUSED(size);
}

int virtual_ef800_keyboard_controller_send(const uint8_t *data, size_t length)
{
	if (data == NULL || length == 0U || !device_is_ready(keyboard_uart)) {
		return -ENODEV;
	}
	return uart_emul_put_rx_data(keyboard_uart, data, length) == length ?
		0 : -ENOSPC;
}

static void keyboard_controller_start(void)
{
	if (!device_is_ready(keyboard_uart)) {
		LOG_WRN("virtual keyboard UART is unavailable");
		return;
	}
	uart_emul_callback_tx_data_ready_set(keyboard_uart, tx_data_ready, NULL);
	/* Keep the response in the emulated RX FIFO until EF800Application arms
	 * its interrupt handler and releases the virtual keyboard reset. */
	k_msleep(20U);
	virtual_ef800_keyboard_controller_reset_released(true);
}

K_THREAD_DEFINE(virtual_ef800_keyboard_controller_thread, 768,
	keyboard_controller_start, NULL, NULL, NULL, 6, 0, 0);
