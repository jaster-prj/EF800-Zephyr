/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Decide safe ownership transitions for shared FAT/MSC storage. */
#ifndef EF800_STORAGE_OWNER_H_
#define EF800_STORAGE_OWNER_H_

#include <stdbool.h>

enum ef800_storage_owner {
	EF800_STORAGE_OWNER_FAT,
	EF800_STORAGE_OWNER_MSC,
};

enum ef800_storage_action {
	EF800_STORAGE_ACTION_NONE,
	EF800_STORAGE_ACTION_FAT_TO_MSC,
	EF800_STORAGE_ACTION_MSC_TO_FAT,
};

/** @brief Select the required transition for the current owner and MSC request.
 * @param owner Current exclusive storage owner.
 * @param msc_requested True when USB mass storage requests the volume.
 * @return None, FAT-to-MSC, or MSC-to-FAT transition.
 */
enum ef800_storage_action ef800_storage_owner_action(
	enum ef800_storage_owner owner, bool msc_requested);

#endif /* EF800_STORAGE_OWNER_H */
