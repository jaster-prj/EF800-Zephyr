/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EF800_KEYBOARD_UPDATER_FLASH_H
#define EF800_KEYBOARD_UPDATER_FLASH_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/storage/flash_map.h>

#include "ef800_keyboard_update_protocol.h"

int ef800_keyboard_updater_flash_open(enum ef800_kb_update_target target,
				      const struct flash_area **area);
int ef800_keyboard_updater_flash_erase_page(const struct flash_area *area,
					    uint32_t relative_page);
int ef800_keyboard_updater_flash_read(const struct flash_area *area,
				     uint32_t offset, void *data, size_t length);
int ef800_keyboard_updater_flash_write(const struct flash_area *area,
				      uint32_t offset, const void *data,
				      size_t length);
void ef800_keyboard_updater_flash_close(const struct flash_area *area);

#endif /* EF800_KEYBOARD_UPDATER_FLASH_H */
