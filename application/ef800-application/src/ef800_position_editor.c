/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_position_editor.h"

#include <errno.h>
#include <string.h>

void ef800_position_editor_start(struct ef800_position_editor *editor)
{
	if (editor == NULL) {
		return;
	}
	memset(editor, 0, sizeof(*editor));
	editor->active = true;
	editor->field = EF800_POSITION_EDITOR_STP;
}

/** @brief Commit the current text buffer into the selected text field.
 * @param editor Editor holding the current field and input text.
 * @return None; height is parsed and committed by the key handler.
 */
static void copy_current_field(struct ef800_position_editor *editor)
{
	switch (editor->field) {
	case EF800_POSITION_EDITOR_STP:
		memcpy(editor->entry.stp, editor->input, editor->length + 1U);
		break;
	case EF800_POSITION_EDITOR_CODE:
		memcpy(editor->entry.code, editor->input, editor->length + 1U);
		break;
	case EF800_POSITION_EDITOR_HEIGHT:
	default:
		break;
	}
}

int ef800_position_editor_handle_key(struct ef800_position_editor *editor,
	struct ef800_workspace *workspace, uint8_t key)
{
	int32_t height;
	int result;

	if (editor == NULL || workspace == NULL || !editor->active) {
		return -EINVAL;
	}
	if (key == 0x1BU) {
		editor->active = false;
		return EF800_POSITION_EDITOR_UPDATED;
	}
	if (key == 0x08U || key == 0x7FU) {
		if (editor->length == 0U) {
			return EF800_POSITION_EDITOR_IGNORED;
		}
		editor->input[--editor->length] = '\0';
		return EF800_POSITION_EDITOR_UPDATED;
	}
	if (key == '\r' || key == '\n') {
		if (editor->length == 0U) {
			return -ENODATA;
		}
		if (editor->field == EF800_POSITION_EDITOR_HEIGHT) {
			result = ef800_position_parse_height(editor->input, &height);
			if (result != 0) {
				return -EBADMSG;
			}
			editor->entry.height_millimeters = height;
			result = ef800_position_append(workspace, &editor->entry);
			if (result != 0) {
				return result;
			}
			editor->active = false;
			return EF800_POSITION_EDITOR_SAVED;
		}
		copy_current_field(editor);
		editor->field++;
		editor->length = 0U;
		editor->input[0] = '\0';
		return EF800_POSITION_EDITOR_UPDATED;
	}
	if (editor->field == EF800_POSITION_EDITOR_HEIGHT) {
		if ((key >= '0' && key <= '9') ||
		    ((key == '+' || key == '-') && editor->length == 0U) ||
		    (key == '.' && editor->length > 0U &&
		     strchr(editor->input, '.') == NULL)) {
			if ((size_t)editor->length + 1U < sizeof(editor->input)) {
				editor->input[editor->length++] = (char)key;
				editor->input[editor->length] = '\0';
				return EF800_POSITION_EDITOR_UPDATED;
			}
		}
	} else if (ef800_position_text_char_valid((char)key) &&
		   (size_t)editor->length + 1U < sizeof(editor->input)) {
		editor->input[editor->length++] = (char)key;
		editor->input[editor->length] = '\0';
		return EF800_POSITION_EDITOR_UPDATED;
	}
	return EF800_POSITION_EDITOR_IGNORED;
}
