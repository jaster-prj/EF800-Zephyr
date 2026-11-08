/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/reboot.h>

#include "ef800_keyboard_update_protocol.h"
#include "ef800_update.h"
#include "ef800_watchdog.h"

#define UPDATER_VERSION_MAJOR 1U
#define UPDATER_VERSION_MINOR 0U
#define UPDATER_VERSION_PATCH 0U
#define ERASED_PAGE_BITMAP_SIZE (128U / 8U)

#define BOOT_PARTITION_ID PARTITION_ID(keyboard_boot_partition)
#define APP_PARTITION_ID PARTITION_ID(keyboard_app_partition)

BUILD_ASSERT(PARTITION_OFFSET(keyboard_boot_partition) == 0x00000000U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_boot_partition) == 0x00008000U);
BUILD_ASSERT(PARTITION_OFFSET(keyboard_app_partition) == 0x00008000U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_app_partition) == 0x00030000U);
BUILD_ASSERT(PARTITION_OFFSET(keyboard_updater_partition) == 0x00038000U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_updater_partition) == 0x00008000U);

static const struct device *const i2c_target =
	DEVICE_DT_GET(DT_NODELABEL(i2c1));

/* The target driver callbacks only copy/request work; Flash operations run in main. */
static uint8_t request_frame[EF800_KB_UPDATE_REQUEST_MAX] __aligned(4);
static size_t request_length;
static uint32_t last_byte_ms;
static bool request_is_version;
static bool request_timed_out;
static bool request_overflow;
static bool write_transaction;
static bool read_transaction;
static uint8_t response[EF800_KB_UPDATE_RESPONSE_MAX] __aligned(4);
static size_t response_length;
static size_t response_position;
static uint8_t version_response[EF800_KB_UPDATE_VERSION_SIZE];
static atomic_t request_busy;
static atomic_t request_pending;
static atomic_t response_ready;
static atomic_t reboot_after_response;
static atomic_t reboot_pending;

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
	response_position = 0U;
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
	response_position = 0U;
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

static int target_area(enum ef800_kb_update_target target,
		       const struct flash_area **area)
{
	uint8_t partition_id;
	int result;

	if (!service_target_allowed(target)) {
		return -EACCES;
	}
	switch (target) {
	case EF800_KB_UPDATE_TARGET_BOOTLOADER:
		partition_id = BOOT_PARTITION_ID;
		break;
	case EF800_KB_UPDATE_TARGET_APPLICATION:
		partition_id = APP_PARTITION_ID;
		break;
	default:
		return -EACCES;
	}
	result = flash_area_open(partition_id, area);
	if (result != 0) {
		return result;
	}
	if (!flash_area_device_is_ready(*area)) {
		(void)flash_area_close(*area);
		return -ENODEV;
	}
	return 0;
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
	response_position = 0U;
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

static int erase_one_page(const struct flash_area *area, uint32_t relative_page)
{
	int result = flash_area_erase(area,
		relative_page * EF800_KB_UPDATE_PAGE_SIZE,
		EF800_KB_UPDATE_PAGE_SIZE);

	(void)ef800_watchdog_feed();
	return result;
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
	    target_area(target, &area) != 0) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	page_count = size / EF800_KB_UPDATE_PAGE_SIZE;
	if (payload[1] == EF800_KB_UPDATE_ERASE_PARTITION && length == 2U) {
		for (uint32_t page = 0U; page < page_count; ++page) {
			result = erase_one_page(area, page);
			if (result != 0) {
				(void)flash_area_close(area);
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
			(void)flash_area_close(area);
			response_status(EF800_KB_PROTOCOL_NACK);
			return;
		}
		result = erase_one_page(area, page - first_page);
		if (result != 0) {
			(void)flash_area_close(area);
			response_status(EF800_KB_PROTOCOL_NACK);
			return;
		}
		mark_page_erased(page);
	} else {
		(void)flash_area_close(area);
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	(void)flash_area_close(area);
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
	    target_area(target, &area) != 0) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	memset(padded_write, 0xFF, padded_length);
	memcpy(padded_write, &payload[7], data_length);
	result = flash_area_write(area, offset, padded_write, padded_length);
	(void)flash_area_close(area);
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
	    target_area(target, &area) != 0) {
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	if (flash_area_read(area, offset, flash_scratch, read_length) != 0) {
		(void)flash_area_close(area);
		response_status(EF800_KB_PROTOCOL_NACK);
		return;
	}
	put_le16(&response[1], read_length);
	memcpy(&response[3], flash_scratch, read_length);
	response[0] = EF800_KB_PROTOCOL_ACK;
	response[3U + read_length] = ef800_kb_update_checksum(response,
		3U + read_length);
	response_length = 4U + read_length;
	response_position = 0U;
	(void)flash_area_close(area);
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
	    target_area(session.target, &area) != 0) {
		return false;
	}
	while (offset < image_size) {
		size_t chunk = MIN(sizeof(flash_scratch), image_size - offset);

		if (flash_area_read(area, offset, flash_scratch, chunk) != 0) {
			(void)flash_area_close(area);
			return false;
		}
		crc = ef800_crc32_update(crc, flash_scratch, chunk);
		offset += chunk;
		(void)ef800_watchdog_feed();
	}
	if (flash_area_read(area, 0U, vector, sizeof(vector)) != 0) {
		(void)flash_area_close(area);
		return false;
	}
	vector_valid = ef800_vector_table_valid(vector, base, image_size,
		sram_address, sram_size);
	(void)flash_area_close(area);
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

static void process_request(void)
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
			atomic_set(&reboot_after_response, 1);
		}
		break;
	default:
		response_status(EF800_KB_PROTOCOL_NACK);
		break;
	}
}

static int write_requested(struct i2c_target_config *config)
{
	ARG_UNUSED(config);
	if (atomic_get(&request_busy) != 0) {
		return -EBUSY;
	}
	atomic_set(&request_busy, 1);
	atomic_set(&response_ready, 0);
	request_length = 0U;
	request_is_version = false;
	request_timed_out = false;
	request_overflow = false;
	last_byte_ms = k_uptime_get_32();
	write_transaction = true;
	read_transaction = false;
	return 0;
}

static int write_received(struct i2c_target_config *config, uint8_t byte)
{
	ARG_UNUSED(config);
	uint32_t now = k_uptime_get_32();

	if (!ef800_kb_update_frame_timing_valid(last_byte_ms, now)) {
		request_timed_out = true;
	}
	last_byte_ms = now;
	if (request_length == 0U && !request_is_version &&
	    byte == EF800_KB_UPDATE_VERSION_COMMAND) {
		request_is_version = true;
		return 0;
	}
	if (request_is_version) {
		request_overflow = true;
		return 0;
	}
	if (request_length >= sizeof(request_frame)) {
		request_overflow = true;
		return 0;
	}
	request_frame[request_length++] = byte;
	return 0;
}

static int read_requested(struct i2c_target_config *config, uint8_t *byte)
{
	ARG_UNUSED(config);
	read_transaction = true;
	write_transaction = false;
	response_position = 0U;
	if (atomic_get(&response_ready) == 0) {
		*byte = EF800_KB_PROTOCOL_BUSY;
		response_length = 1U;
		response_position = 1U;
		return 0;
	}
	if (response_length == 0U) {
		*byte = EF800_KB_PROTOCOL_NACK;
		response_length = 1U;
		response_position = 1U;
		return 0;
	}
	*byte = response[response_position++];
	return 0;
}

static int read_processed(struct i2c_target_config *config, uint8_t *byte)
{
	ARG_UNUSED(config);
	if (response_position < response_length) {
		*byte = response[response_position++];
	} else {
		*byte = 0xFFU;
	}
	return 0;
}

static int i2c_stop(struct i2c_target_config *config)
{
	ARG_UNUSED(config);
	if (write_transaction) {
		write_transaction = false;
		atomic_set(&request_pending, 1);
	} else if (read_transaction) {
		read_transaction = false;
		if (response_position >= response_length) {
			atomic_set(&response_ready, 0);
			if (atomic_cas(&reboot_after_response, 1, 0)) {
				atomic_set(&reboot_pending, 1);
			}
		}
	}
	return 0;
}

static const struct i2c_target_callbacks callbacks = {
	.write_requested = write_requested,
	.write_received = write_received,
	.read_requested = read_requested,
	.read_processed = read_processed,
	.stop = i2c_stop,
};

static struct i2c_target_config config = {
	.address = EF800_KEYBOARD_UPDATER_I2C_ADDRESS,
	.callbacks = &callbacks,
};

int main(void)
{
	if (ef800_watchdog_inherit() != 0) {
		return -EIO;
	}
	if (!device_is_ready(i2c_target) ||
	    i2c_target_register(i2c_target, &config) != 0) {
		return -ENODEV;
	}

	for (;;) {
		if (atomic_cas(&request_pending, 1, 0)) {
			process_request();
			atomic_set(&response_ready, 1);
			atomic_set(&request_busy, 0);
		}
		if (atomic_cas(&reboot_pending, 1, 0)) {
			sys_reboot(SYS_REBOOT_COLD);
		}
		(void)ef800_watchdog_feed();
		k_msleep(1U);
	}
}
