/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_keyboard_update_service.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ef800_keyboard_update_protocol.h"
#include "ef800_keyboard_i2c.h"
#include "ef800_keyboard_boot.h"
#include "ef800_update.h"

LOG_MODULE_REGISTER(ef800_keyboard_update_service, CONFIG_LOG_DEFAULT_LEVEL);

#define KEYBOARD_UPDATE_POLL_MS 10U

int ef800_keyboard_update_plan_select(uint8_t image_kind,
	struct ef800_keyboard_update_plan *plan)
{
	if (plan == NULL) {
		return -EINVAL;
	}
	switch (image_kind) {
	case EF800_KEYBOARD_IMAGE_APPLICATION:
		*plan = (struct ef800_keyboard_update_plan){
			.image_base = EF800_KEYBOARD_PACKAGE_BASE,
			.image_limit = EF800_KEYBOARD_PACKAGE_LIMIT,
			.target = EF800_KB_UPDATE_TARGET_APPLICATION,
			.service_address = EF800_KEYBOARD_UPDATER_I2C_ADDRESS,
		};
		return 0;
	case EF800_KEYBOARD_IMAGE_BOOTLOADER:
		*plan = (struct ef800_keyboard_update_plan){
			.image_base = EF800_KEYBOARD_BOOT_BASE,
			.image_limit = EF800_KEYBOARD_BOOT_LIMIT,
			.target = EF800_KB_UPDATE_TARGET_BOOTLOADER,
			.service_address = EF800_KEYBOARD_UPDATER_I2C_ADDRESS,
		};
		return 0;
	case EF800_KEYBOARD_IMAGE_UPDATER:
		*plan = (struct ef800_keyboard_update_plan){
			.image_base = EF800_KEYBOARD_UPDATER_BASE,
			.image_limit = EF800_KEYBOARD_UPDATER_LIMIT,
			.target = EF800_KB_UPDATE_TARGET_UPDATER,
			.service_address = EF800_KEYBOARD_I2C_ADDRESS,
		};
		return 0;
	default:
		return -EINVAL;
	}
}

int ef800_keyboard_update_read_package(
	struct fs_file_t *file, struct ef800_keyboard_package_header *header,
	size_t file_size)
{
	uint8_t buffer[1024];
	uint32_t crc = 0U;
	uint32_t remaining;

	if (file == NULL || header == NULL || fs_seek(file, 0, FS_SEEK_SET) != 0 ||
	    fs_read(file, header, sizeof(*header)) != sizeof(*header) ||
	    !ef800_keyboard_package_valid(header, file_size)) {
		return -EINVAL;
	}
	if (fs_seek(file, header->header_size, FS_SEEK_SET) != 0) {
		return -EIO;
	}
	remaining = header->image_size;
	while (remaining != 0U) {
		size_t count = MIN(sizeof(buffer), remaining);

		if (fs_read(file, buffer, count) != (ssize_t)count) {
			return -EIO;
		}
		crc = ef800_crc32_update(crc, buffer, count);
		remaining -= count;
	}
	return crc == header->image_crc32 ? 0 : -EBADMSG;
}

int ef800_keyboard_update_transaction_run(
	const struct ef800_keyboard_update_transaction *transaction)
{
	struct fs_dirent entry;
	struct fs_file_t file;
	struct ef800_keyboard_package_header header;
	struct ef800_keyboard_update_plan plan;
	uint8_t data[EF800_KB_UPDATE_BLOCK_MAX];
	uint8_t payload[EF800_KB_UPDATE_BLOCK_MAX + 7U];
	uint8_t response[EF800_KB_UPDATE_RESPONSE_MAX];
	uint32_t vector[2];
	uint32_t source_crc = 0U;
	int result;
	bool package_invalid = false;

	if (transaction == NULL || transaction->i2c == NULL ||
	    transaction->package_path == NULL || transaction->active == NULL ||
	    transaction->storage_mounted == NULL ||
	    transaction->usb_msd_requested == NULL ||
	    transaction->enter_resident_updater == NULL ||
	    transaction->wait_application_service == NULL ||
	    transaction->identify == NULL ||
	    transaction->enter_updater_fallback == NULL ||
	    transaction->wait_application == NULL) {
		return -EINVAL;
	}
	if (*transaction->active ||
	    !transaction->storage_mounted(transaction->context) ||
	    transaction->usb_msd_requested(transaction->context)) {
		return -EAGAIN;
	}
	result = fs_stat(transaction->package_path, &entry);
	if (result == -ENOENT) {
		return 0;
	}
	if (result != 0 || entry.type != FS_DIR_ENTRY_FILE) {
		return result != 0 ? result : -EINVAL;
	}
	fs_file_t_init(&file);
	result = fs_open(&file, transaction->package_path, FS_O_READ);
	if (result != 0) {
		return result;
	}
	*transaction->active = true;
	result = ef800_keyboard_update_read_package(&file, &header, entry.size);
	if (result != 0) {
		package_invalid = true;
		goto cleanup;
	}
	result = ef800_keyboard_update_plan_select(header.image_kind, &plan);
	if (result != 0) {
		package_invalid = true;
		goto cleanup;
	}
	if (header.image_size > plan.image_limit ||
	    fs_seek(&file, header.header_size, FS_SEEK_SET) != 0 ||
	    fs_read(&file, vector, sizeof(vector)) != sizeof(vector) ||
	    !ef800_vector_table_valid((const uint32_t *)vector, plan.image_base,
				      header.image_size, 0x20000000U, 0x0000F000U)) {
		result = -EINVAL;
		package_invalid = true;
		goto cleanup;
	}
	if (header.image_kind != EF800_KEYBOARD_IMAGE_UPDATER) {
		result = transaction->enter_resident_updater(transaction->context);
		if (result != 0) {
			LOG_ERR("keyboard update: resident updater handoff failed: %d", result);
			goto cleanup;
		}
	} else {
		result = transaction->wait_application_service(transaction->context);
		if (result != 0) {
			LOG_ERR("keyboard update: application service not ready: %d", result);
			goto cleanup;
		}
	}
	result = transaction->identify(transaction->context, plan.service_address);
	if (result != 0) {
		LOG_ERR("keyboard update: identify failed: %d", result);
		goto cleanup;
	}
	{
		uint8_t begin[9];
		uint8_t erase[2] = {(uint8_t)plan.target,
			EF800_KB_UPDATE_ERASE_PARTITION};

		begin[0] = (uint8_t)plan.target;
		ef800_keyboard_update_put_le32(&begin[1], header.image_size);
		ef800_keyboard_update_put_le32(&begin[5], header.image_crc32);
		result = ef800_keyboard_update_request(transaction->i2c,
			plan.service_address, EF800_KB_UPDATE_CMD_BEGIN, begin,
			sizeof(begin), response, 2U, 2000U);
		if (result == 0) {
			LOG_INF("keyboard update: partition ERASE started target=0x%02x",
				plan.target);
			result = ef800_keyboard_update_request(transaction->i2c,
				plan.service_address, EF800_KB_UPDATE_CMD_ERASE, erase,
				sizeof(erase), response, 2U, 30000U);
		}
		if (result != 0) {
			LOG_ERR("keyboard update: BEGIN/ERASE failed: %d", result);
			goto cleanup;
		}
	}
	for (uint32_t offset = 0U; offset < header.image_size;) {
		size_t length = MIN(sizeof(data), header.image_size - offset);

		if (fs_seek(&file, header.header_size + offset, FS_SEEK_SET) != 0 ||
		    fs_read(&file, data, length) != (ssize_t)length) {
			result = -EIO;
			goto cleanup;
		}
		source_crc = ef800_crc32_update(source_crc, data, length);
		payload[0] = (uint8_t)plan.target;
		ef800_keyboard_update_put_le32(&payload[1], offset);
		ef800_keyboard_update_put_le16(&payload[5], (uint16_t)length);
		memcpy(&payload[7], data, length);
		result = ef800_keyboard_update_request(transaction->i2c,
			plan.service_address, EF800_KB_UPDATE_CMD_WRITE_MEMORY,
			payload, length + 7U, response, 2U, 5000U);
		if (result != 0) {
			LOG_ERR("keyboard update: WRITE offset 0x%x failed: %d",
				offset, result);
			goto cleanup;
		}
		offset += length;
		if ((offset % 4096U) == 0U || offset == header.image_size) {
			LOG_INF("keyboard update: streamed %u/%u bytes", offset,
				header.image_size);
		}
	}
	if (source_crc != header.image_crc32) {
		result = -EBADMSG;
		goto cleanup;
	}
	{
		uint8_t check_target = (uint8_t)plan.target;

		LOG_INF("keyboard update: Flash CHECK started target=0x%02x",
			plan.target);
		result = ef800_keyboard_update_request(transaction->i2c,
			plan.service_address, EF800_KB_UPDATE_CMD_CHECK, &check_target,
			1U, response, 7U, 30000U);
		if (result != 0 || ef800_keyboard_update_get_le32(&response[1]) !=
		    header.image_crc32 || response[5] != 1U) {
			result = result != 0 ? result : -EBADMSG;
			LOG_ERR("keyboard update: Flash CHECK failed: %d", result);
			goto cleanup;
		}
	}
	if (header.image_kind == EF800_KEYBOARD_IMAGE_UPDATER) {
		uint8_t version[EF800_KB_UPDATE_VERSION_SIZE];

		result = transaction->enter_updater_fallback(transaction->context,
			version);
		if (result == 0 &&
		    (version[1] != header.version_major ||
		     version[2] != header.version_minor ||
		     version[3] != header.version_patch)) {
			LOG_ERR("keyboard updater version mismatch: expected %u.%u.%u, "
				"got %u.%u.%u", header.version_major,
				header.version_minor, header.version_patch,
				version[1], version[2], version[3]);
			result = -EBADMSG;
		}
	} else {
		result = ef800_keyboard_update_request(transaction->i2c,
			plan.service_address, EF800_KB_UPDATE_CMD_RUN, NULL, 0U,
			response, 2U, 2000U);
		if (result == 0) {
			result = transaction->wait_application(transaction->context,
				&header);
		}
	}
	if (result == 0) {
		result = fs_unlink(transaction->package_path);
	}
	if (result == 0) {
		LOG_INF("keyboard update: transfer and verification complete");
	}
cleanup:
	(void)fs_close(&file);
	if (package_invalid) {
		(void)fs_unlink(transaction->package_path);
	}
	*transaction->active = false;
	if (result != 0) {
		LOG_ERR("keyboard update: failed: %d", result);
	}
	return result;
}

int ef800_keyboard_update_request(const struct device *i2c, uint16_t address,
	uint8_t command, const uint8_t *payload, size_t payload_length,
	uint8_t *response, size_t response_length, uint32_t timeout_ms)
{
	uint8_t frame[EF800_KB_UPDATE_REQUEST_MAX];
	size_t frame_length;
	int64_t deadline;
	int result;

	if (i2c == NULL || !device_is_ready(i2c) || response == NULL ||
	    response_length == 0U) {
		return -ENODEV;
	}
	frame_length = ef800_kb_update_frame_encode(command, payload,
		payload_length, frame, sizeof(frame));
	if (frame_length == 0U) {
		return -EINVAL;
	}
	result = i2c_write(i2c, frame, frame_length, address);
	if (result != 0) {
		return result;
	}
	deadline = k_uptime_get() + timeout_ms;
	do {
		memset(response, 0xFF, response_length);
		result = i2c_read(i2c, response, response_length, address);
		if (result == 0) {
			if (response[0] == EF800_KB_PROTOCOL_BUSY) {
				k_msleep(KEYBOARD_UPDATE_POLL_MS);
				continue;
			}
			if (response[0] == EF800_KB_PROTOCOL_NACK) {
				return -EPROTO;
			}
			return ef800_kb_update_response_valid(response, response_length) ?
				0 : -EBADMSG;
		}
		k_msleep(KEYBOARD_UPDATE_POLL_MS);
	} while (k_uptime_get() < deadline);
	return result != 0 ? result : -ETIMEDOUT;
}

int ef800_keyboard_update_query_version(const struct device *i2c,
	uint16_t address, uint8_t *version, uint32_t timeout_ms)
{
	uint8_t command = EF800_KB_UPDATE_VERSION_COMMAND;
	int64_t deadline;
	int result = -EIO;

	if (i2c == NULL || !device_is_ready(i2c) || version == NULL) {
		return -ENODEV;
	}
	deadline = k_uptime_get() + timeout_ms;
	/* Retry both phases because target registration can lag reset release. */
	do {
		result = i2c_write(i2c, &command, sizeof(command), address);
		if (result == 0) {
			break;
		}
		k_msleep(KEYBOARD_UPDATE_POLL_MS);
	} while (k_uptime_get() < deadline);
	if (result != 0) {
		return result;
	}
	do {
		result = i2c_read(i2c, version,
			EF800_KB_UPDATE_VERSION_SIZE, address);
		if (result == 0) {
			if (version[0] == EF800_KB_PROTOCOL_BUSY) {
				k_msleep(KEYBOARD_UPDATE_POLL_MS);
				continue;
			}
			return version[0] == command &&
				version[4] == EF800_KB_UPDATE_VERSION_ACK ? 0 : -EBADMSG;
		}
		k_msleep(KEYBOARD_UPDATE_POLL_MS);
	} while (k_uptime_get() < deadline);
	return result != 0 ? result : -ETIMEDOUT;
}

uint32_t ef800_keyboard_update_get_le32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

void ef800_keyboard_update_put_le16(uint8_t *data, uint16_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
}

void ef800_keyboard_update_put_le32(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}
