/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_app.h"

#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/sys/util.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/storage/disk_access.h>

#include "ef800_mcp23017.h"
#include "ef800_metadata.h"
#include "ef800_ports.h"
#include "ef800_update.h"
#include "ef800_watchdog.h"
#include "ef800_qspi_layout.h"
#include "ef800_composition.h"
#ifdef CONFIG_BOARD_NATIVE_SIM
#include "virtual_ef800_profile.h"
#endif

uint32_t get_fattime(void)
{
	struct rtc_time time;
	uint32_t year;

	if (ef800_app_rtc_get_time(&time) != 0 || time.tm_year < 80) {
		return ((uint32_t)(2024U - 1980U) << 25) |
			       (1U << 21) | (1U << 16);
	}

	year = (uint32_t)(time.tm_year + 1900 - 1980);
	return (year << 25) | ((uint32_t)(time.tm_mon + 1) << 21) |
		       ((uint32_t)time.tm_mday << 16) | ((uint32_t)time.tm_hour << 11) |
		       ((uint32_t)time.tm_min << 5) | ((uint32_t)time.tm_sec / 2U);
}
#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/fs_sys.h>

LOG_MODULE_REGISTER(ef800_app, LOG_LEVEL_INF);

#define EF800_APPLICATION_CONFIGURATION_DISK_NAME "EF800"
#define EF800_APPLICATION_DATA_DISK_NAME "DATA"
#define EF800_APPLICATION_QSPI_BOOTSTATE_OFFSET 0x000000U
#define EF800_APPLICATION_QSPI_BOOTSTATE_SIZE 0x008000U
#define EF800_APPLICATION_QSPI_CONFIGURATION_OFFSET 0x008000U
#define EF800_APPLICATION_QSPI_CONFIGURATION_SIZE 0x0F8000U
#define EF800_APPLICATION_QSPI_DATA_OFFSET 0x100000U
#define EF800_APPLICATION_QSPI_DATA_SIZE 0x300000U
#define EF800_APPLICATION_FAT_SECTOR_SIZE 512U
#define EF800_APPLICATION_COMPOSITION_DIR "/EF800:/.configuration"
#define EF800_APPLICATION_COMPOSITION_SLOT_A EF800_APPLICATION_COMPOSITION_DIR "/target_profile_a"
#define EF800_APPLICATION_COMPOSITION_SLOT_B EF800_APPLICATION_COMPOSITION_DIR "/target_profile_b"

#if DT_HAS_ALIAS(status_led)
static const struct gpio_dt_spec status_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(status_led), gpios);
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(microchip_mcp23017)
static const struct device *const mcp23017 =
	DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(microchip_mcp23017));
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(maxim_ds3231_rtc)
#define EF800_APPLICATION_DS3231_NODE \
	DT_COMPAT_GET_ANY_STATUS_OKAY(maxim_ds3231_rtc)
static const struct device *const ds3231 =
	DEVICE_DT_GET(EF800_APPLICATION_DS3231_NODE);
#endif

static FATFS configuration_fat_fs;
static FATFS data_fat_fs;
static uint8_t qspi_composition_record[EF800_COMPOSITION_RECORD_MAX];
static uint8_t qspi_composition_readback[EF800_COMPOSITION_RECORD_MAX];
static struct ef800_composition_profile qspi_composition_expected;
static struct ef800_composition_profile qspi_composition_decoded;
static struct fs_mount_t configuration_mount = {
	.type = FS_FATFS,
	.mnt_point = EF800_STORAGE_MOUNT_POINT,
	.fs_data = &configuration_fat_fs,
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static struct fs_mount_t data_mount = {
	.type = FS_FATFS,
	.mnt_point = EF800_STORAGE_DATA_MOUNT_POINT,
	.fs_data = &data_fat_fs,
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};

static K_THREAD_STACK_DEFINE(watchdog_stack,
				     CONFIG_EF800_APPLICATION_WATCHDOG_STACK_SIZE);
static struct k_thread watchdog_thread;

static bool status_led_initialized;
static bool mcp23017_detected;
static bool ds3231_ready;
static bool configuration_mounted;
static bool data_mounted;

static bool storage_directory_exists(const char *path)
{
	struct fs_dirent entry;

	return fs_stat(path, &entry) == 0 && entry.type == FS_DIR_ENTRY_DIR;
}

static int storage_ensure_directory(const char *path)
{
	int ret;

	if (storage_directory_exists(path)) {
		return 0;
	}
	ret = fs_mkdir(path);
	if (ret == -EEXIST && storage_directory_exists(path)) {
		return 0;
	}
	return ret;
}

static int mount_storage_volume(struct fs_mount_t *mount, bool *mounted,
				const char *volume_name)
{
	int ret;

	if (*mounted) {
		return 0;
	}
	ret = fs_mount(mount);
#if defined(CONFIG_BOARD_NATIVE_SIM) && CONFIG_EF800_APPLICATION_STORAGE_FORMAT_ON_MOUNT_FAILURE
	if (ret != 0) {
		LOG_WRN("Disposable native-sim volume %s failed to mount; formatting: %d",
			volume_name, ret);
		ret = fs_mkfs(FS_FATFS, (uintptr_t)volume_name, NULL, 0);
		if (ret == 0) {
			ret = fs_mount(mount);
		}
	}
#endif
	if (ret == 0) {
		*mounted = true;
	}
	return ret;
}

static int qspi_partition_has_fat(uint8_t partition_id, off_t expected_offset,
	size_t expected_size, bool *has_fat)
{
	const struct flash_area *area;
	uint8_t boot_sector[EF800_APPLICATION_FAT_SECTOR_SIZE];
	int ret;

	if (has_fat == NULL) {
		return -EINVAL;
	}
	*has_fat = false;
	ret = flash_area_open(partition_id, &area);
	if (ret != 0) {
		return ret;
	}
	if (area->fa_off != expected_offset || area->fa_size != expected_size) {
		flash_area_close(area);
		return -EINVAL;
	}
	ret = flash_area_read(area, 0U, boot_sector, sizeof(boot_sector));
	flash_area_close(area);
	if (ret == 0) {
		*has_fat = ef800_qspi_layout_is_legacy_fat(boot_sector);
	}
	return ret;
}

static int qspi_verify_fat_partition(uint8_t partition_id, off_t expected_offset,
	size_t expected_size, const char *name)
{
	bool has_fat;
	int ret = qspi_partition_has_fat(partition_id, expected_offset,
		expected_size, &has_fat);

	if (ret != 0) {
		LOG_ERR("QSPI %s boot-sector read failed: %d", name, ret);
		return ret;
	}
	if (!has_fat) {
		LOG_ERR("QSPI %s formatter returned success, but raw FAT boot sector is invalid",
			name);
		return -EIO;
	}
	LOG_INF("QSPI %s FAT boot sector verified", name);
	return 0;
}

static int qspi_write_composition_slot(const char *path,
	const struct ef800_composition_profile *profile)
{
	struct fs_file_t file;
	size_t record_length = 0U;
	ssize_t length;
	ssize_t written;
	int ret;

	ret = ef800_composition_profile_encode(profile,
		(char *)qspi_composition_record, sizeof(qspi_composition_record),
		&record_length);
	if (ret != 0) {
		return ret;
	}
	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		return ret;
	}
	written = fs_write(&file, qspi_composition_record, record_length);
	ret = written == (ssize_t)record_length ? fs_sync(&file) : -EIO;
	if (fs_close(&file) != 0 && ret == 0) {
		ret = -EIO;
	}
	if (ret != 0) {
		return ret;
	}
	ret = disk_access_ioctl(EF800_APPLICATION_CONFIGURATION_DISK_NAME,
		DISK_IOCTL_CTRL_SYNC, NULL);
	if (ret != 0) {
		return ret;
	}
	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_READ);
	if (ret != 0) {
		return ret;
	}
	length = fs_read(&file, qspi_composition_readback,
		sizeof(qspi_composition_readback));
	ret = fs_close(&file);
	if (ret != 0) {
		return ret;
	}
	if (length <= 0) {
		return length < 0 ? (int)length : -EIO;
	}
	ret = ef800_composition_profile_decode(qspi_composition_readback,
		(size_t)length, &qspi_composition_decoded);
	if (ret != 0) {
		return ret;
	}
	return ef800_composition_profile_equal(profile,
		&qspi_composition_decoded) ? 0 : -EBADMSG;
}

static int qspi_seed_initial_composition(void)
{
	int ret = fs_mount(&configuration_mount);
	int unmount_ret;

	if (ret != 0) {
		LOG_ERR("QSPI Configuration mount for COMPOSITION seed failed: %d", ret);
		return ret;
	}
	configuration_mounted = true;
	ret = fs_mkdir(EF800_APPLICATION_COMPOSITION_DIR);
	if (ret == -EEXIST) {
		ret = 0;
	}
	qspi_composition_expected = (struct ef800_composition_profile){
		.generation = 1U,
		.token_count = 2U,
	};
	strcpy(qspi_composition_expected.tokens[0], "BATSOC0");
	strcpy(qspi_composition_expected.tokens[1], "LCDCONTR1");
	if (ret == 0) {
		ret = qspi_write_composition_slot(EF800_APPLICATION_COMPOSITION_SLOT_A,
			&qspi_composition_expected);
	}
	if (ret != 0) {
		LOG_ERR("QSPI initial COMPOSITION slot A write/verify failed: %d", ret);
	} else {
		qspi_composition_expected.generation = 2U;
		ret = qspi_write_composition_slot(EF800_APPLICATION_COMPOSITION_SLOT_B,
			&qspi_composition_expected);
		if (ret != 0) {
			LOG_ERR("QSPI initial COMPOSITION slot B write/verify failed: %d",
				ret);
		}
	}
	if (ret == 0) {
		ret = disk_access_ioctl(EF800_APPLICATION_CONFIGURATION_DISK_NAME,
			DISK_IOCTL_CTRL_SYNC, NULL);
	}
	unmount_ret = fs_unmount(&configuration_mount);
	if (unmount_ret == 0) {
		configuration_mounted = false;
	} else if (ret == 0) {
		ret = unmount_ret;
	}
	if (ret == 0) {
		LOG_INF("Initial COMPOSITION profile BATSOC0/LCDCONTR1 verified");
	}
	return ret;
}

static int format_qspi_fat_volumes(void)
{
	int configuration_ret;
	int data_ret;
	int ret;

	ret = fs_mkfs(FS_FATFS,
		(uintptr_t)(EF800_APPLICATION_CONFIGURATION_DISK_NAME ":"), NULL, 0U);
	if (ret != 0) {
		LOG_ERR("QSPI Configuration fs_mkfs failed: %d", ret);
		return ret;
	}
	ret = disk_access_ioctl(EF800_APPLICATION_CONFIGURATION_DISK_NAME,
		DISK_IOCTL_CTRL_SYNC, NULL);
	if (ret != 0) {
		LOG_ERR("QSPI Configuration sync failed: %d", ret);
		return ret;
	}
	ret = fs_mkfs(FS_FATFS,
		(uintptr_t)(EF800_APPLICATION_DATA_DISK_NAME ":"), NULL, 0U);
	if (ret != 0) {
		LOG_ERR("QSPI Shared Data fs_mkfs failed: %d", ret);
		return ret;
	}
	ret = disk_access_ioctl(EF800_APPLICATION_DATA_DISK_NAME,
		DISK_IOCTL_CTRL_SYNC, NULL);
	if (ret != 0) {
		LOG_ERR("QSPI Shared Data sync failed: %d", ret);
		return ret;
	}
	configuration_ret = qspi_verify_fat_partition(
		PARTITION_ID(qspi_configuration_partition),
		EF800_APPLICATION_QSPI_CONFIGURATION_OFFSET,
		EF800_APPLICATION_QSPI_CONFIGURATION_SIZE, "Configuration");
	data_ret = qspi_verify_fat_partition(PARTITION_ID(qspi_storage_partition),
		EF800_APPLICATION_QSPI_DATA_OFFSET, EF800_APPLICATION_QSPI_DATA_SIZE,
		"Shared Data");
	ret = configuration_ret != 0 ? configuration_ret : data_ret;
	return ret == 0 ? qspi_seed_initial_composition() : ret;
}

static int format_blank_qspi_layout_if_needed(void)
{
	const struct flash_area *boot_state_area;
	bool configuration_has_fat;
	bool data_has_fat;
	int ret;

	ret = qspi_partition_has_fat(PARTITION_ID(qspi_configuration_partition),
		EF800_APPLICATION_QSPI_CONFIGURATION_OFFSET,
		EF800_APPLICATION_QSPI_CONFIGURATION_SIZE, &configuration_has_fat);
	if (ret != 0) {
		return ret;
	}
	ret = qspi_partition_has_fat(PARTITION_ID(qspi_storage_partition),
		EF800_APPLICATION_QSPI_DATA_OFFSET, EF800_APPLICATION_QSPI_DATA_SIZE,
		&data_has_fat);
	if (ret != 0 || configuration_has_fat || data_has_fat) {
		return ret;
	}

	LOG_WRN("No valid Configuration or Shared Data FAT boot sector detected; provisioning QSPI for development");
	ret = format_qspi_fat_volumes();
	if (ret != 0) {
		return ret;
	}
	ret = flash_area_open(PARTITION_ID(qspi_bootstate_partition),
		&boot_state_area);
	if (ret != 0) {
		return ret;
	}
	if (boot_state_area->fa_off != EF800_APPLICATION_QSPI_BOOTSTATE_OFFSET ||
	    boot_state_area->fa_size != EF800_APPLICATION_QSPI_BOOTSTATE_SIZE) {
		flash_area_close(boot_state_area);
		return -EINVAL;
	}
	ret = flash_area_erase(boot_state_area, 0U, boot_state_area->fa_size);
	flash_area_close(boot_state_area);
	if (ret != 0) {
		LOG_ERR("QSPI boot-state NVS erase failed: %d", ret);
		return ret;
	}
	LOG_INF("Blank QSPI provisioning complete; boot-state NVS reset");
	return 0;
}

static int format_legacy_qspi_layout_if_needed(void)
{
	const struct flash_area *boot_state_area;
	bool legacy_layout;
	int ret;

	ret = flash_area_open(PARTITION_ID(qspi_bootstate_partition),
		&boot_state_area);
	if (ret != 0) {
		return ret;
	}
	ret = ef800_qspi_layout_check_legacy_area(boot_state_area, &legacy_layout);
	flash_area_close(boot_state_area);
	if (ret != 0) {
		return ret;
	}
	if (!legacy_layout) {
#if defined(CONFIG_FS_FATFS_MKFS) && CONFIG_FS_FATFS_MKFS
		return IS_ENABLED(CONFIG_EF800_APPLICATION_FORMAT_BLANK_QSPI) ?
			format_blank_qspi_layout_if_needed() : 0;
#else
		return 0;
#endif
	}

	LOG_WRN("Legacy monolithic QSPI FAT detected");
#if defined(CONFIG_EF800_APPLICATION_REFORMAT_LEGACY_QSPI) && \
	CONFIG_EF800_APPLICATION_REFORMAT_LEGACY_QSPI && \
	defined(CONFIG_FS_FATFS_MKFS) && CONFIG_FS_FATFS_MKFS
	LOG_WRN("Reformatting legacy QSPI for development; all old volume contents will be lost");
	/* Keep the old boot sector recognizable until both FAT partitions have
	 * formatted successfully. If power fails during either format, the next
	 * boot detects the legacy layout again and retries. */
	ret = format_qspi_fat_volumes();
	if (ret != 0) {
		return ret;
	}
	ret = flash_area_open(PARTITION_ID(qspi_bootstate_partition),
		&boot_state_area);
	if (ret != 0) {
		LOG_ERR("Legacy QSPI boot-state partition unavailable: %d", ret);
		return ret;
	}
	ret = flash_area_erase(boot_state_area, 0U, boot_state_area->fa_size);
	flash_area_close(boot_state_area);
	if (ret != 0) {
		LOG_ERR("Legacy QSPI boot-state erase failed: %d", ret);
		return ret;
	}
	LOG_INF("Legacy QSPI reformat complete: boot-state NVS and both FAT volumes are blank");
	return 0;
#else
	LOG_ERR("Legacy QSPI layout found, but FAT formatting is disabled");
	return -ENOTSUP;
#endif
}

#if !defined(CONFIG_BOARD_NATIVE_SIM)
static bool usb_vbus_initialized;
#endif

static void report_boot_state(void)
{
	struct ef800_boot_metadata metadata;
	bool valid;
	int ret = ef800_metadata_read_current(&metadata, &valid);

	if (ret == EF800_METADATA_RESET) {
		LOG_WRN("boot-state NVS was reset to defaults; watchdog history starts fresh");
	} else if (ret == EF800_METADATA_MIGRATED) {
		LOG_INF("legacy boot-state NVS migrated to watchdog-only record");
	} else if (ret != 0 || !valid) {
		LOG_WRN("boot-state NVS unavailable; EF800 Application continues with defaults: %d",
			ret);
	} else {
		LOG_INF("Bootloader state: main failures=%u target=%u",
			(unsigned int)metadata.main_failures,
			(unsigned int)metadata.last_target);
	}
}
static struct ef800_time_settings time_settings;
K_MUTEX_DEFINE(rtc_lock);

#define EF800_APPLICATION_USER_NODE DT_PATH(zephyr_user)
#if !defined(CONFIG_BOARD_NATIVE_SIM) && \
	DT_NODE_HAS_PROP(EF800_APPLICATION_USER_NODE, usb_vbus_gpios)
static const struct gpio_dt_spec usb_vbus =
	GPIO_DT_SPEC_GET(EF800_APPLICATION_USER_NODE, usb_vbus_gpios);
#endif

#define EF800_TIME_SETTINGS_FILE "time_settings"

static int driver_time_to_standard(const struct rtc_time *driver_time,
					   struct rtc_time *standard_time)
{
	if (driver_time == NULL || standard_time == NULL ||
	    driver_time->tm_mon < 1 || driver_time->tm_mon > 12 ||
	    driver_time->tm_wday < 1 || driver_time->tm_wday > 7) {
		return -EINVAL;
	}
	*standard_time = *driver_time;
	standard_time->tm_mon = driver_time->tm_mon - 1;
	standard_time->tm_wday = driver_time->tm_wday - 1;
	return 0;
}

static int standard_time_to_driver(const struct rtc_time *standard_time,
					   struct rtc_time *driver_time)
{
	if (!ef800_rtc_time_valid(standard_time) || driver_time == NULL) {
		return -EINVAL;
	}
	*driver_time = *standard_time;
	driver_time->tm_mon = standard_time->tm_mon + 1;
	driver_time->tm_wday = standard_time->tm_wday < 0 ? 1 :
		standard_time->tm_wday + 1;
	return 0;
}

static int set_system_time(const struct rtc_time *utc)
{
	struct tm tm = {0};
	struct timespec timestamp;
	int64_t epoch;

	if (!ef800_rtc_time_valid(utc)) {
		return -EINVAL;
	}
	tm.tm_sec = utc->tm_sec;
	tm.tm_min = utc->tm_min;
	tm.tm_hour = utc->tm_hour;
	tm.tm_mday = utc->tm_mday;
	tm.tm_mon = utc->tm_mon;
	tm.tm_year = utc->tm_year;
	epoch = timeutil_timegm64(&tm);
	if (epoch < 0) {
		return -ERANGE;
	}
	timestamp.tv_sec = (time_t)epoch;
	timestamp.tv_nsec = 0;
	return sys_clock_settime(SYS_CLOCK_REALTIME, &timestamp);
}

static void load_time_settings(void)
{
	struct fs_file_t file;
	char buffer[64] = {0};
	ssize_t length;
	int offset;
	int daylight_saving;

	time_settings.utc_offset_minutes = 0;
	time_settings.daylight_saving = false;
	if (ef800_storage_open(&file, EF800_STORAGE_CONFIGURATION,
				       EF800_TIME_SETTINGS_FILE, FS_O_READ) != 0) {
		return;
	}
	length = fs_read(&file, buffer, sizeof(buffer) - 1U);
	(void)fs_close(&file);
	if (length <= 0) {
		return;
	}
	buffer[length] = '\0';
	if (sscanf(buffer, "%d %d", &offset, &daylight_saving) == 2 &&
	    offset >= -24 * 60 && offset <= 24 * 60 &&
	    (daylight_saving == 0 || daylight_saving == 1)) {
		time_settings.utc_offset_minutes = offset;
		time_settings.daylight_saving = daylight_saving != 0;
	}
}

static int save_time_settings(const struct ef800_time_settings *settings)
{
	struct fs_file_t file;
	char buffer[64];
	int length;
	int ret;

	ret = ef800_storage_open(&file, EF800_STORAGE_CONFIGURATION,
					EF800_TIME_SETTINGS_FILE,
					FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		return ret;
	}
	length = snprintk(buffer, sizeof(buffer), "%d %d\n",
				 settings->utc_offset_minutes,
				 settings->daylight_saving ? 1 : 0);
	ret = fs_write(&file, buffer, length);
	if (ret == length) {
		ret = 0;
	} else if (ret >= 0) {
		ret = -EIO;
	}
	if (fs_close(&file) != 0 && ret == 0) {
		ret = -EIO;
	}
	return ret;
}

static const char *storage_root_path(enum ef800_storage_root root)
{
	return root == EF800_STORAGE_CONFIGURATION ?
		EF800_STORAGE_CONFIGURATION_DIR : EF800_STORAGE_DATA_DIR;
}

static bool is_name_character(char character)
{
	return (character >= 'a' && character <= 'z') ||
	       (character >= 'A' && character <= 'Z') ||
	       (character >= '0' && character <= '9') ||
	       character == '_' || character == '-' || character == '.';
}

bool ef800_storage_valid_name(const char *name)
{
	const char *component_start;
	const char *component_end;
	size_t length;

	if (name == NULL || name[0] == '\0' || name[0] == '/') {
		return false;
	}

	length = strlen(name);
	if (length > EF800_STORAGE_MAX_PATH || name[length - 1U] == '/') {
		return false;
	}

	component_start = name;
	for (size_t index = 0U; index <= length; ++index) {
		char character = name[index];

		if (character == '/') {
			component_end = &name[index];
			size_t component_length = component_end - component_start;
			if (component_length == 0U ||
			    (component_length == 1U && component_start[0] == '.') ||
			    (component_length == 2U && component_start[0] == '.' &&
			     component_start[1] == '.')) {
				return false;
			}
			component_start = &name[index + 1U];
			continue;
		}
		if (character == '\0') {
			size_t component_length = &name[index] - component_start;
			return component_length != 0U &&
			       !(component_length == 1U && component_start[0] == '.') &&
			       !(component_length == 2U && component_start[0] == '.' &&
				 component_start[1] == '.');
		}
		if (!is_name_character(character)) {
			return false;
		}
	}

	return true;
}

int ef800_storage_build_path(enum ef800_storage_root root, const char *name,
				     char *path, size_t path_size)
{
	const char *base;
	size_t base_length;
	size_t name_length;

	if (path == NULL || path_size == 0U ||
	    root < EF800_STORAGE_CONFIGURATION || root > EF800_STORAGE_DATA ||
	    !ef800_storage_valid_name(name)) {
		return -EINVAL;
	}

	base = storage_root_path(root);
	base_length = strlen(base);
	name_length = strlen(name);
	if (base_length + name_length + 1U > path_size) {
		return -ENAMETOOLONG;
	}

	memcpy(path, base, base_length);
	memcpy(path + base_length, name, name_length + 1U);
	return 0;
}

bool ef800_storage_is_mounted(void)
{
	/* Preserve the legacy meaning: application data/plugin storage is mounted. */
	return data_mounted;
}

bool ef800_storage_root_is_mounted(enum ef800_storage_root root)
{
	switch (root) {
	case EF800_STORAGE_CONFIGURATION:
		return configuration_mounted;
	case EF800_STORAGE_DATA:
		return data_mounted;
	default:
		return false;
	}
}

bool ef800_storage_configuration_is_mounted(void)
{
	return configuration_mounted;
}

bool ef800_storage_data_is_mounted(void)
{
	return data_mounted;
}

const struct fs_mount_t *ef800_storage_mount_descriptor(void)
{
	return &data_mount;
}

#if !defined(CONFIG_BOARD_NATIVE_SIM)
static bool usb_vbus_present(void)
{
#if DT_NODE_HAS_PROP(EF800_APPLICATION_USER_NODE, usb_vbus_gpios)
	if (!usb_vbus_initialized) {
		if (!gpio_is_ready_dt(&usb_vbus) ||
			gpio_pin_configure_dt(&usb_vbus, GPIO_INPUT) != 0) {
			return false;
		}
		usb_vbus_initialized = true;
	}
	return gpio_pin_get_dt(&usb_vbus) > 0;
#else
	return false;
#endif
}
#endif

#if !defined(CONFIG_BOARD_NATIVE_SIM)
static bool usb_msd_assignment_active(void)
{
	struct ef800_persistence persistence;
	struct ef800_port_assignment_service assignments;
	int ret;

	if (!configuration_mounted) {
		return false;
	}
	ret = ef800_persistence_init(&persistence, ef800_persistence_zephyr_backend());
	if (ret != 0) {
		return false;
	}
	ret = ef800_port_assignment_init(&assignments, &persistence);
	if (ret == 0) {
		ret = ef800_port_assignment_load(&assignments, 4U);
	}
	if (ret != 0) {
		return false;
	}
	return ef800_port_assignment_get_builder(&assignments, 4U) ==
		EF800_PORT_BUILDER_MSD;
}
#endif

const char *ef800_storage_configuration_dir(void)
{
	return EF800_STORAGE_CONFIGURATION_DIR;
}

const char *ef800_storage_data_dir(void)
{
	return EF800_STORAGE_DATA_DIR;
}

int ef800_storage_open(struct fs_file_t *file, enum ef800_storage_root root,
			       const char *name, fs_mode_t flags)
{
	char path[EF800_STORAGE_MAX_PATH];
	int ret;

	if (file == NULL) {
		return -EINVAL;
	}
	ret = ef800_storage_build_path(root, name, path, sizeof(path));
	if (ret != 0) {
		return ret;
	}
	if (!ef800_storage_root_is_mounted(root)) {
		return -ENOTSUP;
	}

	fs_file_t_init(file);
	return fs_open(file, path, flags);
}

static void watchdog_task(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	int result;
	result = ef800_watchdog_start(CONFIG_EF800_APPLICATION_WATCHDOG_TIMEOUT_MS);
	if (result != 0) {
		int inherit_result = ef800_watchdog_inherit();

		if (inherit_result != 0) {
			LOG_ERR("Watchdog start failed: %d; inherited watchdog adoption failed: %d",
				result, inherit_result);
			return;
		}
		LOG_WRN("Watchdog start returned %d; adopted bootloader IWDG",
			result);
	}

	LOG_INF("Watchdog task active (%d ms timeout)",
		CONFIG_EF800_APPLICATION_WATCHDOG_TIMEOUT_MS);
	while (true) {
		result = ef800_watchdog_feed();
		if (result < 0) {
			LOG_ERR("Watchdog feed failed: %d", result);
		}
		k_msleep(CONFIG_EF800_APPLICATION_WATCHDOG_FEED_MS);
	}
}

static bool init_watchdog_task(void)
{
	(void)k_thread_create(&watchdog_thread, watchdog_stack,
				      K_THREAD_STACK_SIZEOF(watchdog_stack),
				      watchdog_task, NULL, NULL, NULL,
				      K_PRIO_PREEMPT(2), 0, K_NO_WAIT);
	return true;
}

static bool init_status_led(void)
{
#if DT_HAS_ALIAS(status_led)
	int result;

	if (!gpio_is_ready_dt(&status_led)) {
		LOG_WRN("Status LED alias exists but GPIO is not ready");
		return false;
	}

	result = gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
	if (result != 0) {
		LOG_WRN("Status LED configuration failed: %d", result);
		return false;
	}

	result = gpio_pin_set_dt(&status_led, 1);
	if (result != 0) {
		LOG_WRN("Status LED activation failed: %d", result);
		return false;
	}

	LOG_INF("Status LED initialized via status_led alias");
	return true;
#else
	LOG_INF("Status LED not initialized: no status_led alias");
	return false;
#endif
}

static bool probe_mcp23017(void)
{
#if DT_HAS_COMPAT_STATUS_OKAY(microchip_mcp23017)
	uint16_t port_value;
	int result;

	if (!device_is_ready(mcp23017)) {
		LOG_WRN("MCP23017 GPIO device is not ready; probe skipped");
		return false;
	}

	result = ef800_mcp23017_probe(mcp23017, &port_value);
	if (result != 0) {
		LOG_WRN("MCP23017 GPIO probe failed: %d", result);
		return false;
	}

	LOG_INF("MCP23017 GPIO expander ready (port=0x%04x)", port_value);
	return true;
#else
	LOG_INF("MCP23017 probe skipped: no enabled mcp23017 node");
	return false;
#endif
}

static bool probe_ds3231(void)
{
#if DT_HAS_COMPAT_STATUS_OKAY(maxim_ds3231_rtc)
	struct rtc_time driver_time;
	struct rtc_time time;
	int result;

	if (!device_is_ready(ds3231)) {
		LOG_WRN("Supported DS3231 RTC node is not ready");
		return false;
	}

	result = rtc_get_time(ds3231, &driver_time);
	if (result != 0) {
		LOG_WRN("DS3231 RTC API is ready but time read failed: %d", result);
		return false;
	}
	result = driver_time_to_standard(&driver_time, &time);
	if (result != 0) {
		/* A new or reset DS3231 may contain an invalid date, but it is still
		 * usable and must remain available for the time-setting workflow. */
		LOG_WRN("DS3231 date/time is not initialized");
		return true;
	}
	if (!ef800_rtc_time_valid(&time)) {
		/* The RTC can still be configured through the editor or shell. */
		LOG_WRN("DS3231 date/time is invalid or not initialized");
		return true;
	}

	LOG_INF("DS3231 RTC ready: %04d-%02d-%02d %02d:%02d:%02d",
		time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
		 time.tm_hour, time.tm_min, time.tm_sec);
	result = set_system_time(&time);
	if (result != 0) {
		LOG_WRN("System realtime clock synchronization failed: %d", result);
	}
	return true;
#else
	LOG_INF("DS3231 probe skipped: no supported maxim,ds3231-rtc node");
	return false;
#endif
}

int ef800_storage_mount(void)
{
	const struct flash_area *boot_state_area;
	bool legacy_layout;
	int result;
	int directory_result;

	if (configuration_mounted && data_mounted) {
		return 0;
	}
#ifdef CONFIG_BOARD_NATIVE_SIM
	if (!virtual_ef800_profile_available(5U)) {
		return -ENODEV;
	}
#endif
	result = flash_area_open(PARTITION_ID(qspi_bootstate_partition),
				 &boot_state_area);
	if (result != 0) {
		return result;
	}
	result = ef800_qspi_layout_check_legacy_area(boot_state_area,
		&legacy_layout);
	flash_area_close(boot_state_area);
	if (result != 0) {
		return result;
	}
	if (legacy_layout) {
		LOG_ERR("legacy monolithic QSPI FAT detected; both new FAT volumes remain untouched");
		return -ENOTSUP;
	}
	if (!configuration_mounted) {
		result = mount_storage_volume(&configuration_mount,
			&configuration_mounted,
			EF800_APPLICATION_CONFIGURATION_DISK_NAME ":");
		if (result != 0) {
			LOG_WRN("QSPI configuration FAT mount failed: %d (no format attempted)",
				result);
			return result;
		}
		LOG_INF("QSPI configuration FAT mounted at %s",
			configuration_mount.mnt_point);
		directory_result = storage_ensure_directory("/EF800:/.configuration");
		if (directory_result != 0) {
			LOG_WRN("Configuration directory unavailable: %d", directory_result);
		}
	}
	if (!data_mounted) {
		if (ef800_app_usb_msd_requested()) {
			LOG_INF("QSPI shared-data FAT mount skipped; USB MSD owns the disk");
			return -EBUSY;
		}
		result = mount_storage_volume(&data_mount, &data_mounted,
			EF800_APPLICATION_DATA_DISK_NAME ":");
		if (result != 0) {
			LOG_WRN("QSPI shared-data FAT mount failed: %d (no format attempted)",
				result);
			return result;
		}
		LOG_INF("QSPI shared-data FAT mounted at %s", data_mount.mnt_point);
		static const char *const data_roots[] = {
			"/DATA:/data",
			"/DATA:/plugins",
			"/DATA:/fwu",
		};

		for (size_t i = 0; i < ARRAY_SIZE(data_roots); ++i) {
			directory_result = storage_ensure_directory(data_roots[i]);
			if (directory_result != 0) {
				LOG_WRN("Shared-data directory unavailable %s: %d",
					data_roots[i], directory_result);
			}
		}
	}
	return 0;
}

int ef800_storage_prepare_mtp_roots(void)
{
	static const char *const roots[] = {
		"/DATA:/data",
		"/DATA:/plugins",
		"/DATA:/fwu",
	};

	if (!configuration_mounted || !data_mounted) {
		int mount_result = ef800_storage_mount();

		if (mount_result != 0) {
			LOG_ERR("Cannot prepare MTP roots: FAT mount failed: %d", mount_result);
			return mount_result;
		}
	}
	for (size_t i = 0; i < ARRAY_SIZE(roots); i++) {
		struct fs_dirent entry;
		int ret = fs_stat(roots[i], &entry);

		if (ret == 0 && entry.type == FS_DIR_ENTRY_DIR) {
			continue;
		}
		ret = fs_mkdir(roots[i]);
		if (ret != 0 && ret != -EEXIST) {
			LOG_ERR("MTP root unavailable %s: %d", roots[i], ret);
			return ret;
		}
	}
	return 0;
}

bool ef800_app_usb_msd_requested(void)
{
#if defined(CONFIG_BOARD_NATIVE_SIM)
	return false;
#else
	return usb_vbus_present() && usb_msd_assignment_active();
#endif
}

int ef800_storage_unmount(void)
{
	int ret;

	if (!data_mounted) {
		return 0;
	}
	ret = fs_unmount(&data_mount);
	if (ret == 0) {
		data_mounted = false;
	}
	return ret;
}

int ef800_storage_unmount_all(void)
{
	int ret;

	if (data_mounted) {
		ret = fs_unmount(&data_mount);
		if (ret != 0) {
			return ret;
		}
		data_mounted = false;
	}
	if (configuration_mounted) {
		ret = fs_unmount(&configuration_mount);
		if (ret != 0) {
			return ret;
		}
		configuration_mounted = false;
	}
	return 0;
}

static int qspi_erase_disk(const char *disk_name, uint32_t expected_sectors)
{
	uint32_t sector_count;
	uint32_t sector_size;
	uint32_t erase_block_sectors;
	uint32_t chunk_sectors;
	int ret;

	ret = disk_access_ioctl(disk_name, DISK_IOCTL_GET_SECTOR_COUNT,
		&sector_count);
	if (ret == 0) {
		ret = disk_access_ioctl(disk_name, DISK_IOCTL_GET_SECTOR_SIZE,
			&sector_size);
	}
	if (ret == 0) {
		ret = disk_access_ioctl(disk_name, DISK_IOCTL_GET_ERASE_BLOCK_SZ,
			&erase_block_sectors);
	}
	if (ret != 0) {
		return ret;
	}
	if (sector_count != expected_sectors || sector_size != EF800_APPLICATION_FAT_SECTOR_SIZE ||
	    erase_block_sectors == 0U) {
		LOG_ERR("QSPI disk %s geometry mismatch: sectors=%u sector-size=%u erase-block=%u",
			disk_name, sector_count, sector_size, erase_block_sectors);
		return -EINVAL;
	}
	chunk_sectors = MAX(128U, erase_block_sectors);
	chunk_sectors -= chunk_sectors % erase_block_sectors;
	if (chunk_sectors == 0U) {
		chunk_sectors = erase_block_sectors;
	}
	for (uint32_t start = 0U; start < sector_count;) {
		uint32_t count = MIN(chunk_sectors, sector_count - start);

		count -= count % erase_block_sectors;
		if (count == 0U) {
			return -EINVAL;
		}
		ret = disk_access_erase(disk_name, start, count,
			DISK_ACCESS_ERASE_PHYSICAL);
		if (ret != 0) {
			LOG_ERR("QSPI disk %s erase sectors %u..%u failed: %d",
				disk_name, start, start + count - 1U, ret);
			return ret;
		}
		start += count;
		ef800_watchdog_feed();
	}
	return disk_access_ioctl(disk_name, DISK_IOCTL_CTRL_SYNC, NULL);
}

static int qspi_validate_partition_map(uint8_t partition_id,
	off_t expected_offset, size_t expected_size, const char *name)
{
	const struct flash_area *area;
	int ret = flash_area_open(partition_id, &area);

	if (ret != 0) {
		LOG_ERR("QSPI %s partition open failed: %d", name, ret);
		return ret;
	}
	ret = (area->fa_off == expected_offset && area->fa_size == expected_size) ?
		0 : -EINVAL;
	if (ret != 0) {
		LOG_ERR("Refusing QSPI format: unexpected %s map off=0x%08x size=0x%08x",
			name, (unsigned int)area->fa_off, (unsigned int)area->fa_size);
	}
	flash_area_close(area);
	return ret;
}

int ef800_storage_format_qspi_layout(void)
{
	const struct flash_area *boot_state_area;
	int ret;

	if (configuration_mounted || data_mounted || ef800_app_usb_msd_requested()) {
		return -EBUSY;
	}
	ret = qspi_validate_partition_map(PARTITION_ID(qspi_bootstate_partition),
		EF800_APPLICATION_QSPI_BOOTSTATE_OFFSET, EF800_APPLICATION_QSPI_BOOTSTATE_SIZE,
		"boot-state NVS");
	if (ret == 0) {
		ret = qspi_validate_partition_map(
			PARTITION_ID(qspi_configuration_partition),
			EF800_APPLICATION_QSPI_CONFIGURATION_OFFSET,
			EF800_APPLICATION_QSPI_CONFIGURATION_SIZE, "Configuration FAT");
	}
	if (ret == 0) {
		ret = qspi_validate_partition_map(PARTITION_ID(qspi_storage_partition),
			EF800_APPLICATION_QSPI_DATA_OFFSET, EF800_APPLICATION_QSPI_DATA_SIZE,
			"Shared Data FAT");
	}
	if (ret != 0) {
		return ret;
	}

	LOG_WRN("Erasing all 4 MiB QSPI contents and formatting Configuration/Shared Data");
	ret = disk_access_init(EF800_APPLICATION_CONFIGURATION_DISK_NAME);
	if (ret == 0) {
		ret = disk_access_init(EF800_APPLICATION_DATA_DISK_NAME);
	}
	if (ret == 0) {
		ret = disk_access_ioctl(EF800_APPLICATION_CONFIGURATION_DISK_NAME,
			DISK_IOCTL_CTRL_SYNC, NULL);
	}
	if (ret == 0) {
		ret = disk_access_ioctl(EF800_APPLICATION_DATA_DISK_NAME,
			DISK_IOCTL_CTRL_SYNC, NULL);
	}
	if (ret == 0) {
		ret = qspi_erase_disk(EF800_APPLICATION_CONFIGURATION_DISK_NAME,
			EF800_APPLICATION_QSPI_CONFIGURATION_SIZE /
			EF800_APPLICATION_FAT_SECTOR_SIZE);
	}
	if (ret == 0) {
		ret = qspi_erase_disk(EF800_APPLICATION_DATA_DISK_NAME,
			EF800_APPLICATION_QSPI_DATA_SIZE / EF800_APPLICATION_FAT_SECTOR_SIZE);
	}
	if (ret != 0) {
		LOG_ERR("QSPI FAT partition erase failed: %d", ret);
		return ret;
	}

	ret = format_qspi_fat_volumes();
	if (ret != 0) {
		LOG_ERR("QSPI FAT format or raw verification failed: %d", ret);
		return ret;
	}
	ret = flash_area_open(PARTITION_ID(qspi_bootstate_partition),
		&boot_state_area);
	if (ret != 0) {
		return ret;
	}
	ret = flash_area_erase(boot_state_area, 0U, boot_state_area->fa_size);
	flash_area_close(boot_state_area);
	if (ret != 0) {
		LOG_ERR("QSPI boot-state NVS erase failed: %d", ret);
		return ret;
	}
	ef800_watchdog_feed();
	LOG_INF("Full QSPI layout erase and FAT formatting verified");
	return 0;
}

bool ef800_app_rtc_is_ready(void)
{
#if DT_HAS_COMPAT_STATUS_OKAY(maxim_ds3231_rtc)
	return ds3231_ready;
#else
	return false;
#endif
}

int ef800_app_rtc_get_time(struct rtc_time *time)
{
#if DT_HAS_COMPAT_STATUS_OKAY(maxim_ds3231_rtc)
	struct rtc_time driver_time;
	int ret;

	if (!ds3231_ready || time == NULL) {
		return -ENODEV;
	}
	k_mutex_lock(&rtc_lock, K_FOREVER);
	ret = rtc_get_time(ds3231, &driver_time);
	k_mutex_unlock(&rtc_lock);
	if (ret != 0) {
		return ret;
	}
	return driver_time_to_standard(&driver_time, time);
#else
	ARG_UNUSED(time);
	return -ENOTSUP;
#endif
}

int ef800_app_rtc_set_utc_time(const struct rtc_time *utc)
{
#if DT_HAS_COMPAT_STATUS_OKAY(maxim_ds3231_rtc)
	struct rtc_time driver_time;
	int ret;

	if (!ds3231_ready || utc == NULL || !ef800_rtc_time_valid(utc)) {
		return -EINVAL;
	}
	ret = standard_time_to_driver(utc, &driver_time);
	if (ret != 0) {
		return ret;
	}
	k_mutex_lock(&rtc_lock, K_FOREVER);
	ret = rtc_set_time(ds3231, &driver_time);
	k_mutex_unlock(&rtc_lock);
	if (ret != 0) {
		return ret;
	}
	return set_system_time(utc);
#else
	ARG_UNUSED(utc);
	return -ENOTSUP;
#endif
}

int ef800_app_rtc_set_local_time(const struct rtc_time *local,
					  const struct ef800_time_settings *settings)
{
	struct rtc_time utc;
	int ret;

	if (local == NULL || settings == NULL) {
		return -EINVAL;
	}
	ret = ef800_local_time_to_utc(local, settings, &utc);
	if (ret != 0) {
		return ret;
	}
	ret = ef800_app_rtc_set_utc_time(&utc);
	if (ret != 0) {
		return ret;
	}
	time_settings = *settings;
	ret = save_time_settings(&time_settings);
	if (ret != 0 && ret != -ENOTSUP) {
		LOG_WRN("RTC updated but timezone settings could not be saved: %d", ret);
	}
	return 0;
}

int ef800_app_rtc_get_local_time(struct rtc_time *local)
{
	struct rtc_time utc;
	int ret;

	if (local == NULL) {
		return -EINVAL;
	}
	ret = ef800_app_rtc_get_time(&utc);
	if (ret != 0) {
		return ret;
	}
	return ef800_utc_time_to_local(&utc, &time_settings, local);
}

const struct ef800_time_settings *ef800_app_get_time_settings(void)
{
	return &time_settings;
}

int ef800_app_reload_time_settings(void)
{
	load_time_settings();
	return 0;
}

int ef800_app_foundation_init(void)
{
	bool watchdog_task_started;
	int qspi_provisioning_result;
	int storage_result;

	watchdog_task_started = init_watchdog_task();
	status_led_initialized = init_status_led();
	mcp23017_detected = probe_mcp23017();
	qspi_provisioning_result = format_legacy_qspi_layout_if_needed();
	if (qspi_provisioning_result != 0) {
		LOG_WRN("QSPI provisioning/reformat was not completed: %d",
			qspi_provisioning_result);
	}
	/* Boot state is independent of both FAT volumes and must recover first. */
	report_boot_state();
	if (qspi_provisioning_result == 0) {
		storage_result = ef800_storage_mount();
	} else {
		storage_result = qspi_provisioning_result;
		LOG_WRN("QSPI mounts skipped because provisioning did not verify");
	}
	if (storage_result != 0 && storage_result != -ENOTSUP &&
		storage_result != -EBUSY) {
		LOG_WRN("QSPI storage unavailable: %d", storage_result);
	}
	load_time_settings();
	ds3231_ready = probe_ds3231();

	LOG_INF("EF800 foundation initialized: watchdog_task=%s status_led=%s "
		"mcp23017=%s ds3231=%s configuration=%s shared_data=%s",
		watchdog_task_started ? "started" : "not-started",
		status_led_initialized ? "ready" : "not-ready",
		mcp23017_detected ? "detected" : "not-detected",
		ds3231_ready ? "available" : "not-available",
		configuration_mounted ? "mounted" : "unavailable",
		data_mounted ? "mounted" : "unmounted-or-unavailable");
	LOG_INF("Legacy Elta3/FTS3XX/port protocol and USB application code "
		"remain explicitly unported");

	return 0;
}
