/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Bounded input state and persistence policy for adding a position record.
 */
#ifndef EF800_POSITION_EDITOR_H
#define EF800_POSITION_EDITOR_H

#include <stdbool.h>
#include <stdint.h>

#include "ef800_position.h"

enum ef800_position_editor_field {
	EF800_POSITION_EDITOR_STP = 0,
	EF800_POSITION_EDITOR_CODE,
	EF800_POSITION_EDITOR_HEIGHT,
};

enum ef800_position_editor_result {
	EF800_POSITION_EDITOR_IGNORED = 0,
	EF800_POSITION_EDITOR_UPDATED = 1,
	EF800_POSITION_EDITOR_SAVED = 2,
};

struct ef800_position_editor {
	bool active;
	enum ef800_position_editor_field field;
	uint8_t length;
	char input[EF800_POSITION_TEXT_MAX + 1U];
	struct ef800_position_entry entry;
};

/** @brief Start a new position entry at its first field.
 * @param editor State to initialize; ignored when NULL.
 * @return None.
 */
void ef800_position_editor_start(struct ef800_position_editor *editor);

/** @brief Apply one key and save when all fields form a valid position.
 * @param editor Active editor state.
 * @param workspace Workspace receiving a completed entry.
 * @param key Input byte to process.
 * @return Negative errno on invalid state or persistence failure; otherwise an
 *         editor result indicating ignored, updated, or saved input.
 */
int ef800_position_editor_handle_key(struct ef800_position_editor *editor,
	struct ef800_workspace *workspace, uint8_t key);

#endif /* EF800_POSITION_EDITOR_H */
