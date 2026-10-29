/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_composition.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define FIELD_FORMAT "format_version="
#define FIELD_PRODUCT "product="
#define FIELD_GENERATION "generation="
#define FIELD_COMPOSITION "composition="
#define FIELD_CRC "crc32="

struct slot_candidate {
	struct ef800_composition_profile profile;
	bool valid;
	int error;
};

static bool bounded_identifier_valid(const char *value, size_t maximum)
{
	size_t length;

	if (value == NULL) {
		return false;
	}
	length = strlen(value);
	if (length == 0U || length > maximum || value[0] < 'A' || value[0] > 'Z') {
		return false;
	}
	for (size_t index = 1U; index < length; ++index) {
		if (!((value[index] >= 'A' && value[index] <= 'Z') ||
		      (value[index] >= '0' && value[index] <= '9'))) {
			return false;
		}
	}
	return true;
}

bool ef800_composition_token_valid(const char *token)
{
	return bounded_identifier_valid(token, EF800_COMPOSITION_TOKEN_MAX);
}

uint32_t ef800_composition_crc32(const uint8_t *data, size_t length)
{
	uint32_t crc = UINT32_MAX;

	if (data == NULL && length != 0U) {
		return 0U;
	}
	for (size_t index = 0U; index < length; ++index) {
		crc ^= data[index];
		for (uint8_t bit = 0U; bit < 8U; ++bit) {
			crc = (crc >> 1) ^ ((crc & 1U) != 0U ? 0xEDB88320U : 0U);
		}
	}
	return ~crc;
}

static int append_text(char *buffer, size_t capacity, size_t *offset,
	const char *text)
{
	size_t text_length = strlen(text);

	if (*offset > capacity || text_length > capacity - *offset) {
		return -ENOSPC;
	}
	memcpy(buffer + *offset, text, text_length);
	*offset += text_length;
	return 0;
}

static int encode_without_crc(const struct ef800_composition_profile *profile,
	char *buffer, size_t capacity, size_t *length)
{
	char number[32];
	size_t offset = 0U;
	int ret;

	ret = append_text(buffer, capacity, &offset, "format_version=1\nproduct=EF800\ngeneration=");
	if (ret != 0) {
		return ret;
	}
	int count = snprintf(number, sizeof(number), "%u\ncomposition=",
		(unsigned int)profile->generation);
	if (count < 0 || (size_t)count >= sizeof(number)) {
		return -EINVAL;
	}
	ret = append_text(buffer, capacity, &offset, number);
	if (ret != 0) {
		return ret;
	}
	for (size_t index = 0U; index < profile->token_count; ++index) {
		if (!ef800_composition_token_valid(profile->tokens[index])) {
			return -EINVAL;
		}
		for (size_t previous = 0U; previous < index; ++previous) {
			if (strcmp(profile->tokens[index], profile->tokens[previous]) == 0) {
				return -EEXIST;
			}
		}
		if (index != 0U && append_text(buffer, capacity, &offset, " ") != 0) {
			return -ENOSPC;
		}
		ret = append_text(buffer, capacity, &offset, profile->tokens[index]);
		if (ret != 0) {
			return ret;
		}
	}
	ret = append_text(buffer, capacity, &offset, "\n");
	if (ret == 0) {
		*length = offset;
	}
	return ret;
}

int ef800_composition_profile_encode(const struct ef800_composition_profile *profile,
	char *buffer, size_t capacity, size_t *length)
{
	char crc_line[32];
	size_t body_length;
	int ret;
	int count;

	if (profile == NULL || buffer == NULL || length == NULL ||
	    profile->token_count > EF800_COMPOSITION_MAX_TOKENS) {
		return -EINVAL;
	}
	ret = encode_without_crc(profile, buffer, capacity, &body_length);
	if (ret != 0) {
		return ret;
	}
	count = snprintf(crc_line, sizeof(crc_line), "crc32=%08X\n",
		(unsigned int)ef800_composition_crc32((const uint8_t *)buffer, body_length));
	if (count < 0 || (size_t)count >= sizeof(crc_line)) {
		return -EINVAL;
	}
	ret = append_text(buffer, capacity, &body_length, crc_line);
	if (ret == 0) {
		*length = body_length;
	}
	return ret;
}

static int parse_u32(const char *value, uint32_t *output)
{
	uint64_t result = 0U;

	if (value == NULL || output == NULL || value[0] == '\0') {
		return -EINVAL;
	}
	for (size_t index = 0U; value[index] != '\0'; ++index) {
		if (value[index] < '0' || value[index] > '9') {
			return -EINVAL;
		}
		result = result * 10U + (uint64_t)(value[index] - '0');
		if (result > UINT32_MAX) {
			return -ERANGE;
		}
	}
	*output = (uint32_t)result;
	return 0;
}

static int parse_hex32(const char *value, uint32_t *output)
{
	uint32_t result = 0U;

	if (value == NULL || output == NULL || strlen(value) != 8U) {
		return -EINVAL;
	}
	for (size_t index = 0U; index < 8U; ++index) {
		uint8_t digit;
		if (value[index] >= '0' && value[index] <= '9') {
			digit = (uint8_t)(value[index] - '0');
		} else if (value[index] >= 'A' && value[index] <= 'F') {
			digit = (uint8_t)(value[index] - 'A' + 10);
		} else {
			return -EINVAL;
		}
		result = (result << 4) | digit;
	}
	*output = result;
	return 0;
}

static int parse_composition(char *value, struct ef800_composition_profile *profile)
{
	char *cursor = value;

	while (*cursor != '\0') {
		char *start;
		size_t token_length;

		while (*cursor == ' ') {
			++cursor;
		}
		if (*cursor == '\0') {
			break;
		}
		start = cursor;
		while (*cursor != '\0' && *cursor != ' ') {
			if (*cursor == '\t') {
				return -EINVAL;
			}
			++cursor;
		}
		token_length = (size_t)(cursor - start);
		if (token_length == 0U || token_length > EF800_COMPOSITION_TOKEN_MAX ||
		    profile->token_count >= EF800_COMPOSITION_MAX_TOKENS) {
			return -E2BIG;
		}
		char saved = *cursor;
		*cursor = '\0';
		if (!ef800_composition_token_valid(start)) {
			return -EINVAL;
		}
		for (size_t index = 0U; index < profile->token_count; ++index) {
			if (strcmp(profile->tokens[index], start) == 0) {
				return -EEXIST;
			}
		}
		strcpy(profile->tokens[profile->token_count++], start);
		if (saved == '\0') {
			break;
		}
		*cursor = saved;
	}
	return 0;
}

int ef800_composition_profile_decode(const uint8_t *buffer, size_t length,
	struct ef800_composition_profile *profile)
{
	char input[EF800_COMPOSITION_RECORD_MAX + 1U];
	char canonical[EF800_COMPOSITION_RECORD_MAX];
	bool seen_format = false;
	bool seen_product = false;
	bool seen_generation = false;
	bool seen_composition = false;
	bool seen_crc = false;
	uint32_t expected_crc = 0U;
	size_t canonical_length;
	char *cursor;
	int ret;

	if (buffer == NULL || profile == NULL || length == 0U ||
	    length > EF800_COMPOSITION_RECORD_MAX) {
		return -EINVAL;
	}
	memcpy(input, buffer, length);
	input[length] = '\0';
	*profile = (struct ef800_composition_profile){0};
	cursor = input;
	while (*cursor != '\0') {
		char *line = cursor;
		char *newline = strchr(cursor, '\n');
		if (newline != NULL) {
			*newline = '\0';
			cursor = newline + 1;
		} else {
			cursor += strlen(cursor);
		}
		size_t line_length = strlen(line);
		if (line_length != 0U && line[line_length - 1U] == '\r') {
			line[--line_length] = '\0';
		}
		if (line_length == 0U) {
			return -EINVAL;
		}
		if (strncmp(line, FIELD_FORMAT, strlen(FIELD_FORMAT)) == 0) {
			uint32_t version;
			if (seen_format || parse_u32(line + strlen(FIELD_FORMAT), &version) != 0 ||
			    version != EF800_COMPOSITION_FORMAT_VERSION) {
				return -EPROTONOSUPPORT;
			}
			seen_format = true;
		} else if (strncmp(line, FIELD_PRODUCT, strlen(FIELD_PRODUCT)) == 0) {
			if (seen_product || strcmp(line + strlen(FIELD_PRODUCT),
				EF800_COMPOSITION_PRODUCT) != 0) {
				return -EINVAL;
			}
			seen_product = true;
		} else if (strncmp(line, FIELD_GENERATION, strlen(FIELD_GENERATION)) == 0) {
			if (seen_generation || parse_u32(line + strlen(FIELD_GENERATION),
				&profile->generation) != 0) {
				return -EINVAL;
			}
			seen_generation = true;
		} else if (strncmp(line, FIELD_COMPOSITION, strlen(FIELD_COMPOSITION)) == 0) {
			if (seen_composition) {
				return -EINVAL;
			}
			ret = parse_composition(line + strlen(FIELD_COMPOSITION), profile);
			if (ret != 0) {
				return ret;
			}
			seen_composition = true;
		} else if (strncmp(line, FIELD_CRC, strlen(FIELD_CRC)) == 0) {
			if (seen_crc || parse_hex32(line + strlen(FIELD_CRC), &expected_crc) != 0) {
				return -EINVAL;
			}
			seen_crc = true;
			if (*cursor != '\0') {
				return -EINVAL;
			}
		} else {
			return -EINVAL;
		}
	}
	if (!seen_format || !seen_product || !seen_generation || !seen_composition || !seen_crc) {
		return -EINVAL;
	}
	ret = encode_without_crc(profile, canonical, sizeof(canonical), &canonical_length);
	if (ret != 0) {
		return ret;
	}
	return ef800_composition_crc32((const uint8_t *)canonical, canonical_length) ==
		expected_crc ? 0 : -EBADMSG;
}

bool ef800_composition_profile_equal(const struct ef800_composition_profile *left,
	const struct ef800_composition_profile *right)
{
	if (left == NULL || right == NULL || left->generation != right->generation ||
	    left->token_count != right->token_count) {
		return false;
	}
	for (size_t index = 0U; index < left->token_count; ++index) {
		if (strcmp(left->tokens[index], right->tokens[index]) != 0) {
			return false;
		}
	}
	return true;
}

int ef800_composition_catalog_validate(const struct ef800_composition_catalog *catalog)
{
	if (catalog == NULL || catalog->entry_count > EF800_COMPOSITION_MAX_TOKENS ||
	    (catalog->entry_count != 0U && catalog->entries == NULL)) {
		return -EINVAL;
	}
	for (size_t index = 0U; index < catalog->entry_count; ++index) {
		const struct ef800_composition_catalog_entry *entry = &catalog->entries[index];
		if (!ef800_composition_token_valid(entry->token) ||
		    !bounded_identifier_valid(entry->group, EF800_COMPOSITION_GROUP_MAX)) {
			return -EINVAL;
		}
		if (entry->is_default && !entry->supported) {
			return -EINVAL;
		}
		for (size_t previous = 0U; previous < index; ++previous) {
			const struct ef800_composition_catalog_entry *other = &catalog->entries[previous];
			if (strcmp(entry->token, other->token) == 0 ||
			    (entry->is_default && other->is_default &&
			     strcmp(entry->group, other->group) == 0)) {
				return -EEXIST;
			}
		}
	}
	return 0;
}

static const struct ef800_composition_catalog_entry *catalog_find(
	const struct ef800_composition_catalog *catalog, const char *token)
{
	for (size_t index = 0U; index < catalog->entry_count; ++index) {
		if (strcmp(catalog->entries[index].token, token) == 0) {
			return &catalog->entries[index];
		}
	}
	return NULL;
}

static bool effective_has_group(const struct ef800_composition_snapshot *snapshot,
	const char *group)
{
	for (size_t index = 0U; index < snapshot->effective_count; ++index) {
		if (strcmp(snapshot->effective[index].catalog_entry->group, group) == 0) {
			return true;
		}
	}
	return false;
}

int ef800_composition_resolve(const struct ef800_composition_profile *profile,
	const struct ef800_composition_catalog *catalog,
	struct ef800_composition_snapshot *snapshot)
{
	int ret;

	if (profile == NULL || snapshot == NULL ||
	    profile->token_count > EF800_COMPOSITION_MAX_TOKENS) {
		return -EINVAL;
	}
	ret = ef800_composition_catalog_validate(catalog);
	if (ret != 0) {
		return ret;
	}
	*snapshot = (struct ef800_composition_snapshot){ .stored = *profile };
	for (size_t index = 0U; index < profile->token_count; ++index) {
		const struct ef800_composition_catalog_entry *entry =
			catalog_find(catalog, profile->tokens[index]);
		if (entry == NULL || !entry->supported) {
			strcpy(snapshot->unknown[snapshot->unknown_count++], profile->tokens[index]);
			continue;
		}
		if (effective_has_group(snapshot, entry->group)) {
			return -EEXIST;
		}
		snapshot->effective[snapshot->effective_count++] =
			(struct ef800_composition_effective_entry){ entry, false };
	}
	for (size_t index = 0U; index < catalog->entry_count; ++index) {
		const struct ef800_composition_catalog_entry *entry = &catalog->entries[index];
		if (entry->is_default && !effective_has_group(snapshot, entry->group)) {
			snapshot->effective[snapshot->effective_count++] =
				(struct ef800_composition_effective_entry){ entry, true };
		}
	}
	return 0;
}

static int read_slot(struct ef800_persistence *persistence, const char *name,
	struct slot_candidate *candidate)
{
	uint8_t buffer[EF800_COMPOSITION_RECORD_MAX];
	size_t length = 0U;
	int ret;

	*candidate = (struct slot_candidate){0};
	ret = ef800_persistence_read(persistence, EF800_PERSISTENCE_CONFIGURATION,
		name, buffer, sizeof(buffer), &length);
	if (ret == 0) {
		ret = ef800_composition_profile_decode(buffer, length, &candidate->profile);
	}
	candidate->error = ret;
	candidate->valid = ret == 0;
	return ret;
}

static int select_candidate(const struct slot_candidate slots[2])
{
	struct ef800_composition_profile selected;
	int selected_slot;
	int ret = ef800_composition_profile_select(&slots[0].profile, slots[0].valid,
		&slots[1].profile, slots[1].valid, &selected, &selected_slot);

	if (ret == -ENOENT) {
		return -1;
	}
	if (ret == -EBADMSG) {
		return -2;
	}
	return ret == 0 ? selected_slot : -2;
}

int ef800_composition_profile_select(
	const struct ef800_composition_profile *slot_a, bool slot_a_valid,
	const struct ef800_composition_profile *slot_b, bool slot_b_valid,
	struct ef800_composition_profile *selected, int *selected_slot)
{
	uint32_t difference;

	if (selected == NULL || selected_slot == NULL ||
	    (slot_a_valid && slot_a == NULL) || (slot_b_valid && slot_b == NULL)) {
		return -EINVAL;
	}
	if (slot_a_valid && !slot_b_valid) {
		*selected = *slot_a;
		*selected_slot = 0;
		return 0;
	}
	if (!slot_a_valid && slot_b_valid) {
		*selected = *slot_b;
		*selected_slot = 1;
		return 0;
	}
	if (!slot_a_valid && !slot_b_valid) {
		*selected_slot = -1;
		return -ENOENT;
	}
	difference = slot_a->generation - slot_b->generation;
	if (difference == 0U) {
		if (!ef800_composition_profile_equal(slot_a, slot_b)) {
			*selected_slot = -1;
			return -EBADMSG;
		}
		*selected = *slot_a;
		*selected_slot = 0;
		return 0;
	}
	if (difference == 0x80000000U) {
		*selected_slot = -1;
		return -EBADMSG;
	}
	if (difference < 0x80000000U) {
		*selected = *slot_a;
		*selected_slot = 0;
	} else {
		*selected = *slot_b;
		*selected_slot = 1;
	}
	return 0;
}

static enum ef800_composition_load_status fallback_status(
	const struct slot_candidate slots[2])
{
	if (slots[0].error == -ENOTSUP || slots[1].error == -ENOTSUP) {
		return EF800_COMPOSITION_LOAD_UNAVAILABLE;
	}
	if ((slots[0].error == -ENOENT || slots[0].error == 0) &&
	    (slots[1].error == -ENOENT || slots[1].error == 0)) {
		return EF800_COMPOSITION_LOAD_DEFAULT;
	}
	return EF800_COMPOSITION_LOAD_INVALID;
}

int ef800_composition_service_init(struct ef800_composition_service *service,
	struct ef800_persistence *persistence,
	const struct ef800_composition_catalog *catalog)
{
	struct slot_candidate slots[2];
	struct ef800_composition_profile selected = {0};
	int selected_slot;
	int ret;

	if (service == NULL || persistence == NULL || persistence->backend == NULL) {
		return -EINVAL;
	}
	ret = ef800_composition_catalog_validate(catalog);
	if (ret != 0) {
		return ret;
	}
	*service = (struct ef800_composition_service){
		.persistence = persistence,
		.catalog = catalog,
		.active_slot = -1,
	};
	(void)read_slot(persistence, EF800_COMPOSITION_SLOT_A, &slots[0]);
	(void)read_slot(persistence, EF800_COMPOSITION_SLOT_B, &slots[1]);
	service->slot_error[0] = slots[0].error;
	service->slot_error[1] = slots[1].error;
	selected_slot = select_candidate(slots);
	if (selected_slot >= 0) {
		selected = slots[selected_slot].profile;
		service->active_slot = selected_slot;
		service->load_status = EF800_COMPOSITION_LOAD_STORED;
	} else if (selected_slot == -2) {
		service->load_status = EF800_COMPOSITION_LOAD_CONFLICT;
	} else {
		service->load_status = fallback_status(slots);
	}
	ret = ef800_composition_resolve(&selected, catalog, &service->active);
	return ret;
}

const struct ef800_composition_snapshot *ef800_composition_service_active(
	const struct ef800_composition_service *service)
{
	return service == NULL ? NULL : &service->active;
}

const struct ef800_composition_snapshot *ef800_composition_service_pending(
	const struct ef800_composition_service *service)
{
	return service == NULL || !service->pending_valid ? NULL : &service->pending;
}

enum ef800_composition_load_status ef800_composition_service_load_status(
	const struct ef800_composition_service *service)
{
	return service == NULL ? EF800_COMPOSITION_LOAD_INVALID : service->load_status;
}

bool ef800_composition_service_restart_required(
	const struct ef800_composition_service *service)
{
	return service != NULL && service->restart_required;
}

static int stage_profile(struct ef800_composition_service *service,
	const struct ef800_composition_profile *profile)
{
	struct ef800_composition_snapshot resolved;
	char encoded[EF800_COMPOSITION_RECORD_MAX];
	size_t length;
	int ret;

	if (service == NULL || profile == NULL) {
		return -EINVAL;
	}
	ret = ef800_composition_resolve(profile, service->catalog, &resolved);
	if (ret == 0) {
		ret = ef800_composition_profile_encode(profile, encoded, sizeof(encoded), &length);
	}
	if (ret != 0) {
		return ret;
	}
	service->pending = resolved;
	service->pending_valid = true;
	return 0;
}

int ef800_composition_service_stage_replace(struct ef800_composition_service *service,
	const char *const *tokens, size_t token_count)
{
	struct ef800_composition_profile profile = {0};

	if (service == NULL || token_count > EF800_COMPOSITION_MAX_TOKENS ||
	    (token_count != 0U && tokens == NULL)) {
		return -EINVAL;
	}
	profile.generation = service->active.stored.generation;
	profile.token_count = token_count;
	for (size_t index = 0U; index < token_count; ++index) {
		if (!ef800_composition_token_valid(tokens[index])) {
			return -EINVAL;
		}
		for (size_t previous = 0U; previous < index; ++previous) {
			if (strcmp(tokens[index], tokens[previous]) == 0) {
				return -EEXIST;
			}
		}
		strcpy(profile.tokens[index], tokens[index]);
	}
	return stage_profile(service, &profile);
}

static struct ef800_composition_profile mutation_base(
	const struct ef800_composition_service *service)
{
	return service->pending_valid ? service->pending.stored : service->active.stored;
}

int ef800_composition_service_stage_add(struct ef800_composition_service *service,
	const char *token)
{
	struct ef800_composition_profile profile;

	if (service == NULL || !ef800_composition_token_valid(token)) {
		return -EINVAL;
	}
	profile = mutation_base(service);
	if (profile.token_count >= EF800_COMPOSITION_MAX_TOKENS) {
		return -ENOSPC;
	}
	for (size_t index = 0U; index < profile.token_count; ++index) {
		if (strcmp(profile.tokens[index], token) == 0) {
			return -EEXIST;
		}
	}
	strcpy(profile.tokens[profile.token_count++], token);
	return stage_profile(service, &profile);
}

int ef800_composition_service_stage_remove(struct ef800_composition_service *service,
	const char *token)
{
	struct ef800_composition_profile profile;
	size_t found = EF800_COMPOSITION_MAX_TOKENS;

	if (service == NULL || !ef800_composition_token_valid(token)) {
		return -EINVAL;
	}
	profile = mutation_base(service);
	for (size_t index = 0U; index < profile.token_count; ++index) {
		if (strcmp(profile.tokens[index], token) == 0) {
			found = index;
			break;
		}
	}
	if (found == EF800_COMPOSITION_MAX_TOKENS) {
		return -ENOENT;
	}
	for (size_t index = found; index + 1U < profile.token_count; ++index) {
		strcpy(profile.tokens[index], profile.tokens[index + 1U]);
	}
	--profile.token_count;
	profile.tokens[profile.token_count][0] = '\0';
	return stage_profile(service, &profile);
}

int ef800_composition_service_stage_group_selection(
	struct ef800_composition_service *service, const char *group,
	const char *token)
{
	struct ef800_composition_profile profile;
	const struct ef800_composition_catalog_entry *selected = NULL;
	size_t output_count = 0U;

	if (service == NULL || service->catalog == NULL || group == NULL || group[0] == '\0' ||
	    !ef800_composition_token_valid(token)) {
		return -EINVAL;
	}
	for (size_t index = 0U; index < service->catalog->entry_count; ++index) {
		const struct ef800_composition_catalog_entry *entry =
			&service->catalog->entries[index];

		if (strcmp(entry->token, token) == 0) {
			selected = entry;
			break;
		}
	}
	if (selected == NULL || strcmp(selected->group, group) != 0) {
		return -ENOENT;
	}
	if (!selected->supported) {
		return -ENOTSUP;
	}
	profile = mutation_base(service);
	for (size_t index = 0U; index < profile.token_count; ++index) {
		const struct ef800_composition_catalog_entry *entry = NULL;

		for (size_t candidate = 0U; candidate < service->catalog->entry_count;
		     ++candidate) {
			if (strcmp(profile.tokens[index],
				service->catalog->entries[candidate].token) == 0) {
				entry = &service->catalog->entries[candidate];
				break;
			}
		}
		if (entry != NULL && strcmp(entry->group, group) == 0) {
			continue;
		}
		if (output_count >= EF800_COMPOSITION_MAX_TOKENS) {
			return -ENOSPC;
		}
		if (output_count != index) {
			strcpy(profile.tokens[output_count], profile.tokens[index]);
		}
		++output_count;
	}
	if (output_count >= EF800_COMPOSITION_MAX_TOKENS) {
		return -ENOSPC;
	}
	strcpy(profile.tokens[output_count++], token);
	profile.token_count = output_count;
	for (size_t index = output_count; index < EF800_COMPOSITION_MAX_TOKENS; ++index) {
		profile.tokens[index][0] = '\0';
	}
	return stage_profile(service, &profile);
}

void ef800_composition_service_discard(struct ef800_composition_service *service)
{
	if (service != NULL) {
		service->pending = (struct ef800_composition_snapshot){0};
		service->pending_valid = false;
	}
}

int ef800_composition_service_commit(struct ef800_composition_service *service)
{
	struct slot_candidate slots[2];
	struct ef800_composition_profile committed;
	struct ef800_composition_profile verified;
	uint8_t encoded[EF800_COMPOSITION_RECORD_MAX];
	size_t encoded_length;
	size_t read_length;
	int selected;
	int target;
	int ret;
	static const char *const names[] = {
		EF800_COMPOSITION_SLOT_A, EF800_COMPOSITION_SLOT_B
	};

	if (service == NULL || !service->pending_valid) {
		return -EINVAL;
	}
	(void)read_slot(service->persistence, names[0], &slots[0]);
	(void)read_slot(service->persistence, names[1], &slots[1]);
	selected = select_candidate(slots);
	if (selected == -2) {
		return -EBADMSG;
	}
	committed = service->pending.stored;
	committed.generation = selected >= 0 ? slots[selected].profile.generation + 1U : 1U;
	target = selected == 0 ? 1 : 0;
	ret = ef800_composition_profile_encode(&committed, (char *)encoded,
		sizeof(encoded), &encoded_length);
	if (ret != 0) {
		return ret;
	}
	ret = ef800_persistence_write(service->persistence,
		EF800_PERSISTENCE_CONFIGURATION, names[target], encoded, encoded_length);
	if (ret != 0) {
		return ret;
	}
	ret = ef800_persistence_read(service->persistence,
		EF800_PERSISTENCE_CONFIGURATION, names[target], encoded, sizeof(encoded),
		&read_length);
	if (ret == 0) {
		ret = ef800_composition_profile_decode(encoded, read_length, &verified);
	}
	if (ret != 0 || !ef800_composition_profile_equal(&committed, &verified)) {
		return ret != 0 ? ret : -EIO;
	}
	service->pending = (struct ef800_composition_snapshot){0};
	service->pending_valid = false;
	service->restart_required = true;
	return 0;
}
