/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Implement asynchronous UART-backed instrument transport. */
#ifndef EF800_MEASUREMENT_ZEPHYR_H
#define EF800_MEASUREMENT_ZEPHYR_H

#include <stdint.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include "ef800_measurement.h"

#define EF800_MEASUREMENT_RX_BUFFER_SIZE 128U

struct ef800_measurement_zephyr_transport {
	struct ef800_measurement_transport transport;
	const struct device *uart;
	struct k_sem tx_complete;
	struct k_sem rx_available;
	struct k_spinlock rx_lock;
	uint8_t rx_buffer[EF800_MEASUREMENT_RX_BUFFER_SIZE];
	volatile uint16_t rx_head;
	volatile uint16_t rx_tail;
	const uint8_t *tx_data;
	size_t tx_length;
	size_t tx_offset;
	int tx_status;
	bool tx_faulted;
	struct uart_config uart_config;
};

/** @brief Initialize UART callbacks using explicit instrument serial settings.
 * @param transport Zephyr transport state to initialize.
 * @param uart Ready UART device.
 * @param config Baud, framing, and flow-control configuration.
 * @return 0 on setup; negative errno if device/configuration is unsupported.
 */
int ef800_measurement_zephyr_init_with_config(
	struct ef800_measurement_zephyr_transport *transport,
	const struct device *uart,
	const struct ef800_measurement_serial_config *config);

#endif /* EF800_MEASUREMENT_ZEPHYR_H */
