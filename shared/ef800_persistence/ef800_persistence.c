/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_persistence.h"

#include <errno.h>
#include <string.h>

static bool is_name_character(char character)
{
	return (character >= 'a' && character <= 'z') ||
	       (character >= 'A' && character <= 'Z') ||
	       (character >= '0' && character <= '9') ||
	       character == '_' || character == '-' || character == '.';
}

bool ef800_persistence_valid_name(const char *name)
{
	const char *component_start;
	size_t length;

	if (name == NULL || name[0] == '\0' || name[0] == '/') {
		return false;
	}

	length = strlen(name);
	if (length > EF800_PERSISTENCE_MAX_NAME || name[length - 1U] == '/') {
		return false;
	}

	component_start = name;
	for (size_t index = 0U; index <= length; ++index) {
		char character = name[index];

		if (character == '/') {
			size_t component_length = &name[index] - component_start;
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

static int validate_operation(struct ef800_persistence *storage,
			      enum ef800_persistence_root root, const char *name)
{
	if (storage == NULL || storage->backend == NULL ||
	    root < EF800_PERSISTENCE_CONFIGURATION ||
	    root > EF800_PERSISTENCE_DATA || !ef800_persistence_valid_name(name)) {
		return -EINVAL;
	}
	return 0;
}

int ef800_persistence_init(struct ef800_persistence *storage,
			   const struct ef800_persistence_backend *backend)
{
	if (storage == NULL || backend == NULL || backend->read == NULL ||
	    backend->write == NULL || backend->append == NULL ||
	    backend->stat == NULL || backend->list == NULL ||
	    backend->remove == NULL) {
		return -EINVAL;
	}
	storage->backend = backend;
	return 0;
}

int ef800_persistence_read(struct ef800_persistence *storage,
			   enum ef800_persistence_root root, const char *name,
			   void *buffer, size_t capacity, size_t *length)
{
	int ret = validate_operation(storage, root, name);

	if (ret != 0 || length == NULL || (buffer == NULL && capacity != 0U)) {
		return ret != 0 ? ret : -EINVAL;
	}
	return storage->backend->read(storage->backend->context, root, name,
				      buffer, capacity, length);
}

int ef800_persistence_write(struct ef800_persistence *storage,
			    enum ef800_persistence_root root, const char *name,
			    const void *buffer, size_t length)
{
	int ret = validate_operation(storage, root, name);

	if (ret != 0 || (buffer == NULL && length != 0U)) {
		return ret != 0 ? ret : -EINVAL;
	}
	return storage->backend->write(storage->backend->context, root, name,
				       buffer, length);
}

int ef800_persistence_append(struct ef800_persistence *storage,
			     enum ef800_persistence_root root, const char *name,
			     const void *buffer, size_t length)
{
	int ret = validate_operation(storage, root, name);

	if (ret != 0 || (buffer == NULL && length != 0U)) {
		return ret != 0 ? ret : -EINVAL;
	}
	return storage->backend->append(storage->backend->context, root, name,
					buffer, length);
}

int ef800_persistence_stat(struct ef800_persistence *storage,
			   enum ef800_persistence_root root, const char *name,
			   struct ef800_persistence_entry *entry)
{
	int ret = validate_operation(storage, root, name);

	if (ret != 0 || entry == NULL) {
		return ret != 0 ? ret : -EINVAL;
	}
	return storage->backend->stat(storage->backend->context, root, name,
				      entry);
}

int ef800_persistence_list(struct ef800_persistence *storage,
			   enum ef800_persistence_root root,
			   ef800_persistence_list_callback callback, void *context)
{
	if (storage == NULL || storage->backend == NULL ||
	    root < EF800_PERSISTENCE_CONFIGURATION ||
	    root > EF800_PERSISTENCE_DATA || callback == NULL) {
		return -EINVAL;
	}
	return storage->backend->list(storage->backend->context, root, callback,
				       context);
}

int ef800_persistence_remove(struct ef800_persistence *storage,
			     enum ef800_persistence_root root, const char *name)
{
	int ret = validate_operation(storage, root, name);

	if (ret != 0) {
		return ret;
	}
	return storage->backend->remove(storage->backend->context, root, name);
}
