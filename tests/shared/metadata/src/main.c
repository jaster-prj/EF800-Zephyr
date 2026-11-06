/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <errno.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "ef800_boot_decision.h"
#include "ef800_metadata.h"
#include "ef800_qspi_layout.h"

static const uint8_t configuration_marker[] = {0x43, 0x46, 0x47, 0x31};
static const uint8_t shared_data_marker[] = {0x44, 0x41, 0x54, 0x31};

static void make_legacy_fat_boot_sector(uint8_t sector[EF800_QSPI_SECTOR_SIZE])
{
	memset(sector, 0, EF800_QSPI_SECTOR_SIZE);
	sector[0] = 0xEB;
	sector[1] = 0x3C;
	sector[2] = 0x90;
	sector[11] = 0x00;
	sector[12] = 0x02; /* 512 bytes per sector */
	sector[13] = 1;
	sector[14] = 1;    /* reserved sectors */
	sector[16] = 2;    /* FAT copies */
	sector[17] = 0xE0;
	sector[18] = 0x00; /* FAT12/16 root entries */
	sector[19] = 0x00;
	sector[20] = 0x20; /* total sectors */
	sector[21] = 0xF8;
	sector[22] = 9;    /* sectors per FAT */
	sector[510] = 0x55;
	sector[511] = 0xAA;
}

static void make_legacy_v2_metadata(uint8_t record[EF800_BOOT_METADATA_V2_SIZE])
{
	memset(record, 0xA6, EF800_BOOT_METADATA_V2_SIZE);
	sys_put_le32(EF800_BOOT_METADATA_MAGIC, &record[0]);
	sys_put_le16(EF800_BOOT_METADATA_V2_VERSION, &record[4]);
	sys_put_le16(EF800_BOOT_METADATA_V2_SIZE, &record[6]);
	sys_put_le32(5U, &record[8]);
	sys_put_le32(EF800_BOOT_TARGET_BACKUP, &record[12]);
	sys_put_le32(17U, &record[16]);
	sys_put_le32(0U, &record[76]);
	sys_put_le32(ef800_crc32(record, 72U), &record[72]);
}

static void prepare_fat_markers(const struct flash_area *configuration,
				const struct flash_area *shared_data)
{
	zassert_ok(flash_area_erase(configuration, 0, 4096), NULL);
	zassert_ok(flash_area_erase(shared_data, 0, 4096), NULL);
	zassert_ok(flash_area_write(configuration, 0, configuration_marker,
				     sizeof(configuration_marker)), NULL);
	zassert_ok(flash_area_write(shared_data, 0, shared_data_marker,
				     sizeof(shared_data_marker)), NULL);
}

static int seed_nvs_record(const struct flash_area *area, const uint8_t *record,
			   size_t record_size)
{
	struct nvs_fs nvs = {0};
	struct flash_pages_info page_info;
	int ret;

	ret = flash_get_page_info_by_offs(area->fa_dev, area->fa_off, &page_info);
	if (ret != 0) {
		return ret;
	}
	nvs.flash_device = area->fa_dev;
	nvs.offset = area->fa_off;
	nvs.sector_size = page_info.size;
	nvs.sector_count = area->fa_size / page_info.size;
	ret = nvs_mount(&nvs);
	if (ret != 0) {
		return ret;
	}
	ret = nvs_write(&nvs, EF800_METADATA_NVS_ID, record, record_size);
	return ret < 0 ? ret : 0;
}

static void assert_fat_markers_unchanged(const struct flash_area *configuration,
					 const struct flash_area *shared_data)
{
	uint8_t marker[sizeof(configuration_marker)];

	zassert_ok(flash_area_read(configuration, 0, marker, sizeof(marker)), NULL);
	zassert_mem_equal(marker, configuration_marker, sizeof(marker), NULL);
	zassert_ok(flash_area_read(shared_data, 0, marker, sizeof(marker)), NULL);
	zassert_mem_equal(marker, shared_data_marker, sizeof(marker), NULL);
}

ZTEST(ef800_shared_metadata, test_missing_record_recovers_defaults_only)
{
	const struct flash_area *boot_state;
	const struct flash_area *configuration;
	const struct flash_area *shared_data;
	struct ef800_boot_metadata metadata;
	struct ef800_boot_metadata defaults;
	bool valid;

	zassert_ok(flash_area_open(PARTITION_ID(metadata_partition), &boot_state), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(qspi_configuration_partition),
				   &configuration), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(qspi_storage_partition), &shared_data),
		   NULL);
	prepare_fat_markers(configuration, shared_data);
	zassert_ok(flash_area_erase(boot_state, 0, boot_state->fa_size), NULL);
	ef800_metadata_test_forget_state();

	zassert_equal(ef800_metadata_read_current(&metadata, &valid),
		      EF800_METADATA_RESET, NULL);
	zassert_true(valid, NULL);
	zassert_true(ef800_boot_metadata_valid(&metadata), NULL);
	ef800_boot_metadata_init(&defaults);
	zassert_mem_equal(&metadata, &defaults, sizeof(metadata), NULL);
	assert_fat_markers_unchanged(configuration, shared_data);

	zassert_ok(ef800_metadata_read_current(&metadata, &valid), NULL);
	zassert_true(valid, NULL);
	zassert_true(ef800_boot_metadata_valid(&metadata), NULL);
	assert_fat_markers_unchanged(configuration, shared_data);
}

ZTEST(ef800_shared_metadata, test_corrupt_record_is_replaced_without_fat_changes)
{
	const struct flash_area *boot_state;
	const struct flash_area *configuration;
	const struct flash_area *shared_data;
	struct ef800_boot_metadata metadata;
	struct ef800_boot_metadata defaults;
	uint8_t corrupt[EF800_BOOT_METADATA_SIZE] = {0};
	bool valid;

	zassert_ok(flash_area_open(PARTITION_ID(metadata_partition), &boot_state), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(qspi_configuration_partition),
				   &configuration), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(qspi_storage_partition), &shared_data),
		   NULL);
	prepare_fat_markers(configuration, shared_data);
	zassert_ok(flash_erase(boot_state->fa_dev, 0, 4096), NULL);
	zassert_ok(flash_area_erase(boot_state, 0, boot_state->fa_size), NULL);
	zassert_ok(seed_nvs_record(boot_state, corrupt, sizeof(corrupt)), NULL);
	ef800_metadata_test_forget_state();

	zassert_equal(ef800_metadata_read_current(&metadata, &valid),
		      EF800_METADATA_RESET, NULL);
	zassert_true(valid, NULL);
	zassert_true(ef800_boot_metadata_valid(&metadata), NULL);
	ef800_boot_metadata_init(&defaults);
	zassert_mem_equal(&metadata, &defaults, sizeof(metadata), NULL);
	assert_fat_markers_unchanged(configuration, shared_data);

	zassert_ok(ef800_metadata_read_current(&metadata, &valid), NULL);
	zassert_true(valid, NULL);
	assert_fat_markers_unchanged(configuration, shared_data);
}

ZTEST(ef800_shared_metadata, test_unavailable_boot_state_uses_safe_main_defaults)
{
	const struct flash_area *configuration;
	const struct flash_area *shared_data;
	const struct flash_area unavailable = {
		.fa_size = 0x8000,
		.fa_dev = NULL,
	};
	struct ef800_boot_metadata metadata;
	struct ef800_boot_metadata defaults;
	bool valid = true;

	zassert_ok(flash_area_open(PARTITION_ID(qspi_configuration_partition),
				   &configuration), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(qspi_storage_partition), &shared_data),
		   NULL);
	prepare_fat_markers(configuration, shared_data);

	zassert_equal(ef800_metadata_read(&unavailable, &metadata, &valid),
		      -ENODEV, NULL);
	zassert_false(valid, NULL);
	ef800_boot_metadata_init(&defaults);
	zassert_mem_equal(&metadata, &defaults, sizeof(metadata), NULL);
	zassert_equal(ef800_boot_decide(&(struct ef800_boot_decision_input){
		.main_valid = true,
		.backup_valid = false,
		.failure_limit_reached = false,
	}), EF800_BOOT_DECISION_MAIN, NULL);
	assert_fat_markers_unchanged(configuration, shared_data);
}

ZTEST(ef800_shared_metadata, test_legacy_volume_is_detected_without_modification)
{
	const struct flash_area *boot_state;
	const struct flash_area *configuration;
	const struct flash_area *shared_data;
	uint8_t boot_sector[EF800_QSPI_SECTOR_SIZE];
	uint8_t readback[EF800_QSPI_SECTOR_SIZE];
	uint8_t legacy_region[EF800_QSPI_SECTOR_SIZE];
	uint8_t region_readback[EF800_QSPI_SECTOR_SIZE];
	uint8_t expected_region[EF800_QSPI_SECTOR_SIZE];
	struct ef800_boot_metadata metadata;
	bool valid;

	zassert_ok(flash_area_open(PARTITION_ID(metadata_partition), &boot_state), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(qspi_configuration_partition),
				   &configuration), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(qspi_storage_partition), &shared_data),
		   NULL);
	prepare_fat_markers(configuration, shared_data);

	make_legacy_fat_boot_sector(boot_sector);
	zassert_true(ef800_qspi_layout_is_legacy_fat(boot_sector), NULL);
	boot_sector[510] = 0;
	zassert_false(ef800_qspi_layout_is_legacy_fat(boot_sector), NULL);
	make_legacy_fat_boot_sector(boot_sector);

	zassert_ok(flash_erase(boot_state->fa_dev, 0, 4096), NULL);
	zassert_ok(flash_area_erase(boot_state, 0, boot_state->fa_size), NULL);
	memset(legacy_region, 0xA5, sizeof(legacy_region));
	for (size_t offset = 0; offset < boot_state->fa_size;
	     offset += sizeof(legacy_region)) {
		zassert_ok(flash_area_write(boot_state, offset, legacy_region,
					    sizeof(legacy_region)), NULL);
	}
	/* Erase its physical page before programming the synthetic boot sector. */
	zassert_ok(flash_area_erase(boot_state, 0U, 4096U), NULL);
	zassert_ok(flash_area_write(boot_state, 0U, boot_sector,
				EF800_QSPI_SECTOR_SIZE), NULL);
	memset(expected_region, 0xFF, sizeof(expected_region));
	ef800_metadata_test_forget_state();

	zassert_equal(ef800_metadata_read_current(&metadata, &valid), -ENOTSUP, NULL);
	zassert_false(valid, NULL);
	zassert_ok(flash_area_read(boot_state, 0U, readback, sizeof(readback)), NULL);
	zassert_mem_equal(readback, boot_sector, sizeof(boot_sector), NULL);
	for (size_t offset = 0; offset < boot_state->fa_size;
	     offset += sizeof(region_readback)) {
		zassert_ok(flash_area_read(boot_state, offset, region_readback,
					   sizeof(region_readback)), NULL);
		if (offset == 0U) {
			zassert_mem_equal(region_readback, boot_sector,
					  sizeof(region_readback), NULL);
		} else if (offset < 4096U) {
			zassert_mem_equal(region_readback, expected_region,
					  sizeof(region_readback), NULL);
		} else {
			zassert_mem_equal(region_readback, legacy_region,
					  sizeof(region_readback), NULL);
		}
	}
	assert_fat_markers_unchanged(configuration, shared_data);

	/* Test-only cleanup restores erased media for the other cases in the suite. */
	zassert_ok(flash_erase(boot_state->fa_dev, 0, 4096), NULL);
	zassert_ok(flash_area_erase(boot_state, 0, boot_state->fa_size), NULL);
}

ZTEST(ef800_shared_metadata, test_v2_metadata_migrates_boot_state_only)
{
	const struct flash_area *boot_state;
	uint8_t legacy_record[EF800_BOOT_METADATA_V2_SIZE];
	struct ef800_boot_metadata metadata;
	bool valid;

	zassert_ok(flash_area_open(PARTITION_ID(metadata_partition), &boot_state), NULL);
	zassert_ok(flash_erase(boot_state->fa_dev, 0, 4096), NULL);
	zassert_ok(flash_area_erase(boot_state, 0, boot_state->fa_size), NULL);
	make_legacy_v2_metadata(legacy_record);
	zassert_ok(seed_nvs_record(boot_state, legacy_record, sizeof(legacy_record)), NULL);
	ef800_metadata_test_forget_state();

	zassert_equal(ef800_metadata_read_current(&metadata, &valid),
		      EF800_METADATA_MIGRATED, NULL);
	zassert_true(valid, NULL);
	zassert_true(ef800_boot_metadata_valid(&metadata), NULL);
	zassert_equal(metadata.main_failures, 5U, NULL);
	zassert_equal(metadata.last_target, EF800_BOOT_TARGET_BACKUP, NULL);
	zassert_equal(metadata.sequence, 18U, NULL);
	zassert_equal(metadata.size, EF800_BOOT_METADATA_SIZE, NULL);
	zassert_ok(ef800_metadata_read_current(&metadata, &valid), NULL);
	zassert_true(valid, NULL);
	zassert_equal(metadata.main_failures, 5U, NULL);
}

ZTEST_SUITE(ef800_shared_metadata, NULL, NULL, NULL, NULL, NULL);
