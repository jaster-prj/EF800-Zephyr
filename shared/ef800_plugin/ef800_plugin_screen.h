/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Initialize and validate bounded plugin display models. */
#ifndef EF800_PLUGIN_SCREEN_H
#define EF800_PLUGIN_SCREEN_H

#include <stddef.h>

#include "ef800_plugin_contract.h"

/** @brief Clear and initialize a screen model with its ABI size.
 * @param screen Screen state to initialize.
 * @return None.
 */
void ef800_plugin_screen_init(struct ef800_plugin_screen *screen);

/** @brief Validate title, row count, selection, and text termination bounds.
 * @param screen Screen model to validate.
 * @return 0 when valid; negative errno when fields exceed ABI limits.
 */
int ef800_plugin_screen_validate(const struct ef800_plugin_screen *screen);

#endif
