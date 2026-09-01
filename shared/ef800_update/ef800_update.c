/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_update.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>

BUILD_ASSERT(sizeof(struct ef800_boot_metadata) == EF800_BOOT_METADATA_SIZE,
	     "EF800 boot metadata layout changed");

static uint8_t assignment_byte(uint64_t assignments, uint8_t port_id)
{
	return port_id >= 1U && port_id <= 5U ?
		(uint8_t)((assignments >> ((port_id - 1U) * 8U)) & 0xffU) : 0U;
}

static uint8_t assignment_builder(uint64_t assignments, uint8_t port_id)
{
	return assignment_byte(assignments, port_id) & 0x0fU;
}

static uint8_t assignment_connection(uint64_t assignments, uint8_t port_id)
{
	return (assignment_byte(assignments, port_id) >> 4) & 0x0fU;
}

static bool assignment_record_valid(uint8_t port_id, uint8_t builder_id,
					    uint8_t connection)
{
	if (port_id < 1U || port_id > 5U || builder_id > EF800_PORT_BUILDER_BLUENRG ||
		connection > EF800_PORT_CONNECTION_SPI) {
		return false;
	}
	if (builder_id == EF800_PORT_BUILDER_NONE) {
		return port_id != 5U && connection == EF800_PORT_CONNECTION_NONE;
	}

	switch (port_id) {
	case 1U:
		if (connection != EF800_PORT_CONNECTION_SERIAL) {
			return false;
		}
		break;
	case 2U:
		if (connection != EF800_PORT_CONNECTION_SERIAL &&
			connection != EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL) {
			return false;
		}
		break;
	case 3U:
		if (connection != EF800_PORT_CONNECTION_SERIAL &&
			connection != EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL &&
			connection != EF800_PORT_CONNECTION_PARALLEL &&
			connection != EF800_PORT_CONNECTION_I2C &&
			connection != EF800_PORT_CONNECTION_SPI) {
			return false;
		}
		break;
	case 4U:
		if (connection != EF800_PORT_CONNECTION_USB_CDC &&
			connection != EF800_PORT_CONNECTION_USB_MSD) {
			return false;
		}
		break;
	case 5U:
		if (builder_id != EF800_PORT_BUILDER_BLUENRG ||
			connection != EF800_PORT_CONNECTION_SPI) {
			return false;
		}
		break;
	default:
		return false;
	}

	if (builder_id == EF800_PORT_BUILDER_MSD &&
		(port_id != 4U || connection != EF800_PORT_CONNECTION_USB_MSD)) {
		return false;
	}
	if (builder_id == EF800_PORT_BUILDER_BLUENRG && port_id != 5U) {
		return false;
	}
	if (builder_id == EF800_PORT_BUILDER_BLUENRG &&
		connection != EF800_PORT_CONNECTION_SPI) {
		return false;
	}
	if ((builder_id == EF800_PORT_BUILDER_ELTA3 ||
	     builder_id == EF800_PORT_BUILDER_FTS3XX) && port_id >= 4U) {
		return false;
	}
	if ((builder_id == EF800_PORT_BUILDER_ELTA3 ||
	     builder_id == EF800_PORT_BUILDER_FTS3XX) &&
		connection != EF800_PORT_CONNECTION_SERIAL &&
		connection != EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL) {
		return false;
	}
	if (builder_id == EF800_PORT_BUILDER_DEBUG && port_id == 4U &&
		connection != EF800_PORT_CONNECTION_USB_CDC) {
		return false;
	}
	if (builder_id != EF800_PORT_BUILDER_DEBUG && port_id == 4U &&
		connection != EF800_PORT_CONNECTION_USB_MSD) {
		return false;
	}

	return true;
}

static int assignment_set_record(uint64_t *assignments, uint8_t port_id,
					 uint8_t builder_id, uint8_t connection)
{
	uint64_t mask;
	uint8_t value;

	if (assignments == NULL || !assignment_record_valid(port_id, builder_id,
							 connection)) {
		return -EINVAL;
	}
	mask = 0xffULL << ((port_id - 1U) * 8U);
	value = (uint8_t)(builder_id | (connection << 4));
	*assignments = (*assignments & ~mask) |
		((uint64_t)value << ((port_id - 1U) * 8U));
	return 0;
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
	sys_put_le32(metadata->console_target, &buffer[20]);
	sys_put_le64(metadata->port_assignments, &buffer[24]);
	sys_put_le32(metadata->reserved, &buffer[36]);
}

int ef800_boot_metadata_encode(const struct ef800_boot_metadata *metadata,
					       uint8_t *buffer, size_t capacity)
{
	if (metadata == NULL || buffer == NULL || capacity < EF800_BOOT_METADATA_SIZE) {
		return -EINVAL;
	}
	memset(buffer, 0, EF800_BOOT_METADATA_SIZE);
	metadata_encode_fields(metadata, buffer);
	sys_put_le32(metadata->crc32, &buffer[32]);
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
	metadata->console_target = sys_get_le32(&buffer[20]);
	metadata->port_assignments = sys_get_le64(&buffer[24]);
	metadata->crc32 = sys_get_le32(&buffer[32]);
	metadata->reserved = sys_get_le32(&buffer[36]);
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
	return ef800_crc32(buffer, 32U);
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
	metadata->console_target = EF800_CONSOLE_TARGET_NONE;
	metadata->port_assignments = 0U;
	metadata->reserved = 0U;
	(void)assignment_set_record(&metadata->port_assignments, 5U,
					EF800_PORT_BUILDER_BLUENRG,
					EF800_PORT_CONNECTION_SPI);
	metadata->crc32 = ef800_boot_metadata_crc(metadata);
}

bool ef800_boot_metadata_valid(const struct ef800_boot_metadata *metadata)
{
	if (metadata == NULL || metadata->magic != EF800_BOOT_METADATA_MAGIC ||
		metadata->version != EF800_BOOT_METADATA_VERSION ||
		metadata->size != EF800_BOOT_METADATA_SIZE ||
		metadata->reserved != 0U ||
		metadata->last_target > EF800_BOOT_TARGET_BACKUP) {
		return false;
	}

	if (metadata->console_target != EF800_CONSOLE_TARGET_NONE &&
		!ef800_console_target_valid(metadata->console_target)) {
		return false;
	}
	if (metadata->console_target != EF800_CONSOLE_TARGET_NONE &&
		ef800_port_assignment_builder(metadata->port_assignments,
										 ef800_console_target_port(
											 metadata->console_target)) !=
			EF800_PORT_BUILDER_DEBUG) {
		return false;
	}
	if (!ef800_port_assignments_valid(metadata->port_assignments)) {
		return false;
	}
	return metadata->crc32 == ef800_boot_metadata_crc(metadata);
}

uint32_t ef800_console_target_encode(uint8_t port_id, uint8_t builder_id)
{
	return (uint32_t)port_id | ((uint32_t)builder_id << 8);
}

uint8_t ef800_console_target_port(uint32_t target)
{
	return (uint8_t)(target & 0xffU);
}

uint8_t ef800_console_target_builder(uint32_t target)
{
	return (uint8_t)((target >> 8) & 0xffU);
}

bool ef800_console_target_valid(uint32_t target)
{
	uint8_t port_id = ef800_console_target_port(target);
	uint8_t builder_id = ef800_console_target_builder(target);

	return port_id != 0U && builder_id == EF800_PORT_BUILDER_DEBUG &&
		port_id <= 4U && (target >> 16) == 0U;
}

bool ef800_port_assignments_valid(uint64_t assignments)
{
	for (uint8_t port_id = 1U; port_id <= 5U; ++port_id) {
		if (!assignment_record_valid(port_id,
					     assignment_builder(assignments, port_id),
					     assignment_connection(assignments, port_id))) {
			return false;
		}
	}
	return true;
}

uint8_t ef800_console_port_select(const struct ef800_boot_metadata *metadata,
					  bool usb_debug_available)
{
	uint8_t port_id;

	if (metadata == NULL) {
		return EF800_CONSOLE_SAFE_PORT;
	}
	if (metadata->console_target != EF800_CONSOLE_TARGET_NONE &&
		ef800_console_target_valid(metadata->console_target)) {
		port_id = ef800_console_target_port(metadata->console_target);
		if (port_id != 4U || usb_debug_available) {
			return port_id;
		}
	}
	if (ef800_port_assignment_builder(metadata->port_assignments,
													  EF800_CONSOLE_FALLBACK_PORT) ==
		EF800_PORT_BUILDER_NONE) {
		return EF800_CONSOLE_FALLBACK_PORT;
	}
	return EF800_CONSOLE_SAFE_PORT;
}

int ef800_boot_metadata_set_console(struct ef800_boot_metadata *metadata,
					    uint8_t debug_port)
{
	if (metadata == NULL ||
		(debug_port != 0U && !ef800_console_target_valid(
			ef800_console_target_encode(debug_port,
							    EF800_PORT_BUILDER_DEBUG)))) {
		return -EINVAL;
	}
	metadata->console_target = debug_port == 0U ? EF800_CONSOLE_TARGET_NONE :
		ef800_console_target_encode(debug_port, EF800_PORT_BUILDER_DEBUG);
	return 0;
}

uint8_t ef800_port_assignment_builder(uint64_t assignments, uint8_t port_id)
{
	return assignment_builder(assignments, port_id);
}

uint8_t ef800_port_assignment_connection(uint64_t assignments, uint8_t port_id)
{
	return assignment_connection(assignments, port_id);
}

int ef800_port_assignment_set_record(uint64_t *assignments, uint8_t port_id,
					     uint8_t builder_id, uint8_t connection)
{
	return assignment_set_record(assignments, port_id, builder_id, connection);
}

int ef800_port_assignment_set_builder(uint64_t *assignments, uint8_t port_id,
					      uint8_t builder_id)
{
	uint8_t connection = EF800_PORT_CONNECTION_NONE;

	if (builder_id != EF800_PORT_BUILDER_NONE) {
		connection = port_id == 4U ?
			(builder_id == EF800_PORT_BUILDER_MSD ?
			 EF800_PORT_CONNECTION_USB_MSD : EF800_PORT_CONNECTION_USB_CDC) :
			(port_id == 5U ? EF800_PORT_CONNECTION_SPI :
			 (port_id == 2U && builder_id == EF800_PORT_BUILDER_DEBUG ?
			  EF800_PORT_CONNECTION_SERIAL_FLOW_CONTROL :
			  EF800_PORT_CONNECTION_SERIAL));
	}
	return assignment_set_record(assignments, port_id, builder_id, connection);
}
