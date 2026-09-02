/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_KEYBOARD_KEY_NAMES_H
#define EF800_KEYBOARD_KEY_NAMES_H

#include <stddef.h>
#include <stdint.h>

/* Resolve a case-insensitive semantic key name to the controller raw key. */
int kb_key_name_to_raw(const char *name, size_t length, uint16_t *raw);

#endif /* EF800_KEYBOARD_KEY_NAMES_H */
