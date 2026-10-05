/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_KEYBOARD_EVENTS_H
#define EF800_KEYBOARD_EVENTS_H
#include <stdint.h>
#include "ef800_plugin_contract.h"
int ef800_keyboard_key_event(uint8_t key, struct ef800_plugin_key_event *event);
int ef800_keyboard_event_key(uint32_t code, struct ef800_plugin_key_event *event);
#endif
