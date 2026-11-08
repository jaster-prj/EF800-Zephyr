/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_keyboard_update_protocol.h"

#include <string.h>

bool ef800_kb_update_command_valid(uint8_t command, uint8_t complement)
{
	return (uint8_t)(command ^ complement) == 0xFFU;
}

uint8_t ef800_kb_update_checksum(const uint8_t *data, size_t length)
{
	uint8_t checksum = 0U;

	if (data == NULL && length != 0U) {
		return 0U;
	}
	for (size_t i = 0U; i < length; ++i) {
		checksum ^= data[i];
	}
	return checksum;
}

bool ef800_kb_update_frame_valid(const uint8_t *data, size_t length)
{
	return data != NULL && length >= 3U &&
		ef800_kb_update_command_valid(data[0], data[1]) &&
		ef800_kb_update_checksum(data, length - 1U) == data[length - 1U];
}

size_t ef800_kb_update_frame_encode(uint8_t command, const uint8_t *payload,
				     size_t payload_length, uint8_t *frame,
				     size_t capacity)
{
	size_t frame_length = payload_length + 3U;

	if (frame == NULL || (payload == NULL && payload_length != 0U) ||
	    frame_length > capacity || frame_length > EF800_KB_UPDATE_REQUEST_MAX) {
		return 0U;
	}
	frame[0] = command;
	frame[1] = (uint8_t)~command;
	if (payload_length != 0U) {
		memcpy(&frame[2], payload, payload_length);
	}
	frame[frame_length - 1U] = ef800_kb_update_checksum(frame,
		frame_length - 1U);
	return frame_length;
}

bool ef800_kb_update_response_valid(const uint8_t *response, size_t length)
{
	if (response == NULL || length == 0U) {
		return false;
	}
	if (response[0] == EF800_KB_PROTOCOL_NACK ||
	    response[0] == EF800_KB_PROTOCOL_BUSY) {
		return length == 1U;
	}
	return response[0] == EF800_KB_PROTOCOL_ACK && length >= 2U &&
		ef800_kb_update_checksum(response, length - 1U) ==
		response[length - 1U];
}

bool ef800_kb_update_frame_timing_valid(uint32_t previous_ms, uint32_t now_ms)
{
	/* Unsigned subtraction intentionally handles uptime wraparound. */
	return (uint32_t)(now_ms - previous_ms) <=
		EF800_KB_UPDATE_FRAME_TIMEOUT_MS;
}

bool ef800_kb_update_target_range(enum ef800_kb_update_target target,
				  uint32_t *base, uint32_t *size)
{
	uint32_t range_base;
	uint32_t range_size;

	switch (target) {
	case EF800_KB_UPDATE_TARGET_BOOTLOADER:
		range_base = 0x08000000U;
		range_size = 0x00008000U;
		break;
	case EF800_KB_UPDATE_TARGET_APPLICATION:
		range_base = 0x08008000U;
		range_size = 0x00030000U;
		break;
	case EF800_KB_UPDATE_TARGET_UPDATER:
		range_base = 0x08038000U;
		range_size = 0x00008000U;
		break;
	default:
		return false;
	}
	if (base != NULL) {
		*base = range_base;
	}
	if (size != NULL) {
		*size = range_size;
	}
	return true;
}

bool ef800_kb_update_service_target_valid(
	enum ef800_kb_update_service service,
	enum ef800_kb_update_target target)
{
	if (service == EF800_KB_UPDATE_SERVICE_UPDATER) {
		return target == EF800_KB_UPDATE_TARGET_BOOTLOADER ||
			target == EF800_KB_UPDATE_TARGET_APPLICATION;
	}
	if (service == EF800_KB_UPDATE_SERVICE_KEYBOARD_APPLICATION) {
		return target == EF800_KB_UPDATE_TARGET_UPDATER;
	}
	return false;
}

bool ef800_kb_update_range_valid(enum ef800_kb_update_target target,
				 uint32_t address, size_t length)
{
	uint32_t base;
	uint32_t size;
	uint64_t end;

	if (!ef800_kb_update_target_range(target, &base, &size) || length == 0U) {
		return false;
	}
	end = (uint64_t)address + length;
	return address >= base && end <= (uint64_t)base + size;
}

bool ef800_kb_update_page_valid(enum ef800_kb_update_target target,
				uint16_t page)
{
	uint32_t base;
	uint32_t size;
	uint32_t first_page;
	uint32_t page_count;

	if (!ef800_kb_update_target_range(target, &base, &size)) {
		return false;
	}
	first_page = (base - 0x08000000U) / EF800_KB_UPDATE_PAGE_SIZE;
	page_count = size / EF800_KB_UPDATE_PAGE_SIZE;
	return page >= first_page && (uint32_t)page < first_page + page_count;
}

bool ef800_kb_update_write_valid(enum ef800_kb_update_target target,
				 uint32_t address, size_t length)
{
	return length != 0U &&
		(address % EF800_KB_UPDATE_WRITE_ALIGNMENT) == 0U &&
		(length % EF800_KB_UPDATE_WRITE_ALIGNMENT) == 0U &&
		length <= EF800_KB_UPDATE_BLOCK_MAX &&
		ef800_kb_update_range_valid(target, address, length);
}

size_t ef800_kb_update_version_encode(uint8_t command, uint8_t ack,
				      uint8_t major, uint8_t minor, uint8_t patch,
				      uint8_t *response, size_t capacity)
{
	if (response == NULL || capacity < EF800_KB_UPDATE_VERSION_SIZE) {
		return 0U;
	}
	response[0] = command;
	response[1] = major;
	response[2] = minor;
	response[3] = patch;
	response[4] = ack;
	return EF800_KB_UPDATE_VERSION_SIZE;
}
