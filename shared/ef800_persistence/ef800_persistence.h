/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Provide validated file operations over interchangeable storage backends. */
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
	/* Shared opaque argument passed to every operation callback. */
	void *context;
	/* Read complete contents; writes the byte count through length. */
	int (*read)(void *context, enum ef800_persistence_root root,
		    const char *name, void *buffer, size_t capacity, size_t *length);
	/* Read up to capacity bytes beginning at offset. */
	int (*read_range)(void *context, enum ef800_persistence_root root,
			 const char *name, size_t offset, void *buffer,
			 size_t capacity, size_t *length);
	/* Replace file contents with the supplied byte range. */
	int (*write)(void *context, enum ef800_persistence_root root,
		     const char *name, const void *buffer, size_t length);
	/* Append the supplied byte range to a file. */
	int (*append)(void *context, enum ef800_persistence_root root,
		      const char *name, const void *buffer, size_t length);
	/* Read one entry's type, name, and size. */
	int (*stat)(void *context, enum ef800_persistence_root root,
		    const char *name, struct ef800_persistence_entry *entry);
	/* Enumerate one root; callback nonzero stops iteration. */
	int (*list)(void *context, enum ef800_persistence_root root,
		    ef800_persistence_list_callback callback, void *callback_context);
	/* Remove one file or empty directory. */
	int (*remove)(void *context, enum ef800_persistence_root root,
		      const char *name);
	/* Rename within one root. The destination MUST be absent. */
	int (*rename)(void *context, enum ef800_persistence_root root,
		      const char *source_name, const char *destination_name);
	/* Flush one existing file's contents to its backing storage. */
	int (*sync)(void *context, enum ef800_persistence_root root,
		    const char *name);
};

struct ef800_persistence {
	const struct ef800_persistence_backend *backend;
};

/** @brief Bind a backend to a persistence service.
 * @param storage Service state to initialize.
 * @param backend Complete backend callback table.
 * @return 0 on initialization; negative errno for invalid/missing callbacks.
 */
int ef800_persistence_init(struct ef800_persistence *storage,
			   const struct ef800_persistence_backend *backend);

/** @brief Validate a relative persistence filename.
 * @param name Name to validate.
 * @return True when nonempty and safe for the backend path rules.
 */
bool ef800_persistence_valid_name(const char *name);

/** @brief Read a complete file into a bounded buffer.
 * @param storage Initialized persistence service.
 * @param root Configuration or data root.
 * @param name Relative file name.
 * @param buffer Destination buffer.
 * @param capacity Destination capacity.
 * @param length Receives bytes read.
 * @return 0 on read; negative errno on invalid input, oversize, or backend failure.
 */
int ef800_persistence_read(struct ef800_persistence *storage,
			   enum ef800_persistence_root root, const char *name,
			   void *buffer, size_t capacity, size_t *length);

/** @brief Read a bounded range from a file.
 * @param storage Initialized persistence service.
 * @param root Storage root.
 * @param name Relative file name.
 * @param offset Starting byte offset.
 * @param buffer Destination buffer.
 * @param capacity Destination capacity.
 * @param length Receives bytes read.
 * @return 0 on read; negative errno on validation or backend failure.
 */
int ef800_persistence_read_range(struct ef800_persistence *storage,
				 enum ef800_persistence_root root, const char *name,
				 size_t offset, void *buffer, size_t capacity,
				 size_t *length);

/** @brief Replace a file's contents.
 * @param storage Initialized persistence service.
 * @param root Storage root.
 * @param name Relative file name.
 * @param buffer Source bytes.
 * @param length Number of bytes to write.
 * @return 0 on write; negative errno on validation or backend failure.
 */
int ef800_persistence_write(struct ef800_persistence *storage,
			    enum ef800_persistence_root root, const char *name,
			    const void *buffer, size_t length);

/** @brief Append bytes to a file.
 * @param storage Initialized persistence service.
 * @param root Storage root.
 * @param name Relative file name.
 * @param buffer Bytes to append.
 * @param length Byte count.
 * @return 0 on append; negative errno on validation or backend failure.
 */
int ef800_persistence_append(struct ef800_persistence *storage,
			     enum ef800_persistence_root root, const char *name,
			     const void *buffer, size_t length);

/** @brief Retrieve file/directory metadata.
 * @param storage Initialized persistence service.
 * @param root Storage root.
 * @param name Relative entry name.
 * @param entry Receives type, name, and size.
 * @return 0 on stat; negative errno when missing or backend fails.
 */
int ef800_persistence_stat(struct ef800_persistence *storage,
			   enum ef800_persistence_root root, const char *name,
			   struct ef800_persistence_entry *entry);

/** @brief Enumerate entries through a caller callback.
 * @param storage Initialized persistence service.
 * @param root Storage root.
 * @param callback Called for each entry; nonzero stops enumeration.
 * @param context Opaque callback argument.
 * @return 0 on completion; negative errno on backend or callback failure.
 */
int ef800_persistence_list(struct ef800_persistence *storage,
			   enum ef800_persistence_root root,
			   ef800_persistence_list_callback callback, void *context);

/** @brief Remove a file or empty directory.
 * @param storage Initialized persistence service.
 * @param root Storage root.
 * @param name Relative entry name.
 * @return 0 on removal; negative errno on validation or backend failure.
 */
int ef800_persistence_remove(struct ef800_persistence *storage,
			     enum ef800_persistence_root root, const char *name);

/** @brief Rename within one root; destination must not already exist.
 * @param storage Initialized persistence service.
 * @param root Storage root.
 * @param source_name Existing relative name.
 * @param destination_name New relative name.
 * @return 0 on rename; negative errno on validation or backend failure.
 */
int ef800_persistence_rename(struct ef800_persistence *storage,
			     enum ef800_persistence_root root,
			     const char *source_name, const char *destination_name);

/** @brief Flush a file's contents to backing storage.
 * @param storage Initialized persistence service.
 * @param root Storage root.
 * @param name Relative file name.
 * @return 0 on sync; negative errno on validation or backend failure.
 */
int ef800_persistence_sync(struct ef800_persistence *storage,
			   enum ef800_persistence_root root, const char *name);

/* Adapter for the existing Zephyr FAT mount service. */

/** @brief Get the backend adapter for the Zephyr FAT storage service.
 * @return Static backend callback table.
 */
const struct ef800_persistence_backend *ef800_persistence_zephyr_backend(void);

#endif /* EF800_PERSISTENCE_H */
