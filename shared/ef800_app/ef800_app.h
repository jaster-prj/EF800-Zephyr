/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief EF800 Application shared storage ownership, file access, and RTC services.
 */
#ifndef EF800_APP_FOUNDATION_H
#define EF800_APP_FOUNDATION_H

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/fs/fs.h>
#include <zephyr/drivers/rtc.h>

#include "ef800_time.h"

#define EF800_STORAGE_MOUNT_POINT "/EF800:"
#define EF800_STORAGE_CONFIGURATION_DIR "/EF800:/.configuration/"
#define EF800_STORAGE_DATA_MOUNT_POINT "/DATA:"
#define EF800_STORAGE_DATA_DIR "/DATA:/data/"
#define EF800_STORAGE_MAX_PATH 128U

enum ef800_storage_root {
	EF800_STORAGE_CONFIGURATION = 0,
	EF800_STORAGE_DATA = 1,
};

/** @brief Initialize EF800 Application storage and platform foundation services.
 * @return 0 on success; negative errno if required platform setup fails.
 */
int ef800_app_foundation_init(void);

/** @brief Query whether all EF800 Application storage roots are mounted.
 * @return True when storage is mounted.
 */
bool ef800_storage_is_mounted(void);

/** @brief Query whether one storage root is mounted.
 * @param root Configuration or data root to inspect.
 * @return True when the selected root is mounted.
 */
bool ef800_storage_root_is_mounted(enum ef800_storage_root root);

/** @brief Query configuration-root mount state.
 * @return True when the configuration root is mounted.
 */
bool ef800_storage_configuration_is_mounted(void);

/** @brief Query data-root mount state.
 * @return True when the data root is mounted.
 */
bool ef800_storage_data_is_mounted(void);

/** @brief Get the Zephyr mount descriptor for managed storage.
 * @return Mount descriptor, or NULL when not configured.
 */
const struct fs_mount_t *ef800_storage_mount_descriptor(void);

/** @brief Mount the configured QSPI filesystem roots.
 * @return 0 on mount; negative errno if ownership or mount fails.
 */
int ef800_storage_mount(void);

/** @brief Ensure the directory roots required by USB MTP are present.
 * @return 0 on success; negative errno on storage or directory failure.
 */
int ef800_storage_prepare_mtp_roots(void);

/** @brief Unmount the configured filesystem.
 * @return 0 on unmount; negative errno if unavailable or unmount fails.
 */
int ef800_storage_unmount(void);

/** @brief Unmount every managed storage root.
 * @return 0 on success; negative errno on the first unmount failure.
 */
int ef800_storage_unmount_all(void);

/** @brief Format QSPI using the declared partition/filesystem layout.
 * @return 0 on format; negative errno if formatting is unsafe or fails.
 */
int ef800_storage_format_qspi_layout(void);

/** @brief Query whether USB mass storage is requesting QSPI ownership.
 * @return True when MSC is requested.
 */
bool ef800_app_usb_msd_requested(void);

/** @brief Get the configuration-root path.
 * @return NUL-terminated root path.
 */
const char *ef800_storage_configuration_dir(void);

/** @brief Get the data-root path.
 * @return NUL-terminated root path.
 */
const char *ef800_storage_data_dir(void);

/* Validate a relative 8.3/FAT-compatible path without touching the filesystem. */

/** @brief Validate a relative FAT-compatible 8.3 name without filesystem I/O.
 * @param name Relative name to validate.
 * @return True when the name is safe and supported.
 */
bool ef800_storage_valid_name(const char *name);

/* Build and open a path below one of the legacy EF800 Application directories. */

/** @brief Build a bounded path below a managed storage root.
 * @param root Root directory selector.
 * @param name Valid relative filename.
 * @param path Receives the complete path.
 * @param path_size Capacity of path in bytes.
 * @return 0 on success; negative errno for invalid name/root or short buffer.
 */
int ef800_storage_build_path(enum ef800_storage_root root, const char *name,
				     char *path, size_t path_size);

/** @brief Build and open a file below a managed storage root.
 * @param file Initialized file object to open.
 * @param root Root directory selector.
 * @param name Valid relative filename.
 * @param flags Zephyr filesystem open flags.
 * @return 0 on open; negative errno on path, mount, or file-open failure.
 */
int ef800_storage_open(struct fs_file_t *file, enum ef800_storage_root root,
			       const char *name, fs_mode_t flags);

/** @brief Query whether the EF800 Application RTC is ready.
 * @return True when the device is ready.
 */
bool ef800_app_rtc_is_ready(void);

/** @brief Read the RTC's raw calendar value.
 * @param time Receives the RTC value.
 * @return 0 on read; negative errno when the RTC is unavailable or read fails.
 */
int ef800_app_rtc_get_time(struct rtc_time *time);

/** @brief Convert and write local time using the supplied timezone settings.
 * @param local Local calendar value.
 * @param settings UTC offset and daylight-saving configuration.
 * @return 0 on write; negative errno for invalid input or RTC failure.
 */
int ef800_app_rtc_set_local_time(const struct rtc_time *local,
					  const struct ef800_time_settings *settings);

/** @brief Write a UTC calendar value directly to the RTC.
 * @param utc UTC calendar value.
 * @return 0 on write; negative errno for invalid input or RTC failure.
 */
int ef800_app_rtc_set_utc_time(const struct rtc_time *utc);

/** @brief Read UTC from the RTC and convert it to configured local time.
 * @param local Receives local calendar value.
 * @return 0 on read/conversion; negative errno on failure.
 */
int ef800_app_rtc_get_local_time(struct rtc_time *local);

/** @brief Get the active timezone and daylight-saving settings.
 * @return Pointer to current settings, or NULL if settings are unavailable.
 */
const struct ef800_time_settings *ef800_app_get_time_settings(void);

/** @brief Reload persisted timezone settings.
 * @return 0 on load; negative errno when storage/settings are unavailable.
 */
int ef800_app_reload_time_settings(void);

#endif /* EF800_APP_FOUNDATION_H */
