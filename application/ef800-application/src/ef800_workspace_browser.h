/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Bounded browsing, selection, and version-checked edits of workspace rows.
 */
#ifndef EF800_WORKSPACE_BROWSER_H
#define EF800_WORKSPACE_BROWSER_H

#include <stdbool.h>
#include <stddef.h>

#include "ef800_workspace.h"

#define EF800_WORKSPACE_BROWSER_ROWS 4U
#define EF800_WORKSPACE_BROWSER_COLUMNS 40U

/* Bounded viewer state; file positions refer to the complete workspace file. */
struct ef800_workspace_browser {
	char lines[EF800_WORKSPACE_BROWSER_ROWS]
		[EF800_WORKSPACE_BROWSER_COLUMNS + 1U];
	size_t line_starts[EF800_WORKSPACE_BROWSER_ROWS];
	size_t line_ends[EF800_WORKSPACE_BROWSER_ROWS];
	size_t line_count;
	size_t start;
	size_t end;
	size_t file_size;
	size_t selected_row;
	struct ef800_workspace_version version;
	bool active;
	bool position_only;
};

/** @brief Clear all cached rows and browsing state.
 * @param browser Browser state to reset; ignored when NULL.
 * @return None.
 */
void ef800_workspace_browser_reset(struct ef800_workspace_browser *browser);

/** @brief Open the newest measurement rows in a selected workspace.
 * @param browser Destination browser state.
 * @param workspace Selected workspace to read.
 * @return 0 on success; a negative errno value if unavailable or unreadable.
 */
int ef800_workspace_browser_open_measurements(
	struct ef800_workspace_browser *browser,
	const struct ef800_workspace *workspace);

/** @brief Rebuild the visible page starting at the row containing anchor.
 * @param browser Browser state to refresh.
 * @param workspace Workspace whose current contents are read.
 * @param anchor File offset to anchor the first visible row, or an offset past
 *        EOF to show the newest rows.
 * @return 0 on success; a negative errno value on invalid input or I/O error.
 */
int ef800_workspace_browser_refresh_at(struct ef800_workspace_browser *browser,
				       const struct ef800_workspace *workspace,
				       size_t anchor);

/** @brief Move selection by one row, loading an adjacent page at its edge.
 * @param browser Active measurement browser.
 * @param workspace Workspace from which adjacent rows are read.
 * @param direction Negative moves earlier, positive later, zero does not move.
 * @return 0 on movement/no-op; negative errno at an edge or on failure.
 */
int ef800_workspace_browser_move(struct ef800_workspace_browser *browser,
				 const struct ef800_workspace *workspace,
				 int direction);

/** @brief Find and display the workspace's current position record.
 * @param browser Destination browser state.
 * @param workspace Selected workspace to scan.
 * @return 0 when scanned; negative errno if no workspace or a read fails.
 */
int ef800_workspace_browser_find_position(
	struct ef800_workspace_browser *browser,
	const struct ef800_workspace *workspace);

/** @brief Resolve the selected cached row after checking workspace freshness.
 * @param browser Active measurement browser with a saved version.
 * @param workspace Workspace to verify and read.
 * @param row Receives the selected row metadata.
 * @return 0 on success, -EAGAIN if workspace contents changed, otherwise errno.
 */
int ef800_workspace_browser_selected_row(
	const struct ef800_workspace_browser *browser,
	const struct ef800_workspace *workspace,
	struct ef800_workspace_row *row);

/** @brief Replace or delete a row only if the cached workspace version is current.
 * @param browser Browser state containing the version used for editing.
 * @param workspace Workspace to modify.
 * @param row Original row metadata to compare and replace.
 * @param replacement New row bytes; may be NULL when deleting.
 * @param replacement_length Number of bytes in replacement.
 * @param delete_row True to remove the row instead of replacing it.
 * @return 0 on commit; negative errno, including -EAGAIN for stale state.
 */
int ef800_workspace_browser_commit_row(
	const struct ef800_workspace_browser *browser,
	struct ef800_workspace *workspace,
	const struct ef800_workspace_row *row, const void *replacement,
	size_t replacement_length, bool delete_row);

#endif /* EF800_WORKSPACE_BROWSER_H */
