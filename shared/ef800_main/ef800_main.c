/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_main.h"

#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/storage/flash_map.h>

#include "ef800_mcp23017.h"
#include "ef800_metadata.h"
#include "ef800_ports.h"
#include "ef800_update.h"
#include "ef800_watchdog.h"

uint32_t get_fattime(void)
{
	struct rtc_time time;
	uint32_t year;

	if (ef800_main_rtc_get_time(&time) != 0 || time.tm_year < 80) {
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

LOG_MODULE_REGISTER(ef800_main, LOG_LEVEL_INF);

#if DT_HAS_ALIAS(status_led)
static const struct gpio_dt_spec status_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(status_led), gpios);
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(microchip_mcp23017)
static const struct device *const mcp23017 =
	DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(microchip_mcp23017));
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(maxim_ds3231_rtc)
#define EF800_MAIN_DS3231_NODE \
	DT_COMPAT_GET_ANY_STATUS_OKAY(maxim_ds3231_rtc)
static const struct device *const ds3231 =
	DEVICE_DT_GET(EF800_MAIN_DS3231_NODE);
#endif

static FATFS storage_fat_fs;
static struct fs_mount_t storage_mount = {
	.type = FS_FATFS,
	.mnt_point = "/EF800:",
	.fs_data = &storage_fat_fs,
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};

static K_THREAD_STACK_DEFINE(watchdog_stack,
				     CONFIG_EF800_MAIN_WATCHDOG_STACK_SIZE);
static struct k_thread watchdog_thread;

static bool status_led_initialized;
static bool mcp23017_detected;
static bool ds3231_ready;
static bool storage_mounted;

static bool storage_directory_exists(const char *path)
{
	struct fs_dirent entry;

	return fs_stat(path, &entry) == 0 && entry.type == FS_DIR_ENTRY_DIR;
}
static bool usb_vbus_initialized;
static struct ef800_time_settings time_settings;
K_MUTEX_DEFINE(rtc_lock);

#define EF800_MAIN_USER_NODE DT_PATH(zephyr_user)
#if DT_NODE_HAS_PROP(EF800_MAIN_USER_NODE, usb_vbus_gpios)
static const struct gpio_dt_spec usb_vbus =
	GPIO_DT_SPEC_GET(EF800_MAIN_USER_NODE, usb_vbus_gpios);
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
	return storage_mounted;
}

static bool usb_vbus_present(void)
{
#if DT_NODE_HAS_PROP(EF800_MAIN_USER_NODE, usb_vbus_gpios)
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

static bool usb_msd_assignment_active(void)
{
	struct ef800_boot_metadata metadata;
	bool valid;
	int ret;

	ret = ef800_metadata_read_current(&metadata, &valid);
	if (ret != 0 || !valid) {
		return false;
	}
	return ef800_port_assignment_builder(metadata.port_assignments, 4U) ==
		EF800_PORT_BUILDER_MSD;
}

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
	if (!storage_mounted) {
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
	result = ef800_watchdog_start(CONFIG_EF800_MAIN_WATCHDOG_TIMEOUT_MS);
	if (result != 0) {
		LOG_ERR("Watchdog start failed: %d", result);
		return;
	}

	LOG_INF("Watchdog task active (%d ms timeout)",
		CONFIG_EF800_MAIN_WATCHDOG_TIMEOUT_MS);
	while (true) {
		result = ef800_watchdog_feed();
		if (result < 0) {
			LOG_ERR("Watchdog feed failed: %d", result);
		}
		k_msleep(CONFIG_EF800_MAIN_WATCHDOG_FEED_MS);
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
	int result;
	int directory_result;

	if (storage_mounted) {
		return 0;
	}
	if (usb_msd_assignment_active() && usb_vbus_present()) {
		LOG_INF("QSPI FAT mount skipped; USB MSD owns the disk");
		return -EBUSY;
	}

	result = fs_mount(&storage_mount);
	if (result != 0) {
		LOG_WRN("QSPI FAT mount failed: %d (no format attempted)", result);
		return result;
	}

	LOG_INF("QSPI FAT filesystem mounted at %s", storage_mount.mnt_point);
	directory_result = storage_directory_exists("/EF800:/.configuration") ?
		0 : fs_mkdir("/EF800:/.configuration");
	if (directory_result == -EEXIST &&
		storage_directory_exists("/EF800:/.configuration")) {
		directory_result = 0;
	}
	if (directory_result != 0) {
		LOG_WRN("Configuration directory unavailable: %d", directory_result);
	}
	directory_result = storage_directory_exists("/EF800:/data") ?
		0 : fs_mkdir("/EF800:/data");
	if (directory_result == -EEXIST &&
		storage_directory_exists("/EF800:/data")) {
		directory_result = 0;
	}
	if (directory_result != 0) {
		LOG_WRN("Data directory unavailable: %d", directory_result);
	}
	storage_mounted = true;
	return 0;
}

bool ef800_main_usb_msd_requested(void)
{
	return usb_vbus_present() && usb_msd_assignment_active();
}

int ef800_storage_unmount(void)
{
	int ret;

	if (!storage_mounted) {
		return 0;
	}
	ret = fs_unmount(&storage_mount);
	if (ret == 0) {
		storage_mounted = false;
	}
	return ret;
}

bool ef800_main_rtc_is_ready(void)
{
#if DT_HAS_COMPAT_STATUS_OKAY(maxim_ds3231_rtc)
	return ds3231_ready;
#else
	return false;
#endif
}

int ef800_main_rtc_get_time(struct rtc_time *time)
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

int ef800_main_rtc_set_utc_time(const struct rtc_time *utc)
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

int ef800_main_rtc_set_local_time(const struct rtc_time *local,
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
	ret = ef800_main_rtc_set_utc_time(&utc);
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

int ef800_main_rtc_get_local_time(struct rtc_time *local)
{
	struct rtc_time utc;
	int ret;

	if (local == NULL) {
		return -EINVAL;
	}
	ret = ef800_main_rtc_get_time(&utc);
	if (ret != 0) {
		return ret;
	}
	return ef800_utc_time_to_local(&utc, &time_settings, local);
}

const struct ef800_time_settings *ef800_main_get_time_settings(void)
{
	return &time_settings;
}

int ef800_main_reload_time_settings(void)
{
	load_time_settings();
	return 0;
}

int ef800_main_foundation_init(void)
{
	bool watchdog_task_started;
	int storage_result;

	watchdog_task_started = init_watchdog_task();
	status_led_initialized = init_status_led();
	mcp23017_detected = probe_mcp23017();
	storage_result = ef800_storage_mount();
	storage_mounted = storage_result == 0;
	if (storage_result != 0 && storage_result != -ENOTSUP &&
		storage_result != -EBUSY) {
		LOG_WRN("QSPI storage unavailable: %d", storage_result);
	}
	load_time_settings();
	ds3231_ready = probe_ds3231();

	LOG_INF("EF800 foundation initialized: watchdog_task=%s status_led=%s "
		"mcp23017=%s ds3231=%s storage=%s",
		watchdog_task_started ? "started" : "not-started",
		status_led_initialized ? "ready" : "not-ready",
		mcp23017_detected ? "detected" : "not-detected",
		ds3231_ready ? "available" : "not-available",
		storage_mounted ? "mounted" : "disabled-or-unavailable");
	LOG_INF("Legacy Elta3/FTS3XX/port protocol and USB application code "
		"remain explicitly unported");

	return 0;
}
