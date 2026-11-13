/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Model virtual keyboard state and inject host key events. */
#ifndef EF800_VIRTUAL_KEYBOARD_H
#define EF800_VIRTUAL_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "keyboard_protocol.h"

#define EF800_VIRTUAL_KEYBOARD_MAX_HELD 64U
#define EF800_VIRTUAL_KEYBOARD_OUTPUT_SIZE 256U
#define EF800_VIRTUAL_KEYBOARD_NAME_SIZE 8U

struct ef800_virtual_keyboard {
	uint16_t held[EF800_VIRTUAL_KEYBOARD_MAX_HELD];
	size_t held_count;
	uint8_t output[EF800_VIRTUAL_KEYBOARD_OUTPUT_SIZE];
	size_t output_length;
	char pending_name[EF800_VIRTUAL_KEYBOARD_NAME_SIZE];
	bool pending_press;
	bool pending_valid;
};

/** @brief Initialize key tracking and clear pending output.
 * @param keyboard Keyboard state to initialize.
 * @return None.
 */
void ef800_virtual_keyboard_init(struct ef800_virtual_keyboard *keyboard);

/** @brief Press a named key and enqueue its protocol make sequence.
 * @param keyboard Keyboard state.
 * @param name Key name bytes.
 * @param length Name length.
 * @return 0 on press; negative errno for unknown key or full state/output.
 */
int ef800_virtual_keyboard_press(struct ef800_virtual_keyboard *keyboard,
					 const char *name, size_t length);

/** @brief Release a held named key and enqueue its break sequence.
 * @param keyboard Keyboard state.
 * @param name Key name bytes.
 * @param length Name length.
 * @return 0 on release; negative errno for unknown/unheld key or full output.
 */
int ef800_virtual_keyboard_release(struct ef800_virtual_keyboard *keyboard,
					   const char *name, size_t length);

/** @brief Release every held key and enqueue corresponding break events.
 * @param keyboard Keyboard state.
 * @return None.
 */
void ef800_virtual_keyboard_release_all(
	struct ef800_virtual_keyboard *keyboard);

/** @brief Drain pending keyboard protocol bytes into a destination buffer.
 * @param keyboard Keyboard state.
 * @param output Destination byte buffer.
 * @param capacity Buffer capacity.
 * @return Number of bytes copied.
 */
size_t ef800_virtual_keyboard_read(struct ef800_virtual_keyboard *keyboard,
							uint8_t *output, size_t capacity);

/** @brief Consume a pending host-visible key press/release event.
 * @param keyboard Keyboard state.
 * @param name Destination key-name buffer.
 * @param capacity Name-buffer capacity.
 * @param pressed Receives true for press and false for release.
 * @return True when an event was consumed.
 */
bool ef800_virtual_keyboard_take_event(struct ef800_virtual_keyboard *keyboard,
							char *name, size_t capacity, bool *pressed);

#endif
