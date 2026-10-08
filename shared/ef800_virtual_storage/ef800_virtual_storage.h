/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_VIRTUAL_STORAGE_H
#define EF800_VIRTUAL_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

enum ef800_virtual_storage_mode {
	EF800_VIRTUAL_STORAGE_DISPOSABLE = 0,
	EF800_VIRTUAL_STORAGE_PERSISTENT,
};

struct ef800_virtual_storage {
	enum ef800_virtual_storage_mode mode;
	char root[256];
	bool mounted;
};

int ef800_virtual_storage_init(struct ef800_virtual_storage *storage,
				       enum ef800_virtual_storage_mode mode,
				       const char *root);
bool ef800_virtual_storage_is_safe(const struct ef800_virtual_storage *storage);

#endif
