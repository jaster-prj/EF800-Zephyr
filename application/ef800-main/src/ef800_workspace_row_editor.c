/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_workspace_row_editor.h"

#include <string.h>

void ef800_workspace_row_editor_init(struct ef800_workspace_row_editor *editor,
				     const struct ef800_workspace_row *row,
				     const char *text, size_t length)
{
	if (editor == NULL || row == NULL || text == NULL ||
	    length > EF800_WORKSPACE_ROW_EDIT_MAX) {
		return;
	}
	memset(editor, 0, sizeof(*editor));
	editor->row = *row;
	memcpy(editor->text, text, length);
	editor->text[length] = '\0';
	editor->length = length;
	editor->caret = length;
	editor->insert_mode = true;
}

void ef800_workspace_row_editor_move(struct ef800_workspace_row_editor *editor,
				     int direction)
{
	if (editor == NULL) {
		return;
	}
	if (direction < 0 && editor->caret > 0U) {
		--editor->caret;
	} else if (direction > 0 && editor->caret < editor->length) {
		++editor->caret;
	}
}

void ef800_workspace_row_editor_toggle_mode(
	struct ef800_workspace_row_editor *editor)
{
	if (editor != NULL) {
		editor->insert_mode = !editor->insert_mode;
	}
}

bool ef800_workspace_row_editor_erase(struct ef800_workspace_row_editor *editor,
				      bool before_caret)
{
	size_t offset;

	if (editor == NULL || editor->length > EF800_WORKSPACE_ROW_EDIT_MAX ||
	    editor->caret > editor->length ||
	    (before_caret ? editor->caret == 0U :
			    editor->caret >= editor->length)) {
		return false;
	}
	offset = before_caret ? editor->caret - 1U : editor->caret;
	memmove(&editor->text[offset], &editor->text[offset + 1U],
		editor->length - offset);
	--editor->length;
	if (before_caret) {
		--editor->caret;
	}
	return true;
}

bool ef800_workspace_row_editor_put(struct ef800_workspace_row_editor *editor,
				    uint8_t character)
{
	if (editor == NULL || editor->length > EF800_WORKSPACE_ROW_EDIT_MAX ||
	    editor->caret > editor->length || character < 0x20U ||
	    character >= 0x7fU) {
		return false;
	}
	if (editor->insert_mode) {
		if (editor->length >= EF800_WORKSPACE_ROW_EDIT_MAX) {
			return false;
		}
		memmove(&editor->text[editor->caret + 1U],
			&editor->text[editor->caret],
			editor->length - editor->caret + 1U);
		++editor->length;
	} else if (editor->caret == editor->length) {
		if (editor->length >= EF800_WORKSPACE_ROW_EDIT_MAX) {
			return false;
		}
		++editor->length;
	}
	editor->text[editor->caret++] = (char)character;
	return true;
}
