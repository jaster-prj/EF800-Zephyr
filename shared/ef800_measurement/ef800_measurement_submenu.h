/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Navigate the bounded action menu for an instrument session. */
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

/** @brief Initialize available actions and reset selection.
 * @param menu Submenu state.
 * @param actions Supported action bitmask.
 * @return None.
 */
void ef800_measurement_submenu_init(struct ef800_measurement_submenu *menu,
	uint32_t actions);

/** @brief Count actions available in the submenu.
 * @param menu Submenu state.
 * @return Number of selectable actions.
 */
size_t ef800_measurement_submenu_count(
	const struct ef800_measurement_submenu *menu);

/** @brief Get the selected action identifier.
 * @param menu Submenu state.
 * @return Selected action bit, or zero when inactive/empty.
 */
uint32_t ef800_measurement_submenu_action(
	const struct ef800_measurement_submenu *menu);

/** @brief Move selection by one available action.
 * @param menu Submenu state.
 * @param direction Negative moves backward, positive forward.
 * @return None.
 */
void ef800_measurement_submenu_move(
	struct ef800_measurement_submenu *menu, int direction);

/** @brief Translate a keyboard shortcut into a measurement action.
 * @param key Input key code.
 * @param action Receives matching action when recognized.
 * @return True when key maps to an action.
 */
bool ef800_measurement_submenu_key(uint8_t key, uint32_t *action);

#endif
