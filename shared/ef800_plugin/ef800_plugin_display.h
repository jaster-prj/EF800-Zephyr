/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Render validated plugin screens through a host line writer. */
#ifndef EF800_PLUGIN_DISPLAY_H
#define EF800_PLUGIN_DISPLAY_H

#include "ef800_plugin_contract.h"

struct ef800_plugin_display {
	void *context;
	int (*write_line)(void *context, uint8_t row, const char *text);
};

/** @brief Write a validated screen's title, rows, and footer to the display.
 * @param display Writer callback and opaque context.
 * @param screen Validated screen model.
 * @return 0 on render; negative errno on invalid input or writer failure.
 */
int ef800_plugin_display_render(const struct ef800_plugin_display *display,
	const struct ef800_plugin_screen *screen);

#endif
