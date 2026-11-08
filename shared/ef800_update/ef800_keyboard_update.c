/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_keyboard_update.h"

#include "ef800_update.h"

static uint32_t get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool ef800_kb_update_header_decode(const uint8_t wire[EF800_KB_UPDATE_HEADER_SIZE],
					   struct ef800_kb_update_header *header)
{
	uint32_t limit;

	if (wire == NULL || header == NULL ||
		get_le32(&wire[0]) != EF800_KB_UPDATE_MAGIC ||
		wire[5] != 0U || wire[6] != 0U || wire[7] != 0U) {
		return false;
	}
	if (wire[4] == EF800_KEYBOARD_IMAGE_APPLICATION) {
		limit = EF800_KEYBOARD_PACKAGE_LIMIT;
	} else if (wire[4] == EF800_KEYBOARD_IMAGE_BOOTLOADER) {
		limit = EF800_KEYBOARD_BOOT_LIMIT;
	} else {
		return false;
	}
	header->target = wire[4];
	header->image_size = get_le32(&wire[8]);
	header->image_crc32 = get_le32(&wire[12]);
	return header->image_size != 0U && header->image_size <= limit;
}

bool ef800_kb_update_stream_begin(struct ef800_kb_update_stream *stream,
					  const struct ef800_kb_update_header *header)
{
	uint32_t limit;

	if (stream == NULL || header == NULL) {
		return false;
	}
	if (header->target == EF800_KEYBOARD_IMAGE_APPLICATION) {
		limit = EF800_KEYBOARD_PACKAGE_LIMIT;
	} else if (header->target == EF800_KEYBOARD_IMAGE_BOOTLOADER) {
		limit = EF800_KEYBOARD_BOOT_LIMIT;
	} else {
		return false;
	}
	if (header->image_size == 0U || header->image_size > limit) {
		return false;
	}
	*stream = (struct ef800_kb_update_stream) {
		.image_size = header->image_size,
		.expected_crc32 = header->image_crc32,
		.received = 0U,
		.crc32 = 0U,
		.target = header->target,
		.active = true,
	};
	return true;
}

bool ef800_kb_update_stream_chunk(struct ef800_kb_update_stream *stream,
					  uint32_t offset, const uint8_t *data,
					  size_t length)
{
	if (stream == NULL || !stream->active || data == NULL || length == 0U ||
		stream->received > stream->image_size ||
		length > EF800_KB_UPDATE_CHUNK_MAX || offset != stream->received ||
		length > (size_t)(stream->image_size - stream->received)) {
		return false;
	}
	stream->crc32 = ef800_crc32_update(stream->crc32, data, length);
	stream->received += (uint32_t)length;
	return true;
}

bool ef800_kb_update_stream_finish(struct ef800_kb_update_stream *stream)
{
	bool valid;

	if (stream == NULL || !stream->active) {
		return false;
	}
	valid = stream->received == stream->image_size &&
		stream->crc32 == stream->expected_crc32;
	stream->active = false;
	return valid;
}
