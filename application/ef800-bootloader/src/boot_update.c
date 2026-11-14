/* SPDX-License-Identifier: Apache-2.0 */

#include "boot_update.h"

#include <errno.h>
#include <string.h>

#include <ff.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#include "ef800_update.h"
#include "ef800_firmware_package.h"
#include "ef800_qspi_layout.h"
#include "ef800_watchdog.h"
#include "boot_trace.h"

LOG_MODULE_REGISTER(ef800_boot_update, LOG_LEVEL_WRN);

#define EF800_PACKAGE_PATH "/DATA:/fwu/EF800_APP.pkg"
#define EF800_BOOTSTRAP_PATH "/DATA:/fwu/EF800_FW.bin"
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
		(void)ef800_watchdog_feed();
		remaining -= count;
	}
	if (crc != expected_crc) {
		LOG_ERR("update CRC mismatch: expected=0x%08x actual=0x%08x",
			expected_crc, crc);
		return -EBADMSG;
	}
	return 0;
}

static int erase_partition_watchdog_safe(const struct flash_area *area)
{
	size_t offset = 0U;
	size_t page_count = 0U;

	ef800_boot_trace("boot update: erase begin\r\n");
	while (offset < area->fa_size) {
		struct flash_pages_info page = {0};
		off_t absolute_offset = area->fa_off + (off_t)offset;
		int rc = flash_get_page_info_by_offs(area->fa_dev, absolute_offset,
						     &page);

		if (rc != 0 || page.size == 0U ||
		    page.start_offset != (off_t)absolute_offset ||
		    page.size > area->fa_size - offset) {
			LOG_ERR("invalid erase page at offset=%u: %d size=%u",
				(unsigned int)offset, rc, (unsigned int)page.size);
			ef800_boot_trace("boot update: invalid erase page\r\n");
			return rc != 0 ? rc : -EINVAL;
		}
		rc = flash_area_erase(area, offset, page.size);
		if (rc < 0) {
			LOG_ERR("partition erase failed at offset=%u size=%u: %d",
				(unsigned int)offset, (unsigned int)page.size, rc);
			ef800_boot_trace("boot update: erase failed\r\n");
			return rc;
		}
		offset += page.size;
		page_count++;
		(void)ef800_watchdog_feed();
		if ((page_count % 32U) == 0U) {
			LOG_INF("main partition erase progress: %u/%u bytes",
				(unsigned int)offset, (unsigned int)area->fa_size);
		}
	}
	ef800_boot_trace("boot update: erase complete\r\n");
	return 0;
}

static bool partition_is_valid(const struct flash_area *area, uintptr_t address,
				       uintptr_t sram_address, size_t sram_size)
{
	uint32_t vector[2] = {0U, 0U};

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
	LOG_INF("erasing main partition page-by-page: size=%u",
		(unsigned int)area->fa_size);
	rc = erase_partition_watchdog_safe(area);
	if (rc < 0) {
		LOG_ERR("main partition erase failed: %d", rc);
		ef800_boot_trace_result("partition erase", rc);
		return rc;
	}
	LOG_INF("main partition erase complete");
	while (remaining != 0U) {
		size_t count = MIN(remaining, sizeof(source));
		size_t write_count = ROUND_UP(count, alignment);

		if (write_count > sizeof(source) || destination > area->fa_size ||
			write_count > area->fa_size - destination || source_offset < 0) {
			return -EINVAL;
		}
		rc = fs_seek(file, source_offset, FS_SEEK_SET);
		if (rc < 0) {
			LOG_ERR("update seek failed at offset=%ld: %d",
				(long)source_offset, rc);
			ef800_boot_trace_result("source seek", rc);
			return rc;
		}
		rc = file_read_exact(file, source, count);
		if (rc < 0) {
			LOG_ERR("update read failed at offset=%ld count=%u: %d",
				(long)source_offset, (unsigned int)count, rc);
			ef800_boot_trace_result("source read", rc);
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
			LOG_ERR("main flash write failed at offset=%u count=%u: %d",
				(unsigned int)destination, (unsigned int)write_count, rc);
			ef800_boot_trace_result("main flash write", rc);
			return rc;
		}
		rc = flash_area_read(area, destination, verify, write_count);
		if (rc < 0 || memcmp(source, verify, write_count) != 0) {
			LOG_ERR("main flash readback failed at offset=%u count=%u: %d",
				(unsigned int)destination, (unsigned int)write_count, rc);
			ef800_boot_trace_result("main flash readback", rc < 0 ? rc : -EIO);
			return rc < 0 ? rc : -EIO;
		}
		source_offset += (off_t)count;
		destination += write_count;
		remaining -= count;
		(void)ef800_watchdog_feed();
	}
	ef800_boot_trace("boot update: program/readback complete\r\n");
	LOG_INF("main image programmed and read back: size=%u",
		(unsigned int)image_size);
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
	struct ef800_firmware_package_header package_header;
	struct ef800_firmware_semver coordinator_version = {
		.major = EF800_FIRMWARE_INITIAL_VERSION_MAJOR,
		.minor = EF800_FIRMWARE_INITIAL_VERSION_MINOR,
		.patch = EF800_FIRMWARE_INITIAL_VERSION_PATCH,
	};
	uint8_t package_wire_header[EF800_FIRMWARE_PACKAGE_HEADER_SIZE];
	uint32_t magic;
	uint32_t vector[2];
	uint32_t expected_crc = 0U;
	const char *update_path = EF800_PACKAGE_PATH;
	off_t source_offset = 0;
	size_t image_size;
	bool mounted = false;
	bool opened = false;
	bool has_crc = false;
	bool legacy_layout;
	int rc;

	fs_file_t_init(&file);
	LOG_INF("EF800 Application update scan: %s", EF800_PACKAGE_PATH);
	ef800_boot_trace("boot update: scan begin\r\n");
	rc = flash_area_open(PARTITION_ID(qspi_bootstate_partition),
		&boot_state_area);
	if (rc != 0) {
		LOG_ERR("boot-state partition open failed: %d", rc);
		ef800_boot_trace_result("state partition open", rc);
		return rc;
	}
	rc = ef800_qspi_layout_check_legacy_area(boot_state_area,
		&legacy_layout);
	flash_area_close(boot_state_area);
	if (rc != 0) {
		LOG_ERR("QSPI layout check failed: %d", rc);
		ef800_boot_trace_result("QSPI layout check", rc);
		return rc;
	}
	if (legacy_layout) {
		LOG_WRN("legacy monolithic QSPI FAT detected; EF800 Application update scan skipped");
		return -ENOTSUP;
	}
	rc = fs_mount(&mount);
	if (rc < 0) {
		LOG_ERR("update filesystem mount failed: %d", rc);
		ef800_boot_trace_result("filesystem mount", rc);
		return rc;
	}
	mounted = true;
	LOG_INF("update filesystem mounted");
	ef800_boot_trace("boot update: filesystem mounted\r\n");
	rc = fs_stat(update_path, &entry);
	if (rc < 0) {
		/*
		 * The raw legacy path is a recovery/bootstrap fallback only. Never
		 * let it overwrite a valid, package-aware EF800 Application.
		 */
		if (partition_is_valid(main_area, main_address, sram_address,
				       sram_size)) {
			rc = -ENOENT;
			goto out;
		}
		update_path = EF800_BOOTSTRAP_PATH;
		rc = fs_stat(update_path, &entry);
	}
	if (rc < 0 || entry.type != FS_DIR_ENTRY_FILE) {
		rc = rc < 0 ? rc : -ENOENT;
		LOG_WRN("update file unavailable: %d", rc);
		ef800_boot_trace_result("update file lookup", rc);
		goto out;
	}
	LOG_INF("update file found: size=%u", (unsigned int)entry.size);
	ef800_boot_trace("boot update: update file found\r\n");
	if (entry.size < EF800_VECTOR_BYTES) {
		rc = -EINVAL;
		LOG_ERR("update file is too small: %u", (unsigned int)entry.size);
		ef800_boot_trace_result("update file size", rc);
		goto out;
	}
	rc = fs_open(&file, update_path, FS_O_READ);
	if (rc < 0) {
		LOG_ERR("update file open failed: %d", rc);
		ef800_boot_trace_result("update file open", rc);
		goto out;
	}
	opened = true;
	if (fs_seek(&file, 0, FS_SEEK_SET) < 0 ||
		file_read_exact(&file, &magic, sizeof(magic)) < 0) {
		rc = -EIO;
		goto out;
	}
	if (strcmp(update_path, EF800_PACKAGE_PATH) == 0) {
		if (entry.size < sizeof(package_wire_header) ||
		    fs_seek(&file, 0, FS_SEEK_SET) < 0 ||
		    file_read_exact(&file, package_wire_header,
				    sizeof(package_wire_header)) < 0) {
			rc = -EINVAL;
			ef800_boot_trace_result("package header read", rc);
			goto out;
		}
		rc = ef800_firmware_package_header_parse(
			package_wire_header, sizeof(package_wire_header),
			(size_t)entry.size,
			EF800_FIRMWARE_TARGET_APPLICATION,
			EF800_FIRMWARE_HARDWARE_ID_L496,
			&coordinator_version, &package_header);
		if (rc != 0) {
			ef800_boot_trace_result("package header validation", rc);
			goto out;
		}
		image_size = package_header.image_size;
		source_offset = package_header.header_size;
		expected_crc = package_header.image_crc32;
		has_crc = true;
		LOG_INF("package image: size=%u base=0x%08x crc=0x%08x version=%u.%u.%u",
			(unsigned int)image_size, package_header.load_address,
			package_header.image_crc32, package_header.image_version.major,
			package_header.image_version.minor,
			package_header.image_version.patch);
	} else if (magic == EF800_UPDATE_MAGIC) {
		if (entry.size < sizeof(header) || fs_seek(&file, 0, FS_SEEK_SET) < 0 ||
			file_read_exact(&file, &header, sizeof(header)) < 0 ||
			!ef800_update_header_valid(&header, entry.size, main_address,
							    main_area->fa_size)) {
			rc = -EINVAL;
			ef800_boot_trace_result("update header validation", rc);
			goto out;
		}
		image_size = header.image_size;
		source_offset = header.header_size;
		expected_crc = header.image_crc32;
		has_crc = true;
		LOG_INF("header image: size=%u base=0x%08x crc=0x%08x",
			(unsigned int)image_size, header.load_address,
			header.image_crc32);
	} else {
		image_size = entry.size;
		LOG_INF("raw image: size=%u", (unsigned int)image_size);
	}
	if ((uintmax_t)source_offset > (uintmax_t)entry.size ||
		image_size > main_area->fa_size ||
		image_size > (size_t)entry.size - (size_t)source_offset ||
		fs_seek(&file, source_offset, FS_SEEK_SET) < 0 ||
		file_read_exact(&file, vector, sizeof(vector)) < 0 ||
		!ef800_vector_table_valid(vector, main_address, image_size,
					  sram_address, sram_size)) {
		rc = -EINVAL;
		LOG_ERR("image/vector validation failed: size=%u msp=0x%08x reset=0x%08x",
			(unsigned int)image_size, vector[0], vector[1]);
		ef800_boot_trace_result("image/vector validation", -EINVAL);
		goto out;
	}
	ef800_boot_trace("boot update: image/vector validated\r\n");
	if (has_crc) {
		rc = verify_update_crc(&file, source_offset, image_size,
				       expected_crc);
		if (rc < 0) {
			LOG_ERR("update CRC validation failed: %d", rc);
			ef800_boot_trace_result("CRC validation", rc);
			goto out;
		}
		LOG_INF("update CRC validated");
		ef800_boot_trace("boot update: CRC validated\r\n");
	}
	rc = copy_update_image(&file, main_area, source_offset, image_size, has_crc,
					       expected_crc);
	if (rc < 0 || !partition_is_valid(main_area, main_address, sram_address,
						  sram_size)) {
		rc = rc < 0 ? rc : -EBADMSG;
		LOG_ERR("installed image validation failed: %d", rc);
		ef800_boot_trace("boot update: installed image verification failed\r\n");
		goto out;
	}
	LOG_INF("installed EF800 Application image verified");
	ef800_boot_trace("boot update: image verified\r\n");
	rc = fs_close(&file);
	opened = false;
	if (rc == 0) {
		/* Deletion is best effort; the verified image is already installed. */
		int unlink_rc = fs_unlink(update_path);

		LOG_INF("update file cleanup: %d", unlink_rc);
		ef800_boot_trace("boot update: update file cleanup attempted\r\n");
	}
out:
	if (opened) {
		(void)fs_close(&file);
	}
	if (mounted) {
		int unmount_rc = fs_unmount(&mount);

		LOG_INF("update filesystem unmount: %d", unmount_rc);
		if (rc == 0) {
			rc = unmount_rc;
		}
	}
	LOG_INF("EF800 Application update scan complete: %d", rc);
	ef800_boot_trace_result("scan complete", rc);
	return rc;
}
