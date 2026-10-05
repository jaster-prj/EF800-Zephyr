/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_metadata.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

static struct nvs_fs metadata_nvs;
static const struct flash_area *metadata_area;
static bool metadata_nvs_ready;

static int metadata_nvs_mount(const struct flash_area *area)
{
	struct flash_pages_info page_info;
	int ret;

	if (area == NULL || !flash_area_device_is_ready(area)) {
		return -ENODEV;
	}
	if (metadata_nvs_ready) {
		return metadata_area == area ? 0 : -EINVAL;
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
	uint8_t buffer[EF800_BOOT_METADATA_SIZE];

	if (area == NULL || metadata == NULL || valid == NULL) {
		return -EINVAL;
	}
	*valid = false;
	ret = metadata_nvs_mount(area);
	if (ret != 0) {
		return ret;
	}
	ret = nvs_read(&metadata_nvs, EF800_METADATA_NVS_ID, buffer,
			       sizeof(buffer));
	if (ret == sizeof(buffer)) {
		ret = ef800_boot_metadata_decode(buffer, sizeof(buffer), metadata);
		*valid = ret == 0 && ef800_boot_metadata_valid(metadata);
	} else if (ret == 40) {
		ret = ef800_boot_metadata_migrate_v1(buffer, 40U, metadata);
		*valid = ret == 0 && ef800_boot_metadata_valid(metadata);
		if (ret == 0) {
                        ret = EF800_METADATA_MIGRATED;
		}
	} else if (ret >= 0) {
		ret = -ENOENT;
	}
	return ret;
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
