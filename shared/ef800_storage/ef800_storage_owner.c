/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_storage_owner.h"

enum ef800_storage_action ef800_storage_owner_action(
	enum ef800_storage_owner owner, bool msc_requested)
{
	if (owner == EF800_STORAGE_OWNER_FAT && msc_requested) {
		return EF800_STORAGE_ACTION_FAT_TO_MSC;
	}
	if (owner == EF800_STORAGE_OWNER_MSC && !msc_requested) {
		return EF800_STORAGE_ACTION_MSC_TO_FAT;
	}
	return EF800_STORAGE_ACTION_NONE;
}
