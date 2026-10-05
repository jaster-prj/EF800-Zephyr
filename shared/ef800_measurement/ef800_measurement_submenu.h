/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_MEASUREMENT_SUBMENU_H
#define EF800_MEASUREMENT_SUBMENU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ef800_measurement_submenu {
	uint32_t actions;
	uint8_t selection;
	bool active;
};

void ef800_measurement_submenu_init(struct ef800_measurement_submenu *menu,
	uint32_t actions);
size_t ef800_measurement_submenu_count(
	const struct ef800_measurement_submenu *menu);
uint32_t ef800_measurement_submenu_action(
	const struct ef800_measurement_submenu *menu);
void ef800_measurement_submenu_move(
	struct ef800_measurement_submenu *menu, int direction);
bool ef800_measurement_submenu_key(uint8_t key, uint32_t *action);

#endif
