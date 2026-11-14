/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Configure isolated disposable or persistent virtual storage. */
#ifndef VIRTUAL_EF800_STORAGE_H
#define VIRTUAL_EF800_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

enum virtual_ef800_storage_mode {
	VIRTUAL_EF800_STORAGE_DISPOSABLE = 0,
	VIRTUAL_EF800_STORAGE_PERSISTENT,
};

struct virtual_ef800_storage {
	enum virtual_ef800_storage_mode mode;
	char root[256];
	bool mounted;
};

/** @brief Initialize a virtual storage root and its requested persistence mode.
 * @param storage State to initialize.
 * @param mode Disposable or persistent storage policy.
 * @param root Host-visible or virtual filesystem root path.
 * @return 0 on initialization; negative errno for invalid path or setup failure.
 */
int virtual_ef800_storage_init(struct virtual_ef800_storage *storage,
				       enum virtual_ef800_storage_mode mode,
				       const char *root);

/** @brief Check whether a virtual storage configuration is safe to mount/use.
 * @param storage Storage state to inspect.
 * @return True when the root and mode satisfy safety constraints.
 */
bool virtual_ef800_storage_is_safe(const struct virtual_ef800_storage *storage);

#endif
