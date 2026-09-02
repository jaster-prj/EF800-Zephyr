/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include <cmsis_core.h>

#include "keyboard_protocol.h"
#include "keyboard_translation.h"
#include "keyboard_key_names.h"

static void jump_to_system_bootloader(void);

#define KB_NODE DT_PATH(keyboard)
#define KB_UART_NODE DT_CHOSEN(zephyr_console)

#define KB_SCAN_PERIOD_MS 2U
#define KB_DEBOUNCE_MS 40U
#define KB_DEBOUNCE_SCANS (KB_DEBOUNCE_MS / KB_SCAN_PERIOD_MS)
#define KB_RX_QUEUE_SIZE 128U
#define KB_KEY_NAME_BUFFER_SIZE (KB_DEBUG_KEY_NAME_MAX + 1U)

#define KB_KEY_COUNT (KB_MATRIX_ROWS * KB_MATRIX_COLS + KB_SPECIAL_KEYS)

static const struct gpio_dt_spec rows[KB_MATRIX_ROWS] = {
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 0),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 1),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 2),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 3),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 4),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 5),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 6),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 7),
};

static const struct gpio_dt_spec columns[KB_MATRIX_COLS] = {
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, column_gpios, 0),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, column_gpios, 1),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, column_gpios, 2),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, column_gpios, 3),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, column_gpios, 4),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, column_gpios, 5),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, column_gpios, 6),
};

static const struct gpio_dt_spec special_keys[KB_SPECIAL_KEYS] = {
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, special_gpios, 0),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, special_gpios, 1),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, special_gpios, 2),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, special_gpios, 3),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, special_gpios, 4),
};

static const struct device *const uart = DEVICE_DT_GET(KB_UART_NODE);
K_MSGQ_DEFINE(rx_msgq, sizeof(uint8_t), KB_RX_QUEUE_SIZE, 4);

static uint64_t stable_state;
static uint8_t debounce_count[KB_KEY_COUNT];
static bool keyboard_active = true;
static bool keyboard_auto = true;
static bool debug_enabled;
static uint8_t typematic = 0x2BU;
static uint64_t virtual_key_state;

static struct {
	bool valid;
	uint8_t prefix;
	uint8_t code;
	uint64_t start_ms;
	uint64_t next_ms;
} repeat_key;

enum command_mode {
	COMMAND_IDLE,
	COMMAND_TYPEMATIC,
	COMMAND_DEBUG_SETTING,
	COMMAND_DEBUG_ACTION,
	COMMAND_DEBUG_NAME_LENGTH,
	COMMAND_DEBUG_NAME,
	COMMAND_DEBUG_DISCARD,
};

static enum command_mode command_mode;
static uint64_t command_timestamp_ms;
static uint8_t debug_action;
static uint8_t debug_name_length;
static uint8_t debug_name_used;
static uint8_t debug_discard_remaining;
static char debug_name[KB_KEY_NAME_BUFFER_SIZE];

static void set_repeat(const struct kb_wire_scancode *scancode,
			       uint64_t now_ms);

static const uint16_t typematic_period_ms[32] = {
	33U, 37U, 42U, 46U, 48U, 54U, 59U, 63U,
	67U, 75U, 83U, 92U, 100U, 109U, 116U, 125U,
	133U, 149U, 167U, 182U, 200U, 217U, 233U, 250U,
	270U, 303U, 333U, 370U, 400U, 435U, 476U, 500U,
};

static void uart_send(const uint8_t *data, size_t length)
{
	size_t i;

	for (i = 0U; i < length; ++i) {
		uart_poll_out(uart, data[i]);
	}
}

static void send_ack(uint8_t command)
{
	const uint8_t response[] = { CMD_RETURN, command, CMD_ACK };

	uart_send(response, sizeof(response));
}

static void send_ack_data(uint8_t command, uint8_t data)
{
	const uint8_t response[] = { CMD_RETURN, command, CMD_ACK, data };

	uart_send(response, sizeof(response));
}

static void send_resend(uint8_t command)
{
	const uint8_t response[] = { CMD_RETURN, command, CMD_RESEND };

	uart_send(response, sizeof(response));
}

static void send_scancode(uint8_t prefix, uint8_t code, bool release)
{
	uint8_t frame[3];
	size_t length = 0U;

	if (prefix != KB_SCANCODE_PREFIX_NONE) {
		frame[length++] = prefix;
	}
	if (release) {
		frame[length++] = KB_SCANCODE_BREAK;
	}
	frame[length++] = code;
	uart_send(frame, length);
}

static void clear_repeat(void)
{
	repeat_key.valid = false;
	repeat_key.prefix = KB_SCANCODE_PREFIX_NONE;
	repeat_key.code = 0U;
	repeat_key.start_ms = 0U;
	repeat_key.next_ms = 0U;
}

static int raw_key_index(uint16_t raw)
{
	if ((raw & 0x0F00U) == 0U && (raw >> 4) < KB_MATRIX_ROWS &&
		(raw & 0x0FU) < KB_MATRIX_COLS) {
		return (int)((raw >> 4) * KB_MATRIX_COLS + (raw & 0x0FU));
	}
	if (raw >= KB_SPECIAL_RAW(0) && raw < KB_SPECIAL_RAW(KB_SPECIAL_KEYS)) {
		return (int)(KB_MATRIX_ROWS * KB_MATRIX_COLS +
			(raw - KB_SPECIAL_RAW(0)));
	}
	return -1;
}

static uint16_t raw_key_for_index(uint8_t index)
{
	if (index < KB_MATRIX_ROWS * KB_MATRIX_COLS) {
		return KB_MATRIX_RAW(index / KB_MATRIX_COLS, index % KB_MATRIX_COLS);
	}
	return KB_SPECIAL_RAW(index - KB_MATRIX_ROWS * KB_MATRIX_COLS);
}

static void release_virtual_keys(void)
{
	uint8_t index;

	clear_repeat();
	for (index = 0U; index < KB_KEY_COUNT; ++index) {
		uint16_t raw;
		struct kb_wire_scancode scancode;

		if ((virtual_key_state & (1ULL << index)) == 0U) {
			continue;
		}
		raw = raw_key_for_index(index);
		if (kb_scancode_for_raw(raw, &scancode) == 0) {
			send_scancode(scancode.prefix, scancode.code, true);
		}
	}
	virtual_key_state = 0U;
}

static void process_debug_key(void)
{
	uint16_t raw;
	struct kb_wire_scancode scancode;
	int index;

	if (!debug_enabled ||
		(debug_action != KB_DEBUG_ACTION_PRESS &&
		 debug_action != KB_DEBUG_ACTION_RELEASE) ||
		kb_key_name_to_raw(debug_name, debug_name_length, &raw) != 0 ||
		kb_scancode_for_raw(raw, &scancode) != 0) {
		return;
	}

	index = raw_key_index(raw);
	if (index < 0) {
		return;
	}
	if (debug_action == KB_DEBUG_ACTION_PRESS) {
		if ((virtual_key_state & (1ULL << index)) != 0U) {
			return;
		}
		send_scancode(scancode.prefix, scancode.code, false);
		virtual_key_state |= 1ULL << index;
		set_repeat(&scancode, k_uptime_get());
	} else if (debug_action == KB_DEBUG_ACTION_RELEASE) {
		if ((virtual_key_state & (1ULL << index)) == 0U) {
			return;
		}
		if (repeat_key.valid && repeat_key.prefix == scancode.prefix &&
			repeat_key.code == scancode.code) {
			clear_repeat();
		}
		send_scancode(scancode.prefix, scancode.code, true);
		virtual_key_state &= ~(1ULL << index);
	}
}

static void set_repeat(const struct kb_wire_scancode *scancode,
			       uint64_t now_ms)
{
	uint8_t delay_units = (uint8_t)(typematic >> 5);

	if (scancode->modifier) {
		return;
	}
	repeat_key.valid = true;
	repeat_key.prefix = scancode->prefix;
	repeat_key.code = scancode->code;
	repeat_key.start_ms = now_ms + ((uint64_t)delay_units + 1U) * 250U;
	repeat_key.next_ms = repeat_key.start_ms;
}

static void repeat_if_due(uint64_t now_ms)
{
	uint8_t rate;
	uint64_t period;

	if ((!keyboard_active && !debug_enabled) || !repeat_key.valid ||
		now_ms < repeat_key.next_ms) {
		return;
	}

	rate = (uint8_t)(typematic & 0x1FU);
	period = typematic_period_ms[rate];
	send_scancode(repeat_key.prefix, repeat_key.code, false);
	repeat_key.next_ms = now_ms + period;
}

static int read_key_state(uint64_t *state)
{
	int value;
	uint8_t col;
	uint8_t row;

	for (col = 0U; col < KB_MATRIX_COLS; ++col) {
		value = gpio_pin_configure_dt(&columns[col], GPIO_INPUT);
		if (value != 0) {
			return value;
		}
	}

	for (col = 0U; col < KB_MATRIX_COLS; ++col) {
		value = gpio_pin_configure_dt(&columns[col], GPIO_OUTPUT_ACTIVE);
		if (value != 0) {
			return value;
		}

		for (row = 0U; row < KB_MATRIX_ROWS; ++row) {
			value = gpio_pin_get_dt(&rows[row]);
			if (value < 0) {
				(void)gpio_pin_configure_dt(&columns[col], GPIO_INPUT);
				return value;
			}
			if (value != 0) {
				*state |= 1ULL << (col * KB_MATRIX_ROWS + row);
			}
		}
		(void)gpio_pin_configure_dt(&columns[col], GPIO_INPUT);
	}

	for (row = 0U; row < KB_SPECIAL_KEYS; ++row) {
		value = gpio_pin_get_dt(&special_keys[row]);
		if (value < 0) {
			return value;
		}
		if (value != 0) {
			*state |= 1ULL << (KB_MATRIX_ROWS * KB_MATRIX_COLS + row);
		}
	}

	return 0;
}

static void report_key_change(uint8_t index, bool pressed, uint64_t now_ms)
{
	uint16_t raw;
	struct kb_wire_scancode scancode;

	if (index < KB_MATRIX_ROWS * KB_MATRIX_COLS) {
		raw = KB_MATRIX_RAW(index % KB_MATRIX_ROWS, index / KB_MATRIX_ROWS);
	} else {
		raw = KB_SPECIAL_RAW(index - KB_MATRIX_ROWS * KB_MATRIX_COLS);
	}

	if (kb_scancode_for_raw(raw, &scancode) != 0) {
		return;
	}

	send_scancode(scancode.prefix, scancode.code, !pressed);
	if (!pressed) {
		if (repeat_key.valid && repeat_key.prefix == scancode.prefix &&
		    repeat_key.code == scancode.code) {
			clear_repeat();
		}
	} else {
		set_repeat(&scancode, now_ms);
	}
}

static void scan_keyboard(uint64_t now_ms)
{
	uint64_t current_state = 0U;
	uint8_t index;
	bool current;
	bool stable;

	if (!keyboard_active || read_key_state(&current_state) != 0) {
		return;
	}
	for (index = 0U; index < KB_KEY_COUNT; ++index) {
		current = (current_state & (1ULL << index)) != 0U;
		stable = (stable_state & (1ULL << index)) != 0U;
		if (current == stable) {
			debounce_count[index] = 0U;
			continue;
		}

		if (debounce_count[index] < KB_DEBOUNCE_SCANS) {
			++debounce_count[index];
		}
		if (debounce_count[index] < KB_DEBOUNCE_SCANS) {
			continue;
		}

		if (current) {
			stable_state |= 1ULL << index;
		} else {
			stable_state &= ~(1ULL << index);
		}
		debounce_count[index] = 0U;
		report_key_change(index, current, now_ms);
	}
}

static void command_complete(void)
{
	command_mode = COMMAND_IDLE;
	command_timestamp_ms = 0U;
	/* Debug input owns the controller while it is enabled. */
	keyboard_active = debug_enabled ? false : keyboard_auto;
}

static void process_command_byte(uint8_t byte, uint64_t now_ms)
{
	if (command_mode != COMMAND_IDLE &&
	    now_ms - command_timestamp_ms > 250U) {
		command_mode = COMMAND_IDLE;
		keyboard_active = debug_enabled ? false : keyboard_auto;
	}

	if (command_mode != COMMAND_IDLE) {
		command_timestamp_ms = now_ms;
		switch (command_mode) {
		case COMMAND_TYPEMATIC:
			typematic = byte;
			send_ack_data(CMD_KEYBOARD_TYPEMATIC, typematic);
			command_complete();
			return;
		case COMMAND_DEBUG_SETTING:
			if (byte == 0U) {
				/* Clear controller-side key state before leaving debug mode. */
				release_virtual_keys();
			}
			debug_enabled = byte != 0U;
			send_ack_data(CMD_KEYBOARD_SET_DEBUG, debug_enabled ? 1U : 0U);
			command_complete();
			return;
		case COMMAND_DEBUG_ACTION:
			debug_action = byte;
			command_mode = COMMAND_DEBUG_NAME_LENGTH;
			return;
		case COMMAND_DEBUG_NAME_LENGTH:
			debug_name_length = byte;
			debug_name_used = 0U;
			if (debug_name_length == 0U ||
				debug_name_length > KB_DEBUG_KEY_NAME_MAX) {
				debug_discard_remaining = debug_name_length;
				if (debug_discard_remaining == 0U) {
					command_complete();
				} else {
					command_mode = COMMAND_DEBUG_DISCARD;
				}
			} else {
				command_mode = COMMAND_DEBUG_NAME;
			}
			return;
		case COMMAND_DEBUG_NAME:
			debug_name[debug_name_used++] = (char)byte;
			if (debug_name_used >= debug_name_length) {
				debug_name[debug_name_used] = '\0';
				process_debug_key();
				command_complete();
			}
			return;
		case COMMAND_DEBUG_DISCARD:
			if (--debug_discard_remaining == 0U) {
				command_complete();
			}
			return;
		default:
			command_mode = COMMAND_IDLE;
			break;
		}
	}

	switch (byte) {
	case CMD_KEYBOARD_ACTIVATE:
		release_virtual_keys();
		keyboard_auto = true;
		keyboard_active = !debug_enabled;
		memset(debounce_count, 0, sizeof(debounce_count));
		send_ack(CMD_KEYBOARD_ACTIVATE);
		break;
	case CMD_KEYBOARD_DEACTIVATE:
		keyboard_auto = false;
		keyboard_active = false;
		release_virtual_keys();
		clear_repeat();
		send_ack(CMD_KEYBOARD_DEACTIVATE);
		break;
	case CMD_KEYBOARD_RESET:
		send_ack(CMD_KEYBOARD_RESET);
		k_sleep(K_MSEC(10));
		sys_reboot(SYS_REBOOT_COLD);
		break;
	case CMD_KEYBOARD_BOOTLOADER:
		keyboard_auto = false;
		keyboard_active = false;
		send_ack(CMD_KEYBOARD_BOOTLOADER);
		k_sleep(K_MSEC(10));
		jump_to_system_bootloader();
		break;
	case CMD_KEYBOARD_TYPEMATIC:
		keyboard_active = false;
		command_mode = COMMAND_TYPEMATIC;
		command_timestamp_ms = now_ms;
		break;
	case CMD_KEYBOARD_SET_DEBUG:
		keyboard_active = false;
		command_mode = COMMAND_DEBUG_SETTING;
		command_timestamp_ms = now_ms;
		break;
	case CMD_KEYBOARD_ECHO:
		send_scancode(KB_SCANCODE_PREFIX_NONE, CMD_KEYBOARD_ECHO, false);
		break;
	case CMD_KEYBOARD_DEBUG:
		/* Always consume the complete packet. Disabled debug requests are silent. */
		if (debug_enabled) {
			keyboard_active = false;
		}
		command_mode = COMMAND_DEBUG_ACTION;
		command_timestamp_ms = now_ms;
		break;
	case CMD_KEYBOARD_ID: {
			const uint8_t response[] = { CMD_RETURN, CMD_KEYBOARD_ID, 0xABU, 0x83U };
			uart_send(response, sizeof(response));
			break;
	}
	default:
		send_resend(byte);
		break;
	}
}

static void expire_command(uint64_t now_ms)
{
	if (command_mode != COMMAND_IDLE &&
	    now_ms - command_timestamp_ms > 250U) {
		command_mode = COMMAND_IDLE;
		keyboard_active = keyboard_auto;
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

static void jump_to_system_bootloader(void)
{
#if defined(CONFIG_CPU_CORTEX_M)
	const uint32_t *const vector_table = (const uint32_t *)0x1FFF0000UL;
	void (*const entry)(void) = (void (*)(void))vector_table[1];

	__disable_irq();
	SysTick->CTRL = 0U;
	SysTick->LOAD = 0U;
	SysTick->VAL = 0U;
	SCB->VTOR = (uint32_t)vector_table;
	__DSB();
	__ISB();
	__set_MSP(vector_table[0]);
	entry();
#endif
	for (;;) {
		/* The system bootloader does not return. */
	}
}

static int init_gpio(void)
{
	uint8_t i;
	int result;

	for (i = 0U; i < KB_MATRIX_ROWS; ++i) {
		if (!gpio_is_ready_dt(&rows[i])) {
			return -ENODEV;
		}
		result = gpio_pin_configure_dt(&rows[i], GPIO_INPUT);
		if (result != 0) {
			return result;
		}
	}
	for (i = 0U; i < KB_MATRIX_COLS; ++i) {
		if (!gpio_is_ready_dt(&columns[i])) {
			return -ENODEV;
		}
		result = gpio_pin_configure_dt(&columns[i], GPIO_INPUT);
		if (result != 0) {
			return result;
		}
	}
	for (i = 0U; i < KB_SPECIAL_KEYS; ++i) {
		if (!gpio_is_ready_dt(&special_keys[i])) {
			return -ENODEV;
		}
		result = gpio_pin_configure_dt(&special_keys[i], GPIO_INPUT);
		if (result != 0) {
			return result;
		}
	}
	return 0;
}

static int init_uart(void)
{
	const struct uart_config config = {
		.baudrate = KB_UART_BAUDRATE,
		.parity = UART_CFG_PARITY_ODD,
		.stop_bits = UART_CFG_STOP_BITS_2,
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
	uint64_t now_ms;

	if (init_gpio() != 0 || init_uart() != 0) {
		return 0;
	}

	{
		const uint8_t startup = CMD_INITIALIZED;
		uart_send(&startup, 1U);
	}

	while (true) {
		now_ms = k_uptime_get();
		expire_command(now_ms);
		while (k_msgq_get(&rx_msgq, &byte, K_NO_WAIT) == 0) {
			process_command_byte(byte, now_ms);
		}
		scan_keyboard(now_ms);
		repeat_if_due(now_ms);
		k_msleep(KB_SCAN_PERIOD_MS);
	}
}
