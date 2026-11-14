/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "boot_update.h"
#include "ef800_firmware_package.h"
#include "ef800_update.h"

#define TEST_MAIN_ADDRESS 0x08030000U
#define TEST_SRAM_ADDRESS 0x20000000U
#define TEST_SRAM_SIZE 0x00010000U
#define TEST_IMAGE_SIZE 64U
#define TEST_UPDATE_PATH "/DATA:/fwu/EF800_FW.bin"
#define TEST_PACKAGE_PATH "/DATA:/fwu/EF800_APP.pkg"
#define TEST_DISK_NAME "DATA"

static FATFS update_fat_fs;
static struct fs_mount_t update_mount = {
	.type = FS_FATFS,
	.mnt_point = "/DATA:",
	.fs_data = &update_fat_fs,
	.storage_dev = (void *)PARTITION_ID(qspi_storage_partition),
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};

static const uint8_t boot_marker[] = {0x42, 0x4F, 0x4F, 0x54};
static const uint8_t backup_marker[] = {0x42, 0x41, 0x43, 0x4B};
static const uint8_t main_marker[] = {0x4D, 0x41, 0x49, 0x4E};

static void build_test_image(uint8_t image[TEST_IMAGE_SIZE])
{
	memset(image, 0xA5, TEST_IMAGE_SIZE);
	uint32_t vector[2] = {
		TEST_SRAM_ADDRESS + 8U,
		TEST_MAIN_ADDRESS + 9U,
	};

	memcpy(image, vector, sizeof(vector));
}

static void prepare_internal_flash(const struct flash_area *boot,
				  const struct flash_area *backup,
				  const struct flash_area *main)
{
	zassert_ok(flash_area_erase(boot, 0, boot->fa_size), NULL);
	zassert_ok(flash_area_erase(backup, 0, backup->fa_size), NULL);
	zassert_ok(flash_area_erase(main, 0, main->fa_size), NULL);
	zassert_ok(flash_area_write(boot, 0, boot_marker, sizeof(boot_marker)), NULL);
	zassert_ok(flash_area_write(backup, 0, backup_marker, sizeof(backup_marker)), NULL);
	zassert_ok(flash_area_write(main, 0, main_marker, sizeof(main_marker)), NULL);
}

static void assert_region_marker(const struct flash_area *area, const uint8_t *marker)
{
	uint8_t actual[sizeof(boot_marker)];

	zassert_ok(flash_area_read(area, 0, actual, sizeof(actual)), NULL);
	zassert_mem_equal(actual, marker, sizeof(actual), NULL);
}

static uint32_t flash_area_crc32(const struct flash_area *area)
{
	uint8_t block[512];
	uint32_t crc = 0U;

	for (size_t offset = 0; offset < area->fa_size; offset += sizeof(block)) {
		zassert_ok(flash_area_read(area, offset, block, sizeof(block)), NULL);
		crc = ef800_crc32_update(crc, block, sizeof(block));
	}
	return crc;
}

static int write_update_package(bool valid_crc, bool truncated,
				bool valid_load_address)
{
	struct ef800_update_header header = {
		.magic = EF800_UPDATE_MAGIC,
		.format_version = EF800_UPDATE_FORMAT_VERSION,
		.header_size = sizeof(struct ef800_update_header),
		.image_size = TEST_IMAGE_SIZE,
		.load_address = valid_load_address ? TEST_MAIN_ADDRESS :
			TEST_MAIN_ADDRESS + 0x10000U,
	};
	uint8_t image[TEST_IMAGE_SIZE];
	struct fs_file_t file;
	int ret;

	build_test_image(image);
	header.image_crc32 = ef800_crc32(image, sizeof(image));
	if (!valid_crc) {
		header.image_crc32 ^= 1U;
	}

	ret = fs_mkfs(FS_FATFS, (uintptr_t)TEST_DISK_NAME ":", NULL, 0);
	if (ret != 0) {
		return ret;
	}
	ret = fs_mount(&update_mount);
	if (ret != 0) {
		return ret;
	}
	ret = fs_mkdir("/DATA:/fwu");
	if (ret != 0 && ret != -EEXIST) {
		(void)fs_unmount(&update_mount);
		return ret;
	}

	fs_file_t_init(&file);
	ret = fs_open(&file, TEST_UPDATE_PATH, FS_O_CREATE | FS_O_WRITE);
	if (ret == 0) {
		ssize_t written = fs_write(&file, &header, sizeof(header));
		if (written != (ssize_t)sizeof(header)) {
			ret = written < 0 ? (int)written : -EIO;
		} else {
			size_t payload_size = truncated ? sizeof(image) / 2U : sizeof(image);
			written = fs_write(&file, image, payload_size);
			if (written != (ssize_t)payload_size) {
				ret = written < 0 ? (int)written : -EIO;
			}
		}
		{
			int close_ret = fs_close(&file);
			if (ret == 0) {
				ret = close_ret;
			}
		}
	}
	{
		int unmount_ret = fs_unmount(&update_mount);
		if (ret == 0) {
			ret = unmount_ret;
		}
	}
	return ret;
}

static int write_versioned_application_package(void)
{
	uint8_t wire_header[EF800_FIRMWARE_PACKAGE_HEADER_SIZE] = {0};
	uint8_t image[TEST_IMAGE_SIZE];
	struct fs_file_t file;
	int ret;

	build_test_image(image);
	sys_put_le32(EF800_FIRMWARE_PACKAGE_MAGIC, &wire_header[0]);
	sys_put_le16(EF800_FIRMWARE_PACKAGE_FORMAT_VERSION, &wire_header[4]);
	sys_put_le16(EF800_FIRMWARE_PACKAGE_HEADER_SIZE, &wire_header[6]);
	sys_put_le16(EF800_FIRMWARE_TARGET_APPLICATION, &wire_header[8]);
	sys_put_le16(EF800_FIRMWARE_COORDINATOR_BOOTLOADER, &wire_header[10]);
	sys_put_le16(EF800_FIRMWARE_HARDWARE_ID_L496, &wire_header[12]);
	sys_put_le16(1U, &wire_header[14]);
	sys_put_le16(0U, &wire_header[16]);
	sys_put_le16(0U, &wire_header[18]);
	sys_put_le16(1U, &wire_header[20]);
	sys_put_le16(0U, &wire_header[22]);
	sys_put_le16(0U, &wire_header[24]);
	sys_put_le32(TEST_MAIN_ADDRESS, &wire_header[26]);
	sys_put_le32(sizeof(image), &wire_header[30]);
	sys_put_le32(ef800_crc32(image, sizeof(image)), &wire_header[34]);
	sys_put_le32(0U, &wire_header[EF800_FIRMWARE_PACKAGE_HEADER_CRC_OFFSET]);
	sys_put_le32(ef800_crc32(wire_header, sizeof(wire_header)),
		     &wire_header[EF800_FIRMWARE_PACKAGE_HEADER_CRC_OFFSET]);

	ret = fs_mkfs(FS_FATFS, (uintptr_t)TEST_DISK_NAME ":", NULL, 0);
	if (ret != 0) {
		return ret;
	}
	ret = fs_mount(&update_mount);
	if (ret != 0) {
		return ret;
	}
	ret = fs_mkdir("/DATA:/fwu");
	if (ret != 0 && ret != -EEXIST) {
		(void)fs_unmount(&update_mount);
		return ret;
	}

	fs_file_t_init(&file);
	ret = fs_open(&file, TEST_PACKAGE_PATH, FS_O_CREATE | FS_O_WRITE);
	if (ret == 0) {
		ssize_t written = fs_write(&file, wire_header, sizeof(wire_header));
		if (written == (ssize_t)sizeof(wire_header)) {
			written = fs_write(&file, image, sizeof(image));
		}
		if (written != (ssize_t)sizeof(image)) {
			ret = written < 0 ? (int)written : -EIO;
		}
		int close_ret = fs_close(&file);
		if (ret == 0) {
			ret = close_ret;
		}
	}
	int unmount_ret = fs_unmount(&update_mount);
	return ret == 0 ? unmount_ret : ret;
}

ZTEST(ef800_boot_update, test_valid_update_changes_only_main_partition)
{
	const struct flash_area *boot;
	const struct flash_area *backup;
	const struct flash_area *main;
	uint8_t expected[TEST_IMAGE_SIZE];
	uint8_t actual[TEST_IMAGE_SIZE];
	uint32_t boot_crc;
	uint32_t backup_crc;

	zassert_ok(flash_area_open(PARTITION_ID(ef800_boot_partition), &boot), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(ef800_backup_partition), &backup), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(ef800_main_partition), &main), NULL);
	prepare_internal_flash(boot, backup, main);
	build_test_image(expected);
	zassert_ok(write_update_package(true, false, true), NULL);
	boot_crc = flash_area_crc32(boot);
	backup_crc = flash_area_crc32(backup);

	zassert_equal(ef800_boot_install_update(main, TEST_MAIN_ADDRESS,
						TEST_SRAM_ADDRESS, TEST_SRAM_SIZE), 0, NULL);
	zassert_ok(flash_area_read(main, 0, actual, sizeof(actual)), NULL);
	zassert_mem_equal(actual, expected, sizeof(expected), NULL);
	zassert_equal(flash_area_crc32(boot), boot_crc, NULL);
	zassert_equal(flash_area_crc32(backup), backup_crc, NULL);
	assert_region_marker(boot, boot_marker);
	assert_region_marker(backup, backup_marker);
}

ZTEST(ef800_boot_update, test_versioned_application_package_installs_and_cleans_up)
{
	const struct flash_area *boot;
	const struct flash_area *backup;
	const struct flash_area *main;
	uint8_t expected[TEST_IMAGE_SIZE];
	uint8_t actual[TEST_IMAGE_SIZE];
	uint32_t boot_crc;
	uint32_t backup_crc;

	zassert_ok(flash_area_open(PARTITION_ID(ef800_boot_partition), &boot), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(ef800_backup_partition), &backup), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(ef800_main_partition), &main), NULL);
	prepare_internal_flash(boot, backup, main);
	build_test_image(expected);
	zassert_ok(write_versioned_application_package(), NULL);
	boot_crc = flash_area_crc32(boot);
	backup_crc = flash_area_crc32(backup);

	zassert_ok(ef800_boot_install_update(main, TEST_MAIN_ADDRESS,
					     TEST_SRAM_ADDRESS, TEST_SRAM_SIZE), NULL);
	zassert_ok(flash_area_read(main, 0, actual, sizeof(actual)), NULL);
	zassert_mem_equal(actual, expected, sizeof(expected), NULL);
	zassert_equal(flash_area_crc32(boot), boot_crc, NULL);
	zassert_equal(flash_area_crc32(backup), backup_crc, NULL);
	assert_region_marker(boot, boot_marker);
	assert_region_marker(backup, backup_marker);
}

ZTEST(ef800_boot_update, test_bad_crc_is_rejected_before_main_erase)
{
	const struct flash_area *boot;
	const struct flash_area *backup;
	const struct flash_area *main;
	uint32_t main_crc;
	uint32_t backup_crc;
	uint32_t boot_crc;

	zassert_ok(flash_area_open(PARTITION_ID(ef800_boot_partition), &boot), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(ef800_backup_partition), &backup), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(ef800_main_partition), &main), NULL);
	prepare_internal_flash(boot, backup, main);
	zassert_ok(write_update_package(false, false, true), NULL);
	main_crc = flash_area_crc32(main);
	backup_crc = flash_area_crc32(backup);
	boot_crc = flash_area_crc32(boot);

	zassert_equal(ef800_boot_install_update(main, TEST_MAIN_ADDRESS,
						TEST_SRAM_ADDRESS, TEST_SRAM_SIZE), -EBADMSG, NULL);
	zassert_equal(flash_area_crc32(main), main_crc, NULL);
	zassert_equal(flash_area_crc32(backup), backup_crc, NULL);
	zassert_equal(flash_area_crc32(boot), boot_crc, NULL);
	assert_region_marker(boot, boot_marker);
	assert_region_marker(backup, backup_marker);
}

ZTEST(ef800_boot_update, test_truncated_update_is_rejected_without_internal_writes)
{
	const struct flash_area *boot;
	const struct flash_area *backup;
	const struct flash_area *main;
	uint32_t main_crc;
	uint32_t backup_crc;
	uint32_t boot_crc;

	zassert_ok(flash_area_open(PARTITION_ID(ef800_boot_partition), &boot), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(ef800_backup_partition), &backup), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(ef800_main_partition), &main), NULL);
	prepare_internal_flash(boot, backup, main);
	zassert_ok(write_update_package(true, true, true), NULL);
	main_crc = flash_area_crc32(main);
	backup_crc = flash_area_crc32(backup);
	boot_crc = flash_area_crc32(boot);

	zassert_equal(ef800_boot_install_update(main, TEST_MAIN_ADDRESS,
						TEST_SRAM_ADDRESS, TEST_SRAM_SIZE), -EINVAL, NULL);
	zassert_equal(flash_area_crc32(main), main_crc, NULL);
	zassert_equal(flash_area_crc32(backup), backup_crc, NULL);
	zassert_equal(flash_area_crc32(boot), boot_crc, NULL);
	assert_region_marker(boot, boot_marker);
	assert_region_marker(backup, backup_marker);
}

ZTEST(ef800_boot_update, test_wrong_load_address_is_rejected_without_internal_writes)
{
	const struct flash_area *boot;
	const struct flash_area *backup;
	const struct flash_area *main;
	uint32_t main_crc;
	uint32_t backup_crc;
	uint32_t boot_crc;

	zassert_ok(flash_area_open(PARTITION_ID(ef800_boot_partition), &boot), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(ef800_backup_partition), &backup), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(ef800_main_partition), &main), NULL);
	prepare_internal_flash(boot, backup, main);
	zassert_ok(write_update_package(true, false, false), NULL);
	main_crc = flash_area_crc32(main);
	backup_crc = flash_area_crc32(backup);
	boot_crc = flash_area_crc32(boot);

	zassert_equal(ef800_boot_install_update(main, TEST_MAIN_ADDRESS,
						TEST_SRAM_ADDRESS, TEST_SRAM_SIZE), -EINVAL, NULL);
	zassert_equal(flash_area_crc32(main), main_crc, NULL);
	zassert_equal(flash_area_crc32(backup), backup_crc, NULL);
	zassert_equal(flash_area_crc32(boot), boot_crc, NULL);
	assert_region_marker(boot, boot_marker);
	assert_region_marker(backup, backup_marker);
}

ZTEST_SUITE(ef800_boot_update, NULL, NULL, NULL, NULL, NULL);
