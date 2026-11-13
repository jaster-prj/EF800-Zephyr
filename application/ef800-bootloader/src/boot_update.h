/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Install a validated main-firmware update using the SRAM staging area.
 */
#ifndef EF800_BOOT_UPDATE_H_
#define EF800_BOOT_UPDATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct flash_area;

/** @brief Copy and install the staged update into the main image partition.
 * @param main_area Flash partition containing the main image.
 * @param main_address Main image load address.
 * @param sram_address SRAM staging buffer address.
 * @param sram_size Available staging-buffer capacity.
 * @return 0 on successful installation; negative errno on validation or flash failure.
 */
int ef800_boot_install_update(const struct flash_area *main_area,
				      uintptr_t main_address,
				      uintptr_t sram_address, size_t sram_size);

#endif /* EF800_BOOT_UPDATE_H_ */
