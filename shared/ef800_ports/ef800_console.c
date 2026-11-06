/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_console.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk-hooks.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/kernel.h>
#include "ef800_update.h"

#define EF800_CONSOLE_DEFAULT_BAUDRATE 9600U
#define EF800_CONSOLE_BOARD_BAUDRATE 115200U
#define EF800_CONSOLE_PENDING_SIZE 512U
#ifndef CONFIG_BOARD_NATIVE_SIM
#define EF800_CONSOLE_RX_SIZE 512U

RING_BUF_DECLARE(console_rx_ring, EF800_CONSOLE_RX_SIZE);
#endif

static const struct device *console_uart;
static uint8_t console_port;
static bool console_ready;
static bool transport_ready;
static struct k_spinlock console_lock;
static uint8_t pending_buffer[EF800_CONSOLE_PENDING_SIZE];
static size_t pending_start;
static size_t pending_length;
static uint32_t pending_dropped;

static int console_putchar(int character);

#ifndef CONFIG_BOARD_NATIVE_SIM
static void console_uart_isr(const struct device *dev, void *user_data)
{
	uint8_t data[32];

	ARG_UNUSED(user_data);
	if (uart_irq_update(dev) <= 0) {
		return;
	}
	while (uart_irq_rx_ready(dev)) {
		int count = uart_fifo_read(dev, data, sizeof(data));

		if (count <= 0) {
			break;
		}
		(void)ring_buf_put(&console_rx_ring, data, (uint32_t)count);
	}
}
#endif

static void pending_put_locked(uint8_t value)
{
	if (pending_length == EF800_CONSOLE_PENDING_SIZE) {
		pending_start = (pending_start + 1U) % EF800_CONSOLE_PENDING_SIZE;
		pending_length--;
		pending_dropped++;
	}
	pending_buffer[(pending_start + pending_length) % EF800_CONSOLE_PENDING_SIZE] = value;
	pending_length++;
}

static void pending_write_locked(const uint8_t *data, size_t length)
{
	for (size_t index = 0U; index < length; ++index) {
		pending_put_locked(data[index]);
	}
}

static size_t pending_flush_locked(void)
{
	size_t flushed = 0U;

	while (pending_length != 0U) {
		uint8_t value = pending_buffer[pending_start];

		if (console_uart == NULL || !device_is_ready(console_uart)) {
			break;
		}
		uart_poll_out(console_uart, value);
		pending_start = (pending_start + 1U) % EF800_CONSOLE_PENDING_SIZE;
		pending_length--;
		flushed++;
	}
	return flushed;
}

static int console_read_locked(uint8_t *data)
{
	if (data == NULL || console_uart == NULL || !device_is_ready(console_uart)) {
		return -ENODEV;
	}
#ifdef CONFIG_BOARD_NATIVE_SIM
	return uart_poll_in(console_uart, data);
#else
	return ring_buf_get(&console_rx_ring, data, 1U) == 1U ? 0 : -EAGAIN;
#endif
}

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
		.baudrate = (port_id == 0U || port_id == 3U) ? EF800_CONSOLE_BOARD_BAUDRATE :
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
	uint8_t value = (uint8_t)character;

	(void)ef800_console_write(&value, sizeof(value));
	return character;
}

int ef800_console_prepare(void)
{
	k_spinlock_key_t key;

	#ifndef CONFIG_BOARD_NATIVE_SIM
	if (console_uart != NULL) {
		uart_irq_rx_disable(console_uart);
	}
	ring_buf_reset(&console_rx_ring);
	#endif
	__printk_hook_install(console_putchar);
	key = k_spin_lock(&console_lock);
	console_uart = NULL;
	console_port = EF800_CONSOLE_NO_TARGET;
	console_ready = false;
	transport_ready = false;
	k_spin_unlock(&console_lock, key);
	return 0;
}

void ef800_console_clear_target(void)
{
	#ifndef CONFIG_BOARD_NATIVE_SIM
	if (console_uart != NULL) {
		uart_irq_rx_disable(console_uart);
	}
	ring_buf_reset(&console_rx_ring);
	#endif
	k_spinlock_key_t key = k_spin_lock(&console_lock);

	console_uart = NULL;
	console_port = EF800_CONSOLE_NO_TARGET;
	console_ready = false;
	transport_ready = false;
	k_spin_unlock(&console_lock, key);
}

int ef800_console_set_target(uint8_t port_id)
{
	return ef800_console_set_target_with_config(port_id, NULL);
}

int ef800_console_set_target_with_config(
	uint8_t port_id, const struct ef800_measurement_serial_config *config)
{
	const struct device *uart;
	const struct device *old_uart;
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
	old_uart = console_uart;
#ifndef CONFIG_BOARD_NATIVE_SIM
	if (old_uart != NULL) {
		uart_irq_rx_disable(old_uart);
	}
	if (port_id != 4U) {
		ret = uart_irq_callback_user_data_set(uart, console_uart_isr, NULL);
		if (ret != 0) {
			if (old_uart != NULL) {
				uart_irq_rx_enable(old_uart);
			}
			return ret;
		}
	}
	ring_buf_reset(&console_rx_ring);
#else
	ARG_UNUSED(old_uart);
#endif
	k_spinlock_key_t key = k_spin_lock(&console_lock);

	console_uart = uart;
	console_port = port_id;
	console_ready = true;
	transport_ready = port_id != 4U;
	if (port_id == 4U) {
		transport_ready = false;
	}
	(void)pending_flush_locked();
	k_spin_unlock(&console_lock, key);
	#ifndef CONFIG_BOARD_NATIVE_SIM
	if (port_id != 4U) {
		uart_irq_rx_enable(uart);
	}
	#endif
	return 0;
}

int ef800_console_init(bool usb_debug_available)
{
	ARG_UNUSED(usb_debug_available);

	/* Port assignments live in Configuration FAT. Select a stable early
	 * fallback until MainApplication loads and applies those records. */
	__printk_hook_install(console_putchar);
	return ef800_console_set_target(EF800_CONSOLE_FALLBACK_PORT);
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
	k_spinlock_key_t key = k_spin_lock(&console_lock);

	if (!console_ready || !transport_ready || console_uart == NULL ||
		!device_is_ready(console_uart)) {
		pending_write_locked(data, length);
		k_spin_unlock(&console_lock, key);
		return (int)length;
	}
	(void)pending_flush_locked();
	for (size_t index = 0U; index < length; ++index) {
		uart_poll_out(console_uart, data[index]);
	}
	k_spin_unlock(&console_lock, key);
	return (int)length;
}

void ef800_console_set_transport_ready(bool ready)
{
	k_spinlock_key_t key = k_spin_lock(&console_lock);

	transport_ready = ready;
	if (ready) {
		(void)pending_flush_locked();
	}
	k_spin_unlock(&console_lock, key);
}

int ef800_console_read(uint8_t *data)
{
	k_spinlock_key_t key = k_spin_lock(&console_lock);
	int ret = console_read_locked(data);

	k_spin_unlock(&console_lock, key);
	return ret;
}

size_t ef800_console_pending(void)
{
	k_spinlock_key_t key = k_spin_lock(&console_lock);
	size_t length = pending_length;

	k_spin_unlock(&console_lock, key);
	return length;
}

uint32_t ef800_console_dropped(void)
{
	k_spinlock_key_t key = k_spin_lock(&console_lock);
	uint32_t dropped = pending_dropped;

	k_spin_unlock(&console_lock, key);
	return dropped;
}
