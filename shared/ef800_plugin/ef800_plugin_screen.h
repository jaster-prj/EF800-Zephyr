/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PLUGIN_SCREEN_H
#define EF800_PLUGIN_SCREEN_H

#include <stddef.h>

#include "ef800_plugin_contract.h"

void ef800_plugin_screen_init(struct ef800_plugin_screen *screen);
int ef800_plugin_screen_validate(const struct ef800_plugin_screen *screen);

#endif
