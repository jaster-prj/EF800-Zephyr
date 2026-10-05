/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_plugin_screen.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

static bool terminated(const char *value, size_t capacity)
{
	return memchr(value, '\0', capacity) != NULL;
}

void ef800_plugin_screen_init(struct ef800_plugin_screen *screen)
{
	if (screen == NULL) {
		return;
	}
	memset(screen, 0, sizeof(*screen));
	screen->struct_size = sizeof(*screen);
}

int ef800_plugin_screen_validate(const struct ef800_plugin_screen *screen)
{
	if (screen == NULL || screen->struct_size < sizeof(*screen) ||
		screen->row_count > EF800_PLUGIN_SCREEN_ROWS ||
		screen->selected_row >= EF800_PLUGIN_SCREEN_ROWS ||
		!terminated(screen->title, sizeof(screen->title)) ||
		!terminated(screen->footer, sizeof(screen->footer))) {
		return -EINVAL;
	}
	for (size_t row = 0U; row < screen->row_count; ++row) {
		if (!terminated(screen->rows[row], sizeof(screen->rows[row]))) {
			return -EOVERFLOW;
		}
	}
	return 0;
}
