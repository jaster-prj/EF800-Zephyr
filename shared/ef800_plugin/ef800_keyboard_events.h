/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Convert legacy keyboard values into plugin key-event objects. */
#ifndef EF800_KEYBOARD_EVENTS_H
#define EF800_KEYBOARD_EVENTS_H
#include <stdint.h>
#include "ef800_plugin_contract.h"

/** @brief Map a legacy keyboard byte to a plugin key event.
 * @param key Legacy key value.
 * @param event Receives the normalized event.
 * @return 0 on mapping; negative errno for unsupported key values.
 */
int ef800_keyboard_key_event(uint8_t key, struct ef800_plugin_key_event *event);

/** @brief Map a plugin key-code value to a normalized key event.
 * @param code Plugin key-code identifier.
 * @param event Receives the normalized event.
 * @return 0 on mapping; negative errno for unsupported codes.
 */
int ef800_keyboard_event_key(uint32_t code, struct ef800_plugin_key_event *event);
#endif
