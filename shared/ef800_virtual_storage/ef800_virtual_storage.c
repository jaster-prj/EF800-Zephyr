/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_virtual_storage.h"

#include <errno.h>
#include <string.h>

int ef800_virtual_storage_init(struct ef800_virtual_storage *storage,
				       enum ef800_virtual_storage_mode mode,
				       const char *root)
{
	if (storage == NULL || root == NULL || root[0] == '\0' ||
		strstr(root, "EF800:") != NULL || mode > EF800_VIRTUAL_STORAGE_PERSISTENT) {
		return -EINVAL;
	}
	memset(storage, 0, sizeof(*storage));
	storage->mode = mode;
	strncpy(storage->root, root, sizeof(storage->root) - 1U);
	storage->mounted = true;
	return 0;
}

bool ef800_virtual_storage_is_safe(const struct ef800_virtual_storage *storage)
{
	return storage != NULL && storage->mounted && storage->root[0] != '\0' &&
		strstr(storage->root, "EF800:") == NULL;
}
