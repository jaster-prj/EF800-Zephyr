/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_measurement_zephyr.h"

#include <errno.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

static int zephyr_write(void *context, const uint8_t *data, size_t length)
{
	struct ef800_measurement_zephyr_transport *transport = context;

	if (transport == NULL || transport->uart == NULL ||
		(data == NULL && length != 0U)) {
		return -EINVAL;
	}
	for (size_t index = 0U; index < length; ++index) {
		uart_poll_out(transport->uart, data[index]);
	}
	return (int)length;
}

static int zephyr_read(void *context, uint8_t *data, int32_t timeout_ms)
{
	struct ef800_measurement_zephyr_transport *transport = context;
	int64_t deadline;

	if (transport == NULL || transport->uart == NULL || data == NULL ||
		timeout_ms < 0) {
		return -EINVAL;
	}
	deadline = k_uptime_get() + timeout_ms;
	while (true) {
		int result = uart_poll_in(transport->uart, data);

		if (result == 0) {
			return 1;
		}
		if (result != -1) {
			return result;
		}
		if (k_uptime_get() >= deadline) {
			return 0;
		}
		k_msleep(1);
	}
}

int ef800_measurement_zephyr_init(
	struct ef800_measurement_zephyr_transport *transport,
	const struct device *uart, uint8_t builder_id, uint32_t connection)
{
	struct ef800_measurement_serial_config measurement_config;
	struct uart_config uart_config;
	int result;

	if (transport == NULL || uart == NULL || !device_is_ready(uart)) {
		return -ENODEV;
	}
	result = ef800_measurement_serial_config_for_builder(builder_id, connection,
										 &measurement_config);
	if (result != 0) {
		return result;
	}
	uart_config = (struct uart_config){
		.baudrate = measurement_config.baudrate,
		.parity = measurement_config.parity == EF800_MEASUREMENT_PARITY_ODD ?
			UART_CFG_PARITY_ODD : UART_CFG_PARITY_NONE,
		.stop_bits = measurement_config.stop_bits == 2U ?
			UART_CFG_STOP_BITS_2 : UART_CFG_STOP_BITS_1,
		.data_bits = measurement_config.data_bits == 7U ?
			UART_CFG_DATA_BITS_7 : UART_CFG_DATA_BITS_8,
		.flow_ctrl = measurement_config.flow_control ?
			UART_CFG_FLOW_CTRL_RTS_CTS : UART_CFG_FLOW_CTRL_NONE,
	};
	result = uart_configure(uart, &uart_config);
	if (result != 0) {
		return result;
	}
	transport->uart = uart;
	transport->transport = (struct ef800_measurement_transport){
		.write = zephyr_write,
		.read = zephyr_read,
		.context = transport,
	};
	return 0;
}
