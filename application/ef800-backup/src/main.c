/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Run BackupApplication recovery, storage, and USB services. */

#include <stdbool.h>
#include <errno.h>

#include <ff.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include "ef800_watchdog.h"
#include "ef800_console.h"
#include "ef800_qspi_layout.h"
#include "ef800_backup_usb_mtp.h"
#include "ef800_backup_composition.h"
#include "ef800_lcd_contrast.h"
#include "ef800_lcd_contrast_storage.h"
#include "ef800_lcd_contrast_stm32.h"

LOG_MODULE_REGISTER(ef800_backup, LOG_LEVEL_INF);

#define EF800_BACKUP_CONFIGURATION_MOUNT "/EF800:"
#define EF800_BACKUP_DATA_MOUNT "/DATA:"

static K_THREAD_STACK_DEFINE(watchdog_stack, 512);
static struct k_thread watchdog_thread;
static FATFS configuration_fat_fs;
static FATFS data_fat_fs;
static struct fs_mount_t configuration_mount = {
	.type = FS_FATFS,
	.mnt_point = EF800_BACKUP_CONFIGURATION_MOUNT,
	.fs_data = &configuration_fat_fs,
	.storage_dev = (void *)PARTITION_ID(qspi_configuration_partition),
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static struct fs_mount_t data_mount = {
	.type = FS_FATFS,
	.mnt_point = EF800_BACKUP_DATA_MOUNT,
	.fs_data = &data_fat_fs,
	.storage_dev = (void *)PARTITION_ID(qspi_storage_partition),
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static struct ef800_lcd_contrast lcd_contrast;

static int backup_contrast_set_pwm(void *context, uint8_t duty_percent)
{
	ARG_UNUSED(context);
	return ef800_lcd_contrast_stm32_set(duty_percent);
}

static void backup_contrast_diagnostic(void *context, const char *message,
				       int error)
{
	ARG_UNUSED(context);
	LOG_WRN("%s: %d", message, error);
}

static const struct ef800_lcd_contrast_ops backup_contrast_ops = {
	.load_duty = ef800_lcd_contrast_storage_load_duty,
	.save_duty = ef800_lcd_contrast_storage_save_duty,
	.set_pwm = backup_contrast_set_pwm,
	.diagnostic = backup_contrast_diagnostic,
};

static void backup_watchdog_task(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		(void)ef800_watchdog_feed();
		k_msleep(250U);
	}
}

static void backup_watchdog_start(void)
{
	if (ef800_watchdog_start(1000U) != 0) {
		return;
	}
	(void)k_thread_create(&watchdog_thread, watchdog_stack,
				      K_THREAD_STACK_SIZEOF(watchdog_stack),
				      backup_watchdog_task, NULL, NULL, NULL,
				      K_PRIO_PREEMPT(2), 0, K_NO_WAIT);
}
#define EF800_USB_UDC_NODE DT_NODELABEL(usbotg_fs)

BUILD_ASSERT(DT_NODE_HAS_STATUS(EF800_USB_UDC_NODE, okay),
		     "EF800 recovery requires an enabled USB device controller");

static int ensure_directory(const char *path)
{
	struct fs_dirent entry;
	int ret = fs_stat(path, &entry);

	if (ret == 0) {
		return entry.type == FS_DIR_ENTRY_DIR ? 0 : -ENOTDIR;
	}
	ret = fs_mkdir(path);
	return ret == -EEXIST ? 0 : ret;
}

static int create_recovery_roots(void)
{
	static const char *const paths[] = {
		"/EF800:/.configuration",
		"/DATA:/data",
		"/DATA:/plugins",
		"/DATA:/fwu",
	};

	for (size_t index = 0U; index < ARRAY_SIZE(paths); ++index) {
		int ret = ensure_directory(paths[index]);

		if (ret != 0) {
			LOG_ERR("Cannot prepare MTP root %s: %d", paths[index], ret);
			return ret;
		}
	}
	return 0;
}

int main(void)
{
	const struct device *const udc = DEVICE_DT_GET(EF800_USB_UDC_NODE);
	const struct flash_area *boot_state_area;
	bool legacy_layout;
	bool configuration_mounted = false;
	bool data_mounted = false;
	int ret;

	(void)ef800_console_init(true);
	printk("ef800 backup application: console=%u\r\n",
	       ef800_console_get_target());
	backup_watchdog_start();

	if (!device_is_ready(udc)) {
		LOG_ERR("USB device controller unavailable; recovery MTP not started");
		goto wait;
	}
	ret = flash_area_open(PARTITION_ID(qspi_bootstate_partition),
		&boot_state_area);
	if (ret != 0) {
		LOG_ERR("Boot-state QSPI partition unavailable: %d", ret);
		goto wait;
	}
	ret = ef800_qspi_layout_check_legacy_area(boot_state_area, &legacy_layout);
	flash_area_close(boot_state_area);
	if (ret != 0 || legacy_layout) {
		LOG_ERR("Legacy or unavailable QSPI layout; recovery MTP not started: %d",
			ret != 0 ? ret : -ENOTSUP);
		goto wait;
	}
	ret = fs_mount(&configuration_mount);
	if (ret != 0) {
		LOG_ERR("Configuration FAT mount failed (no format attempted): %d", ret);
		goto wait;
	}
	configuration_mounted = true;
	ret = fs_mount(&data_mount);
	if (ret != 0) {
		LOG_ERR("Shared-data FAT mount failed (no format attempted): %d", ret);
		goto cleanup;
	}
	data_mounted = true;
	ret = create_recovery_roots();
	if (ret != 0) {
		goto cleanup;
	}
	{
		enum ef800_lcd_contrast_mode mode;

		ret = ef800_backup_composition_lcd_contrast_mode(&mode);
		if (ret == 0) {
			ret = ef800_lcd_contrast_init(&lcd_contrast, mode,
				&backup_contrast_ops, NULL);
		}
		if (ret != 0) {
			LOG_WRN("Backup LCD contrast initialization failed: %d", ret);
		}
	}
	ret = ef800_backup_usb_mtp_start();
	if (ret != 0) {
		LOG_ERR("Recovery MTP start failed: %d", ret);
		goto cleanup;
	}
	LOG_INF("Backup recovery MTP active: Configuration, Data, Plugins, FWU");
	goto wait;

cleanup:
	if (data_mounted) {
		(void)fs_unmount(&data_mount);
	}
	if (configuration_mounted) {
		(void)fs_unmount(&configuration_mount);
	}

wait:
	while (true) {
		k_sleep(K_FOREVER);
	}
}
