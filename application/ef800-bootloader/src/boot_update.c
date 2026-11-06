/* SPDX-License-Identifier: Apache-2.0 */

#include "boot_update.h"

#include <errno.h>
#include <string.h>

#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#include "ef800_update.h"
#include "ef800_qspi_layout.h"

LOG_MODULE_REGISTER(ef800_boot_update, LOG_LEVEL_WRN);

#define EF800_UPDATE_PATH "/DATA:/fwu/EF800_FW.bin"
#define EF800_UPDATE_CHUNK 1024U
#define EF800_VECTOR_BYTES (2U * sizeof(uint32_t))

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

static int verify_update_crc(struct fs_file_t *file, off_t source_offset,
			     size_t image_size, uint32_t expected_crc)
{
	uint8_t buffer[EF800_UPDATE_CHUNK];
	size_t remaining = image_size;
	uint32_t crc = 0U;
	int rc;

	if (source_offset < 0 || fs_seek(file, source_offset, FS_SEEK_SET) < 0) {
		return -EIO;
	}
	while (remaining != 0U) {
		size_t count = MIN(remaining, sizeof(buffer));

		rc = file_read_exact(file, buffer, count);
		if (rc != 0) {
			return rc;
		}
		crc = ef800_crc32_update(crc, buffer, count);
		remaining -= count;
	}
	return crc == expected_crc ? 0 : -EBADMSG;
}

static bool partition_is_valid(const struct flash_area *area, uintptr_t address,
				       uintptr_t sram_address, size_t sram_size)
{
	uint32_t vector[2];

	if (flash_area_read(area, 0U, vector, sizeof(vector)) != 0) {
		return false;
	}
	return ef800_vector_table_valid(vector, address, area->fa_size,
					sram_address, sram_size);
}

static int copy_update_image(struct fs_file_t *file, const struct flash_area *area,
				     off_t source_offset, size_t image_size,
				     bool has_crc, uint32_t expected_crc)
{
	uint8_t source[EF800_UPDATE_CHUNK] __aligned(8);
	uint8_t verify[EF800_UPDATE_CHUNK] __aligned(8);
	size_t remaining = image_size;
	size_t destination = 0U;
	size_t alignment = flash_area_align(area);
	uint32_t crc = 0U;
	int rc;

	if (alignment == 0U || alignment > sizeof(source)) {
		return -EINVAL;
	}
	rc = flash_area_erase(area, 0U, area->fa_size);
	if (rc < 0) {
		return rc;
	}
	while (remaining != 0U) {
		size_t count = MIN(remaining, sizeof(source));
		size_t write_count = ROUND_UP(count, alignment);

		if (write_count > sizeof(source) || destination > area->fa_size ||
			write_count > area->fa_size - destination || source_offset < 0) {
			return -EINVAL;
		}
		if (fs_seek(file, source_offset, FS_SEEK_SET) < 0 ||
			file_read_exact(file, source, count) < 0) {
			return -EIO;
		}
		if (write_count > count) {
			memset(source + count, 0xff, write_count - count);
		}
		if (has_crc) {
			crc = ef800_crc32_update(crc, source, count);
		}
		rc = flash_area_write(area, destination, source, write_count);
		if (rc < 0) {
			return rc;
		}
		rc = flash_area_read(area, destination, verify, write_count);
		if (rc < 0 || memcmp(source, verify, write_count) != 0) {
			return rc < 0 ? rc : -EIO;
		}
		source_offset += (off_t)count;
		destination += write_count;
		remaining -= count;
	}
	return !has_crc || crc == expected_crc ? 0 : -EBADMSG;
}

int ef800_boot_install_update(const struct flash_area *main_area,
				      uintptr_t main_address,
				      uintptr_t sram_address, size_t sram_size)
{
	static FATFS fat_fs;
	struct fs_mount_t mount = {
		.type = FS_FATFS,
		.mnt_point = "/DATA:",
		.fs_data = &fat_fs,
		.storage_dev = (void *)PARTITION_ID(qspi_storage_partition),
		.flags = FS_MOUNT_FLAG_NO_FORMAT,
	};
	struct fs_file_t file;
	struct fs_dirent entry;
	const struct flash_area *boot_state_area;
	struct ef800_update_header header;
	uint32_t magic;
	uint32_t vector[2];
	off_t source_offset = 0;
	size_t image_size;
	bool mounted = false;
	bool opened = false;
	bool has_crc = false;
	bool legacy_layout;
	int rc;

	fs_file_t_init(&file);
	rc = flash_area_open(PARTITION_ID(qspi_bootstate_partition),
		&boot_state_area);
	if (rc != 0) {
		return rc;
	}
	rc = ef800_qspi_layout_check_legacy_area(boot_state_area,
		&legacy_layout);
	flash_area_close(boot_state_area);
	if (rc != 0) {
		return rc;
	}
	if (legacy_layout) {
		LOG_WRN("legacy monolithic QSPI FAT detected; MainApplication update scan skipped");
		return -ENOTSUP;
	}
	rc = fs_mount(&mount);
	if (rc < 0) {
		return rc;
	}
	mounted = true;
	rc = fs_stat(EF800_UPDATE_PATH, &entry);
	if (rc < 0 || entry.type != FS_DIR_ENTRY_FILE) {
		rc = rc < 0 ? rc : -ENOENT;
		goto out;
	}
	if (entry.size < EF800_VECTOR_BYTES || fs_open(&file, EF800_UPDATE_PATH,
							FS_O_READ) < 0) {
		rc = -EINVAL;
		goto out;
	}
	opened = true;
	if (fs_seek(&file, 0, FS_SEEK_SET) < 0 ||
		file_read_exact(&file, &magic, sizeof(magic)) < 0) {
		rc = -EIO;
		goto out;
	}
	if (magic == EF800_UPDATE_MAGIC) {
		if (entry.size < sizeof(header) || fs_seek(&file, 0, FS_SEEK_SET) < 0 ||
			file_read_exact(&file, &header, sizeof(header)) < 0 ||
			!ef800_update_header_valid(&header, entry.size, main_address,
						    main_area->fa_size)) {
			rc = -EINVAL;
			goto out;
		}
		image_size = header.image_size;
		source_offset = header.header_size;
		has_crc = true;
	} else {
		image_size = entry.size;
	}
	if ((uintmax_t)source_offset > (uintmax_t)entry.size ||
		image_size > main_area->fa_size ||
		image_size > (size_t)entry.size - (size_t)source_offset ||
		fs_seek(&file, source_offset, FS_SEEK_SET) < 0 ||
		file_read_exact(&file, vector, sizeof(vector)) < 0 ||
		!ef800_vector_table_valid(vector, main_address, image_size,
					  sram_address, sram_size)) {
		rc = -EINVAL;
		goto out;
	}
	if (has_crc) {
		rc = verify_update_crc(&file, source_offset, image_size,
				       header.image_crc32);
		if (rc < 0) {
			goto out;
		}
	}
	rc = copy_update_image(&file, main_area, source_offset, image_size, has_crc,
				       has_crc ? header.image_crc32 : 0U);
	if (rc < 0 || !partition_is_valid(main_area, main_address, sram_address,
						  sram_size)) {
		rc = rc < 0 ? rc : -EBADMSG;
		goto out;
	}
	rc = fs_close(&file);
	opened = false;
	if (rc == 0) {
		/* Deletion is best effort; the verified image is already installed. */
		(void)fs_unlink(EF800_UPDATE_PATH);
	}
out:
	if (opened) {
		(void)fs_close(&file);
	}
	if (mounted) {
		int unmount_rc = fs_unmount(&mount);
		if (rc == 0) {
			rc = unmount_rc;
		}
	}
	return rc;
}
