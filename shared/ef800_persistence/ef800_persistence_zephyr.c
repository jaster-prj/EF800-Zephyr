/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_persistence.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>

#if defined(CONFIG_FILE_SYSTEM) && CONFIG_FILE_SYSTEM
#include <zephyr/fs/fs.h>

#include "ef800_app.h"
#endif

#if defined(CONFIG_FILE_SYSTEM) && CONFIG_FILE_SYSTEM

static const char *root_path(enum ef800_persistence_root root)
{
	return root == EF800_PERSISTENCE_CONFIGURATION ?
		ef800_storage_configuration_dir() : ef800_storage_data_dir();
}

static int build_path(enum ef800_persistence_root root, const char *name,
		      char *path, size_t path_size)
{
	return ef800_storage_build_path((enum ef800_storage_root)root, name, path,
					path_size);
}

static int zephyr_read(void *context, enum ef800_persistence_root root,
			       const char *name, void *buffer, size_t capacity,
			       size_t *length)
{
	struct fs_dirent fs_entry;
	struct fs_file_t file;
	char path[EF800_PERSISTENCE_MAX_PATH];
	ssize_t read_length;
	int ret;

	ARG_UNUSED(context);
	if (!ef800_storage_root_is_mounted((enum ef800_storage_root)root)) {
		return -ENOTSUP;
	}
	ret = build_path(root, name, path, sizeof(path));
	if (ret != 0) {
		return ret;
	}
	ret = fs_stat(path, &fs_entry);
	if (ret != 0) {
		return ret;
	}
	if (fs_entry.type != FS_DIR_ENTRY_FILE) {
		return -EISDIR;
	}
	*length = fs_entry.size;
	if (fs_entry.size > capacity) {
		return -ENOSPC;
	}
	if (*length == 0U) {
		return 0;
	}
	ret = ef800_storage_open(&file, (enum ef800_storage_root)root, name,
					 FS_O_READ);
	if (ret != 0) {
		return ret;
	}
	read_length = fs_read(&file, buffer, capacity);
	ret = fs_close(&file);
	if (read_length < 0) {
		return (int)read_length;
	}
	if (ret != 0) {
		return ret;
	}
	if ((size_t)read_length != *length) {
		return -EIO;
	}
	return 0;
}

static int zephyr_read_range(void *context, enum ef800_persistence_root root,
				     const char *name, size_t offset, void *buffer,
				     size_t capacity, size_t *length)
{
	struct fs_dirent fs_entry;
	struct fs_file_t file;
	ssize_t read_length;
	char path[EF800_PERSISTENCE_MAX_PATH];
	size_t requested;
	int ret;

	ARG_UNUSED(context);
	if (!ef800_storage_root_is_mounted((enum ef800_storage_root)root)) {
		return -ENOTSUP;
	}
	ret = build_path(root, name, path, sizeof(path));
	if (ret != 0) {
		return ret;
	}
	ret = fs_stat(path, &fs_entry);
	if (ret != 0) {
		return ret;
	}
	if (fs_entry.type != FS_DIR_ENTRY_FILE) {
		return -EISDIR;
	}
	if (offset > fs_entry.size) {
		return -EINVAL;
	}
	requested = MIN(capacity, (size_t)fs_entry.size - offset);
	*length = 0U;
	if (requested == 0U) {
		return 0;
	}
	ret = ef800_storage_open(&file, (enum ef800_storage_root)root, name,
					 FS_O_READ);
	if (ret != 0) {
		return ret;
	}
	ret = fs_seek(&file, (off_t)offset, FS_SEEK_SET);
	if (ret != 0) {
		(void)fs_close(&file);
		return ret;
	}
	read_length = fs_read(&file, buffer, requested);
	ret = fs_close(&file);
	if (read_length < 0) {
		return (int)read_length;
	}
	if (ret != 0) {
		return ret;
	}
	*length = (size_t)read_length;
	return 0;
}

static int write_file(struct fs_file_t *file, const void *buffer, size_t length)
{
	const uint8_t *bytes = buffer;
	size_t offset = 0U;

	while (offset < length) {
		ssize_t written = fs_write(file, bytes + offset, length - offset);
		if (written <= 0) {
			return written < 0 ? (int)written : -EIO;
		}
		offset += (size_t)written;
	}
	return 0;
}

static int zephyr_write(void *context, enum ef800_persistence_root root,
				const char *name, const void *buffer, size_t length)
{
	struct fs_file_t file;
	int ret;

	ARG_UNUSED(context);
	if (!ef800_storage_root_is_mounted((enum ef800_storage_root)root)) {
		return -ENOTSUP;
	}
	ret = ef800_storage_open(&file, (enum ef800_storage_root)root, name,
					 FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		return ret;
	}
	ret = write_file(&file, buffer, length);
	if (fs_close(&file) != 0 && ret == 0) {
		ret = -EIO;
	}
	return ret;
}

static int zephyr_append(void *context, enum ef800_persistence_root root,
				 const char *name, const void *buffer, size_t length)
{
	struct fs_file_t file;
	int ret;

	ARG_UNUSED(context);
	if (!ef800_storage_root_is_mounted((enum ef800_storage_root)root)) {
		return -ENOTSUP;
	}
	ret = ef800_storage_open(&file, (enum ef800_storage_root)root, name,
					 FS_O_CREATE | FS_O_WRITE | FS_O_APPEND);
	if (ret != 0) {
		return ret;
	}
	ret = write_file(&file, buffer, length);
	if (fs_close(&file) != 0 && ret == 0) {
		ret = -EIO;
	}
	return ret;
}

static int zephyr_stat(void *context, enum ef800_persistence_root root,
			       const char *name, struct ef800_persistence_entry *entry)
{
	char path[EF800_PERSISTENCE_MAX_PATH];
	struct fs_dirent fs_entry;
	int ret;

	ARG_UNUSED(context);
	if (!ef800_storage_root_is_mounted((enum ef800_storage_root)root)) {
		return -ENOTSUP;
	}
	ret = build_path(root, name, path, sizeof(path));
	if (ret != 0) {
		return ret;
	}
	ret = fs_stat(path, &fs_entry);
	if (ret != 0) {
		return ret;
	}
	if (strlen(fs_entry.name) > EF800_PERSISTENCE_MAX_NAME) {
		return -ENAMETOOLONG;
	}
	entry->type = (enum ef800_persistence_entry_type)fs_entry.type;
	strcpy(entry->name, fs_entry.name);
	entry->size = fs_entry.size;
	return 0;
}

static int zephyr_list(void *context, enum ef800_persistence_root root,
			       ef800_persistence_list_callback callback, void *callback_context)
{
	struct fs_dir_t directory;
	struct fs_dirent fs_entry;
	struct ef800_persistence_entry entry;
	int ret;

	ARG_UNUSED(context);
	if (!ef800_storage_root_is_mounted((enum ef800_storage_root)root)) {
		return -ENOTSUP;
	}
	fs_dir_t_init(&directory);
	ret = fs_opendir(&directory, root_path(root));
	if (ret != 0) {
		return ret;
	}
	while (true) {
		ret = fs_readdir(&directory, &fs_entry);
		if (ret != 0 || fs_entry.name[0] == '\0') {
			break;
		}
		if (strlen(fs_entry.name) > EF800_PERSISTENCE_MAX_NAME) {
			ret = -ENAMETOOLONG;
			break;
		}
		entry.type = (enum ef800_persistence_entry_type)fs_entry.type;
		strcpy(entry.name, fs_entry.name);
		entry.size = fs_entry.size;
		ret = callback(&entry, callback_context);
		if (ret != 0) {
			break;
		}
	}
	if (fs_closedir(&directory) != 0 && ret == 0) {
		ret = -EIO;
	}
	return ret;
}

static int zephyr_remove(void *context, enum ef800_persistence_root root,
				 const char *name)
{
	char path[EF800_PERSISTENCE_MAX_PATH];
	int ret;

	ARG_UNUSED(context);
	if (!ef800_storage_root_is_mounted((enum ef800_storage_root)root)) {
		return -ENOTSUP;
	}
	ret = build_path(root, name, path, sizeof(path));
	if (ret != 0) {
		return ret;
	}
	return fs_unlink(path);
}

static int zephyr_rename(void *context, enum ef800_persistence_root root,
			 const char *source_name, const char *destination_name)
{
	char source_path[EF800_PERSISTENCE_MAX_PATH];
	char destination_path[EF800_PERSISTENCE_MAX_PATH];
	struct fs_dirent entry;
	int ret;

	ARG_UNUSED(context);
	if (!ef800_storage_root_is_mounted((enum ef800_storage_root)root)) {
		return -ENOTSUP;
	}
	ret = build_path(root, source_name, source_path, sizeof(source_path));
	if (ret != 0) {
		return ret;
	}
	ret = build_path(root, destination_name, destination_path,
			 sizeof(destination_path));
	if (ret != 0) {
		return ret;
	}
	ret = fs_stat(destination_path, &entry);
	if (ret == 0) {
		/* Zephyr's FAT fs_rename unlinks an existing destination first. */
		return -EEXIST;
	}
	if (ret != -ENOENT) {
		return ret;
	}
	return fs_rename(source_path, destination_path);
}

static int zephyr_sync(void *context, enum ef800_persistence_root root,
		       const char *name)
{
	struct fs_file_t file;
	char path[EF800_PERSISTENCE_MAX_PATH];
	int ret;
	int close_ret;

	ARG_UNUSED(context);
	if (!ef800_storage_root_is_mounted((enum ef800_storage_root)root)) {
		return -ENOTSUP;
	}
	ret = build_path(root, name, path, sizeof(path));
	if (ret != 0) {
		return ret;
	}
	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_READ | FS_O_WRITE);
	if (ret != 0) {
		return ret;
	}
	ret = fs_sync(&file);
	close_ret = fs_close(&file);
	return ret != 0 ? ret : close_ret;
}

static const struct ef800_persistence_backend zephyr_backend = {
	.read = zephyr_read,
	.read_range = zephyr_read_range,
	.write = zephyr_write,
	.append = zephyr_append,
	.stat = zephyr_stat,
	.list = zephyr_list,
	.remove = zephyr_remove,
	.rename = zephyr_rename,
	.sync = zephyr_sync,
};

const struct ef800_persistence_backend *ef800_persistence_zephyr_backend(void)
{
	return &zephyr_backend;
}

#else

static int unavailable_read(void *context, enum ef800_persistence_root root,
				   const char *name, void *buffer, size_t capacity, size_t *length)
{
	ARG_UNUSED(context); ARG_UNUSED(root); ARG_UNUSED(name);
	ARG_UNUSED(buffer); ARG_UNUSED(capacity); ARG_UNUSED(length);
	return -ENOTSUP;
}

static int unavailable_read_range(void *context, enum ef800_persistence_root root,
					 const char *name, size_t offset, void *buffer,
					 size_t capacity, size_t *length)
{
	ARG_UNUSED(context); ARG_UNUSED(root); ARG_UNUSED(name); ARG_UNUSED(offset);
	ARG_UNUSED(buffer); ARG_UNUSED(capacity); ARG_UNUSED(length);
	return -ENOTSUP;
}

static int unavailable_write(void *context, enum ef800_persistence_root root,
				    const char *name, const void *buffer, size_t length)
{
	ARG_UNUSED(context); ARG_UNUSED(root); ARG_UNUSED(name);
	ARG_UNUSED(buffer); ARG_UNUSED(length);
	return -ENOTSUP;
}

static int unavailable_append(void *context, enum ef800_persistence_root root,
				     const char *name, const void *buffer, size_t length)
{
	ARG_UNUSED(context); ARG_UNUSED(root); ARG_UNUSED(name);
	ARG_UNUSED(buffer); ARG_UNUSED(length);
	return -ENOTSUP;
}

static int unavailable_stat(void *context, enum ef800_persistence_root root,
				   const char *name, struct ef800_persistence_entry *entry)
{
	ARG_UNUSED(context); ARG_UNUSED(root); ARG_UNUSED(name); ARG_UNUSED(entry);
	return -ENOTSUP;
}

static int unavailable_list(void *context, enum ef800_persistence_root root,
				   ef800_persistence_list_callback callback, void *callback_context)
{
	ARG_UNUSED(context); ARG_UNUSED(root); ARG_UNUSED(callback);
	ARG_UNUSED(callback_context);
	return -ENOTSUP;
}

static int unavailable_remove(void *context, enum ef800_persistence_root root,
				     const char *name)
{
	ARG_UNUSED(context); ARG_UNUSED(root); ARG_UNUSED(name);
	return -ENOTSUP;
}

static int unavailable_rename(void *context, enum ef800_persistence_root root,
			      const char *source_name, const char *destination_name)
{
	ARG_UNUSED(context); ARG_UNUSED(root); ARG_UNUSED(source_name);
	ARG_UNUSED(destination_name);
	return -ENOTSUP;
}

static int unavailable_sync(void *context, enum ef800_persistence_root root,
			    const char *name)
{
	ARG_UNUSED(context); ARG_UNUSED(root); ARG_UNUSED(name);
	return -ENOTSUP;
}

const struct ef800_persistence_backend *ef800_persistence_zephyr_backend(void)
{
	static const struct ef800_persistence_backend backend = {
		.read = unavailable_read,
		.read_range = unavailable_read_range,
		.write = unavailable_write,
		.append = unavailable_append,
		.stat = unavailable_stat,
		.list = unavailable_list,
		.remove = unavailable_remove,
		.rename = unavailable_rename,
		.sync = unavailable_sync,
	};
	return &backend;
}

#endif
