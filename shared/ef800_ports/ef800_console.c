/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_console.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk-hooks.h>
#include "ef800_update.h"

#define EF800_CONSOLE_DEFAULT_BAUDRATE 9600U
#define EF800_CONSOLE_BOARD_BAUDRATE 115200U

static const struct device *console_uart;
static uint8_t console_port;

static const struct device *uart_for_port(uint8_t port_id)
{
	switch (port_id) {
	case 0U:
		return DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	case 1U:
		return DEVICE_DT_GET(DT_NODELABEL(usart3));
	case 2U:
		return DEVICE_DT_GET(DT_NODELABEL(usart1));
	case 3U:
		return DEVICE_DT_GET(DT_NODELABEL(uart4));
	#if DT_HAS_COMPAT_STATUS_OKAY(zephyr_cdc_acm_uart)
	case 4U:
		return DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_cdc_acm_uart));
	#endif
	default:
		return NULL;
	}
}

static int configure_uart(uint8_t port_id, const struct device *uart,
				  const struct ef800_measurement_serial_config *serial_config)
{
	struct uart_config config = {
		.baudrate = port_id == 0U ? EF800_CONSOLE_BOARD_BAUDRATE :
			EF800_CONSOLE_DEFAULT_BAUDRATE,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};

	if (uart == NULL || !device_is_ready(uart)) {
		return -ENODEV;
	}
	if (serial_config != NULL) {
		if (serial_config->baudrate == 0U) {
			return -EINVAL;
		}
		config.baudrate = serial_config->baudrate;
		switch (serial_config->parity) {
		case EF800_MEASUREMENT_PARITY_NONE:
			config.parity = UART_CFG_PARITY_NONE;
			break;
		case EF800_MEASUREMENT_PARITY_ODD:
			config.parity = UART_CFG_PARITY_ODD;
			break;
		case EF800_MEASUREMENT_PARITY_EVEN:
			config.parity = UART_CFG_PARITY_EVEN;
			break;
		default:
			return -EINVAL;
		}
		switch (serial_config->data_bits) {
		case 5U: config.data_bits = UART_CFG_DATA_BITS_5; break;
		case 6U: config.data_bits = UART_CFG_DATA_BITS_6; break;
		case 7U: config.data_bits = UART_CFG_DATA_BITS_7; break;
		case 8U: config.data_bits = UART_CFG_DATA_BITS_8; break;
		default: return -EINVAL;
		}
		switch (serial_config->stop_bits) {
		case 1U: config.stop_bits = UART_CFG_STOP_BITS_1; break;
		case 2U: config.stop_bits = UART_CFG_STOP_BITS_2; break;
		default: return -EINVAL;
		}
		switch (serial_config->flow_control) {
		case EF800_MEASUREMENT_FLOW_NONE:
			config.flow_ctrl = UART_CFG_FLOW_CTRL_NONE;
			break;
		case EF800_MEASUREMENT_FLOW_RTS_CTS:
			config.flow_ctrl = UART_CFG_FLOW_CTRL_RTS_CTS;
			break;
		case EF800_MEASUREMENT_FLOW_RTS:
		case EF800_MEASUREMENT_FLOW_CTS:
		default:
			return -ENOTSUP;
		}
	}
	return uart_configure(uart, &config);
}

static int console_putchar(int character)
{
	if (console_uart != NULL && device_is_ready(console_uart)) {
		uart_poll_out(console_uart, (unsigned char)character);
	}
	return character;
}

static int read_metadata(struct ef800_boot_metadata *metadata, bool *valid)
{
	if (metadata == NULL || valid == NULL) {
		return -EINVAL;
	}
	*valid = false;
	/* Internal flash is memory mapped and remains available before QSPI/FAT. */
	if (ef800_boot_metadata_decode((const uint8_t *)0x08020000UL,
							 EF800_BOOT_METADATA_SIZE, metadata) != 0) {
		return -EIO;
	}
	*valid = ef800_boot_metadata_valid(metadata);
	return 0;
}

int ef800_console_set_target(uint8_t port_id)
{
	return ef800_console_set_target_with_config(port_id, NULL);
}

int ef800_console_set_target_with_config(
	uint8_t port_id, const struct ef800_measurement_serial_config *config)
{
	const struct device *uart;
	int ret;

	if (port_id > 4U) {
		return -EINVAL;
	}
	#if !DT_HAS_COMPAT_STATUS_OKAY(zephyr_cdc_acm_uart)
	if (port_id == 4U) {
		return -ENOTSUP;
	}
	#endif
	uart = uart_for_port(port_id);
	ret = configure_uart(port_id, uart, config);
	if (ret != 0) {
		return ret;
	}
	console_uart = uart;
	console_port = port_id;
	return 0;
}

int ef800_console_init(bool usb_debug_available)
{
	struct ef800_boot_metadata metadata;
	bool valid;
	uint8_t port_id;
	int ret;

	ret = read_metadata(&metadata, &valid);
	if (!valid) {
		ef800_boot_metadata_init(&metadata);
	}
	port_id = ef800_console_port_select(&metadata, usb_debug_available);
	ret = ef800_console_set_target(port_id);
	if (ret != 0 && port_id != 0U) {
		ret = ef800_console_set_target(0U);
	}
	__printk_hook_install(console_putchar);
	return ret;
}

uint8_t ef800_console_get_target(void)
{
	return console_port;
}

int ef800_console_write(const uint8_t *data, size_t length)
{
	if (data == NULL && length != 0U) {
		return -EINVAL;
	}
	if (console_uart == NULL || !device_is_ready(console_uart)) {
		return -ENODEV;
	}
	for (size_t index = 0U; index < length; ++index) {
		uart_poll_out(console_uart, data[index]);
	}
	return (int)length;
}
