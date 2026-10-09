/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_workspace.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
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

static void workspace_reset_context(struct ef800_workspace *workspace)
{
	workspace->current_point_number = 0;
	workspace->current_code[0] = '\0';
}

static void workspace_update_context_from_line(struct ef800_workspace *workspace,
	const char *line)
{
	char copy[EF800_MEASUREMENT_TEXT_MAX + 96U];
	char *token;
	char *last = NULL;
	long point = 0;
	unsigned int field = 0U;
	char *end;

	if (workspace == NULL || line == NULL || ef800_workspace_line_is_position(line) ||
		strlen(line) >= sizeof(copy)) {
		return;
	}
	strcpy(copy, line);
	point = strtol(copy, &end, 10);
	if (end == copy) {
		return;
	}
	token = strtok(copy, " \t\r\n");
	while (token != NULL) {
		last = token;
		++field;
		token = strtok(NULL, " \t\r\n");
	}
	if (field < 6U || last == NULL || point <= 0 || point > INT32_MAX) {
		return;
	}
	workspace->current_point_number = (int32_t)point;
	strncpy(workspace->current_code, last, sizeof(workspace->current_code) - 1U);
	workspace->current_code[sizeof(workspace->current_code) - 1U] = '\0';
}

static void workspace_extract_context(struct ef800_workspace *workspace)
{
	uint8_t buffer[EF800_WORKSPACE_SNAPSHOT_MAX];
	size_t file_size;
	size_t start;
	size_t length;
	size_t line_length = 0U;
	char line[EF800_MEASUREMENT_TEXT_MAX + 96U];

	workspace_reset_context(workspace);
	if (ef800_workspace_size(workspace, &file_size) != 0 || file_size == 0U) {
		return;
	}
	start = file_size > sizeof(buffer) ? file_size - sizeof(buffer) : 0U;
	if (ef800_workspace_read_range(workspace, start, file_size, buffer,
			sizeof(buffer), &length) != 0) {
		return;
	}
	for (size_t index = 0U; index < length; ++index) {
		char character = (char)buffer[index];

		if (start != 0U && index == 0U) {
			while (index < length && buffer[index] != '\n') {
				++index;
			}
			continue;
		}
		if (character == '\n' || character == '\r') {
			line[line_length] = '\0';
			workspace_update_context_from_line(workspace, line);
			line_length = 0U;
		} else if (line_length + 1U < sizeof(line)) {
			line[line_length++] = character;
		}
	}
	if (line_length != 0U) {
		line[line_length] = '\0';
		workspace_update_context_from_line(workspace, line);
	}
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
	workspace_extract_context(workspace);
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
	ret = select_filename(workspace, filename, strlen(filename));
	if (ret == 0) {
		workspace_reset_context(workspace);
	}
	return ret;
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
	ret = select_filename(workspace, filename, strlen(filename));
	if (ret == 0) {
		workspace_extract_context(workspace);
	}
	return ret;
}

int ef800_workspace_delete_selected(struct ef800_workspace *workspace)
{
	struct ef800_persistence *storage;
	char filename[sizeof(workspace->filename)];
	int result;

	if (workspace == NULL || workspace->storage == NULL || !workspace->selected) {
		return -ENODEV;
	}
	storage = workspace->storage;
	strcpy(filename, workspace->filename);
	result = ef800_persistence_remove(storage, EF800_PERSISTENCE_DATA, filename);
	if (result != 0) {
		return result;
	}
	(void)ef800_persistence_remove(storage, EF800_PERSISTENCE_CONFIGURATION,
		EF800_WORKSPACE_CONFIGURATION_FILE);
	memset(workspace, 0, sizeof(*workspace));
	workspace->storage = storage;
	return 0;
}

int ef800_workspace_append(struct ef800_workspace *workspace,
			   const void *data, size_t length)
{
	int ret;

	if (workspace == NULL || workspace->storage == NULL ||
	    !workspace->selected || (data == NULL && length != 0U)) {
		return workspace != NULL && workspace->storage != NULL &&
		       !workspace->selected ? -ENODEV : -EINVAL;
	}
	ret = ef800_persistence_append(workspace->storage, EF800_PERSISTENCE_DATA,
			workspace->filename, data, length);
	if (ret == 0 && data != NULL && length > 0U) {
		char line[EF800_MEASUREMENT_TEXT_MAX + 96U];
		size_t copy_length = MIN(length, sizeof(line) - 1U);

		memcpy(line, data, copy_length);
		line[copy_length] = '\0';
		workspace_update_context_from_line(workspace, line);
	}
	return ret;
}

int ef800_workspace_snapshot(const struct ef800_workspace *workspace,
	void *destination, size_t capacity, size_t *length)
{
	struct ef800_persistence_entry entry;
	int ret;

	if (workspace == NULL || workspace->storage == NULL || destination == NULL ||
		length == NULL || !workspace->selected) {
		return workspace != NULL && workspace->storage != NULL &&
			!workspace->selected ? -ENODEV : -EINVAL;
	}
	*length = 0U;
	ret = ef800_persistence_stat(workspace->storage, EF800_PERSISTENCE_DATA,
		workspace->filename, &entry);
	if (ret != 0) {
		return ret;
	}
	if (entry.type != EF800_PERSISTENCE_FILE) {
		return -EISDIR;
	}
	if (entry.size > capacity) {
		return -EFBIG;
	}
	ret = ef800_persistence_read(workspace->storage, EF800_PERSISTENCE_DATA,
		workspace->filename, destination, capacity, length);
	if (ret != 0) {
		return ret;
	}
	return *length == entry.size ? 0 : -EIO;
}

int ef800_workspace_read_range(const struct ef800_workspace *workspace,
				      size_t start, size_t end, void *destination,
				      size_t capacity, size_t *length)
{
	if (workspace == NULL || workspace->storage == NULL ||
	    !workspace->selected || destination == NULL || length == NULL ||
	    end < start || end - start > capacity) {
		return workspace != NULL && workspace->storage != NULL &&
		       !workspace->selected ? -ENODEV : -EINVAL;
	}
	return ef800_persistence_read_range(workspace->storage, EF800_PERSISTENCE_DATA,
					    workspace->filename, start, destination,
					    end - start, length);
}

int ef800_workspace_size(const struct ef800_workspace *workspace, size_t *size)
{
	struct ef800_persistence_entry entry;
	int ret;

	if (workspace == NULL || workspace->storage == NULL || size == NULL ||
	    !workspace->selected) {
		return workspace != NULL && workspace->storage != NULL &&
		       !workspace->selected ? -ENODEV : -EINVAL;
	}
	ret = ef800_persistence_stat(workspace->storage, EF800_PERSISTENCE_DATA,
					     workspace->filename, &entry);
	if (ret != 0) {
		return ret;
	}
	if (entry.type != EF800_PERSISTENCE_FILE) {
		return -EISDIR;
	}
	*size = entry.size;
	return 0;
}

bool ef800_workspace_line_is_position(const char *line)
{
	return line != NULL && line[0] == '-';
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

int32_t ef800_workspace_current_point_number(
	const struct ef800_workspace *workspace)
{
	return workspace != NULL && workspace->selected ?
		workspace->current_point_number : 0;
}

const char *ef800_workspace_current_code(
	const struct ef800_workspace *workspace)
{
	return workspace != NULL && workspace->selected ? workspace->current_code : NULL;
}
