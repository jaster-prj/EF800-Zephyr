/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_measurement_submenu.h"

#include "ef800_plugin_contract.h"

static const uint32_t ordered_actions[] = {
	EF800_MEASUREMENT_ACTION_MEASURE,
	EF800_MEASUREMENT_ACTION_UP,
	EF800_MEASUREMENT_ACTION_DOWN,
	EF800_MEASUREMENT_ACTION_ENTER,
};

void ef800_measurement_submenu_init(struct ef800_measurement_submenu *menu,
	uint32_t actions)
{
	if (menu != NULL) {
		*menu = (struct ef800_measurement_submenu){
			.actions = actions,
			.active = actions != 0U,
		};
	}
}

size_t ef800_measurement_submenu_count(
	const struct ef800_measurement_submenu *menu)
{
	size_t count = 0U;

	if (menu == NULL) return 0U;
	for (size_t index = 0U; index < sizeof(ordered_actions) /
		sizeof(ordered_actions[0]); ++index) {
		if ((menu->actions & ordered_actions[index]) != 0U) ++count;
	}
	return count;
}

uint32_t ef800_measurement_submenu_action(
	const struct ef800_measurement_submenu *menu)
{
	uint8_t selection = 0U;

	if (menu == NULL) return 0U;
	for (size_t index = 0U; index < sizeof(ordered_actions) /
		sizeof(ordered_actions[0]); ++index) {
		if ((menu->actions & ordered_actions[index]) == 0U) continue;
		if (selection++ == menu->selection) return ordered_actions[index];
	}
	return 0U;
}

void ef800_measurement_submenu_move(
	struct ef800_measurement_submenu *menu, int direction)
{
	size_t count;

	if (menu == NULL || direction == 0) return;
	count = ef800_measurement_submenu_count(menu);
	if (count == 0U) return;
	if (direction < 0) menu->selection = menu->selection == 0U ?
		(uint8_t)(count - 1U) : menu->selection - 1U;
	else menu->selection = menu->selection + 1U >= count ?
		0U : menu->selection + 1U;
}

bool ef800_measurement_submenu_key(uint8_t key, uint32_t *action)
{
	if (action == NULL) return false;
	if (key == 'u' || key == 'U') *action = EF800_MEASUREMENT_ACTION_UP;
	else if (key == 'd' || key == 'D') *action = EF800_MEASUREMENT_ACTION_DOWN;
	else if (key == 'e' || key == 'E') *action = EF800_MEASUREMENT_ACTION_ENTER;
	else return false;
	return true;
}
