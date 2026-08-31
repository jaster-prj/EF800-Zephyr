/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_WORKSPACE_H
#define EF800_WORKSPACE_H

#include <stdbool.h>

#include "ef800_persistence.h"

#define EF800_WORKSPACE_CONFIGURATION_FILE ".workspace"
#define EF800_WORKSPACE_EXTENSION ".dat"
#define EF800_WORKSPACE_NAME_MAX 48U

typedef int (*ef800_workspace_list_callback)(const char *name, void *context);

struct ef800_workspace {
	struct ef800_persistence *storage;
	char name[EF800_WORKSPACE_NAME_MAX + 1U];
	char filename[EF800_WORKSPACE_NAME_MAX + sizeof(EF800_WORKSPACE_EXTENSION)];
	bool selected;
};

int ef800_workspace_init(struct ef800_workspace *workspace,
			 struct ef800_persistence *storage);
int ef800_workspace_load_current(struct ef800_workspace *workspace);
int ef800_workspace_create(struct ef800_workspace *workspace, const char *name);
int ef800_workspace_select(struct ef800_workspace *workspace, const char *name);
int ef800_workspace_append(struct ef800_workspace *workspace,
			   const void *data, size_t length);
int ef800_workspace_list(struct ef800_workspace *workspace,
			const ef800_workspace_list_callback callback, void *context);

bool ef800_workspace_is_selected(const struct ef800_workspace *workspace);
const char *ef800_workspace_get_name(const struct ef800_workspace *workspace);
const char *ef800_workspace_get_filename(
	const struct ef800_workspace *workspace);

#endif /* EF800_WORKSPACE_H */
