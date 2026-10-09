/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PERSISTENCE_H
#define EF800_PERSISTENCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EF800_PERSISTENCE_MAX_PATH 128U
#define EF800_PERSISTENCE_MAX_NAME 64U

enum ef800_persistence_root {
	EF800_PERSISTENCE_CONFIGURATION = 0,
	EF800_PERSISTENCE_DATA = 1,
};

enum ef800_persistence_entry_type {
	EF800_PERSISTENCE_FILE = 0,
	EF800_PERSISTENCE_DIRECTORY = 1,
};

struct ef800_persistence_entry {
	enum ef800_persistence_entry_type type;
	char name[EF800_PERSISTENCE_MAX_NAME + 1U];
	size_t size;
};

typedef int (*ef800_persistence_list_callback)(
	const struct ef800_persistence_entry *entry, void *context);

struct ef800_persistence_backend {
	void *context;
	int (*read)(void *context, enum ef800_persistence_root root,
		    const char *name, void *buffer, size_t capacity, size_t *length);
	int (*read_range)(void *context, enum ef800_persistence_root root,
			 const char *name, size_t offset, void *buffer,
			 size_t capacity, size_t *length);
	int (*write)(void *context, enum ef800_persistence_root root,
		     const char *name, const void *buffer, size_t length);
	int (*append)(void *context, enum ef800_persistence_root root,
		      const char *name, const void *buffer, size_t length);
	int (*stat)(void *context, enum ef800_persistence_root root,
		    const char *name, struct ef800_persistence_entry *entry);
	int (*list)(void *context, enum ef800_persistence_root root,
		    ef800_persistence_list_callback callback, void *callback_context);
	int (*remove)(void *context, enum ef800_persistence_root root,
		      const char *name);
};

struct ef800_persistence {
	const struct ef800_persistence_backend *backend;
};

int ef800_persistence_init(struct ef800_persistence *storage,
			   const struct ef800_persistence_backend *backend);

bool ef800_persistence_valid_name(const char *name);

int ef800_persistence_read(struct ef800_persistence *storage,
			   enum ef800_persistence_root root, const char *name,
			   void *buffer, size_t capacity, size_t *length);
int ef800_persistence_read_range(struct ef800_persistence *storage,
				 enum ef800_persistence_root root, const char *name,
				 size_t offset, void *buffer, size_t capacity,
				 size_t *length);
int ef800_persistence_write(struct ef800_persistence *storage,
			    enum ef800_persistence_root root, const char *name,
			    const void *buffer, size_t length);
int ef800_persistence_append(struct ef800_persistence *storage,
			     enum ef800_persistence_root root, const char *name,
			     const void *buffer, size_t length);
int ef800_persistence_stat(struct ef800_persistence *storage,
			   enum ef800_persistence_root root, const char *name,
			   struct ef800_persistence_entry *entry);
int ef800_persistence_list(struct ef800_persistence *storage,
			   enum ef800_persistence_root root,
			   ef800_persistence_list_callback callback, void *context);
int ef800_persistence_remove(struct ef800_persistence *storage,
			     enum ef800_persistence_root root, const char *name);

/* Adapter for the existing Zephyr FAT mount service. */
const struct ef800_persistence_backend *ef800_persistence_zephyr_backend(void);

#endif /* EF800_PERSISTENCE_H */
