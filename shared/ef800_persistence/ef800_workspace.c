/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_workspace.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(ef800_workspace, LOG_LEVEL_DBG);

static int workspace_transaction_recover(struct ef800_workspace *workspace);

static bool has_suffix(const char *value, const char *suffix)
{
	size_t value_length = strlen(value);
	size_t suffix_length = strlen(suffix);

	return value_length >= suffix_length &&
	       strcmp(value + value_length - suffix_length, suffix) == 0;
}

static int workspace_name_valid(const char *name)
{
	if (name == NULL || name[0] == '\0' || strlen(name) > EF800_WORKSPACE_NAME_MAX ||
	    strchr(name, '/') != NULL || strcmp(name, ".") == 0 ||
	    strcmp(name, "..") == 0 || has_suffix(name, EF800_WORKSPACE_EXTENSION)) {
		return -EINVAL;
	}
	return ef800_persistence_valid_name(name) ? 0 : -EINVAL;
}

static int make_filename(const char *name, char *filename, size_t size)
{
	int length;

	if (workspace_name_valid(name) != 0 || filename == NULL || size == 0U) {
		return -EINVAL;
	}
	length = snprintk(filename, size, "%s%s", name, EF800_WORKSPACE_EXTENSION);
	if (length < 0 || (size_t)length >= size) {
		return -ENAMETOOLONG;
	}
	return 0;
}

static int select_filename(struct ef800_workspace *workspace,
			    const char *filename, size_t length)
{
	char name[EF800_WORKSPACE_NAME_MAX + 1U];
	size_t extension_length = sizeof(EF800_WORKSPACE_EXTENSION) - 1U;

	if (length <= extension_length || length > EF800_PERSISTENCE_MAX_NAME ||
	    memcmp(filename + length - extension_length, EF800_WORKSPACE_EXTENSION,
		   extension_length) != 0 ||
	    length - extension_length > EF800_WORKSPACE_NAME_MAX) {
		return -EINVAL;
	}
	memcpy(name, filename, length - extension_length);
	name[length - extension_length] = '\0';
	if (workspace_name_valid(name) != 0) {
		return -EINVAL;
	}
	memcpy(workspace->name, name, strlen(name) + 1U);
	memcpy(workspace->filename, filename, length);
	workspace->filename[length] = '\0';
	workspace->selected = true;
	return 0;
}

static int save_current(struct ef800_workspace *workspace,
			const char *filename)
{
	return ef800_persistence_write(workspace->storage,
				       EF800_PERSISTENCE_CONFIGURATION,
				       EF800_WORKSPACE_CONFIGURATION_FILE, filename,
			       strlen(filename));
}

static void workspace_reset_context(struct ef800_workspace *workspace)
{
	workspace->current_point_number = 0;
	workspace->current_code[0] = '\0';
}

static void workspace_update_context_from_line(struct ef800_workspace *workspace,
	const char *line)
{
	char copy[EF800_MEASUREMENT_TEXT_MAX + 96U];
	char *token;
	char *last = NULL;
	long point = 0;
	unsigned int field = 0U;
	char *end;

	if (workspace == NULL || line == NULL || ef800_workspace_line_is_position(line) ||
		strlen(line) >= sizeof(copy)) {
		return;
	}
	strcpy(copy, line);
	point = strtol(copy, &end, 10);
	if (end == copy) {
		return;
	}
	token = strtok(copy, " \t\r\n");
	while (token != NULL) {
		last = token;
		++field;
		token = strtok(NULL, " \t\r\n");
	}
	if (field < 6U || last == NULL || point <= 0 || point > INT32_MAX) {
		return;
	}
	workspace->current_point_number = (int32_t)point;
	strncpy(workspace->current_code, last, sizeof(workspace->current_code) - 1U);
	workspace->current_code[sizeof(workspace->current_code) - 1U] = '\0';
}

static void workspace_extract_context(struct ef800_workspace *workspace)
{
	uint8_t buffer[EF800_WORKSPACE_SNAPSHOT_MAX];
	size_t file_size;
	size_t start;
	size_t length;
	size_t line_length = 0U;
	char line[EF800_MEASUREMENT_TEXT_MAX + 96U];

	workspace_reset_context(workspace);
	if (ef800_workspace_size(workspace, &file_size) != 0 || file_size == 0U) {
		return;
	}
	start = file_size > sizeof(buffer) ? file_size - sizeof(buffer) : 0U;
	if (ef800_workspace_read_range(workspace, start, file_size, buffer,
			sizeof(buffer), &length) != 0) {
		return;
	}
	for (size_t index = 0U; index < length; ++index) {
		char character = (char)buffer[index];

		if (start != 0U && index == 0U) {
			while (index < length && buffer[index] != '\n') {
				++index;
			}
			continue;
		}
		if (character == '\n' || character == '\r') {
			line[line_length] = '\0';
			workspace_update_context_from_line(workspace, line);
			line_length = 0U;
		} else if (line_length + 1U < sizeof(line)) {
			line[line_length++] = character;
		}
	}
	if (line_length != 0U) {
		line[line_length] = '\0';
		workspace_update_context_from_line(workspace, line);
	}
}

int ef800_workspace_init(struct ef800_workspace *workspace,
			 struct ef800_persistence *storage)
{
	if (workspace == NULL || storage == NULL || storage->backend == NULL) {
		return -EINVAL;
	}
	memset(workspace, 0, sizeof(*workspace));
	workspace->storage = storage;
	return 0;
}

int ef800_workspace_load_current(struct ef800_workspace *workspace)
{
	char filename[EF800_PERSISTENCE_MAX_NAME + 1U];
	struct ef800_persistence_entry entry;
	size_t length;
	int ret;

	if (workspace == NULL || workspace->storage == NULL) {
		return -EINVAL;
	}
	ret = ef800_persistence_read(workspace->storage,
				     EF800_PERSISTENCE_CONFIGURATION,
				     EF800_WORKSPACE_CONFIGURATION_FILE, filename,
				     sizeof(filename) - 1U, &length);
	if (ret != 0) {
		return ret;
	}
	while (length > 0U && (filename[length - 1U] == '\r' ||
				       filename[length - 1U] == '\n')) {
		--length;
	}
	if (length == 0U) {
		return -EINVAL;
	}
	filename[length] = '\0';
	ret = select_filename(workspace, filename, length);
	if (ret != 0) {
		return ret;
	}
	ret = workspace_transaction_recover(workspace);
	if (ret != 0) {
		workspace->selected = false;
		return ret;
	}
	ret = ef800_persistence_stat(workspace->storage, EF800_PERSISTENCE_DATA,
					 workspace->filename, &entry);
	if (ret != 0) {
		workspace->selected = false;
		return ret;
	}
	if (entry.type != EF800_PERSISTENCE_FILE) {
		workspace->selected = false;
		return -EISDIR;
	}
	workspace_extract_context(workspace);
	return 0;
}

int ef800_workspace_create(struct ef800_workspace *workspace, const char *name)
{
	struct ef800_workspace candidate;
	struct ef800_persistence_entry entry;
	char filename[sizeof(workspace->filename)];
	int ret;

	if (workspace == NULL || workspace->storage == NULL) {
		return -EINVAL;
	}
	ret = make_filename(name, filename, sizeof(filename));
	if (ret != 0) {
		return ret;
	}
	candidate = *workspace;
	ret = select_filename(&candidate, filename, strlen(filename));
	if (ret != 0) {
		return ret;
	}
	ret = workspace_transaction_recover(&candidate);
	if (ret != 0) {
		return ret;
	}
	ret = ef800_persistence_stat(workspace->storage, EF800_PERSISTENCE_DATA,
					 filename, &entry);
	if (ret == 0) {
		return -EEXIST;
	}
	if (ret != -ENOENT) {
		return ret;
	}
	ret = ef800_persistence_write(workspace->storage, EF800_PERSISTENCE_DATA,
				       filename, NULL, 0U);
	if (ret != 0) {
		return ret;
	}
	ret = save_current(workspace, filename);
	if (ret != 0) {
		(void)ef800_persistence_remove(workspace->storage,
						EF800_PERSISTENCE_DATA, filename);
		return ret;
	}
	ret = select_filename(workspace, filename, strlen(filename));
	if (ret == 0) {
		workspace_reset_context(workspace);
	}
	return ret;
}

int ef800_workspace_select(struct ef800_workspace *workspace, const char *name)
{
	struct ef800_workspace candidate;
	struct ef800_persistence_entry entry;
	char filename[sizeof(workspace->filename)];
	int ret;

	if (workspace == NULL || workspace->storage == NULL) {
		return -EINVAL;
	}
	ret = make_filename(name, filename, sizeof(filename));
	if (ret != 0) {
		return ret;
	}
	candidate = *workspace;
	ret = select_filename(&candidate, filename, strlen(filename));
	if (ret != 0) {
		return ret;
	}
	ret = workspace_transaction_recover(&candidate);
	if (ret != 0) {
		return ret;
	}
	ret = ef800_persistence_stat(workspace->storage, EF800_PERSISTENCE_DATA,
					 filename, &entry);
	if (ret != 0) {
		return ret;
	}
	if (entry.type != EF800_PERSISTENCE_FILE) {
		return -EISDIR;
	}
	ret = save_current(workspace, filename);
	if (ret != 0) {
		return ret;
	}
	*workspace = candidate;
	workspace_extract_context(workspace);
	return 0;
}

int ef800_workspace_delete_selected(struct ef800_workspace *workspace)
{
	struct ef800_persistence *storage;
	char filename[sizeof(workspace->filename)];
	int result;

	if (workspace == NULL || workspace->storage == NULL || !workspace->selected) {
		return -ENODEV;
	}
	storage = workspace->storage;
	strcpy(filename, workspace->filename);
	result = ef800_persistence_remove(storage, EF800_PERSISTENCE_DATA, filename);
	if (result != 0) {
		return result;
	}
	(void)ef800_persistence_remove(storage, EF800_PERSISTENCE_CONFIGURATION,
		EF800_WORKSPACE_CONFIGURATION_FILE);
	memset(workspace, 0, sizeof(*workspace));
	workspace->storage = storage;
	return 0;
}

int ef800_workspace_append(struct ef800_workspace *workspace,
			   const void *data, size_t length)
{
	int ret;

	if (workspace == NULL || workspace->storage == NULL ||
	    !workspace->selected || (data == NULL && length != 0U)) {
		return workspace != NULL && workspace->storage != NULL &&
		       !workspace->selected ? -ENODEV : -EINVAL;
	}
	ret = ef800_persistence_append(workspace->storage, EF800_PERSISTENCE_DATA,
			workspace->filename, data, length);
	if (ret == 0 && data != NULL && length > 0U) {
		LOG_INF("Workspace %s: appended new line (%u bytes)",
			workspace->filename, (unsigned int)length);
		LOG_DBG("Workspace %s new line: %.*s", workspace->filename,
			(int)MIN(length, (size_t)INT_MAX), (const char *)data);
	}
	if (ret == 0 && data != NULL && length > 0U) {
		char line[EF800_MEASUREMENT_TEXT_MAX + 96U];
		size_t copy_length = MIN(length, sizeof(line) - 1U);

		memcpy(line, data, copy_length);
		line[copy_length] = '\0';
		workspace_update_context_from_line(workspace, line);
	}
	return ret;
}

int ef800_workspace_snapshot(const struct ef800_workspace *workspace,
	void *destination, size_t capacity, size_t *length)
{
	struct ef800_persistence_entry entry;
	int ret;

	if (workspace == NULL || workspace->storage == NULL || destination == NULL ||
		length == NULL || !workspace->selected) {
		return workspace != NULL && workspace->storage != NULL &&
			!workspace->selected ? -ENODEV : -EINVAL;
	}
	*length = 0U;
	ret = ef800_persistence_stat(workspace->storage, EF800_PERSISTENCE_DATA,
		workspace->filename, &entry);
	if (ret != 0) {
		return ret;
	}
	if (entry.type != EF800_PERSISTENCE_FILE) {
		return -EISDIR;
	}
	if (entry.size > capacity) {
		return -EFBIG;
	}
	ret = ef800_persistence_read(workspace->storage, EF800_PERSISTENCE_DATA,
		workspace->filename, destination, capacity, length);
	if (ret != 0) {
		return ret;
	}
	return *length == entry.size ? 0 : -EIO;
}

int ef800_workspace_read_range(const struct ef800_workspace *workspace,
				      size_t start, size_t end, void *destination,
				      size_t capacity, size_t *length)
{
	if (workspace == NULL || workspace->storage == NULL ||
	    !workspace->selected || destination == NULL || length == NULL ||
	    end < start || end - start > capacity) {
		return workspace != NULL && workspace->storage != NULL &&
		       !workspace->selected ? -ENODEV : -EINVAL;
	}
	return ef800_persistence_read_range(workspace->storage, EF800_PERSISTENCE_DATA,
					    workspace->filename, start, destination,
					    end - start, length);
}

int ef800_workspace_size(const struct ef800_workspace *workspace, size_t *size)
{
	struct ef800_persistence_entry entry;
	int ret;

	if (workspace == NULL || workspace->storage == NULL || size == NULL ||
	    !workspace->selected) {
		return workspace != NULL && workspace->storage != NULL &&
		       !workspace->selected ? -ENODEV : -EINVAL;
	}
	ret = ef800_persistence_stat(workspace->storage, EF800_PERSISTENCE_DATA,
					     workspace->filename, &entry);
	if (ret != 0) {
		return ret;
	}
	if (entry.type != EF800_PERSISTENCE_FILE) {
		return -EISDIR;
	}
	*size = entry.size;
	return 0;
}

#define WORKSPACE_ROW_SCAN_CHUNK 128U
#define WORKSPACE_EDIT_COPY_CHUNK 256U
#define WORKSPACE_TXN_RECORD_SIZE 48U
#define WORKSPACE_TXN_RECORD_CHECKSUM_OFFSET 44U

enum workspace_transaction_phase {
	WORKSPACE_TXN_PREPARED = 1U,
	WORKSPACE_TXN_BACKED_UP = 2U,
	WORKSPACE_TXN_COMMITTED = 3U,
};

struct workspace_transaction {
	uint32_t generation;
	enum workspace_transaction_phase phase;
	size_t old_size;
	uint64_t old_fingerprint;
	size_t new_size;
	uint64_t new_fingerprint;
};

int ef800_workspace_row_at(const struct ef800_workspace *workspace, size_t start,
			   struct ef800_workspace_row *row)
{
	uint8_t buffer[WORKSPACE_ROW_SCAN_CHUNK];
	size_t file_size;
	size_t offset;
	int ret;

	if (row == NULL) {
		return -EINVAL;
	}
	ret = ef800_workspace_size(workspace, &file_size);
	if (ret != 0) {
		return ret;
	}
	if (start >= file_size) {
		return -ENOENT;
	}

	row->start = start;
	row->text_end = file_size;
	row->end = file_size;
	row->ending_length = 0U;
	for (offset = start; offset < file_size;) {
		size_t requested = MIN(sizeof(buffer), file_size - offset);
		size_t length;

		ret = ef800_workspace_read_range(workspace, offset, offset + requested,
						 buffer, sizeof(buffer), &length);
		if (ret != 0) {
			return ret;
		}
		if (length != requested) {
			return -EIO;
		}
		for (size_t index = 0U; index < length; ++index) {
			uint8_t character = buffer[index];
			size_t delimiter = offset + index;

			if (character == '\n') {
				row->text_end = delimiter;
				row->end = delimiter + 1U;
				row->ending_length = 1U;
				return 0;
			}
			if (character == '\r') {
				bool followed_by_lf = false;

				if (delimiter + 1U < file_size) {
					if (index + 1U < length) {
						followed_by_lf = buffer[index + 1U] == '\n';
					} else {
						uint8_t next;
					size_t next_length;

						ret = ef800_workspace_read_range(workspace,
							delimiter + 1U, delimiter + 2U, &next, 1U,
							&next_length);
						if (ret != 0) {
							return ret;
						}
						if (next_length != 1U) {
							return -EIO;
						}
						followed_by_lf = next == '\n';
					}
				}
				row->text_end = delimiter;
				row->ending_length = followed_by_lf ? 2U : 1U;
				row->end = delimiter + row->ending_length;
				return 0;
			}
		}
		offset += length;
	}
	return 0;
}

int ef800_workspace_row_before(const struct ef800_workspace *workspace,
			       size_t row_start,
			       struct ef800_workspace_row *row)
{
	uint8_t buffer[WORKSPACE_ROW_SCAN_CHUNK];
	size_t file_size;
	size_t delimiter_start;
	size_t search_end;
	size_t row_start_offset = 0U;
	uint8_t previous;
	size_t length;
	int ret;

	if (row == NULL) {
		return -EINVAL;
	}
	ret = ef800_workspace_size(workspace, &file_size);
	if (ret != 0) {
		return ret;
	}
	if (row_start == 0U || row_start > file_size) {
		return -ENOENT;
	}
	ret = ef800_workspace_read_range(workspace, row_start - 1U, row_start,
					 &previous, 1U, &length);
	if (ret != 0) {
		return ret;
	}
	if (length != 1U) {
		return -EIO;
	}
	if (previous == '\n') {
		delimiter_start = row_start - 1U;
		if (delimiter_start > 0U) {
			ret = ef800_workspace_read_range(workspace, delimiter_start - 1U,
							 delimiter_start, &previous, 1U,
							 &length);
			if (ret != 0) {
				return ret;
			}
			if (length != 1U) {
				return -EIO;
			}
			if (previous == '\r') {
				--delimiter_start;
			}
		}
	} else if (previous == '\r') {
		delimiter_start = row_start - 1U;
	} else {
		return -EINVAL;
	}

	search_end = delimiter_start;
	while (search_end > 0U) {
		size_t chunk_start = search_end > sizeof(buffer) ?
			search_end - sizeof(buffer) : 0U;

		ret = ef800_workspace_read_range(workspace, chunk_start, search_end,
						 buffer, sizeof(buffer), &length);
		if (ret != 0) {
			return ret;
		}
		if (length != search_end - chunk_start) {
			return -EIO;
		}
		for (size_t index = length; index > 0U; --index) {
			if (buffer[index - 1U] == '\r' || buffer[index - 1U] == '\n') {
				row_start_offset = chunk_start + index;
				search_end = 0U;
				break;
			}
		}
		if (search_end != 0U) {
			search_end = chunk_start;
		}
	}

	row->start = row_start_offset;
	row->text_end = delimiter_start;
	row->end = row_start;
	row->ending_length = (uint8_t)(row_start - delimiter_start);
	return 0;
}

static int workspace_edit_artifact_name(const struct ef800_workspace *workspace,
					const char *suffix, char *name,
					size_t capacity)
{
	int length;

	if (workspace == NULL || !workspace->selected || suffix == NULL ||
	    name == NULL || capacity == 0U) {
		return -EINVAL;
	}
	length = snprintk(name, capacity, ".ef800-edit-%s-%s",
			  workspace->name, suffix);
	return length < 0 || (size_t)length >= capacity ? -ENAMETOOLONG : 0;
}

static uint64_t workspace_fingerprint_update(uint64_t fingerprint,
					     const uint8_t *bytes, size_t length)
{
	for (size_t index = 0U; index < length; ++index) {
		fingerprint ^= bytes[index];
		fingerprint *= UINT64_C(1099511628211);
	}
	return fingerprint;
}

static int workspace_file_fingerprint(struct ef800_persistence *storage,
				      const char *filename, size_t *file_size,
				      uint64_t *fingerprint)
{
	uint8_t buffer[WORKSPACE_EDIT_COPY_CHUNK];
	struct ef800_persistence_entry entry;
	size_t offset = 0U;
	uint64_t hash = UINT64_C(14695981039346656037);
	int ret;

	if (storage == NULL || filename == NULL || file_size == NULL ||
	    fingerprint == NULL) {
		return -EINVAL;
	}
	ret = ef800_persistence_stat(storage, EF800_PERSISTENCE_DATA, filename, &entry);
	if (ret != 0) {
		return ret;
	}
	if (entry.type != EF800_PERSISTENCE_FILE) {
		return -EISDIR;
	}
	while (offset < entry.size) {
		size_t requested = MIN(sizeof(buffer), entry.size - offset);
		size_t length;

		ret = ef800_persistence_read_range(storage, EF800_PERSISTENCE_DATA,
			filename, offset, buffer, requested, &length);
		if (ret != 0) {
			return ret;
		}
		if (length != requested) {
			return -EIO;
		}
		hash = workspace_fingerprint_update(hash, buffer, length);
		offset += length;
	}
	*file_size = entry.size;
	*fingerprint = hash;
	return 0;
}

int ef800_workspace_get_version(const struct ef800_workspace *workspace,
				struct ef800_workspace_version *version)
{
	if (workspace == NULL || workspace->storage == NULL ||
	    version == NULL || !workspace->selected) {
		return workspace != NULL && workspace->storage != NULL &&
		       !workspace->selected ? -ENODEV : -EINVAL;
	}
	return workspace_file_fingerprint(workspace->storage, workspace->filename,
					  &version->size, &version->fingerprint);
}

static void workspace_transaction_encode(const struct workspace_transaction *txn,
					uint8_t bytes[WORKSPACE_TXN_RECORD_SIZE])
{
	memset(bytes, 0, WORKSPACE_TXN_RECORD_SIZE);
	memcpy(bytes, "WSE1", 4U);
	sys_put_le32(txn->generation, &bytes[4]);
	bytes[8] = (uint8_t)txn->phase;
	sys_put_le64((uint64_t)txn->old_size, &bytes[12]);
	sys_put_le64(txn->old_fingerprint, &bytes[20]);
	sys_put_le64((uint64_t)txn->new_size, &bytes[28]);
	sys_put_le64(txn->new_fingerprint, &bytes[36]);
	sys_put_le32(ef800_crc32(bytes, WORKSPACE_TXN_RECORD_CHECKSUM_OFFSET),
		     &bytes[WORKSPACE_TXN_RECORD_CHECKSUM_OFFSET]);
}

static int workspace_transaction_decode(const uint8_t *bytes, size_t length,
					struct workspace_transaction *txn)
{
	uint64_t old_size;
	uint64_t new_size;

	if (bytes == NULL || txn == NULL || length != WORKSPACE_TXN_RECORD_SIZE ||
	    memcmp(bytes, "WSE1", 4U) != 0 || bytes[9] != 0U || bytes[10] != 0U ||
	    bytes[11] != 0U || sys_get_le32(&bytes[WORKSPACE_TXN_RECORD_CHECKSUM_OFFSET]) !=
				       ef800_crc32(bytes,
						   WORKSPACE_TXN_RECORD_CHECKSUM_OFFSET)) {
		return -EBADMSG;
	}
	txn->generation = sys_get_le32(&bytes[4]);
	txn->phase = (enum workspace_transaction_phase)bytes[8];
	old_size = sys_get_le64(&bytes[12]);
	new_size = sys_get_le64(&bytes[28]);
	if (txn->generation == 0U || txn->phase < WORKSPACE_TXN_PREPARED ||
	    txn->phase > WORKSPACE_TXN_COMMITTED || old_size > SIZE_MAX ||
	    new_size > SIZE_MAX) {
		return -EBADMSG;
	}
	txn->old_size = (size_t)old_size;
	txn->old_fingerprint = sys_get_le64(&bytes[20]);
	txn->new_size = (size_t)new_size;
	txn->new_fingerprint = sys_get_le64(&bytes[36]);
	return 0;
}

static int workspace_transaction_read_slot(struct ef800_workspace *workspace,
					   const char *suffix,
					   struct workspace_transaction *txn,
					   bool *exists)
{
	uint8_t bytes[WORKSPACE_TXN_RECORD_SIZE];
	char name[EF800_PERSISTENCE_MAX_NAME + 1U];
	size_t length;
	int ret;

	ret = workspace_edit_artifact_name(workspace, suffix, name, sizeof(name));
	if (ret != 0) {
		return ret;
	}
	ret = ef800_persistence_read(workspace->storage, EF800_PERSISTENCE_DATA,
		name, bytes, sizeof(bytes), &length);
	if (ret == -ENOENT) {
		*exists = false;
		return ret;
	}
	*exists = true;
	if (ret != 0) {
		return ret;
	}
	return workspace_transaction_decode(bytes, length, txn);
}

static int workspace_transaction_write_slot(struct ef800_workspace *workspace,
					    const struct workspace_transaction *txn)
{
	uint8_t bytes[WORKSPACE_TXN_RECORD_SIZE];
	char suffix[4];
	char name[EF800_PERSISTENCE_MAX_NAME + 1U];
	int ret;

	snprintk(suffix, sizeof(suffix), "j%u", (unsigned)((txn->generation - 1U) % 2U));
	ret = workspace_edit_artifact_name(workspace, suffix, name, sizeof(name));
	if (ret != 0) {
		return ret;
	}
	workspace_transaction_encode(txn, bytes);
	ret = ef800_persistence_write(workspace->storage, EF800_PERSISTENCE_DATA,
		name, bytes, sizeof(bytes));
	if (ret != 0) {
		return ret;
	}
	return ef800_persistence_sync(workspace->storage, EF800_PERSISTENCE_DATA, name);
}

static int workspace_edit_remove_if_present(struct ef800_workspace *workspace,
					    const char *suffix)
{
	struct ef800_persistence_entry entry;
	char name[EF800_PERSISTENCE_MAX_NAME + 1U];
	int ret;

	ret = workspace_edit_artifact_name(workspace, suffix, name, sizeof(name));
	if (ret != 0) {
		return ret;
	}
	ret = ef800_persistence_stat(workspace->storage, EF800_PERSISTENCE_DATA,
		name, &entry);
	if (ret == -ENOENT) {
		return 0;
	}
	if (ret != 0) {
		return ret;
	}
	if (entry.type != EF800_PERSISTENCE_FILE) {
		return -EISDIR;
	}
	return ef800_persistence_remove(workspace->storage,
		EF800_PERSISTENCE_DATA, name);
}

static int workspace_edit_file_matches(struct ef800_workspace *workspace,
				       const char *filename, size_t expected_size,
				       uint64_t expected_fingerprint, bool *matches)
{
	size_t actual_size;
	uint64_t actual_fingerprint;
	int ret = workspace_file_fingerprint(workspace->storage, filename,
					     &actual_size, &actual_fingerprint);

	if (ret == -ENOENT) {
		*matches = false;
		return 0;
	}
	if (ret != 0) {
		return ret;
	}
	*matches = actual_size == expected_size &&
		actual_fingerprint == expected_fingerprint;
	return 0;
}

static int workspace_edit_file_exists(struct ef800_workspace *workspace,
				      const char *filename, bool *exists)
{
	struct ef800_persistence_entry entry;
	int ret = ef800_persistence_stat(workspace->storage, EF800_PERSISTENCE_DATA,
					 filename, &entry);

	if (ret == -ENOENT) {
		*exists = false;
		return 0;
	}
	if (ret != 0) {
		return ret;
	}
	if (entry.type != EF800_PERSISTENCE_FILE) {
		return -EISDIR;
	}
	*exists = true;
	return 0;
}

static int workspace_transaction_recover(struct ef800_workspace *workspace)
{
	struct workspace_transaction slots[2];
	struct workspace_transaction *txn = NULL;
	char backup_name[EF800_PERSISTENCE_MAX_NAME + 1U];
	bool slot_exists[2] = {false, false};
	bool slot_valid[2] = {false, false};
	bool backup_exists;
	bool source_exists;
	bool source_old = false;
	bool source_new = false;
	bool backup_old = false;
	int slot_result[2];
	int ret;

	if (workspace == NULL || workspace->storage == NULL || !workspace->selected) {
		return -EINVAL;
	}
	ret = workspace_edit_artifact_name(workspace, "bak", backup_name,
					   sizeof(backup_name));
	if (ret != 0) {
		return ret;
	}
	slot_result[0] = workspace_transaction_read_slot(workspace, "j0", &slots[0],
							 &slot_exists[0]);
	slot_result[1] = workspace_transaction_read_slot(workspace, "j1", &slots[1],
							 &slot_exists[1]);
	for (size_t index = 0U; index < ARRAY_SIZE(slots); ++index) {
		if (slot_result[index] == 0) {
			slot_valid[index] = true;
		} else if (slot_result[index] != -ENOENT &&
			   slot_result[index] != -EBADMSG) {
			return slot_result[index];
		}
	}
	ret = workspace_edit_file_exists(workspace, backup_name, &backup_exists);
	if (ret != 0) {
		return ret;
	}
	ret = workspace_edit_file_exists(workspace, workspace->filename, &source_exists);
	if (ret != 0) {
		return ret;
	}
	if (!slot_valid[0] && !slot_valid[1]) {
		if (backup_exists || !source_exists) {
			if (slot_exists[0] || slot_exists[1] || backup_exists) {
				return -EBADMSG;
			}
			return 0;
		}
		/* No destructive rename can occur before a valid PREPARED slot. */
		ret = workspace_edit_remove_if_present(workspace, "tmp");
		if (ret == 0 && slot_exists[0]) {
			ret = workspace_edit_remove_if_present(workspace, "j0");
		}
		if (ret == 0 && slot_exists[1]) {
			ret = workspace_edit_remove_if_present(workspace, "j1");
		}
		return ret;
	}
	if (slot_valid[0] && slot_valid[1]) {
		txn = slots[0].generation >= slots[1].generation ? &slots[0] : &slots[1];
	} else {
		txn = slot_valid[0] ? &slots[0] : &slots[1];
	}
	ret = workspace_edit_file_matches(workspace, workspace->filename,
		txn->old_size, txn->old_fingerprint, &source_old);
	if (ret != 0) {
		return ret;
	}
	ret = workspace_edit_file_matches(workspace, workspace->filename,
		txn->new_size, txn->new_fingerprint, &source_new);
	if (ret != 0) {
		return ret;
	}
	if (backup_exists) {
		ret = workspace_edit_file_matches(workspace, backup_name,
			txn->old_size, txn->old_fingerprint, &backup_old);
		if (ret != 0) {
			return ret;
		}
		if (!backup_old) {
			return -EBADMSG;
		}
	}

	switch (txn->phase) {
	case WORKSPACE_TXN_PREPARED:
	case WORKSPACE_TXN_BACKED_UP:
		if (backup_exists) {
			if (source_exists && !source_old) {
				if (!source_new) {
					return -EEXIST;
				}
				ret = ef800_persistence_remove(workspace->storage,
					EF800_PERSISTENCE_DATA, workspace->filename);
				if (ret != 0) {
					return ret;
			}
			}
			if (!source_old) {
				ret = ef800_persistence_rename(workspace->storage,
					EF800_PERSISTENCE_DATA, backup_name,
					workspace->filename);
				if (ret != 0) {
					return ret;
				}
				backup_exists = false;
			}
		} else if (!source_old) {
			return -EBADMSG;
		}
		break;
	case WORKSPACE_TXN_COMMITTED:
		if (source_exists && !source_new && !source_old) {
			return -EEXIST;
		}
		if (!source_exists) {
			if (!backup_exists || !backup_old) {
				return -EBADMSG;
			}
			ret = ef800_persistence_rename(workspace->storage,
				EF800_PERSISTENCE_DATA, backup_name, workspace->filename);
			if (ret != 0) {
				return ret;
			}
			backup_exists = false;
		}
		break;
	default:
		return -EBADMSG;
	}

	if (backup_exists) {
		ret = workspace_edit_remove_if_present(workspace, "bak");
		if (ret != 0) {
			return ret;
		}
	}
	ret = workspace_edit_remove_if_present(workspace, "tmp");
	if (ret == 0) {
		ret = workspace_edit_remove_if_present(workspace, "j0");
	}
	if (ret == 0) {
		ret = workspace_edit_remove_if_present(workspace, "j1");
	}
	return ret;
}

static int workspace_edit_append(struct ef800_workspace *workspace,
				 const char *temporary_name, const void *bytes,
				 size_t length, size_t *new_size,
				uint64_t *new_fingerprint)
{
	int ret;

	if (length == 0U) {
		return 0;
	}
	ret = ef800_persistence_append(workspace->storage, EF800_PERSISTENCE_DATA,
		temporary_name, bytes, length);
	if (ret != 0) {
		return ret;
	}
	if (SIZE_MAX - *new_size < length) {
		return -EOVERFLOW;
	}
	*new_size += length;
	*new_fingerprint = workspace_fingerprint_update(*new_fingerprint,
		bytes, length);
	return 0;
}

static int workspace_edit_copy_range(struct ef800_workspace *workspace,
				     const char *temporary_name, size_t start,
				     size_t end, size_t *new_size,
				     uint64_t *new_fingerprint)
{
	uint8_t buffer[WORKSPACE_EDIT_COPY_CHUNK];

	if (end < start) {
		return -EINVAL;
	}
	while (start < end) {
		size_t requested = MIN(sizeof(buffer), end - start);
		size_t length;
		int ret = ef800_workspace_read_range(workspace, start, start + requested,
						     buffer, sizeof(buffer), &length);

		if (ret != 0) {
			return ret;
		}
		if (length != requested) {
			return -EIO;
		}
		ret = workspace_edit_append(workspace, temporary_name, buffer, length,
					     new_size, new_fingerprint);
		if (ret != 0) {
			return ret;
		}
		start += length;
	}
	return 0;
}

static int workspace_transaction_commit_phase(struct ef800_workspace *workspace,
					      struct workspace_transaction *txn,
					      enum workspace_transaction_phase phase)
{
	if (txn->generation == UINT32_MAX) {
		return -EOVERFLOW;
	}
	++txn->generation;
	txn->phase = phase;
	return workspace_transaction_write_slot(workspace, txn);
}

int ef800_workspace_replace_row(struct ef800_workspace *workspace,
				const struct ef800_workspace_version *expected_version,
				const struct ef800_workspace_row *row,
				const void *replacement, size_t replacement_length,
				bool delete_row)
{
	struct ef800_workspace_row current_row;
	struct workspace_transaction txn;
	struct ef800_persistence_entry staged_entry;
	char temporary_name[EF800_PERSISTENCE_MAX_NAME + 1U];
	char backup_name[EF800_PERSISTENCE_MAX_NAME + 1U];
	uint8_t ending[EF800_WORKSPACE_ROW_ENDING_MAX];
	size_t file_size;
	size_t old_size;
	size_t staged_size = 0U;
	size_t ending_length = 0U;
	uint64_t old_fingerprint;
	uint64_t staged_fingerprint = UINT64_C(14695981039346656037);
	uint64_t verified_fingerprint;
	size_t verified_size;
	bool journal_started = false;
	int ret;

	if (workspace == NULL || workspace->storage == NULL ||
	    expected_version == NULL || row == NULL ||
	    !workspace->selected || (replacement == NULL && replacement_length != 0U)) {
		return workspace != NULL && workspace->storage != NULL &&
		       !workspace->selected ? -ENODEV : -EINVAL;
	}
	if (!delete_row && replacement_length > 255U) {
		return -EFBIG;
	}
	if (!delete_row) {
		const uint8_t *characters = replacement;

		for (size_t index = 0U; index < replacement_length; ++index) {
			if (characters[index] == '\r' || characters[index] == '\n' ||
			    characters[index] == '\0') {
				return -EINVAL;
			}
		}
	}
	if (row->end < row->text_end || row->text_end < row->start ||
	    row->end - row->text_end != row->ending_length ||
	    row->ending_length > EF800_WORKSPACE_ROW_ENDING_MAX) {
		return -EINVAL;
	}
	ret = workspace_transaction_recover(workspace);
	if (ret != 0) {
		return ret;
	}
	ret = ef800_workspace_size(workspace, &file_size);
	if (ret != 0) {
		return ret;
	}
	if (row->end > file_size || row->start == row->end) {
		return -EINVAL;
	}
	ret = ef800_workspace_row_at(workspace, row->start, &current_row);
	if (ret != 0) {
		return ret;
	}
	if (current_row.start != row->start || current_row.text_end != row->text_end ||
	    current_row.end != row->end ||
	    current_row.ending_length != row->ending_length) {
		return -EAGAIN;
	}
	if (!delete_row && row->text_end - row->start > 255U) {
		return -EFBIG;
	}
	ret = workspace_file_fingerprint(workspace->storage, workspace->filename,
					 &old_size, &old_fingerprint);
	if (ret != 0) {
		return ret;
	}
	if (old_size != expected_version->size ||
	    old_fingerprint != expected_version->fingerprint) {
		return -EAGAIN;
	}
	ret = workspace_edit_artifact_name(workspace, "tmp", temporary_name,
					   sizeof(temporary_name));
	if (ret != 0) {
		return ret;
	}
	ret = workspace_edit_artifact_name(workspace, "bak", backup_name,
					   sizeof(backup_name));
	if (ret != 0) {
		return ret;
	}
	ret = ef800_persistence_write(workspace->storage, EF800_PERSISTENCE_DATA,
		temporary_name, NULL, 0U);
	if (ret != 0) {
		return ret;
	}
	ret = workspace_edit_copy_range(workspace, temporary_name, 0U, row->start,
					&staged_size, &staged_fingerprint);
	if (ret == 0 && !delete_row) {
		ret = workspace_edit_append(workspace, temporary_name, replacement,
			replacement_length, &staged_size, &staged_fingerprint);
	}
	if (ret == 0 && !delete_row && row->ending_length != 0U) {
		ret = ef800_workspace_read_range(workspace, row->text_end, row->end,
						 ending, sizeof(ending), &ending_length);
		if (ret == 0 && ending_length != row->ending_length) {
			ret = -EIO;
		}
		if (ret == 0) {
			ret = workspace_edit_append(workspace, temporary_name, ending,
				ending_length, &staged_size, &staged_fingerprint);
		}
	}
	if (ret == 0) {
		ret = workspace_edit_copy_range(workspace, temporary_name, row->end,
			file_size, &staged_size, &staged_fingerprint);
	}
	if (ret != 0) {
		(void)workspace_edit_remove_if_present(workspace, "tmp");
		return ret;
	}
	ret = ef800_persistence_sync(workspace->storage, EF800_PERSISTENCE_DATA,
		temporary_name);
	if (ret == 0) {
		ret = ef800_persistence_stat(workspace->storage, EF800_PERSISTENCE_DATA,
			temporary_name, &staged_entry);
	}
	if (ret == 0 && (staged_entry.type != EF800_PERSISTENCE_FILE ||
			 staged_entry.size != staged_size)) {
		ret = -EIO;
	}
	if (ret == 0) {
		ret = workspace_file_fingerprint(workspace->storage, temporary_name,
			&verified_size, &verified_fingerprint);
	}
	if (ret == 0 && (verified_size != staged_size ||
			 verified_fingerprint != staged_fingerprint)) {
		ret = -EIO;
	}
	if (ret == 0) {
		size_t current_size;
		uint64_t current_fingerprint;

		ret = workspace_file_fingerprint(workspace->storage, workspace->filename,
			&current_size, &current_fingerprint);
		if (ret == 0 && (current_size != expected_version->size ||
				 current_fingerprint != expected_version->fingerprint)) {
			ret = -EAGAIN;
		}
	}
	if (ret != 0) {
		(void)workspace_edit_remove_if_present(workspace, "tmp");
		return ret;
	}

	txn.generation = 1U;
	txn.phase = WORKSPACE_TXN_PREPARED;
	txn.old_size = old_size;
	txn.old_fingerprint = old_fingerprint;
	txn.new_size = staged_size;
	txn.new_fingerprint = staged_fingerprint;
	journal_started = true;
	ret = workspace_transaction_write_slot(workspace, &txn);
	if (ret == 0) {
		ret = ef800_persistence_rename(workspace->storage,
			EF800_PERSISTENCE_DATA, workspace->filename, backup_name);
	}
	if (ret == 0) {
		ret = workspace_transaction_commit_phase(workspace, &txn,
			WORKSPACE_TXN_BACKED_UP);
	}
	if (ret == 0) {
		ret = ef800_persistence_rename(workspace->storage,
			EF800_PERSISTENCE_DATA, temporary_name, workspace->filename);
	}
	if (ret == 0) {
		ret = workspace_file_fingerprint(workspace->storage, workspace->filename,
			&verified_size, &verified_fingerprint);
	}
	if (ret == 0 && (verified_size != staged_size ||
			 verified_fingerprint != staged_fingerprint)) {
		ret = -EIO;
	}
	if (ret == 0) {
		ret = workspace_transaction_commit_phase(workspace, &txn,
			WORKSPACE_TXN_COMMITTED);
	}
	if (ret != 0) {
		if (journal_started) {
			int recovery_result = workspace_transaction_recover(workspace);

			if (recovery_result != 0) {
				return recovery_result;
			}
		} else {
			(void)workspace_edit_remove_if_present(workspace, "tmp");
		}
		return ret;
	}
	ret = workspace_transaction_recover(workspace);
	if (ret != 0) {
		return ret;
	}
	workspace_extract_context(workspace);
	return 0;
}

bool ef800_workspace_line_is_position(const char *line)
{
	return line != NULL && line[0] == '-';
}

struct workspace_list_context {
	ef800_workspace_list_callback callback;
	void *context;
};

static int list_workspace_entry(const struct ef800_persistence_entry *entry,
				void *context)
{
	struct workspace_list_context *list_context = context;
	char name[EF800_WORKSPACE_NAME_MAX + 1U];
	size_t extension_length = sizeof(EF800_WORKSPACE_EXTENSION) - 1U;
	size_t length;

	if (entry->type != EF800_PERSISTENCE_FILE ||
	    !has_suffix(entry->name, EF800_WORKSPACE_EXTENSION)) {
		return 0;
	}
	length = strlen(entry->name) - extension_length;
	if (length == 0U || length > EF800_WORKSPACE_NAME_MAX) {
		return 0;
	}
	memcpy(name, entry->name, length);
	name[length] = '\0';
	if (workspace_name_valid(name) != 0) {
		return 0;
	}
	return list_context->callback(name, list_context->context);
}

int ef800_workspace_list(struct ef800_workspace *workspace,
			const ef800_workspace_list_callback callback, void *context)
{
	struct workspace_list_context list_context;

	if (workspace == NULL || workspace->storage == NULL || callback == NULL) {
		return -EINVAL;
	}
	list_context.callback = callback;
	list_context.context = context;
	return ef800_persistence_list(workspace->storage, EF800_PERSISTENCE_DATA,
				      list_workspace_entry, &list_context);
}

bool ef800_workspace_is_selected(const struct ef800_workspace *workspace)
{
	return workspace != NULL && workspace->selected;
}

const char *ef800_workspace_get_name(const struct ef800_workspace *workspace)
{
	return workspace != NULL && workspace->selected ? workspace->name : NULL;
}

const char *ef800_workspace_get_filename(const struct ef800_workspace *workspace)
{
	return workspace != NULL && workspace->selected ? workspace->filename : NULL;
}

int32_t ef800_workspace_current_point_number(
	const struct ef800_workspace *workspace)
{
	return workspace != NULL && workspace->selected ?
		workspace->current_point_number : 0;
}

const char *ef800_workspace_current_code(
	const struct ef800_workspace *workspace)
{
	return workspace != NULL && workspace->selected ? workspace->current_code : NULL;
}
