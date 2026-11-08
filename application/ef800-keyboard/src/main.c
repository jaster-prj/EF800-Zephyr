/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/storage/flash_map.h>

#include <cmsis_core.h>

#include "keyboard_protocol.h"
#include "keyboard_translation.h"
#include "keyboard_key_names.h"
#include "ef800_keyboard_boot.h"
#include "ef800_keyboard_i2c.h"
#include "ef800_keyboard_update_protocol.h"
#include "ef800_update.h"
#include "ef800_watchdog.h"

#define EF800_KEYBOARD_VERSION_MAJOR 1U
#define EF800_KEYBOARD_VERSION_MINOR 1U
#define EF800_KEYBOARD_VERSION_PATCH 3U
#define KEYBOARD_UPDATE_BITMAP_SIZE 16U

#define UPDATER_PARTITION_ID PARTITION_ID(keyboard_updater_partition)
#define UPDATER_HANDOFF_SETTLE_MS 20U

BUILD_ASSERT(PARTITION_OFFSET(keyboard_updater_partition) == 0x38000U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_updater_partition) == 0x8000U);

static int configure_protocol_uart(uint32_t baudrate);

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
static const struct device *const i2c_target = DEVICE_DT_GET(DT_NODELABEL(i2c1));
K_MSGQ_DEFINE(rx_msgq, sizeof(uint8_t), KB_RX_QUEUE_SIZE, 4);

static uint8_t i2c_command;
static bool i2c_command_received;
static bool i2c_framed_write;
static uint8_t i2c_response[EF800_KEYBOARD_I2C_STATUS_SIZE];
static uint8_t i2c_response_length;
static uint8_t i2c_response_position;
static bool i2c_enter_updater_after_stop;
static atomic_t i2c_enter_updater_pending;
static uint8_t update_request[EF800_KB_UPDATE_REQUEST_MAX] __aligned(4);
static size_t update_request_length;
static uint8_t update_response[EF800_KB_UPDATE_RESPONSE_MAX] __aligned(4);
static size_t update_response_length;
static size_t update_response_position;
static bool update_write_transaction;
static bool update_read_transaction;
static uint32_t update_last_byte_ms;
static bool update_timed_out;
static bool update_overflow;
static atomic_t update_request_pending;
static atomic_t update_response_ready;

struct keyboard_updater_session {
	uint32_t image_size;
	uint32_t expected_crc32;
	uint32_t received;
	uint32_t crc32;
	uint16_t erased_pages;
	bool active;
	bool checked;
};

static struct keyboard_updater_session updater_session;
static uint8_t updater_read_buffer[EF800_KB_UPDATE_BLOCK_MAX] __aligned(4);
static uint8_t updater_write_buffer[EF800_KB_UPDATE_BLOCK_MAX] __aligned(8);

static int i2c_write_requested(struct i2c_target_config *config)
{
	ARG_UNUSED(config);
	i2c_command = 0U;
	i2c_command_received = false;
	i2c_framed_write = false;
	i2c_response_length = 0U;
	i2c_response_position = 0U;
	i2c_enter_updater_after_stop = false;
	update_request_length = 0U;
	update_timed_out = false;
	update_overflow = false;
	update_last_byte_ms = k_uptime_get_32();
	update_write_transaction = true;
	update_read_transaction = false;
	return 0;
}

static int i2c_write_received(struct i2c_target_config *config, uint8_t byte)
{
	ARG_UNUSED(config);
	if (i2c_command_received) {
		if (!i2c_framed_write &&
		    (i2c_command == EF800_KEYBOARD_I2C_CMD_STATUS ||
		     i2c_command == EF800_KEYBOARD_I2C_CMD_VERSION ||
		     i2c_command == EF800_KEYBOARD_I2C_CMD_ENTER_UPDATER)) {
			return -ENOTSUP;
		}
		if (!ef800_kb_update_frame_timing_valid(update_last_byte_ms,
							 k_uptime_get_32())) {
			update_timed_out = true;
		}
		update_last_byte_ms = k_uptime_get_32();
		if (update_request_length >= sizeof(update_request)) {
			update_overflow = true;
			return 0;
		}
		update_request[update_request_length++] = byte;
		return 0;
	}
	i2c_command = byte;
	i2c_command_received = true;
	if (byte != EF800_KEYBOARD_I2C_CMD_STATUS &&
	    byte != EF800_KEYBOARD_I2C_CMD_VERSION &&
	    byte != EF800_KEYBOARD_I2C_CMD_ENTER_UPDATER) {
		i2c_framed_write = true;
		update_request[update_request_length++] = byte;
	}
	return 0;
}

static int i2c_read_requested(struct i2c_target_config *config, uint8_t *byte)
{
	ARG_UNUSED(config);
	if (i2c_framed_write) {
		update_read_transaction = true;
		update_write_transaction = false;
		update_response_position = 0U;
		if (atomic_get(&update_response_ready) == 0) {
			*byte = EF800_KB_PROTOCOL_BUSY;
			update_response_length = 1U;
			update_response_position = 1U;
			return 0;
		}
		if (update_response_length == 0U) {
			*byte = EF800_KB_PROTOCOL_NACK;
			update_response_length = 1U;
			update_response_position = 1U;
			return 0;
		}
		*byte = update_response[update_response_position++];
		return 0;
	}
	if (i2c_command == EF800_KEYBOARD_I2C_CMD_STATUS) {
		i2c_response[0] = EF800_KEYBOARD_I2C_CMD_STATUS;
		i2c_response[1] = EF800_KEYBOARD_VERSION_MAJOR;
		i2c_response[2] = EF800_KEYBOARD_VERSION_MINOR;
		i2c_response[3] = EF800_KEYBOARD_VERSION_PATCH;
		i2c_response[4] = EF800_KEYBOARD_I2C_MODE_APPLICATION;
		i2c_response[5] = EF800_KEYBOARD_I2C_ACK;
		i2c_response_length = EF800_KEYBOARD_I2C_STATUS_SIZE;
	} else if (i2c_command == EF800_KEYBOARD_I2C_CMD_VERSION) {
		i2c_response_length = (uint8_t)ef800_kb_update_version_encode(
			EF800_KEYBOARD_I2C_CMD_VERSION, EF800_KEYBOARD_I2C_ACK,
			EF800_KEYBOARD_VERSION_MAJOR, EF800_KEYBOARD_VERSION_MINOR,
			EF800_KEYBOARD_VERSION_PATCH, i2c_response,
			sizeof(i2c_response));
	} else if (i2c_command == EF800_KEYBOARD_I2C_CMD_ENTER_UPDATER) {
		i2c_response[0] = EF800_KEYBOARD_I2C_CMD_ENTER_UPDATER;
		i2c_response[1] = EF800_KEYBOARD_I2C_ACK;
		i2c_response_length = 2U;
		i2c_enter_updater_after_stop = true;
	} else {
		return -ENOTSUP;
	}
	*byte = i2c_response[0];
	i2c_response_position = 1U;
	return 0;
}

static int i2c_read_processed(struct i2c_target_config *config, uint8_t *byte)
{
	ARG_UNUSED(config);
	if (i2c_framed_write) {
		if (update_response_position < update_response_length) {
			*byte = update_response[update_response_position++];
		} else {
			*byte = 0xFFU;
		}
		return 0;
	}
	if (i2c_response_position < i2c_response_length) {
		*byte = i2c_response[i2c_response_position++];
	} else {
		*byte = 0xFFU;
	}
	return 0;
}

static int i2c_stop(struct i2c_target_config *config)
{
	ARG_UNUSED(config);
	if (update_write_transaction) {
		update_write_transaction = false;
		if (i2c_framed_write) {
			atomic_set(&update_response_ready, 0);
			atomic_set(&update_request_pending, 1);
		} else if (i2c_enter_updater_after_stop &&
			   i2c_response_position >= i2c_response_length) {
			atomic_set(&i2c_enter_updater_pending, 1);
		}
		return 0;
	}
	if (update_read_transaction) {
		update_read_transaction = false;
		if (update_response_position >= update_response_length) {
			atomic_set(&update_response_ready, 0);
		}
		return 0;
	}
	if (i2c_enter_updater_after_stop &&
	    i2c_response_position >= i2c_response_length) {
		atomic_set(&i2c_enter_updater_pending, 1);
	}
	return 0;
}

static const struct i2c_target_callbacks i2c_callbacks = {
	.write_requested = i2c_write_requested,
	.write_received = i2c_write_received,
	.read_requested = i2c_read_requested,
	.read_processed = i2c_read_processed,
	.stop = i2c_stop,
};

static void updater_response_status(uint8_t status)
{
	update_response[0] = status;
	update_response_length = 1U;
	update_response_position = 0U;
}

static void updater_response_ack(const uint8_t *payload, size_t length)
{
	if (length > sizeof(update_response) - 2U) {
		updater_response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	update_response[0] = EF800_KB_PROTOCOL_ACK;
	if (length != 0U) {
		memcpy(&update_response[1], payload, length);
	}
	update_response[length + 1U] = ef800_kb_update_checksum(update_response,
		length + 1U);
	update_response_length = length + 2U;
	update_response_position = 0U;
}

static uint16_t updater_get_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t updater_get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void updater_put_le16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void updater_put_le32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

static bool updater_page_erased(uint32_t relative_page)
{
	return relative_page < 16U &&
		(updater_session.erased_pages & BIT(relative_page)) != 0U;
}

static void process_app_update_request(void)
{
	const struct flash_area *area;
	const uint8_t *payload;
	size_t payload_length;
	uint8_t command;
	uint32_t offset;
	uint16_t length;
	int result;

	if (update_timed_out || update_overflow ||
	    !ef800_kb_update_frame_valid(update_request, update_request_length)) {
		updater_response_status(EF800_KB_PROTOCOL_NACK);
		goto done;
	}
	command = update_request[0];
	payload = &update_request[2];
	payload_length = update_request_length - 3U;
	switch (command) {
	case EF800_KB_UPDATE_CMD_GET: {
		static const uint8_t commands[] = {
			EF800_KB_UPDATE_CMD_GET,
			EF800_KB_UPDATE_CMD_GET_ID,
			EF800_KB_UPDATE_CMD_READ_MEMORY,
			EF800_KB_UPDATE_CMD_WRITE_MEMORY,
			EF800_KB_UPDATE_CMD_ERASE,
			EF800_KB_UPDATE_CMD_BEGIN,
			EF800_KB_UPDATE_CMD_CHECK,
		};
		uint8_t response[sizeof(commands) + 2U] = {
			1U, sizeof(commands),
		};

		if (payload_length != 0U) {
			updater_response_status(EF800_KB_PROTOCOL_NACK);
		} else {
			memcpy(&response[2], commands, sizeof(commands));
			updater_response_ack(response, sizeof(response));
		}
		break;
	}
	case EF800_KB_UPDATE_CMD_GET_ID: {
		const uint8_t id[] = {2U, 0x04U, 0x35U};

		if (payload_length != 0U) {
			updater_response_status(EF800_KB_PROTOCOL_NACK);
		} else {
			updater_response_ack(id, sizeof(id));
		}
		break;
	}
	case EF800_KB_UPDATE_CMD_BEGIN:
		if (payload_length != 9U ||
		    !ef800_kb_update_service_target_valid(
			EF800_KB_UPDATE_SERVICE_KEYBOARD_APPLICATION,
			(enum ef800_kb_update_target)payload[0]) ||
		    updater_get_le32(&payload[1]) < 8U ||
		    updater_get_le32(&payload[1]) > PARTITION_SIZE(keyboard_updater_partition)) {
			updater_response_status(EF800_KB_PROTOCOL_NACK);
			break;
		}
		updater_session = (struct keyboard_updater_session) {
			.image_size = updater_get_le32(&payload[1]),
			.expected_crc32 = updater_get_le32(&payload[5]),
			.active = true,
		};
		updater_response_ack(NULL, 0U);
		break;
	case EF800_KB_UPDATE_CMD_ERASE:
		if (!updater_session.active || payload_length < 2U ||
		    !ef800_kb_update_service_target_valid(
			EF800_KB_UPDATE_SERVICE_KEYBOARD_APPLICATION,
			(enum ef800_kb_update_target)payload[0]) ||
		    flash_area_open(UPDATER_PARTITION_ID, &area) != 0) {
			updater_response_status(EF800_KB_PROTOCOL_NACK);
			break;
		}
		if (payload[1] == EF800_KB_UPDATE_ERASE_PARTITION &&
		    payload_length == 2U) {
			result = 0;
			for (uint32_t page = 0U; page < 16U; ++page) {
				result = flash_area_erase(area,
					page * EF800_KB_UPDATE_PAGE_SIZE,
					EF800_KB_UPDATE_PAGE_SIZE);
				(void)ef800_watchdog_feed();
				if (result != 0) {
					break;
				}
				updater_session.erased_pages |= BIT(page);
			}
		} else if (payload[1] == EF800_KB_UPDATE_ERASE_BLOCK &&
			   payload_length == 4U &&
			   ef800_kb_update_page_valid(
				EF800_KB_UPDATE_TARGET_UPDATER,
				updater_get_le16(&payload[2]))) {
			uint32_t page = updater_get_le16(&payload[2]) - 112U;

			result = flash_area_erase(area,
				page * EF800_KB_UPDATE_PAGE_SIZE,
				EF800_KB_UPDATE_PAGE_SIZE);
			if (result == 0) {
				updater_session.erased_pages |= BIT(page);
			}
			(void)ef800_watchdog_feed();
		} else {
			result = -EINVAL;
		}
		(void)flash_area_close(area);
		if (result != 0) {
			updater_response_status(EF800_KB_PROTOCOL_NACK);
		} else {
			updater_session.checked = false;
			updater_response_ack(NULL, 0U);
		}
		break;
	case EF800_KB_UPDATE_CMD_WRITE_MEMORY:
		if (!updater_session.active || payload_length < 8U ||
		    payload[0] != EF800_KB_UPDATE_TARGET_UPDATER) {
			updater_response_status(EF800_KB_PROTOCOL_NACK);
			break;
		}
		offset = updater_get_le32(&payload[1]);
		length = updater_get_le16(&payload[5]);
		if (length == 0U || length > EF800_KB_UPDATE_BLOCK_MAX ||
		    payload_length != (size_t)length + 7U ||
		    offset != updater_session.received ||
		    offset > updater_session.image_size ||
		    length > updater_session.image_size - offset) {
			updater_response_status(EF800_KB_PROTOCOL_NACK);
			break;
		}
		{
			size_t padded = ROUND_UP(length,
				EF800_KB_UPDATE_WRITE_ALIGNMENT);
			uint32_t first = offset / EF800_KB_UPDATE_PAGE_SIZE;
			uint32_t last = (offset + padded - 1U) /
				EF800_KB_UPDATE_PAGE_SIZE;
			bool pages_ok = last < 16U;

			for (uint32_t page = first; pages_ok && page <= last; ++page) {
				pages_ok = updater_page_erased(page);
			}
			if (padded > sizeof(updater_write_buffer) || !pages_ok ||
			    !ef800_kb_update_write_valid(
				EF800_KB_UPDATE_TARGET_UPDATER,
				PARTITION_ADDRESS(keyboard_updater_partition) + offset,
				padded) ||
			    flash_area_open(UPDATER_PARTITION_ID, &area) != 0) {
				updater_response_status(EF800_KB_PROTOCOL_NACK);
				break;
			}
			memset(updater_write_buffer, 0xFF, padded);
			memcpy(updater_write_buffer, &payload[7], length);
			result = flash_area_write(area, offset, updater_write_buffer, padded);
			(void)flash_area_close(area);
			if (result != 0) {
				updater_response_status(EF800_KB_PROTOCOL_NACK);
				break;
			}
			updater_session.crc32 = ef800_crc32_update(
				updater_session.crc32, &payload[7], length);
			updater_session.received += length;
			updater_session.checked = false;
			(void)ef800_watchdog_feed();
			updater_response_ack(NULL, 0U);
		}
		break;
	case EF800_KB_UPDATE_CMD_READ_MEMORY:
		if (payload_length != 7U || !updater_session.active ||
		    payload[0] != EF800_KB_UPDATE_TARGET_UPDATER) {
			updater_response_status(EF800_KB_PROTOCOL_NACK);
			break;
		}
		offset = updater_get_le32(&payload[1]);
		length = updater_get_le16(&payload[5]);
		if (length == 0U || length > EF800_KB_UPDATE_BLOCK_MAX ||
		    offset > PARTITION_SIZE(keyboard_updater_partition) ||
		    length > PARTITION_SIZE(keyboard_updater_partition) - offset ||
		    flash_area_open(UPDATER_PARTITION_ID, &area) != 0) {
			updater_response_status(EF800_KB_PROTOCOL_NACK);
			break;
		}
		if (flash_area_read(area, offset, updater_read_buffer, length) != 0) {
			(void)flash_area_close(area);
			updater_response_status(EF800_KB_PROTOCOL_NACK);
			break;
		}
		update_response[0] = EF800_KB_PROTOCOL_ACK;
		updater_put_le16(&update_response[1], length);
		memcpy(&update_response[3], updater_read_buffer, length);
		update_response[3U + length] = ef800_kb_update_checksum(
			update_response, 3U + length);
		update_response_length = 4U + length;
		update_response_position = 0U;
		(void)flash_area_close(area);
		break;
	case EF800_KB_UPDATE_CMD_CHECK: {
		uint32_t crc = 0U;
		uint32_t done = 0U;
		uint32_t vector[2];
		uint8_t result_payload[5];
		bool valid = payload_length == 1U && updater_session.active &&
			payload[0] == EF800_KB_UPDATE_TARGET_UPDATER &&
			updater_session.received == updater_session.image_size &&
			updater_session.crc32 == updater_session.expected_crc32;
		bool area_opened = false;

		if (valid) {
			area_opened = flash_area_open(UPDATER_PARTITION_ID, &area) == 0;
			valid = area_opened;
		}

		while (valid && done < updater_session.image_size) {
			size_t chunk = MIN(sizeof(updater_read_buffer),
				updater_session.image_size - done);

			if (flash_area_read(area, done, updater_read_buffer, chunk) != 0) {
				valid = false;
				break;
			}
			crc = ef800_crc32_update(crc, updater_read_buffer, chunk);
			done += chunk;
			(void)ef800_watchdog_feed();
		}
		if (valid && flash_area_read(area, 0U, vector, sizeof(vector)) != 0) {
			valid = false;
		}
		if (valid) {
			valid = crc == updater_session.expected_crc32 &&
				ef800_vector_table_valid(vector,
					PARTITION_ADDRESS(keyboard_updater_partition),
					updater_session.image_size,
					DT_REG_ADDR(DT_NODELABEL(sram0)),
					DT_REG_SIZE(DT_NODELABEL(sram0)));
		}
		if (area_opened) {
			(void)flash_area_close(area);
		}
		if (!valid) {
			updater_response_status(EF800_KB_PROTOCOL_NACK);
			break;
		}
		updater_session.checked = true;
		updater_put_le32(result_payload, crc);
		result_payload[4] = 1U;
		updater_response_ack(result_payload, sizeof(result_payload));
		break;
	}
	default:
		updater_response_status(EF800_KB_PROTOCOL_NACK);
		break;
	}
	done:
	atomic_set(&update_response_ready, 1);
}

static struct i2c_target_config i2c_config = {
	.address = EF800_KEYBOARD_I2C_ADDRESS,
	.callbacks = &i2c_callbacks,
};

static int init_i2c_target(void)
{
	if (!device_is_ready(i2c_target)) {
		return -ENODEV;
	}
	return i2c_target_register(i2c_target, &i2c_config);
}

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
	if (i2c_target_unregister(i2c_target, &i2c_config) != 0) {
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
	COMMAND_BAUDRATE,
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
		case COMMAND_BAUDRATE: {
			uint32_t baudrate;
			/* ACK the request at the old baud before changing the UART. */
			send_ack(CMD_KEYBOARD_BAUDRATE);
			/* Let the old-baud ACK drain before reconfiguring USART3. */
			k_msleep(10U);
			(void)(baud_selector_to_rate(byte, &baudrate) == 0 &&
				configure_protocol_uart(baudrate) == 0);
			k_msleep(100U);
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
		atomic_set(&i2c_enter_updater_pending, 1);
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
		k_msleep(10U);
		if (configure_protocol_uart(KB_UART_BAUDRATE) == 0) {
			k_msleep(100U);
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
	uint64_t now_ms;

	struct ef800_keyboard_boot_state *boot_state =
		ef800_keyboard_boot_state_get();
	uint8_t healthy_scans = 0U;

	if (init_gpio() != 0 || init_uart() != 0) {
		return 0;
	}
	if (ef800_watchdog_inherit() != 0) {
		return 0;
	}
	if (init_i2c_target() != 0) {
		return 0;
	}

	{
		const uint8_t startup = CMD_INITIALIZED;
		uart_send(&startup, 1U);
	}

	while (true) {
		(void)ef800_watchdog_feed();
		if (atomic_cas(&update_request_pending, 1, 0)) {
			process_app_update_request();
		}
		if (atomic_cas(&i2c_enter_updater_pending, 1, 0)) {
			/* Let the I2C controller finish the acknowledged write_read and
			 * release the bus before replacing the application context. */
			k_msleep(UPDATER_HANDOFF_SETTLE_MS);
			(void)jump_to_updater();
		}
		now_ms = k_uptime_get();
		expire_command(now_ms);
		while (k_msgq_get(&rx_msgq, &byte, K_NO_WAIT) == 0) {
			process_command_byte(byte, now_ms);
		}
		scan_keyboard(now_ms);
		if (healthy_scans < 3U && ++healthy_scans == 3U) {
			ef800_keyboard_boot_confirm(boot_state);
		}
		repeat_if_due(now_ms);
		k_msleep(KB_SCAN_PERIOD_MS);
	}
}
