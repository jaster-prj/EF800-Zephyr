/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <zephyr/usb/class/usbd_mtp.h>
#include "usbd_mtp_class.h"

#define MTP_OP_OPEN_SESSION 0x1002U
#define MTP_OP_SEND_OBJECT_INFO 0x100CU
#define MTP_OP_SEND_OBJECT 0x100DU
#define MTP_CONTAINER_COMMAND 1U
#define MTP_CONTAINER_DATA 2U
#define MTP_RESP_OK 0x2001U
#define MTP_RESP_GENERAL_ERROR 0x2002U
#define MTP_RESP_TRANSACTION_CANCELLED 0x201FU
#define MTP_FORMAT_UNDEFINED 0x3000U
#define MTP_STORAGE_ID(storage_index) (0x00010000U + (storage_index))
#define MTP_ROOT_PARENT 0xFFFFFFFFU
#define MTP_HEADER_SIZE 12U
#define MTP_OBJECT_INFO_FIXED_SIZE 53U
#define MTP_TEST_PACKET_SIZE 512U
#define MTP_TEST_OBJECT_SIZE 8U
#define MTP_TEST_DISK_CONFIG "EF800"
#define MTP_TEST_DISK_DATA "DATA"

static const char *const storage_roots[] = {
	"/EF800:/.configuration",
	"/DATA:/data",
	"/DATA:/plugins",
	"/DATA:/fwu",
};

USBD_MTP_DEFINE_INSTANCE(backup_mtp_test,
	"EF800", "EF800 Recovery Test", "1.0", "EF800000000000000000000000000004",
	USBD_MTP_STORAGE_ENTRY_NAMED("/EF800:/.configuration", "Configuration", false),
	USBD_MTP_STORAGE_ENTRY_NAMED("/DATA:/data", "Data", false),
	USBD_MTP_STORAGE_ENTRY_NAMED("/DATA:/plugins", "Plugins", false),
	USBD_MTP_STORAGE_ENTRY_NAMED("/DATA:/fwu", "FWU", false));

NET_BUF_POOL_FIXED_DEFINE(backup_mtp_test_pool, 4, MTP_TEST_PACKET_SIZE, 0, NULL);

static FATFS configuration_fat_fs;
static FATFS data_fat_fs;
static struct fs_mount_t configuration_mount = {
	.type = FS_FATFS,
	.mnt_point = "/EF800:",
	.fs_data = &configuration_fat_fs,
	.storage_dev = (void *)PARTITION_ID(qspi_configuration_partition),
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static struct fs_mount_t data_mount = {
	.type = FS_FATFS,
	.mnt_point = "/DATA:",
	.fs_data = &data_fat_fs,
	.storage_dev = (void *)PARTITION_ID(qspi_storage_partition),
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static bool filesystems_mounted;
static struct mtp_context mtp_context;

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

static int write_file(const char *path, const uint8_t *content, size_t length)
{
	struct fs_file_t file;
	size_t offset = 0U;
	int ret;

	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		return ret;
	}
	while (offset < length) {
		ssize_t written = fs_write(&file, content + offset, length - offset);
		if (written <= 0) {
			ret = written < 0 ? (int)written : -EIO;
			break;
		}
		offset += (size_t)written;
	}
	if (fs_close(&file) != 0 && ret == 0) {
		ret = -EIO;
	}
	return ret;
}

static void reset_test_filesystems(void)
{
	static const uint8_t keep_content[] = "preserve-me";

	if (filesystems_mounted) {
		zassert_ok(fs_unmount(&data_mount), NULL);
		zassert_ok(fs_unmount(&configuration_mount), NULL);
		filesystems_mounted = false;
	}
	zassert_ok(fs_mkfs(FS_FATFS, (uintptr_t)MTP_TEST_DISK_CONFIG ":", NULL, 0), NULL);
	zassert_ok(fs_mkfs(FS_FATFS, (uintptr_t)MTP_TEST_DISK_DATA ":", NULL, 0), NULL);
	zassert_ok(fs_mount(&configuration_mount), NULL);
	zassert_ok(fs_mount(&data_mount), NULL);
	filesystems_mounted = true;
	for (size_t index = 0U; index < ARRAY_SIZE(storage_roots); ++index) {
		char path[96];

		zassert_ok(ensure_directory(storage_roots[index]), NULL);
		zassert_true(snprintf(path, sizeof(path), "%s/keep.bin",
			storage_roots[index]) > 0, NULL);
		zassert_ok(write_file(path, keep_content, sizeof(keep_content)), NULL);
	}
}

static void assert_keep_file_unchanged(size_t storage_index)
{
	static const uint8_t expected[] = "preserve-me";
	struct fs_file_t file;
	char path[96];
	uint8_t actual[sizeof(expected)];

	zassert_true(snprintf(path, sizeof(path), "%s/keep.bin",
		storage_roots[storage_index]) > 0, NULL);
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, path, FS_O_READ), NULL);
	zassert_equal(fs_read(&file, actual, sizeof(actual)), sizeof(actual), NULL);
	zassert_ok(fs_close(&file), NULL);
	zassert_mem_equal(actual, expected, sizeof(expected), NULL);
}

static int dispatch_packet(const uint8_t *packet, size_t packet_size,
			   uint8_t *response, size_t response_capacity,
			   size_t *response_size)
{
	struct net_buf *input = net_buf_alloc(&backup_mtp_test_pool, K_NO_WAIT);
	struct net_buf *output = net_buf_alloc(&backup_mtp_test_pool, K_NO_WAIT);
	int ret;

	if (input == NULL || output == NULL) {
		if (input != NULL) {
			net_buf_unref(input);
		}
		if (output != NULL) {
			net_buf_unref(output);
		}
		return -ENOMEM;
	}
	net_buf_add_mem(input, packet, packet_size);
	ret = mtp_commands_handler(&mtp_context, input, output);
	if (ret >= 0) {
		if (output->len > response_capacity) {
			ret = -ENOSPC;
		} else {
			memcpy(response, output->data, output->len);
			*response_size = output->len;
			ret = 0;
		}
	}
	net_buf_unref(output);
	net_buf_unref(input);
	return ret;
}

static size_t make_container(uint8_t *packet, size_t capacity, uint16_t type,
			     uint16_t code, uint32_t transaction_id,
			     const uint32_t *parameters, size_t parameter_count)
{
	size_t length = MTP_HEADER_SIZE + parameter_count * sizeof(uint32_t);

	if (length > capacity) {
		return 0U;
	}
	memset(packet, 0, length);
	sys_put_le32((uint32_t)length, &packet[0]);
	sys_put_le16(type, &packet[4]);
	sys_put_le16(code, &packet[6]);
	sys_put_le32(transaction_id, &packet[8]);
	for (size_t index = 0U; index < parameter_count; ++index) {
		sys_put_le32(parameters[index], &packet[MTP_HEADER_SIZE +
			index * sizeof(uint32_t)]);
	}
	return length;
}

static uint16_t response_code(const uint8_t *response, size_t response_size)
{
	return response_size >= MTP_HEADER_SIZE ? sys_get_le16(&response[6]) : 0U;
}

static void initialize_mtp_context(void)
{
	memset(&mtp_context, 0, sizeof(mtp_context));
	zassert_ok(mtp_init(&mtp_context, &usbd_mtp_instance_backup_mtp_test), NULL);
}

static uint16_t open_mtp_session_expected(uint16_t expected_response)
{
	uint8_t packet[32];
	uint8_t response[MTP_TEST_PACKET_SIZE];
	size_t response_size = 0U;
	const uint32_t parameters[] = {1U};
	size_t packet_size = make_container(packet, sizeof(packet), MTP_CONTAINER_COMMAND,
		MTP_OP_OPEN_SESSION, 1U, parameters, ARRAY_SIZE(parameters));

	zassert_true(packet_size != 0U, NULL);
	zassert_ok(dispatch_packet(packet, packet_size, response, sizeof(response),
			   &response_size), NULL);
	zassert_equal(response_code(response, response_size), expected_response, NULL);
	if (expected_response == MTP_RESP_OK) {
		mtp_context.phase = MTP_PHASE_REQUEST;
	}
	return response_code(response, response_size);
}

static void open_mtp_session(void)
{
	(void)open_mtp_session_expected(MTP_RESP_OK);
}

static size_t make_object_info(uint8_t *packet, size_t capacity, uint32_t transaction_id,
			       uint32_t storage_id, const char *name, uint32_t object_size)
{
	size_t name_chars = strlen(name) + 1U;
	size_t length = MTP_HEADER_SIZE + MTP_OBJECT_INFO_FIXED_SIZE +
		name_chars * sizeof(uint16_t);
	uint8_t *info;

	if (length > capacity || name_chars > UINT8_MAX) {
		return 0U;
	}
	length = make_container(packet, capacity, MTP_CONTAINER_DATA,
		MTP_OP_SEND_OBJECT_INFO, transaction_id, NULL, 0U);
	if (length == 0U) {
		return 0U;
	}
	length += MTP_OBJECT_INFO_FIXED_SIZE + name_chars * sizeof(uint16_t);
	sys_put_le32((uint32_t)length, &packet[0]);
	info = &packet[MTP_HEADER_SIZE];
	sys_put_le32(storage_id, &info[0]);
	sys_put_le16(MTP_FORMAT_UNDEFINED, &info[4]);
	sys_put_le32(object_size, &info[8]);
	info[52] = (uint8_t)name_chars;
	for (size_t index = 0U; index < name_chars; ++index) {
		uint16_t character = index < name_chars - 1U ? (uint8_t)name[index] : 0U;
		sys_put_le16(character, &info[MTP_OBJECT_INFO_FIXED_SIZE +
			index * sizeof(uint16_t)]);
	}
	return length;
}

static void start_partial_upload(size_t storage_index, uint32_t transaction_id,
				 const char *name, char *temporary_path,
				 size_t temporary_capacity, char *final_path,
				 size_t final_capacity)
{
	uint8_t packet[MTP_TEST_PACKET_SIZE];
	uint8_t response[MTP_TEST_PACKET_SIZE];
	size_t response_size = 0U;
	const uint32_t parameters[] = {MTP_STORAGE_ID(storage_index), MTP_ROOT_PARENT};
	size_t packet_size = make_container(packet, sizeof(packet), MTP_CONTAINER_COMMAND,
		MTP_OP_SEND_OBJECT_INFO, transaction_id, parameters, ARRAY_SIZE(parameters));

	zassert_true(packet_size != 0U, NULL);
	zassert_ok(dispatch_packet(packet, packet_size, response, sizeof(response),
			   &response_size), NULL);
	zassert_equal(response_size, 0U, NULL);
	packet_size = make_object_info(packet, sizeof(packet), transaction_id,
		MTP_STORAGE_ID(storage_index), name, MTP_TEST_OBJECT_SIZE);
	zassert_true(packet_size != 0U, NULL);
	zassert_ok(dispatch_packet(packet, packet_size, response, sizeof(response),
			   &response_size), NULL);
	zassert_equal(response_code(response, response_size), MTP_RESP_OK, NULL);
	zassert_true(snprintf(temporary_path, temporary_capacity,
		"%s/.mtp-%08x.tmp", storage_roots[storage_index - 1U], transaction_id) > 0,
		NULL);
	zassert_true(snprintf(final_path, final_capacity, "%s/%s",
		storage_roots[storage_index - 1U], name) > 0, NULL);
	mtp_context.phase = MTP_PHASE_REQUEST;

	packet_size = make_container(packet, sizeof(packet), MTP_CONTAINER_COMMAND,
		MTP_OP_SEND_OBJECT, transaction_id + 1U, NULL, 0U);
	zassert_true(packet_size != 0U, NULL);
	zassert_ok(dispatch_packet(packet, packet_size, response, sizeof(response),
			   &response_size), NULL);
	zassert_equal(response_size, 0U, NULL);
	packet_size = make_container(packet, sizeof(packet), MTP_CONTAINER_DATA,
		MTP_OP_SEND_OBJECT, transaction_id + 1U, NULL, 0U);
	zassert_true(packet_size != 0U, NULL);
	packet[0] = (uint8_t)(packet_size + 4U);
	packet[1] = (uint8_t)((packet_size + 4U) >> 8);
	packet[2] = (uint8_t)((packet_size + 4U) >> 16);
	packet[3] = (uint8_t)((packet_size + 4U) >> 24);
	memcpy(&packet[packet_size], "part", 4U);
	zassert_ok(dispatch_packet(packet, packet_size + 4U, response, sizeof(response),
			   &response_size), NULL);
	zassert_equal(response_size, 0U, NULL);
	zassert_equal(mtp_context.transfer_state.transferred, 4U, NULL);
}

static void assert_no_partial_or_final(const char *temporary_path,
				       const char *final_path)
{
	struct fs_dirent entry;
	zassert_equal(fs_stat(temporary_path, &entry), -ENOENT, NULL);
	zassert_equal(fs_stat(final_path, &entry), -ENOENT, NULL);
}

static void assert_path_absent(const char *path)
{
	struct fs_dirent entry;
	zassert_equal(fs_stat(path, &entry), -ENOENT, NULL);
}

static void verify_uploaded_file(size_t storage_index, const char *name,
				 const uint8_t *expected, size_t length)
{
	struct fs_file_t file;
	char path[96];
	uint8_t actual[MTP_TEST_OBJECT_SIZE];

	zassert_true(snprintf(path, sizeof(path), "%s/%s",
		storage_roots[storage_index], name) > 0, NULL);
	zassert_equal(fs_stat(path, &(struct fs_dirent){0}), 0, NULL);
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, path, FS_O_READ), NULL);
	zassert_equal(fs_read(&file, actual, length), (ssize_t)length, NULL);
	zassert_ok(fs_close(&file), NULL);
	zassert_mem_equal(actual, expected, length, NULL);
}

static void send_complete_upload(size_t storage_index, uint32_t transaction_id,
				 const char *name, const uint8_t *content, size_t size)
{
	uint8_t packet[MTP_TEST_PACKET_SIZE];
	uint8_t response[MTP_TEST_PACKET_SIZE];
	size_t response_size = 0U;
	char temporary_path[128];
	char final_path[128];
	const uint32_t parameters[] = {MTP_STORAGE_ID(storage_index), MTP_ROOT_PARENT};
	size_t packet_size = make_container(packet, sizeof(packet), MTP_CONTAINER_COMMAND,
		MTP_OP_SEND_OBJECT_INFO, transaction_id, parameters, ARRAY_SIZE(parameters));

	zassert_true(packet_size != 0U, NULL);
	zassert_ok(dispatch_packet(packet, packet_size, response, sizeof(response),
			   &response_size), NULL);
	packet_size = make_object_info(packet, sizeof(packet), transaction_id,
		MTP_STORAGE_ID(storage_index), name, (uint32_t)size);
	zassert_true(packet_size != 0U, NULL);
	zassert_ok(dispatch_packet(packet, packet_size, response, sizeof(response),
			   &response_size), NULL);
	zassert_equal(response_code(response, response_size), MTP_RESP_OK, NULL);
	zassert_true(snprintf(temporary_path, sizeof(temporary_path),
		"%s/.mtp-%08x.tmp", storage_roots[storage_index - 1U], transaction_id) > 0,
		NULL);
	zassert_true(snprintf(final_path, sizeof(final_path), "%s/%s",
		storage_roots[storage_index - 1U], name) > 0, NULL);
	mtp_context.phase = MTP_PHASE_REQUEST;

	packet_size = make_container(packet, sizeof(packet), MTP_CONTAINER_COMMAND,
		MTP_OP_SEND_OBJECT, transaction_id + 1U, NULL, 0U);
	zassert_true(packet_size != 0U, NULL);
	zassert_ok(dispatch_packet(packet, packet_size, response, sizeof(response),
			   &response_size), NULL);
	packet_size = make_container(packet, sizeof(packet), MTP_CONTAINER_DATA,
		MTP_OP_SEND_OBJECT, transaction_id + 1U, NULL, 0U);
	zassert_true(packet_size != 0U, NULL);
	packet[0] = (uint8_t)(packet_size + size);
	packet[1] = (uint8_t)((packet_size + size) >> 8);
	packet[2] = (uint8_t)((packet_size + size) >> 16);
	packet[3] = (uint8_t)((packet_size + size) >> 24);
	memcpy(&packet[packet_size], content, size);
	zassert_ok(dispatch_packet(packet, packet_size + size, response, sizeof(response),
			   &response_size), NULL);
	zassert_equal(response_code(response, response_size), MTP_RESP_OK, NULL);
	mtp_context.phase = MTP_PHASE_REQUEST;
	assert_path_absent(temporary_path);
	verify_uploaded_file(storage_index - 1U, name, content, size);
}

ZTEST(backup_mtp, test_complete_uploads_commit_to_all_four_roots)
{
	static const uint8_t content[] = {0x45, 0x46, 0x38, 0x30, 0x30, 0x21, 0x0A, 0xFF};

	reset_test_filesystems();
	initialize_mtp_context();
	open_mtp_session();

	for (size_t storage = 1U; storage <= ARRAY_SIZE(storage_roots); ++storage) {
		char name[20];
		int len = snprintk(name, sizeof(name), "upload%u.bin", (unsigned int)storage);
		zassert_true(len > 0 && (size_t)len < sizeof(name), NULL);
		send_complete_upload(storage, (uint32_t)(2U * storage), name,
			content, sizeof(content));
		assert_keep_file_unchanged(storage - 1U);
	}
}

ZTEST(backup_mtp, test_cancel_and_disconnect_discard_staged_files)
{
	struct net_buf *status;

	reset_test_filesystems();
	initialize_mtp_context();
	open_mtp_session();

	for (size_t storage = 1U; storage <= ARRAY_SIZE(storage_roots); ++storage) {
		char cancel_name[20];
		char cancel_temp[128];
		char cancel_final[128];
		uint32_t cancel_tid = 2U + (uint32_t)((storage - 1U) * 2U);
		int len = snprintk(cancel_name, sizeof(cancel_name), "cancel%u.bin",
			(unsigned int)storage);
		zassert_true(len > 0 && (size_t)len < sizeof(cancel_name), NULL);
		start_partial_upload(storage, cancel_tid, cancel_name, cancel_temp,
			sizeof(cancel_temp), cancel_final, sizeof(cancel_final));
		mtp_cancel(&mtp_context, NULL);
		status = net_buf_alloc(&backup_mtp_test_pool, K_NO_WAIT);
		zassert_not_null(status, NULL);
		zassert_ok(mtp_get_dev_status(&mtp_context, status), NULL);
		zassert_equal(sys_get_le16(&status->data[2]),
			      MTP_RESP_TRANSACTION_CANCELLED, NULL);
		net_buf_unref(status);
		assert_no_partial_or_final(cancel_temp, cancel_final);
		assert_keep_file_unchanged(storage - 1U);
	}
	mtp_reset(&mtp_context);

	for (size_t storage = 1U; storage <= ARRAY_SIZE(storage_roots); ++storage) {
		char name[20];
		char temporary_path[128];
		char final_path[128];
		int len = snprintk(name, sizeof(name), "reset%u.bin", (unsigned int)storage);

		zassert_true(len > 0 && (size_t)len < sizeof(name), NULL);
		initialize_mtp_context();
		open_mtp_session();
		start_partial_upload(storage, 2U, name, temporary_path,
			sizeof(temporary_path), final_path, sizeof(final_path));
		/* MTP device disable/reset is the disconnect cleanup path. */
		mtp_reset(&mtp_context);
		assert_no_partial_or_final(temporary_path, final_path);
		assert_keep_file_unchanged(storage - 1U);
	}
}

ZTEST(backup_mtp, test_object_capacity_is_enforced_per_storage)
{
	reset_test_filesystems();
	for (size_t storage = 0U; storage < ARRAY_SIZE(storage_roots); ++storage) {
		for (size_t object = 0U; object < MTP_MAX_FILES; ++object) {
			char path[96];
			char name[24];
			int len = snprintk(name, sizeof(name), "capacity%02u.bin",
				(unsigned int)object);
			zassert_true(len > 0 && (size_t)len < sizeof(name), NULL);
			len = snprintk(path, sizeof(path), "%s/%s", storage_roots[storage], name);
			zassert_true(len > 0 && (size_t)len < sizeof(path), NULL);
			zassert_ok(write_file(path, (const uint8_t *)"x", 1U), NULL);
		}
	}
	initialize_mtp_context();
	(void)open_mtp_session_expected(MTP_RESP_GENERAL_ERROR);
	for (size_t storage = 1U; storage <= ARRAY_SIZE(storage_roots); ++storage) {
		zassert_true(mtp_context.partitions[storage].overflowed, NULL);
		assert_keep_file_unchanged(storage - 1U);
	}
}

ZTEST_SUITE(backup_mtp, NULL, NULL, NULL, NULL, NULL);
