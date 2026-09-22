/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_BOOT_UPDATE_H_
#define EF800_BOOT_UPDATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct flash_area;

int ef800_boot_install_update(const struct flash_area *main_area,
				      uintptr_t main_address,
				      uintptr_t sram_address, size_t sram_size);

#endif /* EF800_BOOT_UPDATE_H_ */
