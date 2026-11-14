/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Validate keyboard application and updater images in flash.
 */

#ifndef EF800_KEYBOARD_BOOT_FLASH_H
#define EF800_KEYBOARD_BOOT_FLASH_H

#include <stdbool.h>

#include <zephyr/storage/flash_map.h>

/** @brief Check whether an application partition contains a bootable image.
 * @param area Flash area to validate.
 * @return True when its vector table and image bounds are valid.
 */
bool ef800_keyboard_boot_application_valid(const struct flash_area *area);

/** @brief Check whether the fixed updater partition contains a bootable image.
 * @return True when updater image metadata and vector table are valid.
 */
bool ef800_keyboard_boot_updater_valid(void);

#endif /* EF800_KEYBOARD_BOOT_FLASH_H */
