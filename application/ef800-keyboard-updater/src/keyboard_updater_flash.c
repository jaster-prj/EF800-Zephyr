/* SPDX-License-Identifier: Apache-2.0 */

#include "keyboard_updater_flash.h"

#include <errno.h>

#include <zephyr/sys/util.h>

#include "ef800_update.h"
#include "ef800_watchdog.h"

#define BOOT_PARTITION_ID PARTITION_ID(keyboard_boot_partition)
#define APP_PARTITION_ID PARTITION_ID(keyboard_app_partition)

BUILD_ASSERT(PARTITION_OFFSET(keyboard_boot_partition) == 0x00000000U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_boot_partition) == 0x00008000U);
BUILD_ASSERT(PARTITION_OFFSET(keyboard_app_partition) == 0x00008000U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_app_partition) == 0x00030000U);
BUILD_ASSERT(PARTITION_OFFSET(keyboard_updater_partition) == 0x00038000U);
BUILD_ASSERT(PARTITION_SIZE(keyboard_updater_partition) == 0x00008000U);

int ef800_keyboard_updater_flash_open(enum ef800_kb_update_target target,
				      const struct flash_area **area)
{
	uint8_t partition_id;
	uint32_t base;
	uint32_t size;
	int result;

	if (area == NULL || !ef800_kb_update_service_target_valid(
		    EF800_KB_UPDATE_SERVICE_UPDATER, target) ||
	    !ef800_kb_update_target_range(target, &base, &size)) {
		return -EACCES;
	}
	ARG_UNUSED(base);

	switch (target) {
	case EF800_KB_UPDATE_TARGET_BOOTLOADER:
		partition_id = BOOT_PARTITION_ID;
		break;
	case EF800_KB_UPDATE_TARGET_APPLICATION:
		partition_id = APP_PARTITION_ID;
		break;
	default:
		return -EACCES;
	}
	result = flash_area_open(partition_id, area);
	if (result != 0) {
		return result;
	}
	if (!flash_area_device_is_ready(*area) || (*area)->fa_size != size) {
		(void)flash_area_close(*area);
		*area = NULL;
		return -ENODEV;
	}
	return 0;
}

int ef800_keyboard_updater_flash_erase_page(const struct flash_area *area,
					    uint32_t relative_page)
{
	int result;

	if (area == NULL || area->fa_size % EF800_KB_UPDATE_PAGE_SIZE != 0U ||
	    relative_page >= area->fa_size / EF800_KB_UPDATE_PAGE_SIZE) {
		return -EINVAL;
	}
	result = flash_area_erase(area,
		relative_page * EF800_KB_UPDATE_PAGE_SIZE,
		EF800_KB_UPDATE_PAGE_SIZE);
	(void)ef800_watchdog_feed();
	return result;
}

int ef800_keyboard_updater_flash_read(const struct flash_area *area,
				     uint32_t offset, void *data, size_t length)
{
	if (area == NULL || (data == NULL && length != 0U) ||
	    offset > area->fa_size || length > area->fa_size - offset) {
		return -EINVAL;
	}
	return flash_area_read(area, offset, data, length);
}

int ef800_keyboard_updater_flash_write(const struct flash_area *area,
				      uint32_t offset, const void *data,
				      size_t length)
{
	if (area == NULL || (data == NULL && length != 0U) ||
	    offset > area->fa_size || length > area->fa_size - offset) {
		return -EINVAL;
	}
	return flash_area_write(area, offset, data, length);
}

void ef800_keyboard_updater_flash_close(const struct flash_area *area)
{
	if (area != NULL) {
		flash_area_close(area);
	}
}
