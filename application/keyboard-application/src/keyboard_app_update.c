/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#include "keyboard_app_update.h"
#include "keyboard_i2c_service.h"
#include "ef800_keyboard_update_protocol.h"
#include "ef800_update.h"
#include "ef800_watchdog.h"

#define UPDATER_PARTITION_ID PARTITION_ID(keyboard_updater_partition)

BUILD_ASSERT(PARTITION_OFFSET(keyboard_updater_partition) == 0x38000U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_updater_partition) == 0x8000U);

static uint8_t update_response[EF800_KB_UPDATE_RESPONSE_MAX] __aligned(4);
static size_t update_response_length;

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


static void updater_response_status(uint8_t status)
{
	update_response[0] = status;
	update_response_length = 1U;
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

void ef800_keyboard_app_update_process(const uint8_t *request,
				      size_t request_length,
				      bool timed_out, bool overflow)
{
	const struct flash_area *area;
	const uint8_t *payload;
	size_t payload_length;
	uint8_t command;
	uint32_t offset;
	uint16_t length;
	int result;

	if (timed_out || overflow ||
	    !ef800_kb_update_frame_valid(request, request_length)) {
		updater_response_status(EF800_KB_PROTOCOL_NACK);
		goto done;
	}
	command = request[0];
	payload = &request[2];
	payload_length = request_length - 3U;
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
	(void)ef800_keyboard_i2c_update_respond(update_response,
		update_response_length);
}
