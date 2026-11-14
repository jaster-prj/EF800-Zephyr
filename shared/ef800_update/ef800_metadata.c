/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_metadata.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "ef800_qspi_layout.h"

LOG_MODULE_REGISTER(ef800_metadata, LOG_LEVEL_WRN);

static struct nvs_fs metadata_nvs;
static const struct flash_area *metadata_area;
static bool metadata_nvs_ready;

static int metadata_reset_record(const struct flash_area *area,
				struct ef800_boot_metadata *metadata, bool *valid);

static int metadata_nvs_mount(const struct flash_area *area)
{
	struct flash_pages_info page_info;
	bool legacy_layout;
	int ret;

	if (area == NULL || !flash_area_device_is_ready(area)) {
		return -ENODEV;
	}
	if (metadata_nvs_ready) {
		if (metadata_area != area) {
			return -EINVAL;
		}
		if (metadata_nvs.ready) {
			return 0;
		}
		/* NVS may have been cleared by recovery or test setup. */
		metadata_nvs_ready = false;
	}
	ret = ef800_qspi_layout_check_legacy_area(area, &legacy_layout);
	if (ret != 0) {
		return ret;
	}
	if (legacy_layout) {
		/* cppcheck-suppress knownConditionTrueFalse -- Kconfig controls LOG_* expansion */
		LOG_WRN("legacy monolithic QSPI FAT detected; boot-state NVS left untouched");
		return -ENOTSUP;
	}

	ret = flash_get_page_info_by_offs(area->fa_dev, area->fa_off, &page_info);
	if (ret != 0 || page_info.size == 0U ||
		(area->fa_size % page_info.size) != 0U) {
		return ret != 0 ? ret : -EINVAL;
	}
	metadata_nvs.flash_device = area->fa_dev;
	metadata_nvs.offset = area->fa_off;
	metadata_nvs.sector_size = page_info.size;
	metadata_nvs.sector_count = area->fa_size / page_info.size;
	if (metadata_nvs.sector_count < 2U) {
		return -EINVAL;
	}

	ret = nvs_mount(&metadata_nvs);
	if (ret == 0) {
		metadata_area = area;
		metadata_nvs_ready = true;
	}
	return ret;
}

int ef800_metadata_read(const struct flash_area *area,
				struct ef800_boot_metadata *metadata, bool *valid)
{
	int ret;
	uint8_t buffer[EF800_BOOT_METADATA_V2_SIZE];

	if (area == NULL || metadata == NULL || valid == NULL) {
		return -EINVAL;
	}
	ef800_boot_metadata_init(metadata);
	*valid = false;
	ret = metadata_nvs_mount(area);
	if (ret != 0) {
		return ret;
	}
	ret = nvs_read(&metadata_nvs, EF800_METADATA_NVS_ID, buffer,
			       sizeof(buffer));
	if (ret == EF800_BOOT_METADATA_SIZE) {
		ret = ef800_boot_metadata_decode(buffer, EF800_BOOT_METADATA_SIZE,
						  metadata);
		*valid = ret == 0 && ef800_boot_metadata_valid(metadata);
		if (*valid) {
			return 0;
		}
		/* A malformed record is replaced in-place; do not clear NVS or FAT. */
		return metadata_reset_record(area, metadata, valid);
	} else if (ret == EF800_BOOT_METADATA_V2_SIZE || ret == 40) {
		ret = ret == EF800_BOOT_METADATA_V2_SIZE ?
			ef800_boot_metadata_migrate_v2(buffer, (size_t)ret, metadata) :
			ef800_boot_metadata_migrate_v1(buffer, 40U, metadata);
		*valid = ret == 0 && ef800_boot_metadata_valid(metadata);
		if (*valid) {
			ret = ef800_metadata_write(area, metadata);
			if (ret != 0) {
				*valid = false;
				return ret;
			}
			return EF800_METADATA_MIGRATED;
		}
		return metadata_reset_record(area, metadata, valid);
	}
	if (ret == -ENOENT || ret >= 0) {
		return metadata_reset_record(area, metadata, valid);
	}
	return ret;
}

static int metadata_reset_record(const struct flash_area *area,
				struct ef800_boot_metadata *metadata, bool *valid)
{
	uint8_t buffer[EF800_BOOT_METADATA_SIZE];
	int ret;

	ef800_boot_metadata_init(metadata);
	ret = ef800_boot_metadata_encode(metadata, buffer, sizeof(buffer));
	if (ret == 0) {
		ret = nvs_write(&metadata_nvs, EF800_METADATA_NVS_ID, buffer,
				 sizeof(buffer));
		if (ret >= 0) {
			uint8_t verify_buffer[EF800_BOOT_METADATA_SIZE];

			ret = nvs_read(&metadata_nvs, EF800_METADATA_NVS_ID,
					verify_buffer, sizeof(verify_buffer));
			if (ret == sizeof(verify_buffer) &&
				memcmp(verify_buffer, buffer, sizeof(buffer)) == 0) {
				ret = 0;
			} else {
				ret = -EIO;
			}
		}
	}
	*valid = ret == 0;
	if (ret == 0) {
		/* cppcheck-suppress knownConditionTrueFalse -- Kconfig controls LOG_* expansion */
		LOG_WRN("invalid or missing boot-state record reset to defaults");
	}
	return ret == 0 ? EF800_METADATA_RESET : ret;
}

int ef800_metadata_write(const struct flash_area *area,
				struct ef800_boot_metadata *metadata)
{
	uint8_t buffer[EF800_BOOT_METADATA_SIZE];
	uint8_t verify_buffer[EF800_BOOT_METADATA_SIZE];
	int ret;

	if (area == NULL || metadata == NULL) {
		return -EINVAL;
	}
	ret = metadata_nvs_mount(area);
	if (ret != 0) {
		return ret;
	}
	metadata->size = sizeof(*metadata);
	metadata->sequence++;
	metadata->crc32 = ef800_boot_metadata_crc(metadata);
	ret = ef800_boot_metadata_encode(metadata, buffer, sizeof(buffer));
	if (ret != 0) {
		return ret;
	}
	ret = nvs_write(&metadata_nvs, EF800_METADATA_NVS_ID, buffer,
			       sizeof(buffer));
	if (ret < 0) {
		return ret;
	}
	ret = nvs_read(&metadata_nvs, EF800_METADATA_NVS_ID, verify_buffer,
			       sizeof(verify_buffer));
	return ret == sizeof(verify_buffer) &&
		memcmp(verify_buffer, buffer, sizeof(buffer)) == 0 ? 0 : -EIO;
}

int ef800_metadata_read_current(struct ef800_boot_metadata *metadata,
					bool *valid)
{
	const struct flash_area *area;
	int ret;

	if (metadata == NULL || valid == NULL) {
		return -EINVAL;
	}
	ef800_boot_metadata_init(metadata);
	*valid = false;
	ret = flash_area_open(PARTITION_ID(metadata_partition), &area);
	if (ret != 0) {
		return ret;
	}
	return ef800_metadata_read(area, metadata, valid);
}

int ef800_metadata_write_current(struct ef800_boot_metadata *metadata)
{
	const struct flash_area *area;
	int ret;

	if (metadata == NULL) {
		return -EINVAL;
	}
	ret = flash_area_open(PARTITION_ID(metadata_partition), &area);
	if (ret != 0) {
		return ret;
	}
	return ef800_metadata_write(area, metadata);
}

static int journal_valid(const struct ef800_firmware_journal *journal)
{
	uint8_t seen = 0U;

	if (journal == NULL ||
	    journal->target_count > EF800_FIRMWARE_JOURNAL_MAX_TARGETS) {
		return -EINVAL;
	}
	for (size_t index = 0U; index < journal->target_count; index++) {
		const struct ef800_firmware_journal_target *target =
			&journal->targets[index];
		uint16_t id = (uint16_t)target->target_id;

		if (id < EF800_FIRMWARE_TARGET_BOOTLOADER ||
		    id > EF800_FIRMWARE_TARGET_KEYBOARD_APPLICATION ||
		    target->phase > EF800_FIRMWARE_JOURNAL_UNSUPPORTED ||
		    target->result > EF800_FIRMWARE_JOURNAL_RESULT_UNSUPPORTED) {
			return -EINVAL;
		}
		uint8_t bit = (uint8_t)(1U << (id - 1U));
		if ((seen & bit) != 0U) {
			return -EEXIST;
		}
		seen |= bit;
	}
	return 0;
}

int ef800_firmware_journal_encode(
	const struct ef800_firmware_journal *journal,
	uint8_t *buffer, size_t capacity)
{
	size_t length;
	int ret;

	if (buffer == NULL) {
		return -EINVAL;
	}
	ret = journal_valid(journal);
	if (ret != 0) {
		return ret;
	}
	length = EF800_FIRMWARE_JOURNAL_HEADER_SIZE +
		 (size_t)journal->target_count * EF800_FIRMWARE_JOURNAL_TARGET_SIZE;
	if (capacity < length) {
		return -ENOSPC;
	}
	memset(buffer, 0, length);
	sys_put_le32(EF800_FIRMWARE_JOURNAL_MAGIC, &buffer[0]);
	sys_put_le16(EF800_FIRMWARE_JOURNAL_FORMAT_VERSION, &buffer[4]);
	sys_put_le16((uint16_t)length, &buffer[6]);
	sys_put_le32(journal->sequence, &buffer[8]);
	sys_put_le16(journal->target_count, &buffer[12]);

	for (size_t index = 0U; index < journal->target_count; index++) {
		const struct ef800_firmware_journal_target *target =
			&journal->targets[index];
		size_t offset = EF800_FIRMWARE_JOURNAL_HEADER_SIZE +
				index * EF800_FIRMWARE_JOURNAL_TARGET_SIZE;

		sys_put_le16((uint16_t)target->target_id, &buffer[offset]);
		buffer[offset + 2U] = (uint8_t)target->phase;
		buffer[offset + 3U] = (uint8_t)target->result;
		sys_put_le32(target->package_crc32, &buffer[offset + 4U]);
		sys_put_le32(target->image_crc32, &buffer[offset + 8U]);
		sys_put_le16(target->image_version.major, &buffer[offset + 12U]);
		sys_put_le16(target->image_version.minor, &buffer[offset + 14U]);
		sys_put_le16(target->image_version.patch, &buffer[offset + 16U]);
	}
	sys_put_le32(ef800_crc32(buffer, length), &buffer[EF800_FIRMWARE_JOURNAL_CRC_OFFSET]);
	return (int)length;
}

int ef800_firmware_journal_decode(
	const uint8_t *buffer, size_t length,
	struct ef800_firmware_journal *journal)
{
	struct ef800_firmware_journal decoded = {0};
	uint8_t checked[EF800_FIRMWARE_JOURNAL_MAX_SIZE];
	uint16_t target_count;
	size_t expected_length;
	int ret;

	if (buffer == NULL || journal == NULL ||
	    length < EF800_FIRMWARE_JOURNAL_HEADER_SIZE ||
	    length > sizeof(checked)) {
		return -EINVAL;
	}
	if (sys_get_le32(&buffer[0]) != EF800_FIRMWARE_JOURNAL_MAGIC ||
	    sys_get_le16(&buffer[4]) != EF800_FIRMWARE_JOURNAL_FORMAT_VERSION) {
		return -EPROTONOSUPPORT;
	}
	target_count = sys_get_le16(&buffer[12]);
	if (target_count > EF800_FIRMWARE_JOURNAL_MAX_TARGETS) {
		return -EINVAL;
	}
	expected_length = EF800_FIRMWARE_JOURNAL_HEADER_SIZE +
		(size_t)target_count * EF800_FIRMWARE_JOURNAL_TARGET_SIZE;
	if (length != expected_length || sys_get_le16(&buffer[6]) != length ||
	    sys_get_le16(&buffer[14]) != 0U) {
		return -EMSGSIZE;
	}
	memcpy(checked, buffer, length);
	uint32_t expected_crc = sys_get_le32(&checked[EF800_FIRMWARE_JOURNAL_CRC_OFFSET]);
	sys_put_le32(0U, &checked[EF800_FIRMWARE_JOURNAL_CRC_OFFSET]);
	if (ef800_crc32(checked, length) != expected_crc) {
		return -EBADMSG;
	}

	decoded.sequence = sys_get_le32(&buffer[8]);
	decoded.target_count = target_count;
	for (size_t index = 0U; index < target_count; index++) {
		struct ef800_firmware_journal_target *target =
			&decoded.targets[index];
		size_t offset = EF800_FIRMWARE_JOURNAL_HEADER_SIZE +
				index * EF800_FIRMWARE_JOURNAL_TARGET_SIZE;

		target->target_id =
			(enum ef800_firmware_target_id)sys_get_le16(&buffer[offset]);
		target->phase =
			(enum ef800_firmware_journal_phase)buffer[offset + 2U];
		target->result =
			(enum ef800_firmware_journal_result)buffer[offset + 3U];
		target->package_crc32 = sys_get_le32(&buffer[offset + 4U]);
		target->image_crc32 = sys_get_le32(&buffer[offset + 8U]);
		target->image_version.major = sys_get_le16(&buffer[offset + 12U]);
		target->image_version.minor = sys_get_le16(&buffer[offset + 14U]);
		target->image_version.patch = sys_get_le16(&buffer[offset + 16U]);
		if (sys_get_le16(&buffer[offset + 18U]) != 0U) {
			return -EBADMSG;
		}
	}
	ret = journal_valid(&decoded);
	if (ret != 0) {
		return ret;
	}
	*journal = decoded;
	return 0;
}

bool ef800_firmware_journal_recover(struct ef800_firmware_journal *journal)
{
	bool changed = false;

	if (journal == NULL ||
	    journal_valid(journal) != 0) {
		return false;
	}
	for (size_t index = 0U; index < journal->target_count; index++) {
		struct ef800_firmware_journal_target *target =
			&journal->targets[index];

		if (target->phase == EF800_FIRMWARE_JOURNAL_ERASING ||
		    target->phase == EF800_FIRMWARE_JOURNAL_PROGRAMMING ||
		    target->phase == EF800_FIRMWARE_JOURNAL_VERIFYING) {
			target->phase = EF800_FIRMWARE_JOURNAL_RETRY_REQUIRED;
			target->result = EF800_FIRMWARE_JOURNAL_RESULT_RETRY;
			changed = true;
		}
	}
	return changed;
}

bool ef800_firmware_journal_cleanup_allowed(
	const struct ef800_firmware_journal_target *target)
{
	return target != NULL &&
		target->phase == EF800_FIRMWARE_JOURNAL_COMPLETE &&
		target->result == EF800_FIRMWARE_JOURNAL_RESULT_SUCCESS;
}

int ef800_firmware_journal_read(const struct flash_area *area,
				struct ef800_firmware_journal *journal)
{
	uint8_t buffer[EF800_FIRMWARE_JOURNAL_MAX_SIZE];
	int ret;

	if (area == NULL || journal == NULL) {
		return -EINVAL;
	}
	ret = metadata_nvs_mount(area);
	if (ret != 0) {
		return ret;
	}
	ret = nvs_read(&metadata_nvs, EF800_FIRMWARE_JOURNAL_NVS_ID,
		       buffer, sizeof(buffer));
	if (ret < 0) {
		return ret;
	}
	return ef800_firmware_journal_decode(buffer, (size_t)ret, journal);
}

int ef800_firmware_journal_write(const struct flash_area *area,
				 struct ef800_firmware_journal *journal)
{
	struct ef800_firmware_journal next;
	uint8_t buffer[EF800_FIRMWARE_JOURNAL_MAX_SIZE];
	uint8_t verify[EF800_FIRMWARE_JOURNAL_MAX_SIZE];
	int encoded_size;
	int ret;

	if (area == NULL || journal == NULL) {
		return -EINVAL;
	}
	next = *journal;
	next.sequence++;
	encoded_size = ef800_firmware_journal_encode(
		&next, buffer, sizeof(buffer));
	if (encoded_size < 0) {
		return encoded_size;
	}
	ret = metadata_nvs_mount(area);
	if (ret != 0) {
		return ret;
	}
	ret = nvs_write(&metadata_nvs, EF800_FIRMWARE_JOURNAL_NVS_ID,
			buffer, (size_t)encoded_size);
	if (ret < 0) {
		return ret;
	}
	ret = nvs_read(&metadata_nvs, EF800_FIRMWARE_JOURNAL_NVS_ID,
		       verify, sizeof(verify));
	if (ret != encoded_size ||
	    memcmp(verify, buffer, (size_t)encoded_size) != 0) {
		return -EIO;
	}
	journal->sequence = next.sequence;
	return 0;
}

int ef800_firmware_journal_read_current(
	struct ef800_firmware_journal *journal)
{
	const struct flash_area *area;
	int ret;

	if (journal == NULL) {
		return -EINVAL;
	}
	ret = flash_area_open(PARTITION_ID(metadata_partition), &area);
	if (ret != 0) {
		return ret;
	}
	return ef800_firmware_journal_read(area, journal);
}

int ef800_firmware_journal_write_current(
	struct ef800_firmware_journal *journal)
{
	const struct flash_area *area;
	int ret;

	if (journal == NULL) {
		return -EINVAL;
	}
	ret = flash_area_open(PARTITION_ID(metadata_partition), &area);
	if (ret != 0) {
		return ret;
	}
	return ef800_firmware_journal_write(area, journal);
}

#ifdef EF800_TEST_METADATA
void ef800_metadata_test_forget_state(void)
{
	memset(&metadata_nvs, 0, sizeof(metadata_nvs));
	metadata_area = NULL;
	metadata_nvs_ready = false;
}
#endif
