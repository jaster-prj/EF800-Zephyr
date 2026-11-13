/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#include "keyboard_updater_session.h"
#include "keyboard_updater_flash.h"
#include "ef800_keyboard_update_protocol.h"
#include "ef800_update.h"
#include "ef800_watchdog.h"

#define UPDATER_VERSION_MAJOR 1U
#define UPDATER_VERSION_MINOR 0U
#define UPDATER_VERSION_PATCH 0U
#define ERASED_PAGE_BITMAP_SIZE (128U / 8U)

/* Transport invokes this command/session logic from the updater main context. */
static uint8_t request_frame[EF800_KB_UPDATE_REQUEST_MAX] __aligned(4);
static size_t request_length;
static bool request_is_version;
static bool request_timed_out;
static bool request_overflow;
static uint8_t response[EF800_KB_UPDATE_RESPONSE_MAX] __aligned(4);
static size_t response_length;
static uint8_t version_response[EF800_KB_UPDATE_VERSION_SIZE];

struct update_session {
	enum ef800_kb_update_target target;
	uint32_t image_size;
	uint32_t expected_crc32;
	uint32_t received;
	uint32_t crc32;
	uint8_t erased_pages[ERASED_PAGE_BITMAP_SIZE];
	bool active;
	bool checked;
};

static struct update_session session;
static uint8_t flash_scratch[EF800_KB_UPDATE_BLOCK_MAX] __aligned(4);
static uint8_t padded_write[EF800_KB_UPDATE_BLOCK_MAX] __aligned(8);
static const uintptr_t sram_address = DT_REG_ADDR(DT_NODELABEL(sram0));
static const size_t sram_size = DT_REG_SIZE(DT_NODELABEL(sram0));

static uint32_t get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t get_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void put_le16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

static void response_status(uint8_t status)
{
	response[0] = status;
	response_length = 1U;
}

static void response_ack(const uint8_t *payload, size_t payload_length)
{
	if (payload_length > sizeof(response) - 2U) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	response[0] = EF800_KB_PROTOCOL_ACK;
	if (payload_length != 0U) {
		memcpy(&response[1], payload, payload_length);
	}
	response[1U + payload_length] = ef800_kb_update_checksum(response,
		1U + payload_length);
	response_length = payload_length + 2U;
}

static void response_ack_only(void)
{
	response_ack(NULL, 0U);
}

static bool service_target_allowed(enum ef800_kb_update_target target)
{
	return ef800_kb_update_service_target_valid(
		EF800_KB_UPDATE_SERVICE_UPDATER, target);
}

static bool erased_page(uint16_t page)
{
	return (session.erased_pages[page / 8U] & BIT(page % 8U)) != 0U;
}

static void mark_page_erased(uint16_t page)
{
	session.erased_pages[page / 8U] |= BIT(page % 8U);
}

static bool write_pages_erased(uint32_t offset, size_t length,
			       uint32_t partition_size)
{
	uint32_t first_page;
	uint32_t last_page;

	if (length == 0U || offset > partition_size ||
	    length > partition_size - offset) {
		return false;
	}
	first_page = offset / EF800_KB_UPDATE_PAGE_SIZE;
	last_page = (uint32_t)(offset + length - 1U) /
		EF800_KB_UPDATE_PAGE_SIZE;
	for (uint32_t page = first_page; page <= last_page; ++page) {
		uint32_t base;
		uint32_t size;
		uint16_t absolute_page;

		if (!ef800_kb_update_target_range(session.target, &base, &size)) {
			return false;
		}
		ARG_UNUSED(size);
		absolute_page = (uint16_t)((base - 0x08000000U) /
			EF800_KB_UPDATE_PAGE_SIZE + page);
		if (!erased_page(absolute_page)) {
			return false;
		}
	}
	return true;
}

static void process_version(void)
{
	if (ef800_kb_update_version_encode(EF800_KB_UPDATE_VERSION_COMMAND,
			EF800_KB_UPDATE_VERSION_ACK, UPDATER_VERSION_MAJOR,
			UPDATER_VERSION_MINOR, UPDATER_VERSION_PATCH,
			version_response, sizeof(version_response)) !=
	    sizeof(version_response)) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	memcpy(response, version_response, sizeof(version_response));
	response_length = sizeof(version_response);
}

static void process_get(uint8_t command)
{
	static const uint8_t commands[] = {
		EF800_KB_UPDATE_CMD_GET,
		EF800_KB_UPDATE_CMD_GET_ID,
		EF800_KB_UPDATE_CMD_READ_MEMORY,
		EF800_KB_UPDATE_CMD_WRITE_MEMORY,
		EF800_KB_UPDATE_CMD_ERASE,
		EF800_KB_UPDATE_CMD_BEGIN,
		EF800_KB_UPDATE_CMD_CHECK,
		EF800_KB_UPDATE_CMD_RUN,
	};
	uint8_t payload[sizeof(commands) + 2U];

	if (command == EF800_KB_UPDATE_CMD_GET) {
		payload[0] = 1U;
		payload[1] = sizeof(commands);
		memcpy(&payload[2], commands, sizeof(commands));
		response_ack(payload, sizeof(payload));
	} else {
		payload[0] = 2U;
		payload[1] = 0x04U;
		payload[2] = 0x35U;
		response_ack(payload, 3U);
	}
}

static bool parse_frame(uint8_t *command, const uint8_t **payload,
			size_t *payload_length)
{
	if (request_length < 3U || request_length > sizeof(request_frame) ||
	    request_timed_out || request_overflow ||
	    !ef800_kb_update_frame_valid(request_frame, request_length)) {
		return false;
	}
	*command = request_frame[0];
	*payload = &request_frame[2];
	*payload_length = request_length - 3U;
	return true;
}

static void process_begin(const uint8_t *payload, size_t length)
{
	enum ef800_kb_update_target target;
	uint32_t base;
	uint32_t size;

	if (length != 9U) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	target = (enum ef800_kb_update_target)payload[0];
	if (!service_target_allowed(target) ||
	    !ef800_kb_update_target_range(target, &base, &size) ||
	    get_le32(&payload[1]) < 8U || get_le32(&payload[1]) > size) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	session = (struct update_session) {
		.target = target,
		.image_size = get_le32(&payload[1]),
		.expected_crc32 = get_le32(&payload[5]),
		.active = true,
	};
	ARG_UNUSED(base);
	response_ack_only();
}

static void process_erase(const uint8_t *payload, size_t length)
{
	enum ef800_kb_update_target target;
	uint32_t base;
	uint32_t size;
	uint32_t page_count;
	const struct flash_area *area;
	int result;

	if (length < 2U || !session.active) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	target = (enum ef800_kb_update_target)payload[0];
	if (target != session.target || !service_target_allowed(target) ||
	    !ef800_kb_update_target_range(target, &base, &size) ||
	    ef800_keyboard_updater_flash_open(target, &area) != 0) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	page_count = size / EF800_KB_UPDATE_PAGE_SIZE;
	if (payload[1] == EF800_KB_UPDATE_ERASE_PARTITION && length == 2U) {
		for (uint32_t page = 0U; page < page_count; ++page) {
			result = ef800_keyboard_updater_flash_erase_page(area, page);
			if (result != 0) {
				(void)ef800_keyboard_updater_flash_close(area);
				response_status(EF800_KB_PROTOCOL_NACK);
				return;
			}
			mark_page_erased((uint16_t)((base - 0x08000000U) /
				EF800_KB_UPDATE_PAGE_SIZE + page));
		}
	} else if (payload[1] == EF800_KB_UPDATE_ERASE_BLOCK && length == 4U) {
		uint16_t page = get_le16(&payload[2]);
		uint32_t first_page = (base - 0x08000000U) /
			EF800_KB_UPDATE_PAGE_SIZE;

		if (!ef800_kb_update_page_valid(target, page)) {
			(void)ef800_keyboard_updater_flash_close(area);
			response_status(EF800_KB_PROTOCOL_NACK);
			return;
		}
		result = ef800_keyboard_updater_flash_erase_page(area,
			page - first_page);
		if (result != 0) {
			(void)ef800_keyboard_updater_flash_close(area);
			response_status(EF800_KB_PROTOCOL_NACK);
			return;
		}
		mark_page_erased(page);
	} else {
		(void)ef800_keyboard_updater_flash_close(area);
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	(void)ef800_keyboard_updater_flash_close(area);
	session.checked = false;
	response_ack_only();
}

static void process_write(const uint8_t *payload, size_t length)
{
	enum ef800_kb_update_target target;
	uint32_t base;
	uint32_t size;
	uint32_t offset;
	uint16_t data_length;
	size_t padded_length;
	const struct flash_area *area;
	int result;

	if (!session.active || length < 8U) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	target = (enum ef800_kb_update_target)payload[0];
	offset = get_le32(&payload[1]);
	data_length = get_le16(&payload[5]);
	if (data_length == 0U || data_length > EF800_KB_UPDATE_BLOCK_MAX ||
	    length != (size_t)7U + data_length || target != session.target ||
	    offset != session.received || offset > session.image_size ||
	    data_length > session.image_size - offset ||
	    !ef800_kb_update_target_range(target, &base, &size)) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	padded_length = ROUND_UP(data_length, EF800_KB_UPDATE_WRITE_ALIGNMENT);
	if (padded_length > sizeof(padded_write) ||
	    !ef800_kb_update_write_valid(target, base + offset, padded_length) ||
	    !write_pages_erased(offset, padded_length, size) ||
	    ef800_keyboard_updater_flash_open(target, &area) != 0) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	memset(padded_write, 0xFF, padded_length);
	memcpy(padded_write, &payload[7], data_length);
	result = ef800_keyboard_updater_flash_write(area, offset, padded_write,
		padded_length);
	(void)ef800_keyboard_updater_flash_close(area);
	if (result != 0) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	session.crc32 = ef800_crc32_update(session.crc32, &payload[7], data_length);
	session.received += data_length;
	session.checked = false;
	(void)ef800_watchdog_feed();
	response_ack_only();
}

static void process_read(const uint8_t *payload, size_t length)
{
	enum ef800_kb_update_target target;
	uint32_t base;
	uint32_t size;
	uint32_t offset;
	uint16_t read_length;
	const struct flash_area *area;

	if (length != 7U) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	target = (enum ef800_kb_update_target)payload[0];
	offset = get_le32(&payload[1]);
	read_length = get_le16(&payload[5]);
	if (!session.active || target != session.target || read_length == 0U ||
	    read_length > EF800_KB_UPDATE_BLOCK_MAX ||
	    !ef800_kb_update_target_range(target, &base, &size) ||
	    offset > size || read_length > size - offset ||
	    ef800_keyboard_updater_flash_open(target, &area) != 0) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	if (ef800_keyboard_updater_flash_read(area, offset, flash_scratch,
			read_length) != 0) {
		(void)ef800_keyboard_updater_flash_close(area);
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	put_le16(&response[1], read_length);
	memcpy(&response[3], flash_scratch, read_length);
	response[0] = EF800_KB_PROTOCOL_ACK;
	response[3U + read_length] = ef800_kb_update_checksum(response,
		3U + read_length);
	response_length = 4U + read_length;
	(void)ef800_keyboard_updater_flash_close(area);
	(void)base;
}

static bool check_flash_image(uint32_t base, uint32_t size, uint32_t image_size,
			      uint32_t expected_crc, uint32_t *actual_crc)
{
	const struct flash_area *area;
	uint32_t crc = 0U;
	uint32_t offset = 0U;
	uint32_t vector[2];
	bool vector_valid;

	if (image_size < sizeof(vector) || image_size > size ||
	    ef800_keyboard_updater_flash_open(session.target, &area) != 0) {
		return false;
	}
	while (offset < image_size) {
		size_t chunk = MIN(sizeof(flash_scratch), image_size - offset);

		if (ef800_keyboard_updater_flash_read(area, offset, flash_scratch,
				chunk) != 0) {
			(void)ef800_keyboard_updater_flash_close(area);
			return false;
		}
		crc = ef800_crc32_update(crc, flash_scratch, chunk);
		offset += chunk;
		(void)ef800_watchdog_feed();
	}
	if (ef800_keyboard_updater_flash_read(area, 0U, vector,
			sizeof(vector)) != 0) {
		(void)ef800_keyboard_updater_flash_close(area);
		return false;
	}
	vector_valid = ef800_vector_table_valid(vector, base, image_size,
		sram_address, sram_size);
	(void)ef800_keyboard_updater_flash_close(area);
	*actual_crc = crc;
	return crc == expected_crc && vector_valid;
}

static void process_check(const uint8_t *payload, size_t length)
{
	enum ef800_kb_update_target target;
	uint32_t base;
	uint32_t size;
	uint32_t actual_crc;
	uint8_t result[5];

	if (length != 1U || !session.active ||
	    session.received != session.image_size ||
	    session.crc32 != session.expected_crc32) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	target = (enum ef800_kb_update_target)payload[0];
	if (target != session.target ||
	    !ef800_kb_update_target_range(target, &base, &size) ||
	    !check_flash_image(base, size, session.image_size,
			       session.expected_crc32, &actual_crc)) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	session.checked = true;
	put_le32(result, actual_crc);
	result[4] = 1U;
	response_ack(result, sizeof(result));
}

static void process_request(bool *reboot_after_response)
{
	uint8_t command;
	const uint8_t *payload;
	size_t payload_length;

	if (request_is_version) {
		process_version();
		return;
	}
	if (!parse_frame(&command, &payload, &payload_length)) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	switch (command) {
	case EF800_KB_UPDATE_CMD_GET:
	case EF800_KB_UPDATE_CMD_GET_ID:
		if (payload_length != 0U) {
			response_status(EF800_KB_PROTOCOL_NACK);
		} else {
			process_get(command);
		}
		break;
	case EF800_KB_UPDATE_CMD_BEGIN:
		process_begin(payload, payload_length);
		break;
	case EF800_KB_UPDATE_CMD_ERASE:
		process_erase(payload, payload_length);
		break;
	case EF800_KB_UPDATE_CMD_WRITE_MEMORY:
		process_write(payload, payload_length);
		break;
	case EF800_KB_UPDATE_CMD_READ_MEMORY:
		process_read(payload, payload_length);
		break;
	case EF800_KB_UPDATE_CMD_CHECK:
		process_check(payload, payload_length);
		break;
	case EF800_KB_UPDATE_CMD_RUN:
		if (payload_length != 0U || !session.checked) {
			response_status(EF800_KB_PROTOCOL_NACK);
		} else {
			response_ack_only();
			*reboot_after_response = true;
		}
		break;
	default:
		response_status(EF800_KB_PROTOCOL_NACK);
		break;
	}
}


int ef800_keyboard_updater_session_process(
	const uint8_t *incoming, size_t incoming_length, bool incoming_version,
	bool timed_out, bool overflow,
	uint8_t outgoing[EF800_KB_UPDATE_RESPONSE_MAX], size_t *outgoing_length,
	bool *reboot_after_response)
{
	if (outgoing == NULL || outgoing_length == NULL ||
	    reboot_after_response == NULL) {
		return -EINVAL;
	}
	if (incoming == NULL || incoming_length > sizeof(request_frame)) {
		response_status(EF800_KB_PROTOCOL_NACK);
		*reboot_after_response = false;
	} else {
		memcpy(request_frame, incoming, incoming_length);
		request_length = incoming_length;
		request_is_version = incoming_version;
		request_timed_out = timed_out;
		request_overflow = overflow;
		*reboot_after_response = false;
		process_request(reboot_after_response);
	}
	memcpy(outgoing, response, response_length);
	*outgoing_length = response_length;
	return 0;
}
