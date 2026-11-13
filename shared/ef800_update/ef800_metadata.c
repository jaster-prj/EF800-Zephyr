/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_metadata.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
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

#ifdef EF800_TEST_METADATA
void ef800_metadata_test_forget_state(void)
{
	memset(&metadata_nvs, 0, sizeof(metadata_nvs));
	metadata_area = NULL;
	metadata_nvs_ready = false;
}
#endif
