/* SPDX-License-Identifier: Apache-2.0 */
#include "virtual_ef800_storage.h"

#include <errno.h>
#include <string.h>

int virtual_ef800_storage_init(struct virtual_ef800_storage *storage,
				       enum virtual_ef800_storage_mode mode,
				       const char *root)
{
	if (storage == NULL || root == NULL || root[0] == '\0' ||
		strstr(root, "EF800:") != NULL || mode > VIRTUAL_EF800_STORAGE_PERSISTENT) {
		return -EINVAL;
	}
	memset(storage, 0, sizeof(*storage));
	storage->mode = mode;
	strncpy(storage->root, root, sizeof(storage->root) - 1U);
	storage->mounted = true;
	return 0;
}

bool virtual_ef800_storage_is_safe(const struct virtual_ef800_storage *storage)
{
	return storage != NULL && storage->mounted && storage->root[0] != '\0' &&
		strstr(storage->root, "EF800:") == NULL;
}
