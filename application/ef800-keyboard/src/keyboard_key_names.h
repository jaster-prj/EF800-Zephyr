/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Resolve supported keyboard debug key names to protocol codes. */
#ifndef EF800_KEYBOARD_KEY_NAMES_H
#define EF800_KEYBOARD_KEY_NAMES_H

#include <stddef.h>
#include <stdint.h>

/** @brief Resolve a case-insensitive key name to a controller raw key code.
 * @param name Key-name bytes.
 * @param length Name length.
 * @param raw Receives matrix/special-key raw code.
 * @return 0 on lookup; negative errno when name is unknown or arguments invalid.
 */
int kb_key_name_to_raw(const char *name, size_t length, uint16_t *raw);

#endif /* EF800_KEYBOARD_KEY_NAMES_H */
