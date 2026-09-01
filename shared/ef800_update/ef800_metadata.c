/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_metadata.h"

#include <errno.h>
#include <string.h>

#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

int ef800_metadata_read(const struct flash_area *area,
				struct ef800_boot_metadata *metadata, bool *valid)
{
	int ret;
	uint8_t buffer[EF800_BOOT_METADATA_SIZE];

	if (area == NULL || metadata == NULL || valid == NULL) {
		return -EINVAL;
	}
	*valid = false;
	if (!flash_area_device_is_ready(area) || area->fa_size < sizeof(*metadata)) {
		return -ENODEV;
	}
	ret = flash_area_read(area, 0U, buffer, sizeof(buffer));
	if (ret == 0) {
		ret = ef800_boot_metadata_decode(buffer, sizeof(buffer), metadata);
		*valid = ret == 0 && ef800_boot_metadata_valid(metadata);
	}
	return ret;
}

int ef800_metadata_write(const struct flash_area *area,
				struct ef800_boot_metadata *metadata)
{
	struct ef800_boot_metadata verify;
	uint8_t buffer[EF800_BOOT_METADATA_SIZE];
	uint8_t verify_buffer[EF800_BOOT_METADATA_SIZE];
	int ret;

	if (area == NULL || metadata == NULL) {
		return -EINVAL;
	}
	if (!flash_area_device_is_ready(area) || area->fa_size < sizeof(*metadata)) {
		return -ENODEV;
	}
	metadata->size = sizeof(*metadata);
	metadata->sequence++;
	metadata->crc32 = ef800_boot_metadata_crc(metadata);
	ret = ef800_boot_metadata_encode(metadata, buffer, sizeof(buffer));
	if (ret != 0) {
		return ret;
	}
	ret = flash_area_erase(area, 0U, area->fa_size);
	if (ret != 0) {
		return ret;
	}
	ret = flash_area_write(area, 0U, buffer, sizeof(buffer));
	if (ret != 0) {
		return ret;
	}
	ret = flash_area_read(area, 0U, verify_buffer, sizeof(verify_buffer));
	if (ret != 0 || ef800_boot_metadata_decode(verify_buffer,
									 sizeof(verify_buffer), &verify) != 0) {
		return ret != 0 ? ret : -EIO;
	}
	return memcmp(verify_buffer, buffer, sizeof(buffer)) == 0 &&
		ef800_boot_metadata_valid(&verify) ? 0 : -EIO;
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
