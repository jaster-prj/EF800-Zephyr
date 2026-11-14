/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_backup_update.h"

#include <errno.h>

#if defined(CONFIG_BOARD_EF800)
#include <string.h>

#include <ff.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#include "ef800_firmware_package.h"
#include "ef800_update.h"
#include "ef800_watchdog.h"

LOG_MODULE_REGISTER(ef800_backup_update, CONFIG_LOG_DEFAULT_LEVEL);

#define BACKUP_PARTITION_ID PARTITION_ID(backup_partition)
#define BACKUP_UPDATE_CHUNK 1024U
#define L496_SRAM_BASE 0x20000000U
#define L496_SRAM_SIZE 0x00050000U

static int file_read_exact(struct fs_file_t *file, void *buffer, size_t length)
{
	size_t offset = 0U;

	while (offset < length) {
		ssize_t count = fs_read(file, (uint8_t *)buffer + offset,
					length - offset);

		if (count < 0) {
			return (int)count;
		}
		if (count == 0) {
			return -EIO;
		}
		offset += (size_t)count;
	}
	return 0;
}

static int file_crc32(struct fs_file_t *file, off_t offset, size_t length,
		      uint32_t *crc_out)
{
	uint8_t buffer[BACKUP_UPDATE_CHUNK];
	size_t remaining = length;
	uint32_t crc = 0U;
	int rc;

	if (offset < 0 || crc_out == NULL || fs_seek(file, offset, FS_SEEK_SET) < 0) {
		return -EIO;
	}
	while (remaining != 0U) {
		size_t count = MIN(remaining, sizeof(buffer));

		rc = file_read_exact(file, buffer, count);
		if (rc != 0) {
			return rc;
		}
		crc = ef800_crc32_update(crc, buffer, count);
		(void)ef800_watchdog_feed();
		remaining -= count;
	}
	*crc_out = crc;
	return 0;
}

static int erase_backup_partition(const struct flash_area *area)
{
	size_t offset = 0U;

	while (offset < area->fa_size) {
		struct flash_pages_info page = {0};
		off_t absolute = area->fa_off + (off_t)offset;
		int rc = flash_get_page_info_by_offs(area->fa_dev, absolute, &page);

		if (rc != 0 || page.size == 0U ||
		    page.start_offset != absolute ||
		    page.size > area->fa_size - offset) {
			return rc != 0 ? rc : -EINVAL;
		}
		rc = flash_area_erase(area, offset, page.size);
		if (rc != 0) {
			return rc;
		}
		offset += page.size;
		(void)ef800_watchdog_feed();
	}
	return 0;
}

static int install_payload(struct fs_file_t *file,
			   const struct flash_area *area, off_t source_offset,
			   size_t image_size, uint32_t expected_crc)
{
	uint8_t source[BACKUP_UPDATE_CHUNK] __aligned(8);
	uint8_t verify[BACKUP_UPDATE_CHUNK] __aligned(8);
	size_t alignment = flash_area_align(area);
	size_t source_position = 0U;
	size_t destination = 0U;
	uint32_t crc = 0U;
	int rc;

	if (alignment == 0U || alignment > sizeof(source)) {
		return -EINVAL;
	}
	rc = erase_backup_partition(area);
	if (rc != 0) {
		return rc;
	}
	while (source_position < image_size) {
		size_t count = MIN(sizeof(source), image_size - source_position);
		size_t write_count = ROUND_UP(count, alignment);

		if (write_count > sizeof(source) || destination > area->fa_size ||
		    write_count > area->fa_size - destination) {
			return -EFBIG;
		}
		if (fs_seek(file, source_offset + (off_t)source_position,
			    FS_SEEK_SET) < 0) {
			return -EIO;
		}
		rc = file_read_exact(file, source, count);
		if (rc != 0) {
			return rc;
		}
		crc = ef800_crc32_update(crc, source, count);
		if (write_count > count) {
			memset(source + count, 0xff, write_count - count);
		}
		rc = flash_area_write(area, destination, source, write_count);
		if (rc != 0) {
			return rc;
		}
		rc = flash_area_read(area, destination, verify, write_count);
		if (rc != 0 || memcmp(source, verify, write_count) != 0) {
			return rc != 0 ? rc : -EIO;
		}
		source_position += count;
		destination += write_count;
		(void)ef800_watchdog_feed();
	}
	return crc == expected_crc ? 0 : -EBADMSG;
}

int ef800_backup_update_install(
	const char *package_path,
	const struct ef800_firmware_semver *coordinator_version)
{
	struct fs_dirent entry;
	struct fs_file_t file;
	struct ef800_firmware_package_header package;
	uint8_t wire_header[EF800_FIRMWARE_PACKAGE_HEADER_SIZE];
	uint32_t vector[2];
	uint32_t payload_crc;
	const struct flash_area *area = NULL;
	bool opened = false;
	int rc;

	if (package_path == NULL || package_path[0] == '\0' ||
	    coordinator_version == NULL) {
		return -EINVAL;
	}
	rc = fs_stat(package_path, &entry);
	if (rc != 0) {
		return rc;
	}
	if (entry.type != FS_DIR_ENTRY_FILE ||
	    entry.size < EF800_FIRMWARE_PACKAGE_HEADER_SIZE) {
		return -EINVAL;
	}
	fs_file_t_init(&file);
	rc = fs_open(&file, package_path, FS_O_READ);
	if (rc != 0) {
		return rc;
	}
	opened = true;
	rc = file_read_exact(&file, wire_header, sizeof(wire_header));
	if (rc != 0) {
		goto out;
	}
	rc = ef800_firmware_package_header_parse(
		wire_header, sizeof(wire_header), (size_t)entry.size,
		EF800_FIRMWARE_TARGET_BACKUP, EF800_FIRMWARE_HARDWARE_ID_L496,
		coordinator_version, &package);
	if (rc != 0) {
		goto out;
	}
	if (package.load_address != EF800_FIRMWARE_L496_BACKUP_ADDRESS ||
	    package.image_size > EF800_FIRMWARE_L496_BACKUP_SIZE) {
		rc = -EFBIG;
		goto out;
	}
	rc = file_crc32(&file, package.header_size, package.image_size,
			&payload_crc);
	if (rc != 0 || payload_crc != package.image_crc32) {
		rc = rc != 0 ? rc : -EBADMSG;
		goto out;
	}
	if (fs_seek(&file, package.header_size, FS_SEEK_SET) < 0 ||
	    file_read_exact(&file, vector, sizeof(vector)) != 0 ||
	    !ef800_vector_table_valid(vector, package.load_address,
				      package.image_size, L496_SRAM_BASE,
				      L496_SRAM_SIZE)) {
		rc = -EINVAL;
		goto out;
	}
	rc = flash_area_open(BACKUP_PARTITION_ID, &area);
	if (rc != 0) {
		goto out;
	}
	if (area->fa_size != EF800_FIRMWARE_L496_BACKUP_SIZE) {
		rc = -EXDEV;
		goto out;
	}
	rc = install_payload(&file, area, package.header_size,
			     package.image_size, package.image_crc32);
	if (rc != 0) {
		goto out;
	}
	rc = flash_area_read(area, 0U, vector, sizeof(vector));
	if (rc != 0 ||
	    !ef800_vector_table_valid(vector, EF800_FIRMWARE_L496_BACKUP_ADDRESS,
				      package.image_size, L496_SRAM_BASE,
				      L496_SRAM_SIZE)) {
		rc = rc != 0 ? rc : -EBADMSG;
		goto out;
	}
	rc = fs_close(&file);
	opened = false;
	if (rc == 0) {
		rc = fs_unlink(package_path);
	}
out:
	if (area != NULL) {
		flash_area_close(area);
	}
	if (opened) {
		(void)fs_close(&file);
	}
	return rc;
}
#else
int ef800_backup_update_install(
	const char *package_path,
	const struct ef800_firmware_semver *coordinator_version)
{
	(void)package_path;
	(void)coordinator_version;
	return -ENOTSUP;
}
#endif
