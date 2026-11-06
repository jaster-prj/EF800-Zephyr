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

static void zephyr_uart_callback(const struct device *dev,
	void *user_data)
{
	struct ef800_measurement_zephyr_transport *transport = user_data;
	int sent;

	if (transport == NULL || transport->tx_data == NULL ||
		uart_irq_update(dev) <= 0 || !uart_irq_tx_ready(dev)) {
		return;
	}
	sent = uart_fifo_fill(dev, transport->tx_data + transport->tx_offset,
		transport->tx_length - transport->tx_offset);
	if (sent < 0) {
		transport->tx_status = sent;
		transport->tx_faulted = true;
		uart_irq_tx_disable(dev);
		transport->tx_data = NULL;
		k_sem_give(&transport->tx_complete);
		return;
	}
	transport->tx_offset += (size_t)sent;
	if (transport->tx_offset == transport->tx_length) {
		transport->tx_status = 0;
		uart_irq_tx_disable(dev);
		transport->tx_data = NULL;
		k_sem_give(&transport->tx_complete);
	}
}

static int zephyr_write_until(void *context, const uint8_t *data,
	size_t length, uint64_t deadline_ms)
{
	struct ef800_measurement_zephyr_transport *transport = context;
	uint64_t now_ms;
	int ret;

	if (transport == NULL || transport->uart == NULL || data == NULL ||
		length == 0U || transport->tx_faulted) {
		return transport != NULL && transport->tx_faulted ? -EIO : -EINVAL;
	}
	if ((uint64_t)k_uptime_get() >= deadline_ms) {
		return -ETIMEDOUT;
	}
	if (transport->tx_data != NULL) {
		return -EBUSY;
	}
	ret = uart_irq_callback_user_data_set(transport->uart,
		zephyr_uart_callback, transport);
	if (ret != 0) {
		return ret;
	}
	k_sem_reset(&transport->tx_complete);
	transport->tx_data = data;
	transport->tx_length = length;
	transport->tx_offset = 0U;
	transport->tx_status = -EINPROGRESS;
	uart_irq_tx_enable(transport->uart);

	now_ms = (uint64_t)k_uptime_get();
	if (now_ms < deadline_ms) {
		ret = k_sem_take(&transport->tx_complete,
			K_MSEC((int32_t)(deadline_ms - now_ms)));
		if (ret == 0) {
			return transport->tx_status == 0 ? (int)length :
				transport->tx_status;
		}
	}

	if (k_sem_take(&transport->tx_complete, K_NO_WAIT) == 0) {
		return transport->tx_status == 0 ? -ETIMEDOUT :
			transport->tx_status;
	}
	/* Stop queued TX interrupts and reconfigure the peripheral to discard
	 * partially queued bytes before the host staging buffer can be reused. */
	uart_irq_tx_disable(transport->uart);
	transport->tx_data = NULL;
	ret = uart_configure(transport->uart, &transport->uart_config);
	if (ret != 0) {
		transport->tx_faulted = true;
		transport->tx_status = -EIO;
		return -EIO;
	}
	/* A partial command may have reached the instrument. Keep this binding
	 * unavailable until host recovery reinitializes the route. */
	transport->tx_faulted = true;
	transport->tx_status = -ETIMEDOUT;
	return -ETIMEDOUT;
}

static int zephyr_read_until(void *context, uint8_t *data,
	uint64_t deadline_ms)
{
	struct ef800_measurement_zephyr_transport *transport = context;

	if (transport == NULL || transport->uart == NULL || data == NULL) {
		return -EINVAL;
	}
	if (transport->tx_faulted) {
		return -EIO;
	}
	while (true) {
		int result = uart_poll_in(transport->uart, data);

		if (result == 0) {
			return 1;
		}
		if (result != -1) {
			return result;
		}
		if ((uint64_t)k_uptime_get() >= deadline_ms) {
			return 0;
		}
		k_msleep(1);
	}
}

static int zephyr_read(void *context, uint8_t *data, int32_t timeout_ms)
{
	if (timeout_ms < 0) {
		return -EINVAL;
	}
	return zephyr_read_until(context, data,
		(uint64_t)k_uptime_get() + (uint32_t)timeout_ms);
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
	k_sem_init(&transport->tx_complete, 0, 1);
	transport->tx_faulted = false;
	transport->uart_config = uart_config;
	transport->transport = (struct ef800_measurement_transport){
		.write = zephyr_write,
		.read = zephyr_read,
		.context = transport,
		.write_until = zephyr_write_until,
		.read_until = zephyr_read_until,
	};
	return 0;
}
