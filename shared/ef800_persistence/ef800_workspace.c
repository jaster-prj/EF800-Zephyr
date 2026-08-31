/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_workspace.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>

static bool has_suffix(const char *value, const char *suffix)
{
	size_t value_length = strlen(value);
	size_t suffix_length = strlen(suffix);

	return value_length >= suffix_length &&
	       strcmp(value + value_length - suffix_length, suffix) == 0;
}

static int workspace_name_valid(const char *name)
{
	if (name == NULL || name[0] == '\0' || strlen(name) > EF800_WORKSPACE_NAME_MAX ||
	    strchr(name, '/') != NULL || strcmp(name, ".") == 0 ||
	    strcmp(name, "..") == 0 || has_suffix(name, EF800_WORKSPACE_EXTENSION)) {
		return -EINVAL;
	}
	return ef800_persistence_valid_name(name) ? 0 : -EINVAL;
}

static int make_filename(const char *name, char *filename, size_t size)
{
	int length;

	if (workspace_name_valid(name) != 0 || filename == NULL || size == 0U) {
		return -EINVAL;
	}
	length = snprintk(filename, size, "%s%s", name, EF800_WORKSPACE_EXTENSION);
	if (length < 0 || (size_t)length >= size) {
		return -ENAMETOOLONG;
	}
	return 0;
}

static int select_filename(struct ef800_workspace *workspace,
			    const char *filename, size_t length)
{
	char name[EF800_WORKSPACE_NAME_MAX + 1U];
	size_t extension_length = sizeof(EF800_WORKSPACE_EXTENSION) - 1U;

	if (length <= extension_length || length > EF800_PERSISTENCE_MAX_NAME ||
	    memcmp(filename + length - extension_length, EF800_WORKSPACE_EXTENSION,
		   extension_length) != 0 ||
	    length - extension_length > EF800_WORKSPACE_NAME_MAX) {
		return -EINVAL;
	}
	memcpy(name, filename, length - extension_length);
	name[length - extension_length] = '\0';
	if (workspace_name_valid(name) != 0) {
		return -EINVAL;
	}
	memcpy(workspace->name, name, strlen(name) + 1U);
	memcpy(workspace->filename, filename, length);
	workspace->filename[length] = '\0';
	workspace->selected = true;
	return 0;
}

static int save_current(struct ef800_workspace *workspace,
			const char *filename)
{
	return ef800_persistence_write(workspace->storage,
				       EF800_PERSISTENCE_CONFIGURATION,
				       EF800_WORKSPACE_CONFIGURATION_FILE, filename,
				       strlen(filename));
}

int ef800_workspace_init(struct ef800_workspace *workspace,
			 struct ef800_persistence *storage)
{
	if (workspace == NULL || storage == NULL || storage->backend == NULL) {
		return -EINVAL;
	}
	memset(workspace, 0, sizeof(*workspace));
	workspace->storage = storage;
	return 0;
}

int ef800_workspace_load_current(struct ef800_workspace *workspace)
{
	char filename[EF800_PERSISTENCE_MAX_NAME + 1U];
	struct ef800_persistence_entry entry;
	size_t length;
	int ret;

	if (workspace == NULL || workspace->storage == NULL) {
		return -EINVAL;
	}
	ret = ef800_persistence_read(workspace->storage,
				     EF800_PERSISTENCE_CONFIGURATION,
				     EF800_WORKSPACE_CONFIGURATION_FILE, filename,
				     sizeof(filename) - 1U, &length);
	if (ret != 0) {
		return ret;
	}
	while (length > 0U && (filename[length - 1U] == '\r' ||
				       filename[length - 1U] == '\n')) {
		--length;
	}
	if (length == 0U) {
		return -EINVAL;
	}
	filename[length] = '\0';
	ret = select_filename(workspace, filename, length);
	if (ret != 0) {
		return ret;
	}
	ret = ef800_persistence_stat(workspace->storage, EF800_PERSISTENCE_DATA,
					 workspace->filename, &entry);
	if (ret != 0) {
		workspace->selected = false;
		return ret;
	}
	if (entry.type != EF800_PERSISTENCE_FILE) {
		workspace->selected = false;
		return -EISDIR;
	}
	return 0;
}

int ef800_workspace_create(struct ef800_workspace *workspace, const char *name)
{
	struct ef800_persistence_entry entry;
	char filename[sizeof(workspace->filename)];
	int ret;

	if (workspace == NULL || workspace->storage == NULL) {
		return -EINVAL;
	}
	ret = make_filename(name, filename, sizeof(filename));
	if (ret != 0) {
		return ret;
	}
	ret = ef800_persistence_stat(workspace->storage, EF800_PERSISTENCE_DATA,
					 filename, &entry);
	if (ret == 0) {
		return -EEXIST;
	}
	if (ret != -ENOENT) {
		return ret;
	}
	ret = ef800_persistence_write(workspace->storage, EF800_PERSISTENCE_DATA,
				       filename, NULL, 0U);
	if (ret != 0) {
		return ret;
	}
	ret = save_current(workspace, filename);
	if (ret != 0) {
		(void)ef800_persistence_remove(workspace->storage,
						EF800_PERSISTENCE_DATA, filename);
		return ret;
	}
	return select_filename(workspace, filename, strlen(filename));
}

int ef800_workspace_select(struct ef800_workspace *workspace, const char *name)
{
	struct ef800_persistence_entry entry;
	char filename[sizeof(workspace->filename)];
	int ret;

	if (workspace == NULL || workspace->storage == NULL) {
		return -EINVAL;
	}
	ret = make_filename(name, filename, sizeof(filename));
	if (ret != 0) {
		return ret;
	}
	ret = ef800_persistence_stat(workspace->storage, EF800_PERSISTENCE_DATA,
					 filename, &entry);
	if (ret != 0) {
		return ret;
	}
	if (entry.type != EF800_PERSISTENCE_FILE) {
		return -EISDIR;
	}
	ret = save_current(workspace, filename);
	if (ret != 0) {
		return ret;
	}
	return select_filename(workspace, filename, strlen(filename));
}

int ef800_workspace_append(struct ef800_workspace *workspace,
			   const void *data, size_t length)
{
	if (workspace == NULL || workspace->storage == NULL ||
	    !workspace->selected || (data == NULL && length != 0U)) {
		return workspace != NULL && workspace->storage != NULL &&
		       !workspace->selected ? -ENODEV : -EINVAL;
	}
	return ef800_persistence_append(workspace->storage, EF800_PERSISTENCE_DATA,
					workspace->filename, data, length);
}

struct workspace_list_context {
	ef800_workspace_list_callback callback;
	void *context;
};

static int list_workspace_entry(const struct ef800_persistence_entry *entry,
				void *context)
{
	struct workspace_list_context *list_context = context;
	char name[EF800_WORKSPACE_NAME_MAX + 1U];
	size_t extension_length = sizeof(EF800_WORKSPACE_EXTENSION) - 1U;
	size_t length;

	if (entry->type != EF800_PERSISTENCE_FILE ||
	    !has_suffix(entry->name, EF800_WORKSPACE_EXTENSION)) {
		return 0;
	}
	length = strlen(entry->name) - extension_length;
	if (length == 0U || length > EF800_WORKSPACE_NAME_MAX) {
		return 0;
	}
	memcpy(name, entry->name, length);
	name[length] = '\0';
	if (workspace_name_valid(name) != 0) {
		return 0;
	}
	return list_context->callback(name, list_context->context);
}

int ef800_workspace_list(struct ef800_workspace *workspace,
			const ef800_workspace_list_callback callback, void *context)
{
	struct workspace_list_context list_context;

	if (workspace == NULL || workspace->storage == NULL || callback == NULL) {
		return -EINVAL;
	}
	list_context.callback = callback;
	list_context.context = context;
	return ef800_persistence_list(workspace->storage, EF800_PERSISTENCE_DATA,
				      list_workspace_entry, &list_context);
}

bool ef800_workspace_is_selected(const struct ef800_workspace *workspace)
{
	return workspace != NULL && workspace->selected;
}

const char *ef800_workspace_get_name(const struct ef800_workspace *workspace)
{
	return workspace != NULL && workspace->selected ? workspace->name : NULL;
}

const char *ef800_workspace_get_filename(const struct ef800_workspace *workspace)
{
	return workspace != NULL && workspace->selected ? workspace->filename : NULL;
}
