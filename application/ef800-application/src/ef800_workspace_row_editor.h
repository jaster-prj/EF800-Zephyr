/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Bounded raw-text editing policy for one workspace row.
 */
#ifndef EF800_WORKSPACE_ROW_EDITOR_H
#define EF800_WORKSPACE_ROW_EDITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_workspace.h"

#define EF800_WORKSPACE_ROW_EDIT_MAX 255U

/* Bounded raw-text editor state. The row metadata preserves its original
 * ending when the host commits the edited text through ef800_workspace. */
struct ef800_workspace_row_editor {
	struct ef800_workspace_row row;
	char text[EF800_WORKSPACE_ROW_EDIT_MAX + 1U];
	size_t length;
	size_t caret;
	size_t viewport_start;
	bool insert_mode;
};

/** @brief Initialize an editor from an existing row's raw text.
 * @param editor State to initialize; unchanged if any input is invalid.
 * @param row Original row metadata, including its line ending.
 * @param text Initial row text.
 * @param length Number of bytes in text; must not exceed the editor limit.
 * @return None.
 */
void ef800_workspace_row_editor_init(struct ef800_workspace_row_editor *editor,
				     const struct ef800_workspace_row *row,
				     const char *text, size_t length);

/** @brief Move the caret one character within the edited text.
 * @param editor Editor state to update; ignored when NULL.
 * @param direction Negative moves left, positive moves right, zero is a no-op.
 * @return None.
 */
void ef800_workspace_row_editor_move(struct ef800_workspace_row_editor *editor,
				     int direction);

/** @brief Switch between insertion and overwrite editing.
 * @param editor Editor state to update; ignored when NULL.
 * @return None.
 */
void ef800_workspace_row_editor_toggle_mode(
	struct ef800_workspace_row_editor *editor);

/** @brief Erase the character before the caret or at the caret.
 * @param editor Editor state to modify.
 * @param before_caret True for backspace, false for delete.
 * @return True if a character was erased; otherwise false.
 */
bool ef800_workspace_row_editor_erase(struct ef800_workspace_row_editor *editor,
				      bool before_caret);

/** @brief Insert or overwrite with one printable ASCII character.
 * @param editor Editor state to modify.
 * @param character Character to place at the caret.
 * @return True if accepted; false for invalid input or a full buffer.
 */
bool ef800_workspace_row_editor_put(struct ef800_workspace_row_editor *editor,
				    uint8_t character);

#endif /* EF800_WORKSPACE_ROW_EDITOR_H */
