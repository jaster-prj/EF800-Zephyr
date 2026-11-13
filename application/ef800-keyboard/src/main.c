/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Run keyboard scanning, UART protocol, and updater I2C services. */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include <cmsis_core.h>

#include "keyboard_protocol.h"
#include "ef800_keyboard_boot.h"
#include "ef800_update.h"
#include "ef800_watchdog.h"
#include "keyboard_matrix.h"
#include "keyboard_i2c_service.h"
#include "keyboard_app_update.h"
#include "keyboard_uart_protocol.h"

#define UPDATER_HANDOFF_SETTLE_MS 20U

static int configure_protocol_uart(uint32_t baudrate);


#define KB_UART_NODE DT_CHOSEN(zephyr_console)

#define KB_SCAN_PERIOD_MS 2U
#define KB_RX_QUEUE_SIZE 128U

static const struct device *const uart = DEVICE_DT_GET(KB_UART_NODE);
K_MSGQ_DEFINE(rx_msgq, sizeof(uint8_t), KB_RX_QUEUE_SIZE, 4);

__attribute__((naked, noreturn)) static void branch_to_updater(uint32_t msp,
	uint32_t reset_handler)
{
	__asm__ volatile (
		"msr msp, r0\n"
		"movs r0, #0\n"
		"msr control, r0\n"
		"msr basepri, r0\n"
		"msr faultmask, r0\n"
		"isb\n"
		"bx r1\n");
}

static bool jump_to_updater(void)
{
	const uintptr_t address = PARTITION_ADDRESS(keyboard_updater_partition);
	const size_t size = PARTITION_SIZE(keyboard_updater_partition);
	const uint32_t *vector = (const uint32_t *)address;

	if (!ef800_vector_table_valid(vector, address, size,
		DT_REG_ADDR(DT_NODELABEL(sram0)), DT_REG_SIZE(DT_NODELABEL(sram0)))) {
		return false;
	}
	if (ef800_keyboard_i2c_service_unregister() != 0) {
		return false;
	}
	__disable_irq();
	SysTick->CTRL = 0U;
	SysTick->LOAD = 0U;
	SysTick->VAL = 0U;
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
	for (size_t i = 0U; i < ARRAY_SIZE(NVIC->ICER); ++i) {
		NVIC->ICER[i] = UINT32_MAX;
		NVIC->ICPR[i] = UINT32_MAX;
	}
	SCB->VTOR = (uint32_t)address;
	__DSB();
	__ISB();
	branch_to_updater(vector[0], vector[1]);
}

static struct kb_matrix_debounce_state matrix_state;

static void protocol_transmit(void *context, const uint8_t *data, size_t length)
{
	ARG_UNUSED(context);
	for (size_t index = 0U; index < length; ++index) {
		uart_poll_out(uart, data[index]);
	}
}

static int protocol_configure_baudrate(void *context, uint32_t baudrate)
{
	ARG_UNUSED(context);
	return configure_protocol_uart(baudrate);
}

static void protocol_delay_ms(void *context, uint32_t delay_ms)
{
	ARG_UNUSED(context);
	k_msleep(delay_ms);
}

static uint64_t protocol_uptime_ms(void *context)
{
	ARG_UNUSED(context);
	return k_uptime_get();
}

static void protocol_reset_scan_counts(void *context)
{
	ARG_UNUSED(context);
	memset(matrix_state.scan_counts, 0, sizeof(matrix_state.scan_counts));
}

static void protocol_reset_device(void *context)
{
	ARG_UNUSED(context);
	k_sleep(K_MSEC(10));
	sys_reboot(SYS_REBOOT_COLD);
}

static void protocol_enter_updater(void *context)
{
	ARG_UNUSED(context);
	k_sleep(K_MSEC(10));
	ef800_keyboard_i2c_updater_entry_request();
}

static void scan_keyboard(uint64_t now_ms)
{
	struct kb_matrix_key_event events[EF800_KEYBOARD_SCAN_KEY_COUNT];
	size_t event_count;

	if (ef800_keyboard_matrix_scan(&matrix_state,
					       ef800_keyboard_uart_protocol_active(), events,
				       ARRAY_SIZE(events), &event_count) != 0) {
		return;
	}
	for (size_t index = 0U; index < event_count; ++index) {
		ef800_keyboard_uart_protocol_key_change(events[index].key_index,
			events[index].pressed, now_ms);
	}
}

static void uart_isr(const struct device *dev, void *user_data)
{
	uint8_t byte;

	ARG_UNUSED(user_data);
	if (uart_irq_update(dev) <= 0) {
		return;
	}
	if (!uart_irq_rx_ready(dev)) {
		return;
	}
	while (uart_fifo_read(dev, &byte, 1U) == 1) {
		(void)k_msgq_put(&rx_msgq, &byte, K_NO_WAIT);
	}
}

static int init_uart(void)
{
	return configure_protocol_uart(KB_UART_BAUDRATE);
}

static int configure_protocol_uart(uint32_t baudrate)
{
	const struct uart_config config = {
		.baudrate = baudrate,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};
	int result;

	if (!device_is_ready(uart)) {
		return -ENODEV;
	}
	result = uart_configure(uart, &config);
	if (result != 0) {
		return result;
	}
	uart_irq_callback_user_data_set(uart, uart_isr, NULL);
	uart_irq_rx_enable(uart);
	return 0;
}

int main(void)
{
	uint8_t byte;
	uint8_t update_request[EF800_KB_UPDATE_REQUEST_MAX];
	size_t update_request_length;
	bool update_timed_out;
	bool update_overflow;

	struct ef800_keyboard_boot_state *boot_state =
		ef800_keyboard_boot_state_get();
	uint8_t healthy_scans = 0U;

	if (ef800_keyboard_matrix_init() != 0 || init_uart() != 0) {
		return 0;
	}
	if (ef800_watchdog_inherit() != 0) {
		return 0;
	}
	if (ef800_keyboard_i2c_service_init() != 0) {
		return 0;
	}

	{
		const struct keyboard_uart_protocol_ops protocol_ops = {
			.transmit = protocol_transmit,
			.configure_baudrate = protocol_configure_baudrate,
			.delay_ms = protocol_delay_ms,
			.uptime_ms = protocol_uptime_ms,
			.reset_scan_counts = protocol_reset_scan_counts,
			.reset_device = protocol_reset_device,
			.enter_updater = protocol_enter_updater,
		};

		if (ef800_keyboard_uart_protocol_init(&protocol_ops) != 0) {
			return 0;
		}
	}

	while (true) {
		(void)ef800_watchdog_feed();
		if (ef800_keyboard_i2c_update_take(update_request,
				&update_request_length, &update_timed_out,
				&update_overflow)) {
			ef800_keyboard_app_update_process(update_request, update_request_length,
				update_timed_out, update_overflow);
		}
		if (ef800_keyboard_i2c_updater_entry_take()) {
			/* Let the I2C controller finish the acknowledged write_read and
			 * release the bus before replacing the application context. */
			k_msleep(UPDATER_HANDOFF_SETTLE_MS);
			(void)jump_to_updater();
		}
		uint64_t now_ms = k_uptime_get();
		ef800_keyboard_uart_protocol_expire(now_ms);
		while (k_msgq_get(&rx_msgq, &byte, K_NO_WAIT) == 0) {
			ef800_keyboard_uart_protocol_process(byte, now_ms);
		}
		scan_keyboard(now_ms);
		if (healthy_scans < 3U && ++healthy_scans == 3U) {
			ef800_keyboard_boot_confirm(boot_state);
		}
		ef800_keyboard_uart_protocol_repeat(now_ms);
		k_msleep(KB_SCAN_PERIOD_MS);
	}
}
