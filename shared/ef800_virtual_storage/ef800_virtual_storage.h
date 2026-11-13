/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Configure isolated disposable or persistent virtual storage. */
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

/** @brief Initialize a virtual storage root and its requested persistence mode.
 * @param storage State to initialize.
 * @param mode Disposable or persistent storage policy.
 * @param root Host-visible or virtual filesystem root path.
 * @return 0 on initialization; negative errno for invalid path or setup failure.
 */
int ef800_virtual_storage_init(struct ef800_virtual_storage *storage,
				       enum ef800_virtual_storage_mode mode,
				       const char *root);

/** @brief Check whether a virtual storage configuration is safe to mount/use.
 * @param storage Storage state to inspect.
 * @return True when the root and mode satisfy safety constraints.
 */
bool ef800_virtual_storage_is_safe(const struct ef800_virtual_storage *storage);

#endif
