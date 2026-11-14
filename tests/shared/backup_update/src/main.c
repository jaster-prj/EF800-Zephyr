/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "ef800_backup_update.h"
#include "ef800_firmware_package.h"
#include "ef800_update.h"

#define TEST_PACKAGE_PATH "/DATA:/fwu/EF800_BACKUP.pkg"
#define TEST_IMAGE_SIZE 64U
#define TEST_BACKUP_ADDRESS EF800_FIRMWARE_L496_BACKUP_ADDRESS

static FATFS test_fat_fs;
static struct fs_mount_t test_mount = {
	.type = FS_FATFS,
	.mnt_point = "/DATA:",
	.fs_data = &test_fat_fs,
	.storage_dev = (void *)PARTITION_ID(qspi_storage_partition),
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static const struct ef800_firmware_semver coordinator_version = {
	.major = 1U,
	.minor = 0U,
	.patch = 0U,
};

static void build_image(uint8_t image[TEST_IMAGE_SIZE], bool valid_vector)
{
	memset(image, 0x5a, TEST_IMAGE_SIZE);
	sys_put_le32(0x20050000U, &image[0]);
	sys_put_le32(valid_vector ? (TEST_BACKUP_ADDRESS + 9U) :
		     (TEST_BACKUP_ADDRESS + TEST_IMAGE_SIZE + 1U), &image[4]);
}

static void build_package(uint8_t header[EF800_FIRMWARE_PACKAGE_HEADER_SIZE],
			  const uint8_t image[TEST_IMAGE_SIZE], uint16_t target)
{
	memset(header, 0, EF800_FIRMWARE_PACKAGE_HEADER_SIZE);
	sys_put_le32(EF800_FIRMWARE_PACKAGE_MAGIC, &header[0]);
	sys_put_le16(EF800_FIRMWARE_PACKAGE_FORMAT_VERSION, &header[4]);
	sys_put_le16(EF800_FIRMWARE_PACKAGE_HEADER_SIZE, &header[6]);
	sys_put_le16(target, &header[8]);
	sys_put_le16(EF800_FIRMWARE_COORDINATOR_APPLICATION, &header[10]);
	sys_put_le16(EF800_FIRMWARE_HARDWARE_ID_L496, &header[12]);
	sys_put_le16(1U, &header[14]);
	sys_put_le16(1U, &header[20]);
	sys_put_le32(TEST_BACKUP_ADDRESS, &header[26]);
	sys_put_le32(TEST_IMAGE_SIZE, &header[30]);
	sys_put_le32(ef800_crc32(image, TEST_IMAGE_SIZE), &header[34]);
	sys_put_le32(0U, &header[EF800_FIRMWARE_PACKAGE_HEADER_CRC_OFFSET]);
	sys_put_le32(ef800_crc32(header, EF800_FIRMWARE_PACKAGE_HEADER_SIZE),
		     &header[EF800_FIRMWARE_PACKAGE_HEADER_CRC_OFFSET]);
}

static int stage_package(bool valid_vector, uint16_t target, bool oversized,
			 bool corrupt_payload)
{
	uint8_t image[TEST_IMAGE_SIZE];
	uint8_t header[EF800_FIRMWARE_PACKAGE_HEADER_SIZE];
	struct fs_file_t file;
	ssize_t written;
	int rc;

	build_image(image, valid_vector);
	build_package(header, image, target);
	if (oversized) {
		sys_put_le32(EF800_FIRMWARE_L496_BACKUP_SIZE + 1U, &header[30]);
		sys_put_le32(0U,
			     &header[EF800_FIRMWARE_PACKAGE_HEADER_CRC_OFFSET]);
		sys_put_le32(ef800_crc32(header, sizeof(header)),
			     &header[EF800_FIRMWARE_PACKAGE_HEADER_CRC_OFFSET]);
	}
	if (corrupt_payload) {
		image[TEST_IMAGE_SIZE - 1U] ^= 0x01U;
	}
	rc = fs_mkfs(FS_FATFS, (uintptr_t)"DATA:", NULL, 0);
	if (rc != 0) {
		return rc;
	}
	rc = fs_mount(&test_mount);
	if (rc != 0) {
		return rc;
	}
	rc = fs_mkdir("/DATA:/fwu");
	if (rc != 0 && rc != -EEXIST) {
		(void)fs_unmount(&test_mount);
		return rc;
	}
	fs_file_t_init(&file);
	rc = fs_open(&file, TEST_PACKAGE_PATH,
		     FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (rc == 0) {
		written = fs_write(&file, header, sizeof(header));
		if (written == (ssize_t)sizeof(header)) {
			written = fs_write(&file, image, sizeof(image));
		}
		rc = written == (ssize_t)sizeof(image) ? 0 :
			(written < 0 ? (int)written : -EIO);
		{
			int close_rc = fs_close(&file);
			if (rc == 0) {
				rc = close_rc;
			}
		}
	}
	{
		int unmount_rc = fs_unmount(&test_mount);
		if (rc == 0) {
			rc = unmount_rc;
		}
	}
	return rc;
}

static int package_exists(void)
{
	struct fs_dirent entry;
	int rc = fs_mount(&test_mount);

	if (rc != 0) {
		return rc;
	}
	rc = fs_stat(TEST_PACKAGE_PATH, &entry);
	(void)fs_unmount(&test_mount);
	return rc;
}

static int mount_data(void)
{
	return fs_mount(&test_mount);
}

ZTEST(ef800_backup_update, test_fixed_internal_flash_partition_map)
{
	const struct flash_area *boot;
	const struct flash_area *backup;
	const struct flash_area *main;

	zassert_ok(flash_area_open(PARTITION_ID(boot_partition), &boot), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(backup_partition), &backup), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(main_partition), &main), NULL);
	zassert_equal(boot->fa_off, 0U, NULL);
	zassert_equal(boot->fa_size, EF800_FIRMWARE_L496_BOOTLOADER_SIZE, NULL);
	zassert_equal(backup->fa_off, EF800_FIRMWARE_L496_BACKUP_ADDRESS -
		      EF800_FIRMWARE_L496_BOOTLOADER_ADDRESS, NULL);
	zassert_equal(backup->fa_size, EF800_FIRMWARE_L496_BACKUP_SIZE, NULL);
	zassert_equal(main->fa_off, EF800_FIRMWARE_L496_APPLICATION_ADDRESS -
		      EF800_FIRMWARE_L496_BOOTLOADER_ADDRESS, NULL);
	zassert_equal(main->fa_size, EF800_FIRMWARE_L496_APPLICATION_SIZE, NULL);
	flash_area_close(main);
	flash_area_close(backup);
	flash_area_close(boot);
}

ZTEST(ef800_backup_update, test_valid_package_programs_only_backup_and_cleans_up)
{
	const struct flash_area *boot;
	const struct flash_area *backup;
	const struct flash_area *main;
	uint8_t expected[TEST_IMAGE_SIZE];
	uint8_t actual[TEST_IMAGE_SIZE];
	uint8_t marker[4];
	int rc;

	zassert_ok(flash_area_open(PARTITION_ID(boot_partition), &boot), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(backup_partition), &backup), NULL);
	zassert_ok(flash_area_open(PARTITION_ID(main_partition), &main), NULL);
	zassert_ok(flash_area_erase(boot, 0U, boot->fa_size), NULL);
	zassert_ok(flash_area_erase(backup, 0U, backup->fa_size), NULL);
	zassert_ok(flash_area_erase(main, 0U, main->fa_size), NULL);
	marker[0] = 0xb0U;
	marker[1] = 0xb1U;
	marker[2] = 0xb2U;
	marker[3] = 0xb3U;
	zassert_ok(flash_area_write(boot, 0U, marker, sizeof(marker)), NULL);
	marker[0] = 0xa0U;
	marker[1] = 0xa1U;
	marker[2] = 0xa2U;
	marker[3] = 0xa3U;
	zassert_ok(flash_area_write(main, 0U, marker, sizeof(marker)), NULL);
	build_image(expected, true);
	rc = stage_package(true, EF800_FIRMWARE_TARGET_BACKUP, false, false);
	zassert_ok(rc, "package staging failed: %d", rc);

	zassert_ok(mount_data(), NULL);
	rc = ef800_backup_update_install(TEST_PACKAGE_PATH, &coordinator_version);
	zassert_ok(rc, "installer rc=%d", rc);
	zassert_ok(fs_unmount(&test_mount), NULL);
	zassert_ok(flash_area_read(backup, 0U, actual, sizeof(actual)), NULL);
	zassert_mem_equal(actual, expected, sizeof(expected), NULL);
	zassert_ok(flash_area_read(boot, 0U, actual, sizeof(marker)), NULL);
	zassert_equal(actual[0], 0xb0U, NULL);
	zassert_ok(flash_area_read(main, 0U, actual, sizeof(marker)), NULL);
	zassert_equal(actual[0], 0xa0U, NULL);
	zassert_equal(package_exists(), -ENOENT, "verified package should be removed");
	flash_area_close(main);
	flash_area_close(backup);
	flash_area_close(boot);
}

ZTEST(ef800_backup_update, test_wrong_target_is_rejected_before_backup_erase)
{
	const struct flash_area *backup;
	uint8_t marker[4] = {0x41U, 0x42U, 0x43U, 0x44U};
	uint8_t actual[sizeof(marker)];
	int rc;

	zassert_ok(flash_area_open(PARTITION_ID(backup_partition), &backup), NULL);
	zassert_ok(flash_area_erase(backup, 0U, backup->fa_size), NULL);
	zassert_ok(flash_area_write(backup, 0U, marker, sizeof(marker)), NULL);
	rc = stage_package(true, EF800_FIRMWARE_TARGET_APPLICATION, false, false);
	zassert_ok(rc, "package staging failed: %d", rc);
	zassert_ok(mount_data(), NULL);
	rc = ef800_backup_update_install(TEST_PACKAGE_PATH, &coordinator_version);
	zassert_ok(fs_unmount(&test_mount), NULL);
	zassert_true(rc < 0, "wrong target must be rejected");
	zassert_ok(flash_area_read(backup, 0U, actual, sizeof(actual)), NULL);
	zassert_mem_equal(actual, marker, sizeof(marker), NULL);
	zassert_ok(package_exists(), "invalid package retained for diagnosis");
	flash_area_close(backup);
}

ZTEST(ef800_backup_update, test_invalid_vector_is_rejected_before_backup_erase)
{
	const struct flash_area *backup;
	uint8_t marker[4] = {0x51U, 0x52U, 0x53U, 0x54U};
	uint8_t actual[sizeof(marker)];
	int rc;

	zassert_ok(flash_area_open(PARTITION_ID(backup_partition), &backup), NULL);
	zassert_ok(flash_area_erase(backup, 0U, backup->fa_size), NULL);
	zassert_ok(flash_area_write(backup, 0U, marker, sizeof(marker)), NULL);
	rc = stage_package(false, EF800_FIRMWARE_TARGET_BACKUP, false, false);
	zassert_ok(rc, "package staging failed: %d", rc);
	zassert_ok(mount_data(), NULL);
	rc = ef800_backup_update_install(TEST_PACKAGE_PATH, &coordinator_version);
	zassert_ok(fs_unmount(&test_mount), NULL);
	zassert_true(rc < 0, "invalid vector must be rejected");
	zassert_ok(flash_area_read(backup, 0U, actual, sizeof(actual)), NULL);
	zassert_mem_equal(actual, marker, sizeof(marker), NULL);
	zassert_ok(package_exists(), "rejected package retained");
	flash_area_close(backup);
}

ZTEST(ef800_backup_update, test_oversized_package_is_rejected_without_erase)
{
	const struct flash_area *backup;
	uint8_t marker[4] = {0x61U, 0x62U, 0x63U, 0x64U};
	uint8_t actual[sizeof(marker)];
	int rc;

	zassert_ok(flash_area_open(PARTITION_ID(backup_partition), &backup), NULL);
	zassert_ok(flash_area_erase(backup, 0U, backup->fa_size), NULL);
	zassert_ok(flash_area_write(backup, 0U, marker, sizeof(marker)), NULL);
	rc = stage_package(true, EF800_FIRMWARE_TARGET_BACKUP, true, false);
	zassert_ok(rc, "package staging failed: %d", rc);
	zassert_ok(mount_data(), NULL);
	rc = ef800_backup_update_install(TEST_PACKAGE_PATH, &coordinator_version);
	zassert_ok(fs_unmount(&test_mount), NULL);
	zassert_equal(rc, -EMSGSIZE, "oversized image should be rejected");
	zassert_ok(flash_area_read(backup, 0U, actual, sizeof(actual)), NULL);
	zassert_mem_equal(actual, marker, sizeof(marker), NULL);
	zassert_ok(package_exists(), "oversized package retained");
	flash_area_close(backup);
}

ZTEST(ef800_backup_update, test_corrupt_payload_is_rejected_without_erase)
{
	const struct flash_area *backup;
	uint8_t marker[4] = {0x71U, 0x72U, 0x73U, 0x74U};
	uint8_t actual[sizeof(marker)];
	int rc;

	zassert_ok(flash_area_open(PARTITION_ID(backup_partition), &backup), NULL);
	zassert_ok(flash_area_erase(backup, 0U, backup->fa_size), NULL);
	zassert_ok(flash_area_write(backup, 0U, marker, sizeof(marker)), NULL);
	rc = stage_package(true, EF800_FIRMWARE_TARGET_BACKUP, false, true);
	zassert_ok(rc, "package staging failed: %d", rc);
	zassert_ok(mount_data(), NULL);
	rc = ef800_backup_update_install(TEST_PACKAGE_PATH, &coordinator_version);
	zassert_ok(fs_unmount(&test_mount), NULL);
	zassert_equal(rc, -EBADMSG, "corrupt image should be rejected");
	zassert_ok(flash_area_read(backup, 0U, actual, sizeof(actual)), NULL);
	zassert_mem_equal(actual, marker, sizeof(marker), NULL);
	zassert_ok(package_exists(), "corrupt package retained");
	flash_area_close(backup);
}

ZTEST_SUITE(ef800_backup_update, NULL, NULL, NULL, NULL, NULL);
