/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_keyboard_events.h"
#include <errno.h>
#include "keyboard_protocol.h"
int ef800_keyboard_key_event(uint8_t key, struct ef800_plugin_key_event *event)
{
	if (event == NULL) return -EINVAL;
	*event = (struct ef800_plugin_key_event){.struct_size = sizeof(*event), .code = EF800_PLUGIN_KEY_CHARACTER, .character = key};
	return 0;
}
int ef800_keyboard_event_key(uint32_t code, struct ef800_plugin_key_event *event)
{
	if (event == NULL) return -EINVAL;
	*event = (struct ef800_plugin_key_event){.struct_size = sizeof(*event)};
	if (code & KB_EVENT_ARROWUP) event->code = EF800_PLUGIN_KEY_UP;
	else if (code & KB_EVENT_ARROWDOWN) event->code = EF800_PLUGIN_KEY_DOWN;
	else if (code & KB_EVENT_ARROWLEFT) event->code = EF800_PLUGIN_KEY_LEFT;
	else if (code & KB_EVENT_ARROWRIGHT) event->code = EF800_PLUGIN_KEY_RIGHT;
	else return -ENOTSUP;
	return 0;
}
