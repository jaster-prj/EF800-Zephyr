/* SPDX-License-Identifier: Apache-2.0 */
#include "virtual_ef800_protocol.h"

#include <errno.h>
#include <string.h>

static void put_u32(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}

static uint32_t get_u32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

size_t virtual_ef800_protocol_encode(uint8_t type, const uint8_t *payload,
					     uint16_t length, uint8_t *frame,
					     size_t frame_size)
{
	size_t total = VIRTUAL_EF800_PROTOCOL_HEADER_SIZE + length;

	if (frame == NULL || length > VIRTUAL_EF800_PROTOCOL_MAX_PAYLOAD ||
		(length != 0U && payload == NULL) || frame_size < total) {
		return 0U;
	}
	put_u32(frame, VIRTUAL_EF800_PROTOCOL_MAGIC);
	frame[4] = VIRTUAL_EF800_PROTOCOL_VERSION;
	frame[5] = type;
	frame[6] = (uint8_t)length;
	frame[7] = (uint8_t)(length >> 8);
	put_u32(&frame[8], 0U);
	if (length != 0U) {
		memcpy(&frame[VIRTUAL_EF800_PROTOCOL_HEADER_SIZE], payload, length);
	}
	return total;
}

int virtual_ef800_protocol_decode(const uint8_t *frame, size_t frame_size,
					  struct virtual_ef800_message *message)
{
	uint16_t length;

	if (frame == NULL || message == NULL ||
		frame_size < VIRTUAL_EF800_PROTOCOL_HEADER_SIZE ||
		get_u32(frame) != VIRTUAL_EF800_PROTOCOL_MAGIC ||
		frame[4] != VIRTUAL_EF800_PROTOCOL_VERSION) {
		return -EINVAL;
	}
	length = (uint16_t)frame[6] | ((uint16_t)frame[7] << 8);
	if (length > VIRTUAL_EF800_PROTOCOL_MAX_PAYLOAD ||
		frame_size != VIRTUAL_EF800_PROTOCOL_HEADER_SIZE + length) {
		return -EMSGSIZE;
	}
	message->version = frame[4];
	message->type = frame[5];
	message->length = length;
	message->payload = &frame[VIRTUAL_EF800_PROTOCOL_HEADER_SIZE];
	return 0;
}

size_t virtual_ef800_log_encode(const struct virtual_ef800_log_record *record,
					uint8_t *payload, size_t payload_size)
{
	size_t total;

	if (record == NULL || payload == NULL ||
		record->severity > VIRTUAL_EF800_LOG_PANIC ||
		(record->flags & ~(VIRTUAL_EF800_LOG_REPLAY |
			VIRTUAL_EF800_LOG_TRUNCATED | VIRTUAL_EF800_LOG_GAP |
			VIRTUAL_EF800_LOG_EPOCH)) != 0U ||
		record->text_length > VIRTUAL_EF800_LOG_MAX_TEXT ||
		(record->text_length != 0U && record->text == NULL)) {
		return 0U;
	}
	total = VIRTUAL_EF800_LOG_PAYLOAD_HEADER_SIZE + record->text_length;
	if (payload_size < total) {
		return 0U;
	}
	payload[0] = record->severity;
	payload[1] = record->flags;
	put_u32(&payload[2], record->sequence);
	payload[6] = (uint8_t)record->text_length;
	payload[7] = (uint8_t)(record->text_length >> 8);
	if (record->text_length != 0U) {
		memcpy(&payload[VIRTUAL_EF800_LOG_PAYLOAD_HEADER_SIZE], record->text,
			record->text_length);
	}
	return total;
}

int virtual_ef800_log_decode(const uint8_t *payload, size_t payload_size,
					 struct virtual_ef800_log_record *record)
{
	uint16_t text_length;

	if (payload == NULL || record == NULL ||
		payload_size < VIRTUAL_EF800_LOG_PAYLOAD_HEADER_SIZE) {
		return -EINVAL;
	}
	text_length = (uint16_t)payload[6] | ((uint16_t)payload[7] << 8);
	if (payload[0] > VIRTUAL_EF800_LOG_PANIC ||
		(payload[1] & ~(VIRTUAL_EF800_LOG_REPLAY |
			VIRTUAL_EF800_LOG_TRUNCATED | VIRTUAL_EF800_LOG_GAP |
			VIRTUAL_EF800_LOG_EPOCH)) != 0U ||
		text_length > VIRTUAL_EF800_LOG_MAX_TEXT ||
		payload_size != VIRTUAL_EF800_LOG_PAYLOAD_HEADER_SIZE + text_length) {
		return -EINVAL;
	}
	record->severity = payload[0];
	record->flags = payload[1];
	record->sequence = get_u32(&payload[2]);
	record->text_length = text_length;
	record->text = &payload[VIRTUAL_EF800_LOG_PAYLOAD_HEADER_SIZE];
	return 0;
}
