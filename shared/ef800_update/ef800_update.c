/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_update.h"
#include "ef800_keyboard_package.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>

BUILD_ASSERT(sizeof(struct ef800_boot_metadata) == EF800_BOOT_METADATA_SIZE,
	     "EF800 boot metadata layout changed");

bool ef800_port_builder_connection_valid(uint8_t builder_id,
								 uint8_t connection)
{
	switch (builder_id) {
	case EF800_PORT_BUILDER_DEBUG:
		return connection == EF800_PORT_CONNECTION_SERIAL ||
			connection == EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL ||
			connection == EF800_PORT_CONNECTION_USB_CDC;
	case EF800_PORT_BUILDER_ELTA3:
	case EF800_PORT_BUILDER_FTS3XX:
		return false;
	case EF800_PORT_BUILDER_MSD:
		return connection == EF800_PORT_CONNECTION_USB_MSD;
	case EF800_PORT_BUILDER_MTP:
		return connection == EF800_PORT_CONNECTION_USB_MTP;
	case EF800_PORT_BUILDER_BLUENRG:
		return connection == EF800_PORT_CONNECTION_SPI;
	default:
		return false;
	}
}

bool ef800_port_builder_allowed_on_port(uint8_t port_id, uint8_t builder_id)
{
	if (port_id < 1U || port_id > 5U || builder_id > EF800_PORT_BUILDER_MTP) {
		return false;
	}
	if (builder_id == EF800_PORT_BUILDER_NONE) {
		return true;
	}
	if (builder_id == EF800_PORT_BUILDER_MSD) {
		return port_id == 4U;
	}
	if (builder_id == EF800_PORT_BUILDER_MTP) {
		return port_id == 4U;
	}
	if (builder_id == EF800_PORT_BUILDER_BLUENRG) {
		return port_id == 5U;
	}
	if (builder_id == EF800_PORT_BUILDER_DEBUG) {
		return port_id <= 4U;
	}
	if (builder_id == EF800_PORT_BUILDER_ELTA3 ||
	    builder_id == EF800_PORT_BUILDER_FTS3XX) return false;
	return port_id <= 3U;
}

bool ef800_port_assignment_record_valid(uint8_t port_id, uint8_t builder_id,
								uint8_t connection)
{
	if (port_id < 1U || port_id > 5U ||
		builder_id > EF800_PORT_BUILDER_MTP ||
		connection > EF800_PORT_CONNECTION_USB_MTP) {
		return false;
	}
	if (!ef800_port_builder_allowed_on_port(port_id, builder_id)) {
		return false;
	}
	if (builder_id == EF800_PORT_BUILDER_NONE) {
		return port_id != 5U && connection == EF800_PORT_CONNECTION_NONE;
	}
	if (!ef800_port_builder_connection_valid(builder_id, connection)) {
		return false;
	}

	switch (port_id) {
	case 1U:
		return connection == EF800_PORT_CONNECTION_SERIAL &&
			builder_id == EF800_PORT_BUILDER_DEBUG;
	case 2U:
		return connection == EF800_PORT_CONNECTION_SERIAL ||
			connection == EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL;
	case 3U:
		return connection == EF800_PORT_CONNECTION_SERIAL ||
			connection == EF800_PORT_CONNECTION_PARALLEL ||
			connection == EF800_PORT_CONNECTION_I2C ||
			connection == EF800_PORT_CONNECTION_SPI;
	case 4U:
		return (builder_id == EF800_PORT_BUILDER_DEBUG &&
			connection == EF800_PORT_CONNECTION_USB_CDC) ||
			(builder_id == EF800_PORT_BUILDER_MSD &&
			 connection == EF800_PORT_CONNECTION_USB_MSD) ||
			(builder_id == EF800_PORT_BUILDER_MTP &&
			 connection == EF800_PORT_CONNECTION_USB_MTP);
	case 5U:
		return builder_id == EF800_PORT_BUILDER_BLUENRG &&
			connection == EF800_PORT_CONNECTION_SPI;
	default:
		return false;
	}
}

static void metadata_encode_fields(const struct ef800_boot_metadata *metadata,
						   uint8_t *buffer)
{
	sys_put_le32(metadata->magic, &buffer[0]);
	sys_put_le16(metadata->version, &buffer[4]);
	sys_put_le16(metadata->size, &buffer[6]);
	sys_put_le32(metadata->main_failures, &buffer[8]);
	sys_put_le32(metadata->last_target, &buffer[12]);
	sys_put_le32(metadata->sequence, &buffer[16]);
}

int ef800_boot_metadata_encode(const struct ef800_boot_metadata *metadata,
					       uint8_t *buffer, size_t capacity)
{
	if (metadata == NULL || buffer == NULL || capacity < EF800_BOOT_METADATA_SIZE) {
		return -EINVAL;
	}
	memset(buffer, 0, EF800_BOOT_METADATA_SIZE);
	metadata_encode_fields(metadata, buffer);
	sys_put_le32(metadata->crc32, &buffer[20]);
	return 0;
}

int ef800_boot_metadata_decode(const uint8_t *buffer, size_t length,
					       struct ef800_boot_metadata *metadata)
{
	if (buffer == NULL || metadata == NULL ||
		length != EF800_BOOT_METADATA_SIZE) {
		return -EINVAL;
	}
	metadata->magic = sys_get_le32(&buffer[0]);
	metadata->version = sys_get_le16(&buffer[4]);
	metadata->size = sys_get_le16(&buffer[6]);
	metadata->main_failures = sys_get_le32(&buffer[8]);
	metadata->last_target = sys_get_le32(&buffer[12]);
	metadata->sequence = sys_get_le32(&buffer[16]);
	metadata->crc32 = sys_get_le32(&buffer[20]);
	return 0;
}

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

bool ef800_keyboard_package_valid(
	const struct ef800_keyboard_package_header *header, size_t file_size)
{
	return header != NULL && header->magic == EF800_KEYBOARD_PACKAGE_MAGIC &&
		header->format == EF800_KEYBOARD_PACKAGE_FORMAT &&
		header->header_size == EF800_KEYBOARD_PACKAGE_HEADER_SIZE &&
		header->target_id == EF800_KEYBOARD_PACKAGE_TARGET &&
		header->image_base == EF800_KEYBOARD_PACKAGE_BASE &&
		header->image_size != 0U &&
		header->image_size <= EF800_KEYBOARD_PACKAGE_LIMIT &&
		file_size == (size_t)header->header_size + header->image_size;
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
	uint8_t buffer[EF800_BOOT_METADATA_SIZE];

	if (metadata == NULL) {
		return 0U;
	}
	metadata_encode_fields(metadata, buffer);
	return ef800_crc32(buffer, 20U);
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
	metadata->crc32 = ef800_boot_metadata_crc(metadata);
}

bool ef800_boot_metadata_valid(const struct ef800_boot_metadata *metadata)
{
	if (metadata == NULL || metadata->magic != EF800_BOOT_METADATA_MAGIC ||
		metadata->version != EF800_BOOT_METADATA_VERSION ||
		metadata->size != EF800_BOOT_METADATA_SIZE ||
		metadata->last_target > EF800_BOOT_TARGET_BACKUP) {
		return false;
	}
	return metadata->crc32 == ef800_boot_metadata_crc(metadata);
}

int ef800_boot_metadata_migrate_v1(const uint8_t *buffer, size_t length,
	struct ef800_boot_metadata *metadata)
{
	uint32_t magic;
	uint16_t version;

	if (buffer == NULL || metadata == NULL || length != 40U) {
		return -EINVAL;
	}
	magic = sys_get_le32(&buffer[0]);
	version = sys_get_le16(&buffer[4]);
	if (magic != EF800_BOOT_METADATA_MAGIC ||
		version != EF800_BOOT_METADATA_LEGACY_VERSION) {
		return -EINVAL;
	}
	ef800_boot_metadata_init(metadata);
	metadata->main_failures = sys_get_le32(&buffer[8]);
	metadata->last_target = sys_get_le32(&buffer[12]);
	metadata->sequence = sys_get_le32(&buffer[16]);
	if (metadata->last_target > EF800_BOOT_TARGET_BACKUP) {
		return -EINVAL;
	}
	metadata->crc32 = ef800_boot_metadata_crc(metadata);
	return 0;
}

int ef800_boot_metadata_migrate_v2(const uint8_t *buffer, size_t length,
	struct ef800_boot_metadata *metadata)
{
	uint32_t magic;
	uint16_t version;
	uint16_t size;
	uint32_t failures;
	uint32_t last_target;
	uint32_t sequence;
	uint32_t stored_crc;

	if (buffer == NULL || metadata == NULL ||
		length != EF800_BOOT_METADATA_V2_SIZE) {
		return -EINVAL;
	}
	magic = sys_get_le32(&buffer[0]);
	version = sys_get_le16(&buffer[4]);
	size = sys_get_le16(&buffer[6]);
	failures = sys_get_le32(&buffer[8]);
	last_target = sys_get_le32(&buffer[12]);
	sequence = sys_get_le32(&buffer[16]);
	stored_crc = sys_get_le32(&buffer[72]);
	if (magic != EF800_BOOT_METADATA_MAGIC ||
		version != EF800_BOOT_METADATA_V2_VERSION ||
		size != EF800_BOOT_METADATA_V2_SIZE ||
		last_target > EF800_BOOT_TARGET_BACKUP ||
		sys_get_le32(&buffer[76]) != 0U ||
		stored_crc != ef800_crc32(buffer, 72U)) {
		return -EBADMSG;
	}
	ef800_boot_metadata_init(metadata);
	metadata->main_failures = failures;
	metadata->last_target = last_target;
	metadata->sequence = sequence;
	metadata->crc32 = ef800_boot_metadata_crc(metadata);
	return 0;
}
