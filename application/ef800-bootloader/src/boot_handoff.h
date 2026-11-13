/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Validate and transfer control from the bootloader to a firmware image.
 */

#ifndef EF800_BOOT_HANDOFF_H
#define EF800_BOOT_HANDOFF_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

/** @brief Configure the image's memory state and branch to its reset vector.
 * @param area Flash area containing the image.
 * @param address Image base address.
 * @param sram_address SRAM bounds used to validate the initial stack pointer.
 * @param sram_size Size of the valid SRAM region.
 * @return Does not return; invalid images are rejected or trigger a fatal path.
 */
FUNC_NORETURN void ef800_boot_handoff(const struct flash_area *area,
				      uintptr_t address,
				      uintptr_t sram_address,
				      size_t sram_size);

#endif /* EF800_BOOT_HANDOFF_H */
