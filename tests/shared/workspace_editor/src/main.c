/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <errno.h>

#include <zephyr/ztest.h>

#include "ef800_workspace_row_editor.h"
#include "ef800_workspace_browser.h"
#include "ef800_position_editor.h"

static const uint8_t *workspace_data;
static size_t workspace_data_length;
static uint64_t workspace_version_fingerprint = UINT64_C(1);
static unsigned int workspace_replace_calls;
static bool workspace_replace_deleted;
static int position_append_result;
static char appended_position_record[2U * EF800_POSITION_TEXT_MAX + 32U];
static size_t appended_position_length;

int ef800_workspace_append(struct ef800_workspace *workspace,
	const void *data, size_t length)
{
	if (!ef800_workspace_is_selected(workspace) || data == NULL ||
	    length > sizeof(appended_position_record)) {
		return -ENODEV;
	}
	if (position_append_result != 0) {
		return position_append_result;
	}
	memcpy(appended_position_record, data, length);
	appended_position_length = length;
	return 0;
}

static void test_workspace_set_data(const char *data, size_t length)
{
	workspace_data = (const uint8_t *)data;
	workspace_data_length = length;
}

bool ef800_workspace_is_selected(const struct ef800_workspace *workspace)
{
	return workspace != NULL && workspace->selected;
}

int ef800_workspace_get_version(const struct ef800_workspace *workspace,
				struct ef800_workspace_version *version)
{
	if (!ef800_workspace_is_selected(workspace) || version == NULL) {
		return -EINVAL;
	}
	version->size = workspace_data_length;
	version->fingerprint = workspace_version_fingerprint;
	return 0;
}

int ef800_workspace_size(const struct ef800_workspace *workspace, size_t *size)
{
	if (!ef800_workspace_is_selected(workspace) || size == NULL) {
		return -EINVAL;
	}
	*size = workspace_data_length;
	return 0;
}

int ef800_workspace_read_range(const struct ef800_workspace *workspace,
			       size_t start, size_t end, void *destination,
			       size_t capacity, size_t *length)
{
	if (!ef800_workspace_is_selected(workspace) || length == NULL ||
	    start > end || end > workspace_data_length || end - start > capacity ||
	    (destination == NULL && end != start)) {
		return -EINVAL;
	}
	*length = end - start;
	if (*length != 0U) {
		memcpy(destination, &workspace_data[start], *length);
	}
	return 0;
}

int ef800_workspace_row_at(const struct ef800_workspace *workspace, size_t start,
			   struct ef800_workspace_row *row)
{
	size_t index;

	if (!ef800_workspace_is_selected(workspace) || row == NULL ||
	    start >= workspace_data_length) {
		return -ENOENT;
	}
	row->start = start;
	index = start;
	while (index < workspace_data_length && workspace_data[index] != '\r' &&
	       workspace_data[index] != '\n') {
		++index;
	}
	row->text_end = index;
	row->ending_length = 0U;
	if (index < workspace_data_length) {
		if (workspace_data[index] == '\r' && index + 1U < workspace_data_length &&
		    workspace_data[index + 1U] == '\n') {
			row->ending_length = 2U;
		} else {
			row->ending_length = 1U;
		}
	}
	row->end = index + row->ending_length;
	return 0;
}

int ef800_workspace_row_before(const struct ef800_workspace *workspace,
			       size_t row_start, struct ef800_workspace_row *row)
{
	struct ef800_workspace_row candidate;
	size_t offset = 0U;
	int result;

	while (offset < row_start && offset < workspace_data_length) {
		result = ef800_workspace_row_at(workspace, offset, &candidate);
		if (result != 0) {
			return result;
		}
		if (candidate.end == row_start) {
			*row = candidate;
			return 0;
		}
		if (candidate.end <= offset) {
			return -EIO;
		}
		offset = candidate.end;
	}
	return -ENOENT;
}

int ef800_workspace_replace_row(struct ef800_workspace *workspace,
				const struct ef800_workspace_version *expected_version,
				const struct ef800_workspace_row *row,
				const void *replacement, size_t replacement_length,
				bool delete_row)
{
	if (!ef800_workspace_is_selected(workspace) || expected_version == NULL ||
	    row == NULL || expected_version->size != workspace_data_length ||
	    expected_version->fingerprint != workspace_version_fingerprint) {
		return -EAGAIN;
	}
	workspace_replace_calls++;
	workspace_replace_deleted = delete_row;
	ARG_UNUSED(replacement);
	ARG_UNUSED(replacement_length);
	return 0;
}

ZTEST(workspace_editor, test_insert_overwrite_and_caret_bounds)
{
	const struct ef800_workspace_row row = {
		.start = 4U,
		.text_end = 7U,
		.end = 9U,
		.ending_length = 2U,
	};
	struct ef800_workspace_row_editor editor;

	ef800_workspace_row_editor_init(&editor, &row, "abc", 3U);
	ef800_workspace_row_editor_move(&editor, -1);
	zassert_equal(editor.caret, 2U, NULL);
	zassert_true(ef800_workspace_row_editor_put(&editor, 'X'), NULL);
	zassert_str_equal(editor.text, "abXc");
	zassert_equal(editor.row.ending_length, 2U, NULL);
	ef800_workspace_row_editor_move(&editor, -1);
	ef800_workspace_row_editor_toggle_mode(&editor);
	zassert_true(ef800_workspace_row_editor_put(&editor, 'Y'), NULL);
	zassert_str_equal(editor.text, "abYc");
	ef800_workspace_row_editor_move(&editor, 1);
	ef800_workspace_row_editor_move(&editor, 1);
	ef800_workspace_row_editor_move(&editor, 1);
	zassert_equal(editor.caret, editor.length, NULL);
}

ZTEST(workspace_editor, test_backspace_delete_and_edit_limit)
{
	const struct ef800_workspace_row row = {0};
	struct ef800_workspace_row_editor editor;
	char full_row[EF800_WORKSPACE_ROW_EDIT_MAX + 1U];

	ef800_workspace_row_editor_init(&editor, &row, "abcd", 4U);
	ef800_workspace_row_editor_move(&editor, -1);
	zassert_true(ef800_workspace_row_editor_erase(&editor, true), NULL);
	zassert_str_equal(editor.text, "abd");
	zassert_true(ef800_workspace_row_editor_erase(&editor, false), NULL);
	zassert_str_equal(editor.text, "ab");

	memset(full_row, 'x', sizeof(full_row) - 1U);
	full_row[sizeof(full_row) - 1U] = '\0';
	ef800_workspace_row_editor_init(&editor, &row, full_row,
					EF800_WORKSPACE_ROW_EDIT_MAX);
	zassert_false(ef800_workspace_row_editor_put(&editor, 'y'), NULL);
	zassert_equal(editor.length, EF800_WORKSPACE_ROW_EDIT_MAX, NULL);
}

ZTEST(workspace_editor, test_browser_keeps_order_and_stops_at_boundaries)
{
	static const char contents[] = "old\r\nmid\nnew\n";
	struct ef800_workspace workspace = {.selected = true};
	struct ef800_workspace_browser browser;

	test_workspace_set_data(contents, sizeof(contents) - 1U);
	zassert_ok(ef800_workspace_browser_open_measurements(&browser, &workspace),
		   NULL);
	zassert_equal(browser.line_count, 3U, NULL);
	zassert_str_equal(browser.lines[0], "old");
	zassert_str_equal(browser.lines[1], "mid");
	zassert_str_equal(browser.lines[2], "new");
	zassert_equal(browser.selected_row, 2U, NULL);
	zassert_ok(ef800_workspace_browser_move(&browser, &workspace, -1), NULL);
	zassert_equal(browser.selected_row, 1U, NULL);
	zassert_ok(ef800_workspace_browser_move(&browser, &workspace, -1), NULL);
	zassert_str_equal(browser.lines[0], "old");
	zassert_equal(ef800_workspace_browser_move(&browser, &workspace, -1),
		-ENOENT, NULL);
	while (ef800_workspace_browser_move(&browser, &workspace, 1) == 0) {
	}
	zassert_equal(ef800_workspace_browser_move(&browser, &workspace, 1),
		-ENOENT, NULL);
}

ZTEST(workspace_editor, test_position_after_4096_bytes_and_no_position)
{
	static char contents[5000];
	static const char position[] = "-STP 7.250 CODE";
	struct ef800_workspace workspace = {.selected = true};
	struct ef800_workspace_browser browser;
	size_t offset = 0U;

	while (offset + 2U < sizeof(contents) - sizeof(position)) {
		contents[offset++] = 'M';
		contents[offset++] = '\n';
	}
	memcpy(&contents[offset], position, sizeof(position) - 1U);
	offset += sizeof(position) - 1U;
	test_workspace_set_data(contents, offset);
	zassert_ok(ef800_workspace_browser_find_position(&browser, &workspace),
		   NULL);
	zassert_true(browser.active, NULL);
	zassert_true(browser.position_only, NULL);
	zassert_equal(browser.line_count, 1U, NULL);
	zassert_str_equal(browser.lines[0], position);

	test_workspace_set_data("1 2 3\n", 6U);
	zassert_ok(ef800_workspace_browser_find_position(&browser, &workspace),
		   NULL);
	zassert_equal(browser.line_count, 0U, NULL);
}

ZTEST(workspace_editor, test_browser_row_commit_rejects_stale_version)
{
	static const char contents[] = "entry\r\n";
	struct ef800_workspace workspace = {.selected = true};
	struct ef800_workspace_browser browser;
	struct ef800_workspace_row row;
	unsigned int calls_before = workspace_replace_calls;

	test_workspace_set_data(contents, sizeof(contents) - 1U);
	workspace_version_fingerprint = UINT64_C(11);
	zassert_ok(ef800_workspace_browser_refresh_at(&browser, &workspace, 0U),
		   NULL);
	zassert_ok(ef800_workspace_browser_selected_row(&browser, &workspace, &row),
		   NULL);
	zassert_ok(ef800_workspace_browser_commit_row(&browser, &workspace, &row,
		"updated", 7U, false), NULL);
	zassert_equal(workspace_replace_calls, calls_before + 1U, NULL);
	zassert_false(workspace_replace_deleted, NULL);

	workspace_version_fingerprint++;
	zassert_equal(ef800_workspace_browser_selected_row(&browser, &workspace,
		&row), -EAGAIN, NULL);
	zassert_equal(ef800_workspace_browser_commit_row(&browser, &workspace, &row,
		"updated", 7U, false), -EAGAIN, NULL);
	zassert_equal(workspace_replace_calls, calls_before + 1U, NULL);
}

ZTEST(workspace_editor, test_position_editor_collects_and_persists_entry)
{
	struct ef800_position_editor editor;
	struct ef800_workspace workspace = {.selected = true};
	const char *stp = "STP-1";
	const char *code = "CODE_2";
	const char *height = "-1.250";

	position_append_result = 0;
	ef800_position_editor_start(&editor);
	zassert_true(editor.active, NULL);
	for (size_t i = 0U; stp[i] != '\0'; ++i) {
		zassert_equal(ef800_position_editor_handle_key(&editor, &workspace,
			(uint8_t)stp[i]), EF800_POSITION_EDITOR_UPDATED, NULL);
	}
	zassert_equal(ef800_position_editor_handle_key(&editor, &workspace, '\r'),
		EF800_POSITION_EDITOR_UPDATED, NULL);
	for (size_t i = 0U; code[i] != '\0'; ++i) {
		zassert_equal(ef800_position_editor_handle_key(&editor, &workspace,
			(uint8_t)code[i]), EF800_POSITION_EDITOR_UPDATED, NULL);
	}
	zassert_equal(ef800_position_editor_handle_key(&editor, &workspace, '\r'),
		EF800_POSITION_EDITOR_UPDATED, NULL);
	for (size_t i = 0U; height[i] != '\0'; ++i) {
		zassert_equal(ef800_position_editor_handle_key(&editor, &workspace,
			(uint8_t)height[i]), EF800_POSITION_EDITOR_UPDATED, NULL);
	}
	zassert_equal(ef800_position_editor_handle_key(&editor, &workspace, '\r'),
		EF800_POSITION_EDITOR_SAVED, NULL);
	zassert_false(editor.active, NULL);
	zassert_equal(appended_position_length, strlen("-STP-1 -1.250 CODE_2 \r\n"),
		NULL);
	zassert_mem_equal(appended_position_record,
		"-STP-1 -1.250 CODE_2 \r\n", appended_position_length);
}

ZTEST(workspace_editor, test_position_editor_rejects_invalid_and_failed_saves)
{
	struct ef800_position_editor editor;
	struct ef800_workspace workspace = {.selected = true};
	const char *fields = "S\rC\r1.";

	ef800_position_editor_start(&editor);
	zassert_equal(ef800_position_editor_handle_key(&editor, &workspace, '\r'),
		-ENODATA, NULL);
	for (size_t i = 0U; fields[i] != '\0'; ++i) {
		(void)ef800_position_editor_handle_key(&editor, &workspace,
			(uint8_t)fields[i]);
	}
	zassert_equal(ef800_position_editor_handle_key(&editor, &workspace, '\r'),
		-EBADMSG, NULL);
	zassert_true(editor.active, NULL);

	/* Correct the malformed decimal and verify the persistence error is kept. */
	(void)ef800_position_editor_handle_key(&editor, &workspace, 0x08U);
	(void)ef800_position_editor_handle_key(&editor, &workspace, '5');
	position_append_result = -EIO;
	zassert_equal(ef800_position_editor_handle_key(&editor, &workspace, '\r'),
		-EIO, NULL);
	zassert_true(editor.active, NULL);
}

ZTEST_SUITE(workspace_editor, NULL, NULL, NULL, NULL, NULL);
