/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_workspace_browser.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#define WORKSPACE_VIEW_SCAN_CHUNK 128U

void ef800_workspace_browser_reset(struct ef800_workspace_browser *browser)
{
	if (browser != NULL) {
		memset(browser, 0, sizeof(*browser));
	}
}

/** @brief Copy one workspace row into a bounded display slot.
 * @param browser Destination browser state.
 * @param workspace Workspace supplying the row bytes.
 * @param index Destination row index.
 * @param row Row boundaries and metadata to cache.
 * @return 0 on copy; negative errno for invalid bounds or workspace I/O failure.
 */
static int browser_set_row(struct ef800_workspace_browser *browser,
			   const struct ef800_workspace *workspace, size_t index,
			   const struct ef800_workspace_row *row)
{
	size_t text_length;
	size_t copy_length;
	size_t length = 0U;
	int result;

	if (browser == NULL || workspace == NULL || row == NULL ||
	    index >= EF800_WORKSPACE_BROWSER_ROWS || row->text_end < row->start) {
		return -EINVAL;
	}
	text_length = row->text_end - row->start;
	copy_length = text_length < EF800_WORKSPACE_BROWSER_COLUMNS ?
		text_length : EF800_WORKSPACE_BROWSER_COLUMNS;
	if (copy_length != 0U) {
		result = ef800_workspace_read_range(workspace, row->start,
			row->start + copy_length, browser->lines[index],
			EF800_WORKSPACE_BROWSER_COLUMNS, &length);
		if (result != 0) {
			return result;
		}
		if (length != copy_length) {
			return -EIO;
		}
	}
	browser->lines[index][copy_length] = '\0';
	browser->line_starts[index] = row->start;
	browser->line_ends[index] = row->end;
	return 0;
}

/** @brief Reverse cached rows and offsets into chronological order.
 * @param browser Browser state whose populated rows are reversed.
 * @return None.
 */
static void browser_reverse_rows(struct ef800_workspace_browser *browser)
{
	for (size_t left = 0U; left < browser->line_count / 2U; ++left) {
		size_t right = browser->line_count - left - 1U;
		char line[EF800_WORKSPACE_BROWSER_COLUMNS + 1U];
		size_t offset;

		memcpy(line, browser->lines[left], sizeof(line));
		memcpy(browser->lines[left], browser->lines[right], sizeof(line));
		memcpy(browser->lines[right], line, sizeof(line));
		offset = browser->line_starts[left];
		browser->line_starts[left] = browser->line_starts[right];
		browser->line_starts[right] = offset;
		offset = browser->line_ends[left];
		browser->line_ends[left] = browser->line_ends[right];
		browser->line_ends[right] = offset;
	}
}

/** @brief Find the last logical row, accounting for trailing line endings.
 * @param workspace Workspace file to scan.
 * @param file_size Current file length.
 * @param row Receives the final row metadata.
 * @return 0 on success; negative errno for empty data or read/parse failure.
 */
static int browser_find_last_row(const struct ef800_workspace *workspace,
				 size_t file_size,
				 struct ef800_workspace_row *row)
{
	uint8_t buffer[WORKSPACE_VIEW_SCAN_CHUNK];
	uint8_t last;
	size_t length;
	size_t search_end = file_size;
	int result;

	if (row == NULL || file_size == 0U) {
		return -ENODATA;
	}
	result = ef800_workspace_read_range(workspace, file_size - 1U,
		file_size, &last, 1U, &length);
	if (result != 0) {
		return result;
	}
	if (length != 1U) {
		return -EIO;
	}
	if (last == '\r' || last == '\n') {
		return ef800_workspace_row_before(workspace, file_size, row);
	}
	while (search_end > 0U) {
		size_t chunk_start = search_end > sizeof(buffer) ?
			search_end - sizeof(buffer) : 0U;

		result = ef800_workspace_read_range(workspace, chunk_start, search_end,
			buffer, sizeof(buffer), &length);
		if (result != 0) {
			return result;
		}
		if (length != search_end - chunk_start) {
			return -EIO;
		}
		for (size_t index = length; index > 0U; --index) {
			if (buffer[index - 1U] == '\r' || buffer[index - 1U] == '\n') {
				return ef800_workspace_row_at(workspace,
					chunk_start + index, row);
			}
		}
		search_end = chunk_start;
	}
	return ef800_workspace_row_at(workspace, 0U, row);
}

/** @brief Cache up to four rows preceding and including the newest row.
 * @param browser Browser state to populate.
 * @param workspace Source workspace.
 * @param last_row Metadata for the newest row.
 * @return 0 on success; negative errno if a row cannot be read.
 */
static int browser_add_older_rows(struct ef800_workspace_browser *browser,
				  const struct ef800_workspace *workspace,
				  const struct ef800_workspace_row *last_row)
{
	struct ef800_workspace_row row = *last_row;
	int result;

	browser->line_count = 0U;
	memset(browser->lines, 0, sizeof(browser->lines));
	memset(browser->line_starts, 0, sizeof(browser->line_starts));
	memset(browser->line_ends, 0, sizeof(browser->line_ends));
	for (size_t count = 0U; count < EF800_WORKSPACE_BROWSER_ROWS; ++count) {
		result = browser_set_row(browser, workspace, browser->line_count, &row);
		if (result != 0) {
			return result;
		}
		++browser->line_count;
		if (row.start == 0U) {
			break;
		}
		result = ef800_workspace_row_before(workspace, row.start, &row);
		if (result == -ENOENT) {
			break;
		}
		if (result != 0) {
			return result;
		}
	}
	if (browser->line_count == 0U) {
		return -ENODATA;
	}
	browser_reverse_rows(browser);
	browser->start = browser->line_starts[0];
	browser->end = browser->line_ends[browser->line_count - 1U];
	browser->selected_row = browser->line_count - 1U;
	return 0;
}

int ef800_workspace_browser_refresh_at(struct ef800_workspace_browser *browser,
				       const struct ef800_workspace *workspace,
				       size_t anchor)
{
	struct ef800_workspace_row row;
	int result;

	if (browser == NULL || workspace == NULL) {
		return -EINVAL;
	}
	result = ef800_workspace_get_version(workspace, &browser->version);
	if (result != 0) {
		return result;
	}
	result = ef800_workspace_size(workspace, &browser->file_size);
	if (result != 0) {
		return result;
	}
	browser->line_count = 0U;
	browser->selected_row = 0U;
	memset(browser->lines, 0, sizeof(browser->lines));
	memset(browser->line_starts, 0, sizeof(browser->line_starts));
	memset(browser->line_ends, 0, sizeof(browser->line_ends));
	if (browser->file_size == 0U) {
		browser->start = 0U;
		browser->end = 0U;
		browser->active = false;
		return -ENODATA;
	}
	if (anchor >= browser->file_size) {
		result = browser_find_last_row(workspace, browser->file_size, &row);
	} else {
		result = ef800_workspace_row_at(workspace, anchor, &row);
	}
	if (result != 0) {
		return result;
	}
	browser->active = true;
	browser->position_only = false;
	for (size_t index = 0U; index < EF800_WORKSPACE_BROWSER_ROWS; ++index) {
		result = browser_set_row(browser, workspace, index, &row);
		if (result != 0) {
			return result;
		}
		++browser->line_count;
		if (row.end >= browser->file_size) {
			break;
		}
		result = ef800_workspace_row_at(workspace, row.end, &row);
		if (result == -ENOENT) {
			break;
		}
		if (result != 0) {
			return result;
		}
	}
	browser->start = browser->line_starts[0];
	browser->end = browser->line_ends[browser->line_count - 1U];
	return 0;
}

int ef800_workspace_browser_open_measurements(
	struct ef800_workspace_browser *browser,
	const struct ef800_workspace *workspace)
{
	struct ef800_workspace_row last_row;
	int result;

	if (browser == NULL || workspace == NULL) {
		return -EINVAL;
	}
	ef800_workspace_browser_reset(browser);
	if (!ef800_workspace_is_selected(workspace)) {
		return -ENODEV;
	}
	result = ef800_workspace_get_version(workspace, &browser->version);
	if (result != 0) {
		return result;
	}
	result = ef800_workspace_size(workspace, &browser->file_size);
	if (result != 0 || browser->file_size == 0U) {
		return result != 0 ? result : -ENODATA;
	}
	result = browser_find_last_row(workspace, browser->file_size, &last_row);
	if (result == 0) {
		result = browser_add_older_rows(browser, workspace, &last_row);
	}
	if (result == 0) {
		browser->active = true;
	}
	return result;
}

int ef800_workspace_browser_move(struct ef800_workspace_browser *browser,
				 const struct ef800_workspace *workspace,
				 int direction)
{
	struct ef800_workspace_row adjacent;
	size_t last_index;
	int result;

	if (browser == NULL || workspace == NULL || !browser->active ||
	    browser->line_count == 0U || browser->position_only) {
		return -EINVAL;
	}
	last_index = browser->line_count - 1U;
	if (direction < 0) {
		if (browser->selected_row > 0U) {
			--browser->selected_row;
			return 0;
		}
		result = ef800_workspace_row_before(workspace,
			browser->line_starts[0], &adjacent);
		if (result != 0) {
			return result;
		}
		if (browser->line_count < EF800_WORKSPACE_BROWSER_ROWS) {
			++browser->line_count;
		}
		for (size_t index = browser->line_count - 1U; index > 0U; --index) {
			memcpy(browser->lines[index], browser->lines[index - 1U],
			       sizeof(browser->lines[0]));
			browser->line_starts[index] = browser->line_starts[index - 1U];
			browser->line_ends[index] = browser->line_ends[index - 1U];
		}
		result = browser_set_row(browser, workspace, 0U, &adjacent);
		if (result != 0) {
			return result;
		}
		browser->selected_row = 0U;
	} else if (direction > 0) {
		if (browser->selected_row < last_index) {
			++browser->selected_row;
			return 0;
		}
		if (browser->line_ends[last_index] >= browser->file_size) {
			return -ENOENT;
		}
		result = ef800_workspace_row_at(workspace,
			browser->line_ends[last_index], &adjacent);
		if (result != 0) {
			return result;
		}
		if (browser->line_count == EF800_WORKSPACE_BROWSER_ROWS) {
			for (size_t index = 1U; index < EF800_WORKSPACE_BROWSER_ROWS; ++index) {
				memcpy(browser->lines[index - 1U], browser->lines[index],
				       sizeof(browser->lines[0]));
				browser->line_starts[index - 1U] = browser->line_starts[index];
				browser->line_ends[index - 1U] = browser->line_ends[index];
			}
		} else {
			++browser->line_count;
		}
		last_index = browser->line_count - 1U;
		result = browser_set_row(browser, workspace, last_index, &adjacent);
		if (result != 0) {
			return result;
		}
		browser->selected_row = last_index;
	} else {
		return 0;
	}
	browser->start = browser->line_starts[0];
	browser->end = browser->line_ends[browser->line_count - 1U];
	return 0;
}

int ef800_workspace_browser_find_position(
	struct ef800_workspace_browser *browser,
	const struct ef800_workspace *workspace)
{
	struct ef800_workspace_row row;
	uint8_t first_character;
	char line[EF800_WORKSPACE_BROWSER_COLUMNS + 1U];
	size_t offset = 0U;
	int result;

	if (browser == NULL || workspace == NULL) {
		return -EINVAL;
	}
	ef800_workspace_browser_reset(browser);
	if (!ef800_workspace_is_selected(workspace)) {
		return -ENODEV;
	}
	result = ef800_workspace_size(workspace, &browser->file_size);
	if (result != 0) {
		return result;
	}
	while (offset < browser->file_size) {
		size_t text_length;
		size_t copy_length;
		size_t length;

		result = ef800_workspace_row_at(workspace, offset, &row);
		if (result != 0) {
			return result;
		}
		text_length = row.text_end - row.start;
		if (text_length != 0U) {
			result = ef800_workspace_read_range(workspace, row.start,
				row.start + 1U, &first_character, 1U, &length);
			if (result != 0) {
				return result;
			}
			if (length != 1U) {
				return -EIO;
			}
			if (first_character == '-') {
				copy_length = text_length < EF800_WORKSPACE_BROWSER_COLUMNS ?
					text_length : EF800_WORKSPACE_BROWSER_COLUMNS;
				result = ef800_workspace_read_range(workspace, row.start,
					row.start + copy_length, line, sizeof(line), &length);
				if (result != 0) {
					return result;
				}
				if (length != copy_length) {
					return -EIO;
				}
				line[copy_length] = '\0';
				memcpy(browser->lines[0], line, copy_length + 1U);
				browser->line_count = 1U;
			}
		}
		if (row.end <= offset) {
			return -EIO;
		}
		offset = row.end;
	}
	browser->active = true;
	browser->position_only = true;
	return 0;
}

int ef800_workspace_browser_selected_row(
	const struct ef800_workspace_browser *browser,
	const struct ef800_workspace *workspace,
	struct ef800_workspace_row *row)
{
	struct ef800_workspace_version current_version;
	int result;

	if (browser == NULL || workspace == NULL || row == NULL || !browser->active ||
	    browser->position_only || browser->selected_row >= browser->line_count) {
		return -EINVAL;
	}
	result = ef800_workspace_get_version(workspace, &current_version);
	if (result != 0) {
		return result;
	}
	if (current_version.size != browser->version.size ||
	    current_version.fingerprint != browser->version.fingerprint) {
		return -EAGAIN;
	}
	result = ef800_workspace_row_at(workspace,
		browser->line_starts[browser->selected_row], row);
	if (result != 0) {
		return result;
	}
	if (row->end != browser->line_ends[browser->selected_row]) {
		return -EAGAIN;
	}
	return 0;
}

int ef800_workspace_browser_commit_row(
	const struct ef800_workspace_browser *browser,
	struct ef800_workspace *workspace,
	const struct ef800_workspace_row *row, const void *replacement,
	size_t replacement_length, bool delete_row)
{
	struct ef800_workspace_version current_version;
	struct ef800_workspace_row current_row;
	int result;

	if (browser == NULL || workspace == NULL || row == NULL || !browser->active ||
	    browser->position_only) {
		return -EINVAL;
	}
	result = ef800_workspace_get_version(workspace, &current_version);
	if (result != 0) {
		return result;
	}
	if (current_version.size != browser->version.size ||
	    current_version.fingerprint != browser->version.fingerprint) {
		return -EAGAIN;
	}
	result = ef800_workspace_row_at(workspace, row->start, &current_row);
	if (result != 0) {
		return result;
	}
	if (current_row.end != row->end || current_row.text_end != row->text_end) {
		return -EAGAIN;
	}
	return ef800_workspace_replace_row(workspace, &browser->version, row,
		replacement, replacement_length, delete_row);
}
