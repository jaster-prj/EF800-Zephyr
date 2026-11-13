/* SPDX-License-Identifier: Apache-2.0 */

#include "keyboard_boot_flash.h"

#include <stdint.h>

#include <zephyr/devicetree.h>

#include "ef800_update.h"

static const uintptr_t sram_address = DT_REG_ADDR(DT_NODELABEL(sram0));
static const size_t sram_size = DT_REG_SIZE(DT_NODELABEL(sram0));

bool ef800_keyboard_boot_application_valid(const struct flash_area *area)
{
	uint32_t vector[2];

	return area != NULL &&
		flash_area_read(area, 0U, vector, sizeof(vector)) == 0 &&
		ef800_vector_table_valid(vector,
			PARTITION_ADDRESS(keyboard_app_partition), area->fa_size,
			sram_address, sram_size);
}

bool ef800_keyboard_boot_updater_valid(void)
{
	const uint32_t *vector =
		(const uint32_t *)PARTITION_ADDRESS(keyboard_updater_partition);

	return ef800_vector_table_valid(vector,
		PARTITION_ADDRESS(keyboard_updater_partition),
		PARTITION_SIZE(keyboard_updater_partition), sram_address, sram_size);
}
