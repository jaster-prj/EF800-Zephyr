/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EF800_KEYBOARD_BOOT_FLASH_H
#define EF800_KEYBOARD_BOOT_FLASH_H

#include <stdbool.h>

#include <zephyr/storage/flash_map.h>

bool ef800_keyboard_boot_application_valid(const struct flash_area *area);
bool ef800_keyboard_boot_updater_valid(void);

#endif /* EF800_KEYBOARD_BOOT_FLASH_H */
