/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Resolve, stage, and persist target composition profiles. */
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

/** @brief Validate a bounded composition token.
 * @param token NUL-terminated token text.
 * @return True when token syntax and length are supported.
 */
bool ef800_composition_token_valid(const char *token);

/** @brief Compute CRC-32 for a serialized composition record.
 * @param data Record bytes.
 * @param length Byte count.
 * @return CRC-32 value.
 */
uint32_t ef800_composition_crc32(const uint8_t *data, size_t length);

/** @brief Serialize a composition profile into a bounded record.
 * @param profile Profile to encode.
 * @param buffer Destination buffer.
 * @param capacity Buffer capacity.
 * @param length Receives encoded byte count.
 * @return 0 on encode; negative errno for invalid data or insufficient space.
 */
int ef800_composition_profile_encode(const struct ef800_composition_profile *profile,
	char *buffer, size_t capacity, size_t *length);

/** @brief Decode and validate a serialized composition profile.
 * @param buffer Encoded record bytes.
 * @param length Record byte count.
 * @param profile Receives decoded profile.
 * @return 0 on decode; negative errno for malformed data or checksum mismatch.
 */
int ef800_composition_profile_decode(const uint8_t *buffer, size_t length,
	struct ef800_composition_profile *profile);

/** @brief Compare profile generation and ordered token contents.
 * @param left First profile.
 * @param right Second profile.
 * @return True when profiles are equivalent.
 */
bool ef800_composition_profile_equal(const struct ef800_composition_profile *left,
				     const struct ef800_composition_profile *right);

/** @brief Select the newest valid profile from two redundant slots.
 * @param slot_a First decoded profile.
 * @param slot_a_valid Whether slot_a passed validation.
 * @param slot_b Second decoded profile.
 * @param slot_b_valid Whether slot_b passed validation.
 * @param selected Receives the chosen profile.
 * @param selected_slot Receives slot index, or a default marker.
 * @return 0 when a profile/default is selected; negative errno on invalid inputs.
 */
int ef800_composition_profile_select(
	const struct ef800_composition_profile *slot_a, bool slot_a_valid,
	const struct ef800_composition_profile *slot_b, bool slot_b_valid,
	struct ef800_composition_profile *selected, int *selected_slot);

/** @brief Validate catalog entries, tokens, groups, and default constraints.
 * @param catalog Catalog to validate.
 * @return 0 when valid; negative errno when entries conflict or are malformed.
 */
int ef800_composition_catalog_validate(const struct ef800_composition_catalog *catalog);

/** @brief Resolve stored tokens into supported effective catalog selections.
 * @param profile Stored profile to resolve.
 * @param catalog Platform catalog.
 * @param snapshot Receives effective entries and unknown tokens.
 * @return 0 on resolution; negative errno for invalid profile/catalog.
 */
int ef800_composition_resolve(const struct ef800_composition_profile *profile,
	const struct ef800_composition_catalog *catalog,
	struct ef800_composition_snapshot *snapshot);

/** @brief Load active profile and prepare a composition service.
 * @param service Service state to initialize.
 * @param persistence Storage for redundant profile records.
 * @param catalog Supported target catalog.
 * @return 0 on initialization; negative errno on invalid arguments/setup.
 */
int ef800_composition_service_init(struct ef800_composition_service *service,
	struct ef800_persistence *persistence,
	const struct ef800_composition_catalog *catalog);

/** @brief Get the currently committed effective composition.
 * @param service Service to inspect.
 * @return Active snapshot pointer, or NULL for invalid service.
 */
const struct ef800_composition_snapshot *ef800_composition_service_active(
	const struct ef800_composition_service *service);

/** @brief Get the staged effective composition, if one exists.
 * @param service Service to inspect.
 * @return Pending snapshot pointer, or NULL when none is staged.
 */
const struct ef800_composition_snapshot *ef800_composition_service_pending(
	const struct ef800_composition_service *service);

/** @brief Get the result of loading the active composition.
 * @param service Service to inspect.
 * @return Load status, or unavailable status for invalid service.
 */
enum ef800_composition_load_status ef800_composition_service_load_status(
	const struct ef800_composition_service *service);

/** @brief Check whether a committed change requires a restart to take effect.
 * @param service Service to inspect.
 * @return True when restart is required.
 */
bool ef800_composition_service_restart_required(
	const struct ef800_composition_service *service);

/** @brief Stage a complete replacement token list.
 * @param service Service to update.
 * @param tokens Ordered replacement tokens.
 * @param token_count Number of tokens.
 * @return 0 when staged; negative errno for invalid/conflicting tokens.
 */
int ef800_composition_service_stage_replace(struct ef800_composition_service *service,
	const char *const *tokens, size_t token_count);

/** @brief Stage one additional token while preserving other group choices.
 * @param service Service to update.
 * @param token Catalog token to add.
 * @return 0 when staged; negative errno for invalid token or profile conflict.
 */
int ef800_composition_service_stage_add(struct ef800_composition_service *service,
	const char *token);

/** @brief Stage removal of one token.
 * @param service Service to update.
 * @param token Token to remove.
 * @return 0 when staged; negative errno when token/profile is invalid.
 */
int ef800_composition_service_stage_remove(struct ef800_composition_service *service,
	const char *token);

/** @brief Stage selection of one token for a catalog group.
 * @param service Service to update.
 * @param group Group whose selection is replaced.
 * @param token Replacement token belonging to the group.
 * @return 0 when staged; negative errno for unknown or conflicting entries.
 */
int ef800_composition_service_stage_group_selection(
	struct ef800_composition_service *service, const char *group,
	const char *token);

/** @brief Discard staged changes without altering the active profile.
 * @param service Service whose pending profile is cleared.
 * @return None.
 */
void ef800_composition_service_discard(struct ef800_composition_service *service);

/** @brief Persist and activate the currently staged profile.
 * @param service Service with a valid pending profile.
 * @return 0 on commit; negative errno if nothing is staged or persistence fails.
 */
int ef800_composition_service_commit(struct ef800_composition_service *service);

#endif /* EF800_COMPOSITION_H_ */
