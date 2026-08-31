/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_update.h"

#include <stddef.h>

static bool range_contains(uintptr_t address, size_t length,
			   uintptr_t range_start, size_t range_size)
{
	uintptr_t range_end;

	if (range_size > (UINTPTR_MAX - range_start)) {
		return false;
	}
	if (length > (UINTPTR_MAX - address)) {
		return false;
	}

	range_end = range_start + range_size;
	return address >= range_start && address <= range_end &&
		(address + length) <= range_end;
}

bool ef800_vector_table_valid(const uint32_t vector[2], uintptr_t image_address,
				      size_t image_size, uintptr_t sram_address,
				      size_t sram_size)
{
	uintptr_t stack_pointer;
	uintptr_t reset_handler;

	if (vector == NULL || image_size < (2U * sizeof(uint32_t))) {
		return false;
	}

	stack_pointer = (uintptr_t)vector[0];
	reset_handler = (uintptr_t)(vector[1] & ~1U);

	/* Cortex-M requires an aligned stack and a Thumb reset handler. */
	if ((stack_pointer & 0x7U) != 0U || (vector[1] & 1U) == 0U) {
		return false;
	}
	if (image_address > (UINTPTR_MAX - (2U * sizeof(uint32_t))) ||
		reset_handler < (image_address + (2U * sizeof(uint32_t)))) {
		return false;
	}
	if (!range_contains(stack_pointer, 0U, sram_address, sram_size)) {
		return false;
	}
	if (!range_contains(reset_handler, 1U, image_address, image_size)) {
		return false;
	}

	return true;
}

bool ef800_update_header_valid(const struct ef800_update_header *header,
				       size_t file_size, uintptr_t image_address,
				       size_t image_partition_size)
{
	size_t payload_end;

	if (header == NULL || header->magic != EF800_UPDATE_MAGIC ||
		header->format_version != EF800_UPDATE_FORMAT_VERSION ||
		header->header_size < sizeof(*header) ||
		header->header_size > file_size || header->image_size == 0U ||
		header->image_size > image_partition_size ||
		header->load_address != (uint32_t)image_address) {
		return false;
	}

	payload_end = (size_t)header->header_size;
	if (header->image_size > file_size - payload_end) {
		return false;
	}

	payload_end += header->image_size;
	return payload_end <= file_size;
}

uint32_t ef800_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
	static const uint32_t table[16] = {
		0x00000000U, 0x1db71064U, 0x3b6e20c8U, 0x26d930acU,
		0x76dc4190U, 0x6b6b51f4U, 0x4db26158U, 0x5005713cU,
		0xedb88320U, 0xf00f9344U, 0xd6d6a3e8U, 0xcb61b38cU,
		0x9b64c2b0U, 0x86d3d2d4U, 0xa00ae278U, 0xbdbdf21cU,
	};

	if (data == NULL && length != 0U) {
		return 0U;
	}

	crc = ~crc;
	for (size_t index = 0U; index < length; index++) {
		uint8_t byte = data[index];

		crc = (crc >> 4) ^ table[(crc ^ byte) & 0x0fU];
		crc = (crc >> 4) ^ table[(crc ^ (byte >> 4)) & 0x0fU];
	}

	return ~crc;
}

uint32_t ef800_crc32(const uint8_t *data, size_t length)
{
	return ef800_crc32_update(0U, data, length);
}

uint32_t ef800_boot_metadata_crc(const struct ef800_boot_metadata *metadata)
{
	if (metadata == NULL) {
		return 0U;
	}

	return ef800_crc32((const uint8_t *)metadata,
			   offsetof(struct ef800_boot_metadata, crc32));
}

void ef800_boot_metadata_init(struct ef800_boot_metadata *metadata)
{
	if (metadata == NULL) {
		return;
	}

	metadata->magic = EF800_BOOT_METADATA_MAGIC;
	metadata->version = EF800_BOOT_METADATA_VERSION;
	metadata->size = sizeof(*metadata);
	metadata->main_failures = 0U;
	metadata->last_target = EF800_BOOT_TARGET_MAIN;
	metadata->sequence = 0U;
	metadata->reserved0 = 0U;
	metadata->reserved1 = 0U;
	metadata->crc32 = ef800_boot_metadata_crc(metadata);
}

bool ef800_boot_metadata_valid(const struct ef800_boot_metadata *metadata)
{
	if (metadata == NULL || metadata->magic != EF800_BOOT_METADATA_MAGIC ||
		metadata->version != EF800_BOOT_METADATA_VERSION ||
		metadata->size != sizeof(*metadata) ||
		metadata->last_target > EF800_BOOT_TARGET_BACKUP) {
		return false;
	}

	return metadata->crc32 == ef800_boot_metadata_crc(metadata);
}
