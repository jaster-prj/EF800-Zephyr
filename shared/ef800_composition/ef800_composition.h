/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_COMPOSITION_H_
#define EF800_COMPOSITION_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ef800_persistence.h"

#define EF800_COMPOSITION_FORMAT_VERSION 1U
#define EF800_COMPOSITION_PRODUCT "EF800"
#define EF800_COMPOSITION_SLOT_A "target_profile_a"
#define EF800_COMPOSITION_SLOT_B "target_profile_b"
#define EF800_COMPOSITION_MAX_TOKENS 32U
#define EF800_COMPOSITION_TOKEN_MAX 16U
#define EF800_COMPOSITION_GROUP_MAX 16U
#define EF800_COMPOSITION_RECORD_MAX 1024U

struct ef800_composition_profile {
	uint32_t generation;
	size_t token_count;
	char tokens[EF800_COMPOSITION_MAX_TOKENS][EF800_COMPOSITION_TOKEN_MAX + 1U];
};

struct ef800_composition_catalog_entry {
	const char *token;
	const char *group;
	uintptr_t selection;
	bool is_default;
	/* Target-incompatible entries remain stored but resolve as unknown. */
	bool supported;
};

struct ef800_composition_catalog {
	const struct ef800_composition_catalog_entry *entries;
	size_t entry_count;
};

struct ef800_composition_effective_entry {
	const struct ef800_composition_catalog_entry *catalog_entry;
	bool defaulted;
};

struct ef800_composition_snapshot {
	struct ef800_composition_profile stored;
	struct ef800_composition_effective_entry effective[EF800_COMPOSITION_MAX_TOKENS];
	size_t effective_count;
	char unknown[EF800_COMPOSITION_MAX_TOKENS][EF800_COMPOSITION_TOKEN_MAX + 1U];
	size_t unknown_count;
};

enum ef800_composition_load_status {
	EF800_COMPOSITION_LOAD_DEFAULT = 0,
	EF800_COMPOSITION_LOAD_STORED,
	EF800_COMPOSITION_LOAD_UNAVAILABLE,
	EF800_COMPOSITION_LOAD_INVALID,
	EF800_COMPOSITION_LOAD_CONFLICT,
};

struct ef800_composition_service {
	struct ef800_persistence *persistence;
	const struct ef800_composition_catalog *catalog;
	struct ef800_composition_snapshot active;
	struct ef800_composition_snapshot pending;
	bool pending_valid;
	bool restart_required;
	enum ef800_composition_load_status load_status;
	int slot_error[2];
	int active_slot;
};

bool ef800_composition_token_valid(const char *token);
uint32_t ef800_composition_crc32(const uint8_t *data, size_t length);
int ef800_composition_profile_encode(const struct ef800_composition_profile *profile,
	char *buffer, size_t capacity, size_t *length);
int ef800_composition_profile_decode(const uint8_t *buffer, size_t length,
	struct ef800_composition_profile *profile);
bool ef800_composition_profile_equal(const struct ef800_composition_profile *left,
				     const struct ef800_composition_profile *right);
int ef800_composition_profile_select(
	const struct ef800_composition_profile *slot_a, bool slot_a_valid,
	const struct ef800_composition_profile *slot_b, bool slot_b_valid,
	struct ef800_composition_profile *selected, int *selected_slot);

int ef800_composition_catalog_validate(const struct ef800_composition_catalog *catalog);
int ef800_composition_resolve(const struct ef800_composition_profile *profile,
	const struct ef800_composition_catalog *catalog,
	struct ef800_composition_snapshot *snapshot);

int ef800_composition_service_init(struct ef800_composition_service *service,
	struct ef800_persistence *persistence,
	const struct ef800_composition_catalog *catalog);
const struct ef800_composition_snapshot *ef800_composition_service_active(
	const struct ef800_composition_service *service);
const struct ef800_composition_snapshot *ef800_composition_service_pending(
	const struct ef800_composition_service *service);
enum ef800_composition_load_status ef800_composition_service_load_status(
	const struct ef800_composition_service *service);
bool ef800_composition_service_restart_required(
	const struct ef800_composition_service *service);
int ef800_composition_service_stage_replace(struct ef800_composition_service *service,
	const char *const *tokens, size_t token_count);
int ef800_composition_service_stage_add(struct ef800_composition_service *service,
	const char *token);
int ef800_composition_service_stage_remove(struct ef800_composition_service *service,
	const char *token);
int ef800_composition_service_stage_group_selection(
	struct ef800_composition_service *service, const char *group,
	const char *token);
void ef800_composition_service_discard(struct ef800_composition_service *service);
int ef800_composition_service_commit(struct ef800_composition_service *service);

#endif /* EF800_COMPOSITION_H_ */
