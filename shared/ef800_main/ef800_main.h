/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_MAIN_FOUNDATION_H
#define EF800_MAIN_FOUNDATION_H

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/fs/fs.h>
#include <zephyr/drivers/rtc.h>

#include "ef800_time.h"

#define EF800_STORAGE_MOUNT_POINT "/EF800:"
#define EF800_STORAGE_CONFIGURATION_DIR "/EF800:/.configuration/"
#define EF800_STORAGE_DATA_DIR "/EF800:/data/"
#define EF800_STORAGE_MAX_PATH 128U

enum ef800_storage_root {
	EF800_STORAGE_CONFIGURATION = 0,
	EF800_STORAGE_DATA = 1,
};

/* Initialize the isolated Zephyr MainApplication foundation services. */
int ef800_main_foundation_init(void);

/* The service is deliberately unavailable when storage support is disabled. */
bool ef800_storage_is_mounted(void);
int ef800_storage_mount(void);
int ef800_storage_unmount(void);
bool ef800_main_usb_msd_requested(void);
const char *ef800_storage_configuration_dir(void);
const char *ef800_storage_data_dir(void);

/* Validate a relative 8.3/FAT-compatible path without touching the filesystem. */
bool ef800_storage_valid_name(const char *name);

/* Build and open a path below one of the legacy MainApplication directories. */
int ef800_storage_build_path(enum ef800_storage_root root, const char *name,
				     char *path, size_t path_size);
int ef800_storage_open(struct fs_file_t *file, enum ef800_storage_root root,
			       const char *name, fs_mode_t flags);

bool ef800_main_rtc_is_ready(void);
int ef800_main_rtc_get_time(struct rtc_time *time);
int ef800_main_rtc_set_local_time(const struct rtc_time *local,
					  const struct ef800_time_settings *settings);
int ef800_main_rtc_set_utc_time(const struct rtc_time *utc);
int ef800_main_rtc_get_local_time(struct rtc_time *local);
const struct ef800_time_settings *ef800_main_get_time_settings(void);
int ef800_main_reload_time_settings(void);

#endif /* EF800_MAIN_FOUNDATION_H */
