/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_qspi_layout.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>

static uint16_t read_le16(const uint8_t *bytes)
{
	return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

bool ef800_qspi_layout_is_legacy_fat(const uint8_t sector[EF800_QSPI_SECTOR_SIZE])
{
	uint16_t bytes_per_sector;
	uint16_t reserved_sectors;
	uint16_t root_entries;
	uint16_t total_sectors_16;
	uint16_t fat_size_16;
	uint32_t total_sectors_32;
	uint32_t fat_size_32;
	uint8_t sectors_per_cluster;
	uint8_t fat_count;
	uint8_t media;

	if (sector == NULL || sector[510] != 0x55U || sector[511] != 0xAAU ||
		!((sector[0] == 0xE9U) ||
		  (sector[0] == 0xEBU && sector[2] == 0x90U))) {
		return false;
	}

	bytes_per_sector = read_le16(&sector[11]);
	sectors_per_cluster = sector[13];
	reserved_sectors = read_le16(&sector[14]);
	fat_count = sector[16];
	root_entries = read_le16(&sector[17]);
	total_sectors_16 = read_le16(&sector[19]);
	media = sector[21];
	fat_size_16 = read_le16(&sector[22]);
	total_sectors_32 = read_le32(&sector[32]);
	fat_size_32 = read_le32(&sector[36]);

	if (bytes_per_sector != EF800_QSPI_SECTOR_SIZE ||
		sectors_per_cluster == 0U || sectors_per_cluster > 128U ||
		(sectors_per_cluster & (sectors_per_cluster - 1U)) != 0U ||
		reserved_sectors == 0U || (fat_count != 1U && fat_count != 2U) ||
		(media != 0xF0U && media < 0xF8U) ||
		(total_sectors_16 == 0U && total_sectors_32 == 0U) ||
		(fat_size_16 == 0U && fat_size_32 == 0U)) {
		return false;
	}

	/* FAT12/16 use a fixed root-entry table; FAT32 uses a root cluster. */
	return root_entries != 0U || fat_size_32 != 0U;
}

static int qspi_layout_check_legacy_at(const struct device *flash, off_t offset,
				       bool *legacy)
{
	uint8_t sector[EF800_QSPI_SECTOR_SIZE];
	int ret;

	if (flash == NULL || legacy == NULL) {
		return -EINVAL;
	}
	*legacy = false;
	if (!device_is_ready(flash)) {
		return -ENODEV;
	}
	ret = flash_read(flash, offset, sector, sizeof(sector));
	if (ret != 0) {
		return ret;
	}
	*legacy = ef800_qspi_layout_is_legacy_fat(sector);
	return 0;
}

int ef800_qspi_layout_check_legacy(const struct device *flash, bool *legacy)
{
	return qspi_layout_check_legacy_at(flash, 0, legacy);
}

int ef800_qspi_layout_check_legacy_area(const struct flash_area *base_area,
						bool *legacy)
{
	if (base_area == NULL) {
		return -EINVAL;
	}
	return qspi_layout_check_legacy_at(base_area->fa_dev, base_area->fa_off,
		legacy);
}
