/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_plugin_display.h"

#include <errno.h>

#include "ef800_plugin_screen.h"

int ef800_plugin_display_render(const struct ef800_plugin_display *display,
	const struct ef800_plugin_screen *screen)
{
	int ret;

	if (display == NULL || display->write_line == NULL || screen == NULL) {
		return -EINVAL;
	}
	ret = ef800_plugin_screen_validate(screen);
	if (ret != 0) {
		return ret;
	}
	/* Row 0 and row 7 are deliberately never delegated to plugins. */
	for (uint8_t row = 0U; row < EF800_PLUGIN_SCREEN_ROWS; ++row) {
		ret = display->write_line(display->context, (uint8_t)(row + 1U),
			screen->rows[row]);
		if (ret != 0) {
			return ret;
		}
	}
	return 0;
}
