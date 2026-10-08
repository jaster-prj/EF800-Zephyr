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

int ef800_measurement_zephyr_init_with_config(
	struct ef800_measurement_zephyr_transport *transport,
	const struct device *uart,
	const struct ef800_measurement_serial_config *measurement_config)
{
	struct uart_config uart_config;
	int result;
	enum uart_config_parity parity;
	enum uart_config_data_bits data_bits;
	enum uart_config_flow_control flow_control;

	if (transport == NULL || uart == NULL || measurement_config == NULL ||
		measurement_config->baudrate == 0U ||
		!device_is_ready(uart)) {
		return -ENODEV;
	}
	switch (measurement_config->parity) {
	case EF800_MEASUREMENT_PARITY_NONE:
		parity = UART_CFG_PARITY_NONE;
		break;
	case EF800_MEASUREMENT_PARITY_ODD:
		parity = UART_CFG_PARITY_ODD;
		break;
	case EF800_MEASUREMENT_PARITY_EVEN:
		parity = UART_CFG_PARITY_EVEN;
		break;
	default:
		return -EINVAL;
	}
	switch (measurement_config->data_bits) {
	case 5U:
		data_bits = UART_CFG_DATA_BITS_5;
		break;
	case 6U:
		data_bits = UART_CFG_DATA_BITS_6;
		break;
	case 7U:
		data_bits = UART_CFG_DATA_BITS_7;
		break;
	case 8U:
		data_bits = UART_CFG_DATA_BITS_8;
		break;
	default:
		return -EINVAL;
	}
	switch (measurement_config->flow_control) {
	case EF800_MEASUREMENT_FLOW_NONE:
		flow_control = UART_CFG_FLOW_CTRL_NONE;
		break;
	case EF800_MEASUREMENT_FLOW_RTS_CTS:
		flow_control = UART_CFG_FLOW_CTRL_RTS_CTS;
		break;
	case EF800_MEASUREMENT_FLOW_RTS:
	case EF800_MEASUREMENT_FLOW_CTS:
	default:
		/* Zephyr has no portable RTS-only or CTS-only UART mode. */
		return -ENOTSUP;
	}
	if (measurement_config->stop_bits != 1U && measurement_config->stop_bits != 2U) {
		return -EINVAL;
	}
	uart_config = (struct uart_config){
		.baudrate = measurement_config->baudrate,
		.parity = parity,
		.stop_bits = measurement_config->stop_bits == 2U ?
			UART_CFG_STOP_BITS_2 : UART_CFG_STOP_BITS_1,
		.data_bits = data_bits,
		.flow_ctrl = flow_control,
	};
	result = uart_configure(uart, &uart_config);
	#ifdef CONFIG_BOARD_NATIVE_SIM
	/* native_sim UART emulation provides the byte stream but does not
	 * implement physical framing configuration.  Keep the plugin-declared
	 * framing for diagnostics while allowing the emulated transport. */
	if (result == -EINVAL || result == -ENOTSUP) {
		result = 0;
	}
	#endif
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
