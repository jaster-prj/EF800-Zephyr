/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Manage named workspaces and version-checked row editing. */
#ifndef EF800_WORKSPACE_H
#define EF800_WORKSPACE_H

#include <stdbool.h>

#include "ef800_persistence.h"
#include "ef800_measurement.h"

#define EF800_WORKSPACE_CONFIGURATION_FILE ".workspace"
#define EF800_WORKSPACE_EXTENSION ".dat"
#define EF800_WORKSPACE_NAME_MAX 48U
#define EF800_WORKSPACE_SNAPSHOT_MAX 4096U
#define EF800_WORKSPACE_ROW_ENDING_MAX 2U

struct ef800_workspace_row {
	size_t start;
	size_t text_end;
	size_t end;
	uint8_t ending_length;
};

struct ef800_workspace_version {
	size_t size;
	uint64_t fingerprint;
};

typedef int (*ef800_workspace_list_callback)(const char *name, void *context);

struct ef800_workspace {
	struct ef800_persistence *storage;
	char name[EF800_WORKSPACE_NAME_MAX + 1U];
	char filename[EF800_WORKSPACE_NAME_MAX + sizeof(EF800_WORKSPACE_EXTENSION)];
	int32_t current_point_number;
	char current_code[EF800_MEASUREMENT_TEXT_MAX + 1U];
	bool selected;
};

/** @brief Bind workspace state to its persistence service.
 * @param workspace State to initialize.
 * @param storage Persistence backend service.
 * @return 0 on initialization; negative errno for invalid arguments.
 */
int ef800_workspace_init(struct ef800_workspace *workspace,
			 struct ef800_persistence *storage);

/** @brief Load the currently selected workspace from configuration storage.
 * @param workspace Workspace state to populate.
 * @return 0 on load; negative errno if configuration/file is invalid or missing.
 */
int ef800_workspace_load_current(struct ef800_workspace *workspace);

/** @brief Create and select a new named workspace.
 * @param workspace Workspace service/state.
 * @param name Requested workspace name.
 * @return 0 on creation; negative errno for invalid/duplicate name or I/O error.
 */
int ef800_workspace_create(struct ef800_workspace *workspace, const char *name);

/** @brief Select an existing workspace and persist that selection.
 * @param workspace Workspace service/state.
 * @param name Existing workspace name.
 * @return 0 on selection; negative errno if missing, invalid, or persistence fails.
 */
int ef800_workspace_select(struct ef800_workspace *workspace, const char *name);

/** @brief Delete the selected workspace and clear selection state.
 * @param workspace Workspace to delete.
 * @return 0 on deletion; negative errno if none is selected or I/O fails.
 */
int ef800_workspace_delete_selected(struct ef800_workspace *workspace);

/** @brief Append bytes to the selected workspace file.
 * @param workspace Destination workspace.
 * @param data Bytes to append.
 * @param length Byte count.
 * @return 0 on append; negative errno if no workspace or storage fails.
 */
int ef800_workspace_append(struct ef800_workspace *workspace,
			   const void *data, size_t length);

/** @brief Read the complete selected file into a bounded snapshot.
 * @param workspace Selected workspace.
 * @param destination Snapshot buffer.
 * @param capacity Buffer capacity.
 * @param length Receives bytes copied.
 * @return 0 on read; negative errno if unavailable, too large, or I/O fails.
 */
int ef800_workspace_snapshot(const struct ef800_workspace *workspace,
				void *destination, size_t capacity, size_t *length);

/** @brief Read a byte range from the selected workspace.
 * @param workspace Selected workspace.
 * @param start Inclusive starting offset.
 * @param end Exclusive ending offset.
 * @param destination Destination buffer.
 * @param capacity Buffer capacity.
 * @param length Receives bytes read.
 * @return 0 on read; negative errno for invalid range or I/O failure.
 */
int ef800_workspace_read_range(const struct ef800_workspace *workspace,
				      size_t start, size_t end, void *destination,
				      size_t capacity, size_t *length);

/** @brief Get the selected workspace file size.
 * @param workspace Selected workspace.
 * @param size Receives file size in bytes.
 * @return 0 on success; negative errno if unavailable or stat fails.
 */
int ef800_workspace_size(const struct ef800_workspace *workspace, size_t *size);
/* Read one logical row at an exact row-start offset. CR, LF, and CRLF end rows. */

/** @brief Parse one logical row beginning at an exact row boundary.
 * @param workspace Workspace to read.
 * @param start Exact byte offset where the row starts.
 * @param row Receives start/text-end/end offsets and line-ending size.
 * @return 0 on parse; negative errno when offset is not a row start or I/O fails.
 */
int ef800_workspace_row_at(const struct ef800_workspace *workspace, size_t start,
			   struct ef800_workspace_row *row);
/* Read the logical row immediately before an exact row-start offset. */

/** @brief Find the logical row immediately before a row boundary.
 * @param workspace Workspace to read.
 * @param row_start Exact start offset of the following row.
 * @param row Receives preceding row boundaries.
 * @return 0 on success; negative errno if no preceding row or I/O fails.
 */
int ef800_workspace_row_before(const struct ef800_workspace *workspace,
			       size_t row_start,
			       struct ef800_workspace_row *row);

/** @brief Capture size and fingerprint for optimistic concurrency checks.
 * @param workspace Workspace to fingerprint.
 * @param version Receives current version token.
 * @return 0 on success; negative errno on stat/read failure.
 */
int ef800_workspace_get_version(const struct ef800_workspace *workspace,
				struct ef800_workspace_version *version);

/** @brief Replace/delete one row only if the expected workspace version matches.
 * @param workspace Workspace to modify.
 * @param expected_version Version captured before editing.
 * @param row Original row boundaries and ending metadata.
 * @param replacement Replacement text, or NULL when deleting.
 * @param replacement_length Replacement byte count.
 * @param delete_row True to delete rather than replace.
 * @return 0 on commit; negative errno, including -EAGAIN for stale version.
 */
int ef800_workspace_replace_row(struct ef800_workspace *workspace,
				const struct ef800_workspace_version *expected_version,
				const struct ef800_workspace_row *row,
				const void *replacement, size_t replacement_length,
				bool delete_row);

/** @brief Identify whether a line is a serialized position record.
 * @param line NUL-terminated row text.
 * @return True when it matches the position-record format.
 */
bool ef800_workspace_line_is_position(const char *line);

/** @brief Enumerate workspace names through a caller callback.
 * @param workspace Workspace service/state.
 * @param callback Called for each name; nonzero stops enumeration.
 * @param context Opaque callback argument.
 * @return 0 on completion; negative errno on storage or callback failure.
 */
int ef800_workspace_list(struct ef800_workspace *workspace,
			const ef800_workspace_list_callback callback, void *context);

/** @brief Check whether a workspace is currently selected.
 * @param workspace State to inspect.
 * @return True when selected.
 */
bool ef800_workspace_is_selected(const struct ef800_workspace *workspace);

/** @brief Get the selected workspace name.
 * @param workspace State to inspect.
 * @return Internal name pointer, or NULL when no workspace is selected.
 */
const char *ef800_workspace_get_name(const struct ef800_workspace *workspace);

/** @brief Get the selected workspace filename.
 * @param workspace State to inspect.
 * @return Internal filename pointer, or NULL when no workspace is selected.
 */
const char *ef800_workspace_get_filename(
	const struct ef800_workspace *workspace);

/** @brief Get the current point number parsed from workspace data.
 * @param workspace State to inspect.
 * @return Point number, or the implementation's unset value if unavailable.
 */
int32_t ef800_workspace_current_point_number(
	const struct ef800_workspace *workspace);

/** @brief Get the current measurement code parsed from workspace data.
 * @param workspace State to inspect.
 * @return Internal code pointer, or NULL when unavailable.
 */
const char *ef800_workspace_current_code(
	const struct ef800_workspace *workspace);

#endif /* EF800_WORKSPACE_H */
