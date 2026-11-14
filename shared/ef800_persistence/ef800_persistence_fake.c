/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_persistence_fake.h"

#include <errno.h>
#include <string.h>

static struct ef800_persistence_fake_file *find_file(
	struct ef800_persistence_fake *fake, enum ef800_persistence_root root,
	const char *name)
{
	for (size_t index = 0U; index < EF800_PERSISTENCE_FAKE_FILE_COUNT; ++index) {
		struct ef800_persistence_fake_file *file = &fake->files[index];

		if (file->used && file->root == root && strcmp(file->name, name) == 0) {
			return file;
		}
	}
	return NULL;
}

static struct ef800_persistence_fake_file *get_or_create_file(
	struct ef800_persistence_fake *fake, enum ef800_persistence_root root,
	const char *name)
{
	struct ef800_persistence_fake_file *file = find_file(fake, root, name);

	if (file != NULL) {
		return file;
	}
	for (size_t index = 0U; index < EF800_PERSISTENCE_FAKE_FILE_COUNT; ++index) {
		file = &fake->files[index];
		if (!file->used) {
			file->used = true;
			file->root = root;
			strcpy(file->name, name);
			file->length = 0U;
			return file;
		}
	}
	return NULL;
}

static int fake_read(void *context, enum ef800_persistence_root root,
			     const char *name, void *buffer, size_t capacity, size_t *length)
{
	struct ef800_persistence_fake *fake = context;
	const struct ef800_persistence_fake_file *file = find_file(fake, root, name);

	if (file == NULL) {
		return -ENOENT;
	}
	*length = file->length;
	if (file->length > capacity) {
		return -ENOSPC;
	}
	if (file->length != 0U) {
		memcpy(buffer, file->data, file->length);
	}
	return 0;
}

static int fake_read_range(void *context, enum ef800_persistence_root root,
				   const char *name, size_t offset, void *buffer,
				   size_t capacity, size_t *length)
{
	struct ef800_persistence_fake *fake = context;
	const struct ef800_persistence_fake_file *file = find_file(fake, root, name);
	size_t available;

	if (file == NULL) {
		return -ENOENT;
	}
	if (offset > file->length) {
		return -EINVAL;
	}
	available = file->length - offset;
	*length = available < capacity ? available : capacity;
	if (*length != 0U) {
		memcpy(buffer, file->data + offset, *length);
	}
	return 0;
}

static int fake_write(void *context, enum ef800_persistence_root root,
			      const char *name, const void *buffer, size_t length)
{
	struct ef800_persistence_fake *fake = context;
	struct ef800_persistence_fake_file *file;

	if (length > EF800_PERSISTENCE_FAKE_FILE_SIZE) {
		return -ENOSPC;
	}
	file = get_or_create_file(fake, root, name);
	if (file == NULL) {
		return -ENOSPC;
	}
	if (length != 0U) {
		memcpy(file->data, buffer, length);
	}
	file->length = length;
	return 0;
}

static int fake_append(void *context, enum ef800_persistence_root root,
			       const char *name, const void *buffer, size_t length)
{
	struct ef800_persistence_fake *fake = context;
	struct ef800_persistence_fake_file *file;

	file = get_or_create_file(fake, root, name);
	if (file == NULL || file->length + length > EF800_PERSISTENCE_FAKE_FILE_SIZE) {
		return -ENOSPC;
	}
	if (length != 0U) {
		memcpy(file->data + file->length, buffer, length);
	}
	file->length += length;
	return 0;
}

static int fake_stat(void *context, enum ef800_persistence_root root,
			     const char *name, struct ef800_persistence_entry *entry)
{
	struct ef800_persistence_fake *fake = context;
	const struct ef800_persistence_fake_file *file = find_file(fake, root, name);

	if (file == NULL) {
		return -ENOENT;
	}
	entry->type = EF800_PERSISTENCE_FILE;
	strcpy(entry->name, file->name);
	entry->size = file->length;
	return 0;
}

static int fake_list(void *context, enum ef800_persistence_root root,
			    ef800_persistence_list_callback callback, void *callback_context)
{
	struct ef800_persistence_fake *fake = context;

	for (size_t index = 0U; index < EF800_PERSISTENCE_FAKE_FILE_COUNT; ++index) {
		const struct ef800_persistence_fake_file *file = &fake->files[index];
		struct ef800_persistence_entry entry;

		if (!file->used || file->root != root) {
			continue;
		}
		entry.type = EF800_PERSISTENCE_FILE;
		strcpy(entry.name, file->name);
		entry.size = file->length;
		int ret = callback(&entry, callback_context);
		if (ret != 0) {
			return ret;
		}
	}
	return 0;
}

static int fake_remove(void *context, enum ef800_persistence_root root,
			       const char *name)
{
	struct ef800_persistence_fake *fake = context;
	struct ef800_persistence_fake_file *file = find_file(fake, root, name);

	if (file == NULL) {
		return -ENOENT;
	}
	memset(file, 0, sizeof(*file));
	return 0;
}

static int fake_rename(void *context, enum ef800_persistence_root root,
		       const char *source_name, const char *destination_name)
{
	struct ef800_persistence_fake *fake = context;
	struct ef800_persistence_fake_file *source =
		find_file(fake, root, source_name);

	if (source == NULL) {
		return -ENOENT;
	}
	if (find_file(fake, root, destination_name) != NULL) {
		return -EEXIST;
	}
	strcpy(source->name, destination_name);
	return 0;
}

static int fake_sync(void *context, enum ef800_persistence_root root,
		     const char *name)
{
	struct ef800_persistence_fake *fake = context;
	struct ef800_persistence_fake_file *file = find_file(fake, root, name);

	if (file == NULL) {
		return -ENOENT;
	}
	if (fake->corrupt_on_sync_enabled &&
	    strcmp(fake->corrupt_on_sync_name, name) == 0) {
		if (file->length != 0U) {
			file->data[0] ^= 0x01U;
		}
		fake->corrupt_on_sync_enabled = false;
	}
	return 0;
}

void ef800_persistence_fake_init(struct ef800_persistence_fake *fake)
{
	if (fake == NULL) {
		return;
	}
	memset(fake, 0, sizeof(*fake));
	fake->backend.context = fake;
	fake->backend.read = fake_read;
	fake->backend.read_range = fake_read_range;
	fake->backend.write = fake_write;
	fake->backend.append = fake_append;
	fake->backend.stat = fake_stat;
	fake->backend.list = fake_list;
	fake->backend.remove = fake_remove;
	fake->backend.rename = fake_rename;
	fake->backend.sync = fake_sync;
}
