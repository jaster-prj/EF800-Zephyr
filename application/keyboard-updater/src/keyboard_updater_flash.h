/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Provide bounded erase, read, and write access to updater flash targets.
 */

#ifndef EF800_KEYBOARD_UPDATER_FLASH_H
#define EF800_KEYBOARD_UPDATER_FLASH_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/storage/flash_map.h>

#include "ef800_keyboard_update_protocol.h"

/** @brief Open the flash partition authorized for an update target.
 * @param target Protocol target whose partition is requested.
 * @param area Receives the opened partition handle.
 * @return 0 on success; negative errno when target/open fails.
 */
int ef800_keyboard_updater_flash_open(enum ef800_kb_update_target target,
				      const struct flash_area **area);

/** @brief Erase one page at a relative offset within the open partition.
 * @param area Open flash area.
 * @param relative_page Page index relative to the area base.
 * @return 0 on erase; negative errno for invalid bounds or flash failure.
 */
int ef800_keyboard_updater_flash_erase_page(const struct flash_area *area,
					    uint32_t relative_page);

/** @brief Read bytes from a relative partition offset.
 * @param area Open flash area.
 * @param offset Byte offset from the area base.
 * @param data Destination buffer.
 * @param length Number of bytes to read.
 * @return 0 on read; negative errno for invalid bounds or flash failure.
 */
int ef800_keyboard_updater_flash_read(const struct flash_area *area,
				     uint32_t offset, void *data, size_t length);

/** @brief Program bytes at a relative partition offset.
 * @param area Open flash area.
 * @param offset Byte offset from the area base.
 * @param data Source bytes.
 * @param length Number of bytes to write.
 * @return 0 on write; negative errno for invalid bounds or flash failure.
 */
int ef800_keyboard_updater_flash_write(const struct flash_area *area,
				      uint32_t offset, const void *data,
				      size_t length);

/** @brief Release an opened flash area.
 * @param area Area handle to close.
 * @return None.
 */
void ef800_keyboard_updater_flash_close(const struct flash_area *area);

#endif /* EF800_KEYBOARD_UPDATER_FLASH_H */
