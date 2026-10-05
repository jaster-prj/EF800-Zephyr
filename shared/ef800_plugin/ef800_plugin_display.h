/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PLUGIN_DISPLAY_H
#define EF800_PLUGIN_DISPLAY_H

#include "ef800_plugin_contract.h"

struct ef800_plugin_display {
	void *context;
	int (*write_line)(void *context, uint8_t row, const char *text);
};

int ef800_plugin_display_render(const struct ef800_plugin_display *display,
	const struct ef800_plugin_screen *screen);

#endif
