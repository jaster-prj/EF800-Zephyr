/* SPDX-License-Identifier: Apache-2.0 */
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

int ef800_workspace_init(struct ef800_workspace *workspace,
			 struct ef800_persistence *storage);
int ef800_workspace_load_current(struct ef800_workspace *workspace);
int ef800_workspace_create(struct ef800_workspace *workspace, const char *name);
int ef800_workspace_select(struct ef800_workspace *workspace, const char *name);
int ef800_workspace_delete_selected(struct ef800_workspace *workspace);
int ef800_workspace_append(struct ef800_workspace *workspace,
			   const void *data, size_t length);
int ef800_workspace_snapshot(const struct ef800_workspace *workspace,
				void *destination, size_t capacity, size_t *length);
int ef800_workspace_read_range(const struct ef800_workspace *workspace,
				      size_t start, size_t end, void *destination,
				      size_t capacity, size_t *length);
int ef800_workspace_size(const struct ef800_workspace *workspace, size_t *size);
/* Read one logical row at an exact row-start offset. CR, LF, and CRLF end rows. */
int ef800_workspace_row_at(const struct ef800_workspace *workspace, size_t start,
			   struct ef800_workspace_row *row);
/* Read the logical row immediately before an exact row-start offset. */
int ef800_workspace_row_before(const struct ef800_workspace *workspace,
			       size_t row_start,
			       struct ef800_workspace_row *row);
int ef800_workspace_get_version(const struct ef800_workspace *workspace,
				struct ef800_workspace_version *version);
int ef800_workspace_replace_row(struct ef800_workspace *workspace,
				const struct ef800_workspace_version *expected_version,
				const struct ef800_workspace_row *row,
				const void *replacement, size_t replacement_length,
				bool delete_row);
bool ef800_workspace_line_is_position(const char *line);
int ef800_workspace_list(struct ef800_workspace *workspace,
			const ef800_workspace_list_callback callback, void *context);

bool ef800_workspace_is_selected(const struct ef800_workspace *workspace);
const char *ef800_workspace_get_name(const struct ef800_workspace *workspace);
const char *ef800_workspace_get_filename(
	const struct ef800_workspace *workspace);
int32_t ef800_workspace_current_point_number(
	const struct ef800_workspace *workspace);
const char *ef800_workspace_current_code(
	const struct ef800_workspace *workspace);

#endif /* EF800_WORKSPACE_H */
