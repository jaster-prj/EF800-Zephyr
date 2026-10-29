/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "ef800_console.h"
#include "ef800_main_usb_mtp.h"
#include "ef800_virtual_usb_peer.h"

#define TEST_DATA_DISK "DATA"
#define TEST_DATA_ROOT "/DATA:/data"
#define TEST_PLUGIN_ROOT "/DATA:/plugins"
#define TEST_FWU_ROOT "/DATA:/fwu"

BUILD_ASSERT(!IS_ENABLED(CONFIG_USBD_MSC_CLASS),
	"Native-sim MainApplication USBMSD0 must omit the MSC class");

static FATFS data_fat_fs;
static struct fs_mount_t data_mount = {
	.type = FS_FATFS,
	.mnt_point = "/DATA:",
	.fs_data = &data_fat_fs,
	.storage_dev = (void *)PARTITION_ID(qspi_storage_partition),
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static bool data_mounted;
static bool mtp_started;
static bool cdc_started;

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

static int write_test_file(const char *path, const uint8_t *data, size_t length)
{
	struct fs_file_t file;
	int ret;

	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		return ret;
	}
	ssize_t written = fs_write(&file, data, length);
	if (written != (ssize_t)length) {
		ret = written < 0 ? (int)written : -EIO;
	}
	if (fs_close(&file) != 0 && ret == 0) {
		ret = -EIO;
	}
	return ret;
}

static void reset_test_filesystem(void)
{
	static const uint8_t baseline[] = "baseline";
	static const char *const roots[] = {
		TEST_DATA_ROOT,
		TEST_PLUGIN_ROOT,
		TEST_FWU_ROOT,
	};

	if (data_mounted) {
		zassert_ok(fs_unmount(&data_mount), NULL);
		data_mounted = false;
	}
	zassert_ok(fs_mkfs(FS_FATFS, (uintptr_t)TEST_DATA_DISK ":", NULL, 0), NULL);
	zassert_ok(fs_mount(&data_mount), NULL);
	data_mounted = true;
	for (size_t index = 0U; index < ARRAY_SIZE(roots); ++index) {
		char path[96];

		zassert_ok(ensure_directory(roots[index]), NULL);
		zassert_true(snprintf(path, sizeof(path), "%s/baseline.txt", roots[index]) > 0,
			     NULL);
		zassert_ok(write_test_file(path, baseline, sizeof(baseline)), NULL);
	}
	zassert_ok(ensure_directory("/DATA:/data/nested"), NULL);
	zassert_ok(write_test_file("/DATA:/data/nested/inside.txt", baseline,
				   sizeof(baseline)), NULL);
}

static void *virtual_usb_setup(void)
{
	zassert_ok(ef800_virtual_usb_peer_init(), NULL);
	return NULL;
}

static void virtual_usb_before(void *fixture)
{
	ARG_UNUSED(fixture);
	reset_test_filesystem();
}

static void virtual_usb_after(void *fixture)
{
	ARG_UNUSED(fixture);
	if (mtp_started) {
		(void)ef800_main_usb_mtp_stop();
		mtp_started = false;
	}
	if (cdc_started) {
		(void)ef800_console_usb_stop();
		cdc_started = false;
		ef800_console_clear_target();
	}
	ef800_virtual_usb_peer_invalidate_session();
	if (data_mounted) {
		(void)fs_unmount(&data_mount);
		data_mounted = false;
	}
}

static void start_mtp_context(void)
{
	zassert_ok(ef800_main_usb_mtp_start(), NULL);
	mtp_started = true;
	zassert_ok(ef800_virtual_usb_peer_wait_for_device(5000U), NULL);
}

static bool entry_named(const struct ef800_virtual_mtp_entry *entries,
			size_t count, const char *name)
{
	for (size_t index = 0U; index < count; ++index) {
		if (strcmp(entries[index].name, name) == 0) {
			return true;
		}
	}
	return false;
}

static void assert_file_contents(const char *path, const uint8_t *expected,
				 size_t expected_length)
{
	struct fs_file_t file;
	uint8_t actual[64];

	zassert_true(expected_length <= sizeof(actual), NULL);
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, path, FS_O_READ), NULL);
	zassert_equal(fs_read(&file, actual, expected_length),
		      (ssize_t)expected_length, NULL);
	zassert_ok(fs_close(&file), NULL);
	zassert_mem_equal(actual, expected, expected_length, NULL);
}

ZTEST(ef800_virtual_usb, test_real_main_mtp_lists_uploads_and_downloads)
{
	static const uint8_t upload[] = {0x45, 0x46, 0x38, 0x30, 0x30, 0x21, 0x0A, 0xFF};
	struct ef800_virtual_mtp_entry entries[EF800_VIRTUAL_USB_MAX_OBJECTS];
	uint8_t too_small[sizeof(upload) - 1U];
	uint8_t downloaded[sizeof(upload)];
	size_t count = 0U;
	size_t downloaded_length = 0U;
	size_t rejected_length = 0U;
	bool found_nested = false;
	int ret;

	start_mtp_context();
	ret = ef800_virtual_usb_peer_mtp_list(EF800_VIRTUAL_MTP_DATA,
		"", entries, ARRAY_SIZE(entries), &count);
	zassert_equal(ret, 0, "MTP root list failed: %d", ret);
	zassert_true(entry_named(entries, count, "baseline.txt"), NULL);
	zassert_true(entry_named(entries, count, "nested"), NULL);

	zassert_ok(ef800_virtual_usb_peer_mtp_list(EF800_VIRTUAL_MTP_DATA,
		"nested", entries, ARRAY_SIZE(entries), &count), NULL);
	found_nested = entry_named(entries, count, "inside.txt");
	zassert_true(found_nested, NULL);

	zassert_ok(ef800_virtual_usb_peer_mtp_upload(EF800_VIRTUAL_MTP_DATA,
		"nested/peer-upload.bin", upload, sizeof(upload)), NULL);
	assert_file_contents("/DATA:/data/nested/peer-upload.bin", upload,
			     sizeof(upload));
	zassert_ok(ef800_virtual_usb_peer_mtp_download(EF800_VIRTUAL_MTP_DATA,
		"nested/peer-upload.bin", downloaded, sizeof(downloaded),
		&downloaded_length), NULL);
	zassert_equal(downloaded_length, sizeof(upload), NULL);
	zassert_mem_equal(downloaded, upload, sizeof(upload), NULL);

	zassert_equal(ef800_virtual_usb_peer_mtp_list(EF800_VIRTUAL_MTP_DATA,
		"../", entries, ARRAY_SIZE(entries), &count), -EINVAL, NULL);
	zassert_equal(ef800_virtual_usb_peer_mtp_download(EF800_VIRTUAL_MTP_DATA,
		"nested/peer-upload.bin", too_small, sizeof(too_small),
		&rejected_length), -ENOSPC, NULL);
	zassert_equal(rejected_length, sizeof(upload), NULL);
	zassert_equal(ef800_virtual_usb_peer_mtp_upload(EF800_VIRTUAL_MTP_DATA,
		"../escape.bin", upload, sizeof(upload)), -EINVAL, NULL);
}

ZTEST(ef800_virtual_usb, test_usbmsd0_keeps_shared_data_fat_owned)
{
	struct fs_dirent entry;

	zassert_true(IS_ENABLED(CONFIG_BOARD_NATIVE_SIM), NULL);
	zassert_false(IS_ENABLED(CONFIG_USBD_MSC_CLASS), NULL);
	zassert_true(data_mounted, NULL);
	zassert_ok(fs_stat("/DATA:/data/baseline.txt", &entry), NULL);
	zassert_equal(entry.type, FS_DIR_ENTRY_FILE, NULL);
}

ZTEST(ef800_virtual_usb, test_real_main_cdc_acm_exchanges_bytes)
{
	static const uint8_t host_to_device[] = {0x43, 0x44, 0x43, 0x31};
	static const uint8_t device_to_host[] = {0x44, 0x45, 0x56, 0x31};
	uint8_t received[sizeof(host_to_device)];
	uint8_t usb_data[512];
	size_t usb_length = 0U;
	bool host_received_output = false;
	int64_t deadline;

	ef800_console_prepare();
	zassert_ok(ef800_console_set_target(4U), NULL);
	zassert_ok(ef800_console_usb_start(), NULL);
	cdc_started = true;
	zassert_ok(ef800_virtual_usb_peer_wait_for_device(5000U), NULL);

	zassert_ok(ef800_virtual_usb_peer_cdc_write(host_to_device,
						     sizeof(host_to_device)), NULL);
	deadline = k_uptime_get() + 1000;
	for (size_t index = 0U; index < sizeof(received);) {
		if (ef800_console_read(&received[index]) == 0) {
			index++;
		} else if (k_uptime_get() < deadline) {
			k_msleep(1U);
		} else {
			zassert_true(false, "CDC ACM host data was not delivered to Main console");
		}
	}
	zassert_mem_equal(received, host_to_device, sizeof(received), NULL);

	zassert_equal(ef800_console_write(device_to_host, sizeof(device_to_host)),
		      sizeof(device_to_host), NULL);
	deadline = k_uptime_get() + 1500;
	while (k_uptime_get() < deadline && !host_received_output) {
		usb_length = 0U;
		if (ef800_virtual_usb_peer_cdc_read(usb_data, sizeof(usb_data),
						     &usb_length, 100U) == 0 &&
			usb_length >= sizeof(device_to_host)) {
			for (size_t index = 0U;
				index + sizeof(device_to_host) <= usb_length; ++index) {
				if (memcmp(&usb_data[index], device_to_host,
					   sizeof(device_to_host)) == 0) {
					host_received_output = true;
					break;
				}
			}
		}
	}
	zassert_true(host_received_output,
		     "CDC ACM device output was not observed on the virtual host");
}

ZTEST_SUITE(ef800_virtual_usb, NULL, virtual_usb_setup, virtual_usb_before,
	virtual_usb_after, NULL);
