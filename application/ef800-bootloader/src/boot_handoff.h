/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EF800_BOOT_HANDOFF_H
#define EF800_BOOT_HANDOFF_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

FUNC_NORETURN void ef800_boot_handoff(const struct flash_area *area,
				      uintptr_t address,
				      uintptr_t sram_address,
				      size_t sram_size);

#endif /* EF800_BOOT_HANDOFF_H */
