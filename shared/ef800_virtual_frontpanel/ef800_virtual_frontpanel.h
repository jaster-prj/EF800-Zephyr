/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_VIRTUAL_FRONTPANEL_H
#define EF800_VIRTUAL_FRONTPANEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "ef800_virtual_display.h"
#include "ef800_virtual_keyboard.h"

struct ef800_virtual_frontpanel {
	struct ef800_virtual_display *display;
	struct ef800_virtual_keyboard *keyboard;
	uint16_t port;
	bool running;
};

typedef int (*ef800_virtual_plugin_assign_fn)(void *context, uint8_t port_id,
	uint32_t plugin_id, uint32_t connection);
typedef int (*ef800_virtual_plugin_execute_fn)(void *context, uint8_t port_id);

int ef800_virtual_frontpanel_start(struct ef800_virtual_frontpanel *panel,
					   struct ef800_virtual_display *display,
					   struct ef800_virtual_keyboard *keyboard,
					   uint16_t port);
void ef800_virtual_frontpanel_stop(struct ef800_virtual_frontpanel *panel);
void ef800_virtual_frontpanel_set_plugin_assign_callback(
	ef800_virtual_plugin_assign_fn callback, void *context);
void ef800_virtual_frontpanel_set_plugin_execute_callback(
	ef800_virtual_plugin_execute_fn callback, void *context);

void ef800_virtual_frontpanel_log_record(uint8_t severity,
						 const uint8_t *text, size_t length);

#endif
