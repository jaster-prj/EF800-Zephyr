/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_measurement_zephyr.h"

#include <errno.h>
#if !defined(CONFIG_BOARD_NATIVE_SIM) && defined(CONFIG_SOC_SERIES_STM32L4X)
#include <stm32_ll_rcc.h>
#include <stm32_ll_usart.h>
#endif
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ef800_measurement_uart, LOG_LEVEL_INF);

#if !defined(CONFIG_BOARD_NATIVE_SIM) && defined(CONFIG_SOC_SERIES_STM32L4X)
/* Port 1/2 USARTs can select an independent kernel clock. PCLK is 80 MHz on
 * EF800 and its 16x divider cannot represent 1200 baud (BRR > 0xffff), while
 * HSI is 16 MHz and is a good low-baud source. Keep the APB clocks untouched. */
static int measurement_uart_clock_configure(const struct device *uart,
	uint32_t baudrate)
{
	bool low_baud = baudrate < 9600U;
	uint32_t wait_us;

	if (low_baud && LL_RCC_HSI_IsReady() == 0U) {
		LL_RCC_HSI_Enable();
		for (wait_us = 0U; wait_us < 1000U; ++wait_us) {
			if (LL_RCC_HSI_IsReady() != 0U) {
				break;
			}
			k_busy_wait(1U);
		}
		if (LL_RCC_HSI_IsReady() == 0U) {
			LOG_ERR("HSI did not become ready for low-baud measurement UART");
			return -EIO;
		}
	}

	if (uart == DEVICE_DT_GET(DT_NODELABEL(usart3))) {
		LL_USART_Disable(USART3);
		LL_RCC_SetUSARTClockSource(low_baud ?
			LL_RCC_USART3_CLKSOURCE_HSI :
			LL_RCC_USART3_CLKSOURCE_PCLK1);
		return 1;
	}
	if (uart == DEVICE_DT_GET(DT_NODELABEL(usart1))) {
		LL_USART_Disable(USART1);
		LL_RCC_SetUSARTClockSource(low_baud ?
			LL_RCC_USART1_CLKSOURCE_HSI :
			LL_RCC_USART1_CLKSOURCE_PCLK2);
		return 2;
	}
	return 0;
}

static void measurement_uart_baudrate_apply(int instance, uint32_t baudrate)
{
	uint32_t clock_hz = baudrate < 9600U ? 16000000U : 80000000U;
	USART_TypeDef *usart = instance == 1 ? USART3 : USART1;

	/* uart_configure() uses the DTS PCLK rate when calculating BRR. Rewrite
	 * BRR using the selected kernel clock so both sides of the clock switch
	 * remain in sync. The STM32L4 peripheral is 16x oversampling here. */
	LL_USART_SetBaudRate(usart, clock_hz, LL_USART_OVERSAMPLING_16, baudrate);
}
#endif

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
	uint8_t byte;
	int sent;

	if (transport == NULL || uart_irq_update(dev) <= 0) {
		return;
	}
	if (uart_irq_rx_ready(dev)) {
		while (uart_fifo_read(dev, &byte, 1U) == 1) {
			k_spinlock_key_t key = k_spin_lock(&transport->rx_lock);
			uint16_t next = (uint16_t)((transport->rx_head + 1U) %
				EF800_MEASUREMENT_RX_BUFFER_SIZE);

			if (next != transport->rx_tail) {
				transport->rx_buffer[transport->rx_head] = byte;
				transport->rx_head = next;
				k_spin_unlock(&transport->rx_lock, key);
				k_sem_give(&transport->rx_available);
			} else {
				k_spin_unlock(&transport->rx_lock, key);
			}
		}
	}
	if (transport->tx_data != NULL && uart_irq_tx_ready(dev)) {
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
		LOG_ERR("measurement UART read rejected: %s TX is faulted",
			transport->uart->name);
		return -EIO;
	}
	LOG_DBG("measurement UART read begin: %s", transport->uart->name);
	while (true) {
		k_spinlock_key_t key = k_spin_lock(&transport->rx_lock);
		bool available = transport->rx_tail != transport->rx_head;

		if (available) {
			*data = transport->rx_buffer[transport->rx_tail];
			transport->rx_tail = (uint16_t)((transport->rx_tail + 1U) %
				EF800_MEASUREMENT_RX_BUFFER_SIZE);
		}
		k_spin_unlock(&transport->rx_lock, key);
		if (available) {
			if (transport->uart_config.data_bits == UART_CFG_DATA_BITS_7) {
				*data &= 0x7fU;
			}
			LOG_DBG("measurement UART RX %s byte=0x%02x",
				transport->uart->name, (unsigned int)*data);
			return 1;
		}
		int64_t remaining = (int64_t)deadline_ms - k_uptime_get();
		if (remaining <= 0) {
			LOG_WRN("measurement UART read timeout: %s", transport->uart->name);
			return 0;
		}
		if (k_sem_take(&transport->rx_available, K_MSEC(remaining)) != 0) {
			return 0;
		}
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
	int clock_instance = 0;
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
#if !defined(CONFIG_BOARD_NATIVE_SIM) && defined(CONFIG_SOC_SERIES_STM32L4X)
	clock_instance = measurement_uart_clock_configure(uart,
		measurement_config->baudrate);
	if (clock_instance < 0) {
		return clock_instance;
	}
	if (clock_instance != 0) {
		LOG_INF("measurement UART clock selected: %s baud=%u source=%s",
			uart->name, measurement_config->baudrate,
			measurement_config->baudrate < 9600U ? "HSI16" : "PCLK");
	}
#endif
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
#if !defined(CONFIG_BOARD_NATIVE_SIM) && defined(CONFIG_SOC_SERIES_STM32L4X)
	if (clock_instance != 0) {
		measurement_uart_baudrate_apply(clock_instance,
			measurement_config->baudrate);
	}
#endif
	transport->uart = uart;
	k_sem_init(&transport->tx_complete, 0, 1);
	k_sem_init(&transport->rx_available, 0, EF800_MEASUREMENT_RX_BUFFER_SIZE);
	transport->rx_head = 0U;
	transport->rx_tail = 0U;
	transport->tx_faulted = false;
	transport->uart_config = uart_config;
	result = uart_irq_callback_user_data_set(uart, zephyr_uart_callback,
		transport);
	if (result != 0) {
		return result;
	}
	uart_irq_rx_enable(uart);
	transport->transport = (struct ef800_measurement_transport){
		.write = zephyr_write,
		.read = zephyr_read,
		.context = transport,
		.write_until = zephyr_write_until,
		.read_until = zephyr_read_until,
	};
	return 0;
}
