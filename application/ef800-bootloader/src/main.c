/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>

#include <cmsis_core.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#if CONFIG_EF800_BOOT_STORAGE
#include <ff.h>
#include <zephyr/fs/fs.h>
#endif

#if CONFIG_HWINFO
#include <zephyr/drivers/hwinfo.h>
#endif

#include "ef800_update.h"

LOG_MODULE_REGISTER(ef800_bootloader, LOG_LEVEL_INF);

#define EF800_VECTOR_BYTES  (2U * sizeof(uint32_t))
#define EF800_UPDATE_PATH   "/EF800:/fwu/EF800_FW.bin"
#define EF800_UPDATE_CHUNK 1024U

#define MAIN_PARTITION_ID PARTITION_ID(main_partition)
#define BACKUP_PARTITION_ID PARTITION_ID(backup_partition)

BUILD_ASSERT(PARTITION_EXISTS(main_partition), "main partition is required");
BUILD_ASSERT(PARTITION_EXISTS(backup_partition), "backup partition is required");
BUILD_ASSERT(PARTITION_EXISTS(metadata_partition), "metadata partition is required");
BUILD_ASSERT(PARTITION_OFFSET(boot_partition) == 0x00000000U,
	     "bootloader partition offset must remain 0x00000000");
BUILD_ASSERT(PARTITION_SIZE(boot_partition) == 0x00010000U,
	     "bootloader partition size must remain 64 KiB");
BUILD_ASSERT(PARTITION_OFFSET(backup_partition) == 0x00010000U,
	     "backup partition offset must remain 0x00010000");
BUILD_ASSERT(PARTITION_SIZE(backup_partition) == 0x00010000U,
	     "backup partition size must remain 64 KiB");
BUILD_ASSERT(PARTITION_OFFSET(metadata_partition) == 0x00020000U,
	     "metadata partition offset must remain 0x00020000");
BUILD_ASSERT(PARTITION_SIZE(metadata_partition) == 0x00010000U,
	     "metadata partition size must remain 64 KiB");
BUILD_ASSERT(PARTITION_OFFSET(main_partition) == 0x00030000U,
	     "main partition offset must remain 0x00030000");
BUILD_ASSERT(PARTITION_SIZE(main_partition) == 0x000D0000U,
	     "main partition size must remain 832 KiB");

static const uintptr_t sram_address = DT_REG_ADDR(DT_NODELABEL(sram0));
static const size_t sram_size = DT_REG_SIZE(DT_NODELABEL(sram0));

static int read_vector(const struct flash_area *area, uint32_t vector[2])
{
	if (!flash_area_device_is_ready(area) || area->fa_size < EF800_VECTOR_BYTES) {
		return -ENODEV;
	}

	return flash_area_read(area, 0, vector, EF800_VECTOR_BYTES);
}

static bool partition_is_valid(const struct flash_area *area, uintptr_t address)
{
	uint32_t vector[2];

	if (read_vector(area, vector) != 0) {
		return false;
	}

	return ef800_vector_table_valid(vector, address, area->fa_size,
					 sram_address, sram_size);
}

#if CONFIG_EF800_BOOT_METADATA
static int metadata_read(struct ef800_boot_metadata *metadata, bool *valid)
{
	const struct flash_area *area;
	int rc;

	*valid = false;
	rc = flash_area_open(PARTITION_ID(metadata_partition), &area);
	if (rc < 0) {
		return rc;
	}
	if (!flash_area_device_is_ready(area) ||
		area->fa_size < sizeof(*metadata)) {
		return -ENODEV;
	}

	rc = flash_area_read(area, 0, metadata, sizeof(*metadata));
	if (rc == 0) {
		*valid = ef800_boot_metadata_valid(metadata);
	}

	return rc;
}

static int metadata_write(struct ef800_boot_metadata *metadata)
{
	const struct flash_area *area;
	struct ef800_boot_metadata verify __aligned(8);
	int rc;

	metadata->sequence++;
	metadata->size = sizeof(*metadata);
	metadata->crc32 = ef800_boot_metadata_crc(metadata);

	rc = flash_area_open(PARTITION_ID(metadata_partition), &area);
	if (rc < 0) {
		return rc;
	}
	if (!flash_area_device_is_ready(area) ||
		area->fa_size < sizeof(*metadata)) {
		return -ENODEV;
	}

	rc = flash_area_erase(area, 0, area->fa_size);
	if (rc < 0) {
		return rc;
	}

	rc = flash_area_write(area, 0, metadata, sizeof(*metadata));
	if (rc < 0) {
		return rc;
	}

	rc = flash_area_read(area, 0, &verify, sizeof(verify));
	if (rc < 0 || memcmp(&verify, metadata, sizeof(verify)) != 0 ||
		!ef800_boot_metadata_valid(&verify)) {
		return rc < 0 ? rc : -EIO;
	}

	return 0;
}

static bool reset_was_watchdog(void)
{
#if CONFIG_HWINFO
	uint32_t cause;
	bool watchdog = false;

	if (hwinfo_get_reset_cause(&cause) == 0) {
		watchdog = (cause & RESET_WATCHDOG) != 0U;
		/* Prevent accumulated reset flags from counting the same failure twice. */
		(void)hwinfo_clear_reset_cause();
	}
	return watchdog;
#else
	return false;
#endif
}
#endif

#if CONFIG_EF800_BOOT_STORAGE
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

	rc = flash_area_erase(area, 0, area->fa_size);
	if (rc < 0) {
		return rc;
	}

	while (remaining != 0U) {
		size_t count = MIN(remaining, sizeof(source));
		size_t write_count = ROUND_UP(count, alignment);

		if (write_count > sizeof(source) || destination > area->fa_size ||
			write_count > (area->fa_size - destination) || source_offset < 0) {
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
		if (rc < 0) {
			return rc;
		}
		if (memcmp(source, verify, write_count) != 0) {
			return -EIO;
		}

		source_offset += (off_t)count;
		destination += write_count;
		remaining -= count;
	}

	return !has_crc || crc == expected_crc ? 0 : -EBADMSG;
}

static int install_update(void)
{
	static FATFS fat_fs;
	struct fs_mount_t mount = {
		.type = FS_FATFS,
		.mnt_point = "/EF800:",
		.fs_data = &fat_fs,
		.storage_dev = (void *)PARTITION_ID(qspi_storage_partition),
		.flags = FS_MOUNT_FLAG_NO_FORMAT,
	};
	struct fs_file_t file;
	struct fs_dirent entry;
	struct ef800_update_header header;
	const struct flash_area *area;
	uint32_t magic;
	uint32_t vector[2];
	off_t source_offset = 0;
	size_t image_size;
	bool mounted = false;
	bool opened = false;
	bool has_crc = false;
	int rc;

	fs_file_t_init(&file);
	rc = fs_mount(&mount);
	if (rc < 0) {
		LOG_WRN("update disk mount failed: %d", rc);
		return rc;
	}
	mounted = true;

	rc = fs_stat(EF800_UPDATE_PATH, &entry);
	if (rc < 0 || entry.type != FS_DIR_ENTRY_FILE) {
		rc = rc < 0 ? rc : -ENOENT;
		goto out;
	}
	if (entry.size < EF800_VECTOR_BYTES) {
		rc = -EINVAL;
		goto out;
	}

	rc = fs_open(&file, EF800_UPDATE_PATH, FS_O_READ);
	if (rc < 0) {
		goto out;
	}
	opened = true;

	rc = flash_area_open(MAIN_PARTITION_ID, &area);
	if (rc < 0 || !flash_area_device_is_ready(area)) {
		rc = rc < 0 ? rc : -ENODEV;
		goto out;
	}

	if (fs_seek(&file, 0, FS_SEEK_SET) < 0 ||
		file_read_exact(&file, &magic, sizeof(magic)) < 0) {
		rc = -EIO;
		goto out;
	}

	if (magic == EF800_UPDATE_MAGIC) {
		if (entry.size < sizeof(header) ||
			fs_seek(&file, 0, FS_SEEK_SET) < 0 ||
			file_read_exact(&file, &header, sizeof(header)) < 0 ||
			!ef800_update_header_valid(&header, entry.size,
						   PARTITION_ADDRESS(main_partition),
						   area->fa_size)) {
			rc = -EINVAL;
			goto out;
		}
		image_size = header.image_size;
		source_offset = header.header_size;
		has_crc = true;
	} else {
		image_size = entry.size;
	}
	if (source_offset < 0 || (uintmax_t)source_offset > (uintmax_t)entry.size ||
		image_size > area->fa_size ||
		image_size > ((size_t)entry.size - (size_t)source_offset)) {
		rc = -EFBIG;
		goto out;
	}

	if (fs_seek(&file, source_offset, FS_SEEK_SET) < 0 ||
		file_read_exact(&file, vector, sizeof(vector)) < 0 ||
		!ef800_vector_table_valid(vector, PARTITION_ADDRESS(main_partition),
					   image_size, sram_address, sram_size)) {
		rc = -EINVAL;
		goto out;
	}

	rc = copy_update_image(&file, area, source_offset, image_size, has_crc,
				       has_crc ? header.image_crc32 : 0U);
	if (rc < 0) {
		goto out;
	}

	if (!partition_is_valid(area, PARTITION_ADDRESS(main_partition))) {
		rc = -EBADMSG;
		goto out;
	}

	rc = fs_close(&file);
	opened = false;
	if (rc < 0) {
		goto out;
	}

	/* Deleting is deliberately last; a failed delete leaves the valid image. */
	rc = fs_unlink(EF800_UPDATE_PATH);
	if (rc < 0) {
		LOG_WRN("verified update could not be removed: %d", rc);
		rc = 0;
	} else {
		LOG_INF("verified update installed");
	}

out:
	if (opened) {
		int close_rc = fs_close(&file);

		if (rc == 0 && close_rc < 0) {
			rc = close_rc;
		}
	}
	if (mounted) {
		int unmount_rc = fs_unmount(&mount);

		if (rc == 0 && unmount_rc < 0) {
			rc = unmount_rc;
		}
	}

	return rc;
}
#endif

static FUNC_NORETURN void handoff(const struct flash_area *area, uintptr_t address)
{
	uint32_t vector[2];
	void (*reset_handler)(void);

	if (read_vector(area, vector) != 0 ||
		!ef800_vector_table_valid(vector, address, area->fa_size,
					  sram_address, sram_size)) {
		LOG_ERR("refusing handoff to invalid image at 0x%lx",
			(unsigned long)address);
		while (true) {
			k_sleep(K_FOREVER);
		}
	}

	reset_handler = (void (*)(void))(uintptr_t)vector[1];
	__disable_irq();
	SysTick->CTRL = 0U;
	SCB->VTOR = (uint32_t)address;
	__DSB();
	__ISB();
	__set_MSP(vector[0]);
	__DSB();
	__ISB();
	reset_handler();

	/* A valid application reset handler must not return. */
	sys_reboot(SYS_REBOOT_COLD);
}

int main(void)
{
	const struct flash_area *main_area;
	const struct flash_area *backup_area;
#if CONFIG_EF800_BOOT_METADATA
	struct ef800_boot_metadata metadata __aligned(8);
	bool metadata_valid = false;
	bool metadata_dirty = false;
	bool watchdog_reset = false;
#endif
	bool main_valid;
	bool backup_valid;
	bool use_backup = false;
#if CONFIG_EF800_BOOT_STORAGE
	bool update_installed = false;
#endif
	int rc;

	rc = flash_area_open(MAIN_PARTITION_ID, &main_area);
	if (rc < 0 || !flash_area_device_is_ready(main_area)) {
		LOG_ERR("main partition unavailable: %d", rc);
		return 0;
	}
	rc = flash_area_open(BACKUP_PARTITION_ID, &backup_area);
	if (rc < 0 || !flash_area_device_is_ready(backup_area)) {
		LOG_ERR("backup partition unavailable: %d", rc);
		return 0;
	}

#if CONFIG_EF800_BOOT_METADATA
	rc = metadata_read(&metadata, &metadata_valid);
	if (rc < 0 || !metadata_valid) {
		LOG_WRN("boot metadata unavailable or invalid: %d", rc);
		ef800_boot_metadata_init(&metadata);
		metadata_valid = false;
		metadata_dirty = true;
	}

	if (metadata_valid) {
		watchdog_reset = reset_was_watchdog();
		if (watchdog_reset &&
			metadata.last_target == EF800_BOOT_TARGET_MAIN &&
			metadata.main_failures < UINT32_MAX) {
			metadata.main_failures++;
			metadata_dirty = true;
			LOG_WRN("main watchdog failure %u/%u", metadata.main_failures,
				CONFIG_EF800_BOOT_MAIN_FAILURE_LIMIT);
		}
	}
#endif

#if CONFIG_EF800_BOOT_STORAGE
	rc = install_update();
	update_installed = rc == 0;
	if (rc < 0 && rc != -ENOENT && rc != -ENODEV) {
		LOG_WRN("update inspection failed: %d", rc);
	}
#if CONFIG_EF800_BOOT_METADATA
	if (update_installed) {
		metadata.main_failures = 0U;
		metadata.last_target = EF800_BOOT_TARGET_MAIN;
		metadata_dirty = true;
	}
#endif
#endif

	main_valid = partition_is_valid(main_area, PARTITION_ADDRESS(main_partition));
	backup_valid = partition_is_valid(backup_area,
					 PARTITION_ADDRESS(backup_partition));

#if CONFIG_EF800_BOOT_METADATA
	use_backup = metadata.main_failures >= CONFIG_EF800_BOOT_MAIN_FAILURE_LIMIT;
#endif

	if (use_backup && backup_valid) {
		LOG_WRN("main failure limit reached; starting backup");
#if CONFIG_EF800_BOOT_METADATA
		metadata.last_target = EF800_BOOT_TARGET_BACKUP;
		metadata_dirty = true;
		if (metadata_dirty) {
			rc = metadata_write(&metadata);
			if (rc < 0) {
				LOG_ERR("boot metadata write failed: %d", rc);
			}
		}
#endif
		handoff(backup_area, PARTITION_ADDRESS(backup_partition));
	}

	if (main_valid) {
#if CONFIG_EF800_BOOT_METADATA
		/* A non-watchdog reset is the only safe indication that main ran. */
		if (!watchdog_reset && metadata.main_failures != 0U) {
			metadata.main_failures = 0U;
			metadata_dirty = true;
		}
		if (metadata.last_target != EF800_BOOT_TARGET_MAIN) {
			metadata.last_target = EF800_BOOT_TARGET_MAIN;
			metadata_dirty = true;
		}
		if (metadata_dirty) {
			rc = metadata_write(&metadata);
			if (rc < 0) {
				LOG_ERR("boot metadata write failed: %d", rc);
			}
		}
#endif
		LOG_INF("starting main application");
		handoff(main_area, PARTITION_ADDRESS(main_partition));
	}

	if (backup_valid) {
#if CONFIG_EF800_BOOT_METADATA
		if (metadata.last_target != EF800_BOOT_TARGET_BACKUP) {
			metadata.last_target = EF800_BOOT_TARGET_BACKUP;
			metadata_dirty = true;
		}
		if (metadata_dirty) {
			rc = metadata_write(&metadata);
			if (rc < 0) {
				LOG_ERR("boot metadata write failed: %d", rc);
			}
		}
#endif
		LOG_WRN("main application invalid; starting backup");
		handoff(backup_area, PARTITION_ADDRESS(backup_partition));
	}

	LOG_ERR("no valid main or backup application");
	while (true) {
		k_sleep(K_FOREVER);
	}
}
