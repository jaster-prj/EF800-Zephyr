/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Model virtual keyboard state and inject host key events. */
#ifndef VIRTUAL_EF800_KEYBOARD_H
#define VIRTUAL_EF800_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "keyboard_protocol.h"

#define VIRTUAL_EF800_KEYBOARD_MAX_HELD 64U
#define VIRTUAL_EF800_KEYBOARD_OUTPUT_SIZE 256U
#define VIRTUAL_EF800_KEYBOARD_NAME_SIZE 8U

struct virtual_ef800_keyboard {
	uint16_t held[VIRTUAL_EF800_KEYBOARD_MAX_HELD];
	size_t held_count;
	uint8_t output[VIRTUAL_EF800_KEYBOARD_OUTPUT_SIZE];
	size_t output_length;
	char pending_name[VIRTUAL_EF800_KEYBOARD_NAME_SIZE];
	bool pending_press;
	bool pending_valid;
};

/** @brief Initialize key tracking and clear pending output.
 * @param keyboard Keyboard state to initialize.
 * @return None.
 */
void virtual_ef800_keyboard_init(struct virtual_ef800_keyboard *keyboard);

/** @brief Press a named key and enqueue its protocol make sequence.
 * @param keyboard Keyboard state.
 * @param name Key name bytes.
 * @param length Name length.
 * @return 0 on press; negative errno for unknown key or full state/output.
 */
int virtual_ef800_keyboard_press(struct virtual_ef800_keyboard *keyboard,
					 const char *name, size_t length);

/** @brief Release a held named key and enqueue its break sequence.
 * @param keyboard Keyboard state.
 * @param name Key name bytes.
 * @param length Name length.
 * @return 0 on release; negative errno for unknown/unheld key or full output.
 */
int virtual_ef800_keyboard_release(struct virtual_ef800_keyboard *keyboard,
					   const char *name, size_t length);

/** @brief Release every held key and enqueue corresponding break events.
 * @param keyboard Keyboard state.
 * @return None.
 */
void virtual_ef800_keyboard_release_all(
	struct virtual_ef800_keyboard *keyboard);

/** @brief Drain pending keyboard protocol bytes into a destination buffer.
 * @param keyboard Keyboard state.
 * @param output Destination byte buffer.
 * @param capacity Buffer capacity.
 * @return Number of bytes copied.
 */
size_t virtual_ef800_keyboard_read(struct virtual_ef800_keyboard *keyboard,
							uint8_t *output, size_t capacity);

/** @brief Consume a pending host-visible key press/release event.
 * @param keyboard Keyboard state.
 * @param name Destination key-name buffer.
 * @param capacity Name-buffer capacity.
 * @param pressed Receives true for press and false for release.
 * @return True when an event was consumed.
 */
bool virtual_ef800_keyboard_take_event(struct virtual_ef800_keyboard *keyboard,
							char *name, size_t capacity, bool *pressed);

#endif
