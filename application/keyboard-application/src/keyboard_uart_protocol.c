/* SPDX-License-Identifier: Apache-2.0 */
#include "keyboard_uart_protocol.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "keyboard_protocol.h"
#include "keyboard_translation.h"
#include "keyboard_key_names.h"

#define KB_KEY_NAME_BUFFER_SIZE (KB_DEBUG_KEY_NAME_MAX + 1U)
#define KB_KEY_COUNT (KB_MATRIX_ROWS * KB_MATRIX_COLS + KB_SPECIAL_KEYS)
#define EF800_KEYBOARD_VERSION_MAJOR 1U
#define EF800_KEYBOARD_VERSION_MINOR 1U
#define EF800_KEYBOARD_VERSION_PATCH 3U

struct keyboard_repeat_key {
	bool valid;
	uint8_t prefix;
	uint8_t code;
	uint64_t start_ms;
	uint64_t next_ms;
};

enum command_mode {
	COMMAND_IDLE,
	COMMAND_TYPEMATIC,
	COMMAND_DEBUG_SETTING,
	COMMAND_DEBUG_ACTION,
	COMMAND_DEBUG_NAME_LENGTH,
	COMMAND_DEBUG_NAME,
	COMMAND_DEBUG_DISCARD,
	COMMAND_BAUDRATE,
};

static struct keyboard_uart_protocol_ops protocol_ops;
static bool keyboard_active = true;
static bool keyboard_auto = true;
static bool debug_enabled;
static uint8_t typematic = 0x2BU;
static uint64_t virtual_key_state;
static struct keyboard_repeat_key repeat_key;
static enum command_mode command_mode;
static uint64_t command_timestamp_ms;
static uint8_t debug_action;
static uint8_t debug_name_length;
static uint8_t debug_name_used;
static uint8_t debug_discard_remaining;
static char debug_name[KB_KEY_NAME_BUFFER_SIZE];

static const uint16_t typematic_period_ms[32] = {
	33U, 37U, 42U, 46U, 48U, 54U, 59U, 63U,
	67U, 75U, 83U, 92U, 100U, 109U, 116U, 125U,
	133U, 149U, 167U, 182U, 200U, 217U, 233U, 250U,
	270U, 303U, 333U, 370U, 400U, 435U, 476U, 500U,
};

static void set_repeat(const struct kb_wire_scancode *scancode,
		       uint64_t now_ms);

static void uart_send(const uint8_t *data, size_t length)
{
	protocol_ops.transmit(protocol_ops.context, data, length);
}

static int baud_selector_to_rate(uint8_t selector, uint32_t *rate)
{
	switch (selector) {
	case KB_BAUD_SELECTOR_9600: *rate = 9600U; return 0;
	case KB_BAUD_SELECTOR_19200: *rate = 19200U; return 0;
	case KB_BAUD_SELECTOR_38400: *rate = 38400U; return 0;
	case KB_BAUD_SELECTOR_57600: *rate = 57600U; return 0;
	case KB_BAUD_SELECTOR_115200: *rate = 115200U; return 0;
	default: return -EINVAL;
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

static void send_initialized(void)
{
	const uint8_t startup = CMD_INITIALIZED;

	uart_send(&startup, 1U);
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
		set_repeat(&scancode,
			protocol_ops.uptime_ms(protocol_ops.context));
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
		case COMMAND_BAUDRATE: {
			uint32_t baudrate;
			/* ACK the request at the old baud before changing the UART. */
			send_ack(CMD_KEYBOARD_BAUDRATE);
			/* Let the old-baud ACK drain before reconfiguring USART3. */
			protocol_ops.delay_ms(protocol_ops.context, 10U);
			(void)(baud_selector_to_rate(byte, &baudrate) == 0 &&
				protocol_ops.configure_baudrate(protocol_ops.context, baudrate) == 0);
			protocol_ops.delay_ms(protocol_ops.context, 100U);
			send_initialized();
			command_complete();
			return;
		}
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
		protocol_ops.reset_scan_counts(protocol_ops.context);
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
		protocol_ops.reset_device(protocol_ops.context);
		break;
	case CMD_KEYBOARD_BOOTLOADER:
		keyboard_auto = false;
		keyboard_active = false;
		send_ack(CMD_KEYBOARD_BOOTLOADER);
		protocol_ops.enter_updater(protocol_ops.context);
		break;
	case CMD_KEYBOARD_BAUDRATE:
		keyboard_auto = false;
		keyboard_active = false;
		command_mode = COMMAND_BAUDRATE;
		command_timestamp_ms = now_ms;
		break;
	case CMD_KEYBOARD_BAUDRATE_DEFAULT:
		keyboard_auto = false;
		keyboard_active = false;
		send_ack(CMD_KEYBOARD_BAUDRATE_DEFAULT);
		protocol_ops.delay_ms(protocol_ops.context, 10U);
		if (protocol_ops.configure_baudrate(protocol_ops.context,
					    KB_UART_BAUDRATE) == 0) {
			protocol_ops.delay_ms(protocol_ops.context, 100U);
			send_initialized();
		}
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
	case CMD_KEYBOARD_VERSION: {
			/* Firmware version 1.1.3, encoded as SemVer major/minor/patch. */
			const uint8_t response[] = { CMD_RETURN, CMD_KEYBOARD_VERSION,
				EF800_KEYBOARD_VERSION_MAJOR, EF800_KEYBOARD_VERSION_MINOR,
				EF800_KEYBOARD_VERSION_PATCH };
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
		keyboard_active = debug_enabled ? false : keyboard_auto;
	}
}


int ef800_keyboard_uart_protocol_init(
	const struct keyboard_uart_protocol_ops *ops)
{
	if (ops == NULL || ops->transmit == NULL ||
	    ops->configure_baudrate == NULL || ops->delay_ms == NULL ||
	    ops->uptime_ms == NULL ||
	    ops->reset_scan_counts == NULL ||
	    ops->reset_device == NULL || ops->enter_updater == NULL) {
		return -EINVAL;
	}
	protocol_ops = *ops;
	command_mode = COMMAND_IDLE;
	command_timestamp_ms = 0U;
	keyboard_active = true;
	keyboard_auto = true;
	debug_enabled = false;
	typematic = 0x2BU;
	virtual_key_state = 0U;
	debug_action = 0U;
	debug_name_length = 0U;
	debug_name_used = 0U;
	debug_discard_remaining = 0U;
	memset(debug_name, 0, sizeof(debug_name));
	clear_repeat();
	send_initialized();
	return 0;
}

void ef800_keyboard_uart_protocol_process(uint8_t byte, uint64_t now_ms)
{
	process_command_byte(byte, now_ms);
}

void ef800_keyboard_uart_protocol_expire(uint64_t now_ms)
{
	expire_command(now_ms);
}

void ef800_keyboard_uart_protocol_key_change(uint8_t index, bool pressed,
					    uint64_t now_ms)
{
	report_key_change(index, pressed, now_ms);
}

void ef800_keyboard_uart_protocol_repeat(uint64_t now_ms)
{
	repeat_if_due(now_ms);
}

bool ef800_keyboard_uart_protocol_active(void)
{
	return keyboard_active;
}
