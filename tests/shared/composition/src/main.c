/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>

#include <errno.h>
#include <string.h>

#include "ef800_composition.h"
#include "ef800_persistence_fake.h"

static const struct ef800_composition_catalog empty_catalog;

static void profile_with_tokens(struct ef800_composition_profile *profile,
	uint32_t generation, const char *const *tokens, size_t count)
{
	*profile = (struct ef800_composition_profile){ .generation = generation };
	for (size_t index = 0U; index < count; ++index) {
		strcpy(profile->tokens[index], tokens[index]);
	}
	profile->token_count = count;
}

static size_t encode_profile(const struct ef800_composition_profile *profile,
	uint8_t buffer[EF800_COMPOSITION_RECORD_MAX])
{
	size_t length = 0U;
	zassert_ok(ef800_composition_profile_encode(profile, (char *)buffer,
		EF800_COMPOSITION_RECORD_MAX, &length), NULL);
	return length;
}

static void write_profile(struct ef800_persistence *persistence, const char *name,
	const struct ef800_composition_profile *profile)
{
	uint8_t buffer[EF800_COMPOSITION_RECORD_MAX];
	size_t length = encode_profile(profile, buffer);
	zassert_ok(ef800_persistence_write(persistence, EF800_PERSISTENCE_CONFIGURATION,
		name, buffer, length), NULL);
}

ZTEST(composition_encoding, test_canonical_round_trip_and_empty_profile)
{
	static const char *const tokens[] = { "LCD0", "BT12" };
	struct ef800_composition_profile original;
	struct ef800_composition_profile decoded;
	uint8_t buffer[EF800_COMPOSITION_RECORD_MAX];
	size_t length;

	profile_with_tokens(&original, 42U, tokens, ARRAY_SIZE(tokens));
	length = encode_profile(&original, buffer);
	zassert_ok(ef800_composition_profile_decode(buffer, length, &decoded), NULL);
	zassert_true(ef800_composition_profile_equal(&original, &decoded), NULL);
	zassert_not_null(strstr((char *)buffer, "composition=LCD0 BT12\n"), NULL);

	profile_with_tokens(&original, 43U, NULL, 0U);
	length = encode_profile(&original, buffer);
	zassert_ok(ef800_composition_profile_decode(buffer, length, &decoded), NULL);
	zassert_equal(decoded.token_count, 0U, NULL);
}

ZTEST(composition_encoding, test_whitespace_and_crlf_are_normalized)
{
	struct ef800_composition_profile decoded;
	struct ef800_composition_profile canonical_profile;
	uint8_t canonical[EF800_COMPOSITION_RECORD_MAX];
	char input[EF800_COMPOSITION_RECORD_MAX];
	char body[EF800_COMPOSITION_RECORD_MAX];
	size_t body_length;
	uint32_t crc;
	int length;
	static const char *const tokens[] = { "LCD0", "BT0" };

	profile_with_tokens(&canonical_profile, 7U, tokens, ARRAY_SIZE(tokens));
	(void)encode_profile(&canonical_profile, canonical);
	length = snprintf(body, sizeof(body),
		"format_version=1\nproduct=EF800\ngeneration=7\ncomposition=LCD0 BT0\n");
	zassert_true(length > 0, NULL);
	body_length = (size_t)length;
	crc = ef800_composition_crc32((const uint8_t *)body, body_length);
	length = snprintf(input, sizeof(input),
		"format_version=1\r\nproduct=EF800\r\ngeneration=7\r\ncomposition=  LCD0   BT0  \r\ncrc32=%08X\r\n",
		(unsigned int)crc);
	zassert_true(length > 0, NULL);
	zassert_ok(ef800_composition_profile_decode((const uint8_t *)input,
		(size_t)length, &decoded), NULL);
	zassert_equal(decoded.token_count, 2U, NULL);
}

ZTEST(composition_encoding, test_malformed_duplicate_and_checksum_inputs_are_rejected)
{
	struct ef800_composition_profile decoded;
	struct ef800_composition_profile valid_profile = { .generation = 1U };
	uint8_t valid_record[EF800_COMPOSITION_RECORD_MAX];
	size_t valid_length;
	static const char duplicate_field[] =
		"format_version=1\nproduct=EF800\ngeneration=1\ncomposition=A0\ncomposition=B0\ncrc32=00000000\n";
	static const char duplicate_token[] =
		"format_version=1\nproduct=EF800\ngeneration=1\ncomposition=A0 A0\ncrc32=00000000\n";
	static const char bad_token[] =
		"format_version=1\nproduct=EF800\ngeneration=1\ncomposition=a0\ncrc32=00000000\n";

	zassert_true(ef800_composition_profile_decode((const uint8_t *)duplicate_field,
		sizeof(duplicate_field) - 1U, &decoded) < 0, NULL);
	zassert_equal(ef800_composition_profile_decode((const uint8_t *)duplicate_token,
		sizeof(duplicate_token) - 1U, &decoded), -EEXIST, NULL);
	zassert_true(ef800_composition_profile_decode((const uint8_t *)bad_token,
		sizeof(bad_token) - 1U, &decoded) < 0, NULL);
	valid_length = encode_profile(&valid_profile, valid_record);
	valid_record[valid_length - 3U] ^= 1U;
	zassert_equal(ef800_composition_profile_decode(valid_record, valid_length,
		&decoded), -EBADMSG, NULL);
}

ZTEST(composition_encoding, test_published_token_and_count_bounds)
{
	char maximum[EF800_COMPOSITION_TOKEN_MAX + 1U];
	char too_long[EF800_COMPOSITION_TOKEN_MAX + 2U];
	struct ef800_composition_profile profile = {0};
	char output[EF800_COMPOSITION_RECORD_MAX];
	uint8_t oversized[EF800_COMPOSITION_RECORD_MAX + 1U] = {0};
	size_t length;

	memset(maximum, 'A', sizeof(maximum) - 1U);
	maximum[sizeof(maximum) - 1U] = '\0';
	memset(too_long, 'A', sizeof(too_long) - 1U);
	too_long[sizeof(too_long) - 1U] = '\0';
	zassert_true(ef800_composition_token_valid(maximum), NULL);
	zassert_false(ef800_composition_token_valid(too_long), NULL);
	profile.token_count = EF800_COMPOSITION_MAX_TOKENS;
	for (size_t index = 0U; index < profile.token_count; ++index) {
		snprintk(profile.tokens[index], sizeof(profile.tokens[index]), "T%02u",
			(unsigned int)index);
	}
	zassert_ok(ef800_composition_profile_encode(&profile, output, sizeof(output),
		&length), NULL);
	profile.token_count++;
	zassert_equal(ef800_composition_profile_encode(&profile, output, sizeof(output),
		&length), -EINVAL, NULL);
	zassert_equal(ef800_composition_profile_decode(oversized, sizeof(oversized),
		&profile), -EINVAL, NULL);
}

ZTEST(composition_encoding, test_profile_slot_selection_uses_newest_valid_generation)
{
	static const char *const older_tokens[] = { "LCDCONTR0" };
	static const char *const newer_tokens[] = { "LCDCONTR1" };
	struct ef800_composition_profile older;
	struct ef800_composition_profile newer;
	struct ef800_composition_profile selected;
	int selected_slot = -1;

	profile_with_tokens(&older, 9U, older_tokens, ARRAY_SIZE(older_tokens));
	profile_with_tokens(&newer, 10U, newer_tokens, ARRAY_SIZE(newer_tokens));
	zassert_ok(ef800_composition_profile_select(&older, true, &newer, true,
		&selected, &selected_slot), NULL);
	zassert_equal(selected_slot, 1, NULL);
	zassert_true(ef800_composition_profile_equal(&selected, &newer), NULL);
	zassert_equal(ef800_composition_profile_select(&older, false, &newer, true,
		&selected, &selected_slot), 0, NULL);
	zassert_equal(selected_slot, 1, NULL);
	zassert_equal(ef800_composition_profile_select(&older, false, &newer, false,
		&selected, &selected_slot), -ENOENT, NULL);
}

ZTEST(composition_catalog, test_empty_catalog_keeps_all_tokens_unknown)
{
	static const char *const tokens[] = { "FUTURE1", "OTHER2" };
	struct ef800_composition_profile profile;
	struct ef800_composition_snapshot snapshot;

	profile_with_tokens(&profile, 1U, tokens, ARRAY_SIZE(tokens));
	zassert_ok(ef800_composition_resolve(&profile, &empty_catalog, &snapshot), NULL);
	zassert_equal(snapshot.effective_count, 0U, NULL);
	zassert_equal(snapshot.unknown_count, 2U, NULL);
}

ZTEST(composition_catalog, test_known_selection_and_omitted_default_resolve)
{
	static const struct ef800_composition_catalog_entry entries[] = {
		{ "ONE0", "ONE", 10U, true, true },
		{ "ONE1", "ONE", 11U, false, true },
		{ "TWO0", "TWO", 20U, true, true },
	};
	static const struct ef800_composition_catalog catalog = { entries, ARRAY_SIZE(entries) };
	static const char *const tokens[] = { "ONE1", "FUTURE0" };
	struct ef800_composition_profile profile;
	struct ef800_composition_snapshot snapshot;

	profile_with_tokens(&profile, 1U, tokens, ARRAY_SIZE(tokens));
	zassert_ok(ef800_composition_resolve(&profile, &catalog, &snapshot), NULL);
	zassert_equal(snapshot.effective_count, 2U, NULL);
	zassert_equal(snapshot.effective[0].catalog_entry->selection, 11U, NULL);
	zassert_false(snapshot.effective[0].defaulted, NULL);
	zassert_equal(snapshot.effective[1].catalog_entry->selection, 20U, NULL);
	zassert_true(snapshot.effective[1].defaulted, NULL);
	zassert_equal(snapshot.unknown_count, 1U, NULL);
}

ZTEST(composition_catalog, test_catalog_and_selection_conflicts_are_rejected)
{
	static const struct ef800_composition_catalog_entry duplicate_token[] = {
		{ "ONE0", "ONE", 0U, true, true }, { "ONE0", "TWO", 0U, true, true },
	};
	static const struct ef800_composition_catalog_entry duplicate_default[] = {
		{ "ONE0", "ONE", 0U, true, true }, { "ONE1", "ONE", 0U, true, true },
	};
	static const struct ef800_composition_catalog_entry entries[] = {
		{ "ONE0", "ONE", 0U, true, true }, { "ONE1", "ONE", 0U, false, true },
	};
	static const char *const tokens[] = { "ONE0", "ONE1" };
	struct ef800_composition_profile profile;
	struct ef800_composition_snapshot snapshot;

	zassert_equal(ef800_composition_catalog_validate(&(struct ef800_composition_catalog){
		duplicate_token, ARRAY_SIZE(duplicate_token) }), -EEXIST, NULL);
	zassert_equal(ef800_composition_catalog_validate(&(struct ef800_composition_catalog){
		duplicate_default, ARRAY_SIZE(duplicate_default) }), -EEXIST, NULL);
	profile_with_tokens(&profile, 1U, tokens, ARRAY_SIZE(tokens));
	zassert_equal(ef800_composition_resolve(&profile,
		&(struct ef800_composition_catalog){ entries, ARRAY_SIZE(entries) }, &snapshot),
		-EEXIST, NULL);
}

ZTEST(composition_catalog, test_usb_msd_selection_is_target_specific)
{
	static const struct ef800_composition_catalog_entry native_entries[] = {
		{ "USBMSD0", "USBMSD", 0U, true, true },
		{ "USBMSD1", "USBMSD", 1U, false, false },
	};
	static const struct ef800_composition_catalog_entry physical_entries[] = {
		{ "USBMSD0", "USBMSD", 0U, false, true },
		{ "USBMSD1", "USBMSD", 1U, true, true },
	};
	static const char *const native_stored_tokens[] = { "USBMSD1" };
	static const char *const physical_stored_tokens[] = { "USBMSD0" };
	struct ef800_composition_profile profile;
	struct ef800_composition_snapshot snapshot;

	profile_with_tokens(&profile, 1U, native_stored_tokens,
			    ARRAY_SIZE(native_stored_tokens));
	zassert_ok(ef800_composition_resolve(&profile,
		&(struct ef800_composition_catalog){
			.entries = native_entries,
			.entry_count = ARRAY_SIZE(native_entries),
		}, &snapshot), NULL);
	zassert_equal(snapshot.effective_count, 1U, NULL);
	zassert_equal(strcmp(snapshot.effective[0].catalog_entry->token, "USBMSD0"),
		       0, NULL);
	zassert_true(snapshot.effective[0].defaulted, NULL);
	zassert_equal(snapshot.unknown_count, 1U, NULL);
	zassert_equal(strcmp(snapshot.unknown[0], "USBMSD1"), 0, NULL);

	profile_with_tokens(&profile, 2U, physical_stored_tokens,
			    ARRAY_SIZE(physical_stored_tokens));
	zassert_ok(ef800_composition_resolve(&profile,
		&(struct ef800_composition_catalog){
			.entries = physical_entries,
			.entry_count = ARRAY_SIZE(physical_entries),
		}, &snapshot), NULL);
	zassert_equal(snapshot.effective_count, 1U, NULL);
	zassert_equal(strcmp(snapshot.effective[0].catalog_entry->token, "USBMSD0"),
		       0, NULL);
	zassert_false(snapshot.effective[0].defaulted, NULL);
	zassert_equal(snapshot.unknown_count, 0U, NULL);
}

ZTEST(composition_catalog, test_lcd_contrast_catalog_defaults_and_resolves_token)
{
	static const struct ef800_composition_catalog_entry entries[] = {
		{ "LCDCONTR0", "LCDCONTR", 0U, true, true },
		{ "LCDCONTR1", "LCDCONTR", 1U, false, true },
	};
	static const struct ef800_composition_catalog catalog = {
		.entries = entries,
		.entry_count = ARRAY_SIZE(entries),
	};
	static const char *const enabled_tokens[] = { "LCDCONTR1" };
	struct ef800_composition_profile profile;
	struct ef800_composition_snapshot snapshot;

	zassert_ok(ef800_composition_resolve(&(struct ef800_composition_profile){0},
		&catalog, &snapshot), NULL);
	zassert_equal(snapshot.effective_count, 1U, NULL);
	zassert_equal(strcmp(snapshot.effective[0].catalog_entry->token,
		"LCDCONTR0"), 0, NULL);
	zassert_true(snapshot.effective[0].defaulted, NULL);

	profile_with_tokens(&profile, 1U, enabled_tokens, ARRAY_SIZE(enabled_tokens));
	zassert_ok(ef800_composition_resolve(&profile, &catalog, &snapshot), NULL);
	zassert_equal(strcmp(snapshot.effective[0].catalog_entry->token,
		"LCDCONTR1"), 0, NULL);
	zassert_false(snapshot.effective[0].defaulted, NULL);
}

ZTEST(composition_service, test_battery_soc_defaults_and_restart_activation)
{
	static const struct ef800_composition_catalog_entry entries[] = {
		{ "BATSOC0", "BATSOC", 0U, false, true },
		{ "BATSOC1", "BATSOC", 1U, false, true },
		{ "BATSOC2", "BATSOC", 2U, true, true },
	};
	static const struct ef800_composition_catalog catalog = {
		.entries = entries,
		.entry_count = ARRAY_SIZE(entries),
	};
	struct ef800_persistence_fake fake;
	struct ef800_persistence persistence;
	struct ef800_composition_service service;
	struct ef800_composition_service restarted;
	const struct ef800_composition_snapshot *active;
	const char *active_battery_token = NULL;

	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&persistence, &fake.backend), NULL);
	zassert_ok(ef800_composition_service_init(&service, &persistence, &catalog), NULL);
	active = ef800_composition_service_active(&service);
	zassert_equal(active->effective_count, 1U, NULL);
	zassert_equal(strcmp(active->effective[0].catalog_entry->token, "BATSOC2"), 0,
		NULL);
	zassert_true(active->effective[0].defaulted, NULL);

	zassert_ok(ef800_composition_service_stage_group_selection(&service,
		"BATSOC", "BATSOC1"), NULL);
	zassert_ok(ef800_composition_service_commit(&service), NULL);
	zassert_true(ef800_composition_service_restart_required(&service), NULL);
	active = ef800_composition_service_active(&service);
	zassert_equal(strcmp(active->effective[0].catalog_entry->token, "BATSOC2"), 0,
		NULL);

	zassert_ok(ef800_composition_service_init(&restarted, &persistence, &catalog), NULL);
	active = ef800_composition_service_active(&restarted);
	for (size_t index = 0U; index < active->effective_count; ++index) {
		const struct ef800_composition_catalog_entry *entry =
			active->effective[index].catalog_entry;

		if (entry != NULL && strcmp(entry->group, "BATSOC") == 0) {
			active_battery_token = entry->token;
		}
	}
	zassert_not_null(active_battery_token, NULL);
	zassert_equal(strcmp(active_battery_token, "BATSOC1"), 0, NULL);
}

struct controlled_backend {
	struct ef800_persistence_fake fake;
	struct ef800_persistence_backend inner;
	struct ef800_persistence_backend exposed;
	bool fail_write;
	bool torn_write;
	bool corrupt_slot_b_read_once;
};

static int controlled_read(void *context, enum ef800_persistence_root root,
	const char *name, void *buffer, size_t capacity, size_t *length)
{
	struct controlled_backend *controlled = context;
	int ret = controlled->inner.read(controlled->inner.context, root, name, buffer,
		capacity, length);
	if (ret == 0 && controlled->corrupt_slot_b_read_once && *length != 0U &&
	    strcmp(name, EF800_COMPOSITION_SLOT_B) == 0) {
		((uint8_t *)buffer)[0] ^= 1U;
		controlled->corrupt_slot_b_read_once = false;
	}
	return ret;
}

static int controlled_write(void *context, enum ef800_persistence_root root,
	const char *name, const void *buffer, size_t length)
{
	struct controlled_backend *controlled = context;
	if (controlled->fail_write) {
		return -EIO;
	}
	if (controlled->torn_write) {
		(void)controlled->inner.write(controlled->inner.context, root, name, buffer,
			length / 2U);
		return -EIO;
	}
	return controlled->inner.write(controlled->inner.context, root, name, buffer,
		length);
}

static void controlled_init(struct controlled_backend *controlled,
	struct ef800_persistence *persistence)
{
	ef800_persistence_fake_init(&controlled->fake);
	controlled->inner = controlled->fake.backend;
	controlled->exposed = controlled->inner;
	controlled->exposed.context = controlled;
	controlled->exposed.read = controlled_read;
	controlled->exposed.write = controlled_write;
	controlled->fail_write = false;
	controlled->torn_write = false;
	controlled->corrupt_slot_b_read_once = false;
	zassert_ok(ef800_persistence_init(persistence, &controlled->exposed), NULL);
}

ZTEST(composition_service, test_missing_profile_uses_nonfatal_empty_default)
{
	struct ef800_persistence_fake fake;
	struct ef800_persistence persistence;
	struct ef800_composition_service service;

	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&persistence, &fake.backend), NULL);
	zassert_ok(ef800_composition_service_init(&service, &persistence,
		&empty_catalog), NULL);
	zassert_equal(ef800_composition_service_load_status(&service),
		EF800_COMPOSITION_LOAD_DEFAULT, NULL);
	zassert_equal(ef800_composition_service_active(&service)->effective_count, 0U,
		NULL);
}

ZTEST(composition_service, test_stage_mutations_discard_and_unknown_preservation)
{
	struct ef800_persistence_fake fake;
	struct ef800_persistence persistence;
	struct ef800_composition_service service;
	static const char *const tokens[] = { "FUTURE1", "OTHER2" };

	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&persistence, &fake.backend), NULL);
	zassert_ok(ef800_composition_service_init(&service, &persistence,
		&empty_catalog), NULL);
	zassert_ok(ef800_composition_service_stage_replace(&service, tokens,
		ARRAY_SIZE(tokens)), NULL);
	zassert_equal(ef800_composition_service_pending(&service)->unknown_count, 2U, NULL);
	zassert_ok(ef800_composition_service_stage_add(&service, "THIRD3"), NULL);
	zassert_equal(ef800_composition_service_pending(&service)->stored.token_count, 3U,
		NULL);
	zassert_ok(ef800_composition_service_stage_remove(&service, "OTHER2"), NULL);
	zassert_equal(ef800_composition_service_pending(&service)->stored.token_count, 2U,
		NULL);
	zassert_equal(ef800_composition_service_active(&service)->stored.token_count, 0U,
		NULL);
	ef800_composition_service_discard(&service);
	zassert_is_null(ef800_composition_service_pending(&service), NULL);
}

ZTEST(composition_service, test_group_selection_preserves_other_and_unknown_tokens)
{
	static const struct ef800_composition_catalog_entry entries[] = {
		{ "LCDCONTR0", "LCDCONTR", 0U, true, true },
		{ "LCDCONTR1", "LCDCONTR", 1U, false, true },
		{ "OTHER0", "OTHER", 0U, true, true },
	};
	static const struct ef800_composition_catalog catalog = {
		.entries = entries,
		.entry_count = ARRAY_SIZE(entries),
	};
	static const char *const stored_tokens[] = {
		"FUTURE7", "LCDCONTR0", "OTHER0",
	};
	struct ef800_persistence_fake fake;
	struct ef800_persistence persistence;
	struct ef800_composition_service service;
	struct ef800_composition_service restarted;
	struct ef800_composition_profile profile;
	const struct ef800_composition_profile *pending;
	const char *active_lcd_token = NULL;

	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&persistence, &fake.backend), NULL);
	profile_with_tokens(&profile, 1U, stored_tokens, ARRAY_SIZE(stored_tokens));
	write_profile(&persistence, EF800_COMPOSITION_SLOT_A, &profile);
	zassert_ok(ef800_composition_service_init(&service, &persistence, &catalog), NULL);

	zassert_ok(ef800_composition_service_stage_group_selection(&service,
		"LCDCONTR", "LCDCONTR1"), NULL);
	pending = &ef800_composition_service_pending(&service)->stored;
	zassert_equal(pending->token_count, 3U, NULL);
	zassert_equal(strcmp(pending->tokens[0], "FUTURE7"), 0, NULL);
	zassert_equal(strcmp(pending->tokens[1], "OTHER0"), 0, NULL);
	zassert_equal(strcmp(pending->tokens[2], "LCDCONTR1"), 0, NULL);

	zassert_ok(ef800_composition_service_commit(&service), NULL);
	zassert_true(ef800_composition_service_restart_required(&service), NULL);
	const struct ef800_composition_snapshot *active =
		ef800_composition_service_active(&service);
	for (size_t index = 0U; index < active->effective_count; ++index) {
		const struct ef800_composition_catalog_entry *entry =
			active->effective[index].catalog_entry;
		if (entry != NULL && strcmp(entry->group, "LCDCONTR") == 0) {
			active_lcd_token = entry->token;
		}
	}
	zassert_not_null(active_lcd_token, NULL);
	zassert_equal(strcmp(active_lcd_token, "LCDCONTR0"), 0, NULL);
	zassert_ok(ef800_composition_service_init(&restarted, &persistence, &catalog), NULL);
	active_lcd_token = NULL;
	for (size_t index = 0U; index < restarted.active.effective_count; ++index) {
		const struct ef800_composition_catalog_entry *entry =
			restarted.active.effective[index].catalog_entry;
		if (entry != NULL && strcmp(entry->group, "LCDCONTR") == 0) {
			active_lcd_token = entry->token;
		}
	}
	zassert_not_null(active_lcd_token, NULL);
	zassert_equal(strcmp(active_lcd_token, "LCDCONTR1"), 0, NULL);
	zassert_equal(restarted.active.unknown_count, 1U, NULL);
}

ZTEST(composition_service, test_commit_advances_generation_only_after_restart)
{
	struct ef800_persistence_fake fake;
	struct ef800_persistence persistence;
	struct ef800_composition_service service;
	struct ef800_composition_service restarted;
	static const char *const tokens[] = { "FUTURE1" };

	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&persistence, &fake.backend), NULL);
	zassert_ok(ef800_composition_service_init(&service, &persistence,
		&empty_catalog), NULL);
	zassert_ok(ef800_composition_service_stage_replace(&service, tokens, 1U), NULL);
	zassert_ok(ef800_composition_service_commit(&service), NULL);
	zassert_true(ef800_composition_service_restart_required(&service), NULL);
	zassert_equal(ef800_composition_service_active(&service)->stored.token_count, 0U,
		NULL);
	zassert_ok(ef800_composition_service_init(&restarted, &persistence,
		&empty_catalog), NULL);
	zassert_equal(restarted.active.stored.generation, 1U, NULL);
	zassert_equal(restarted.active.unknown_count, 1U, NULL);
}

ZTEST(composition_service, test_newest_slot_and_equal_generation_conflict)
{
	struct ef800_persistence_fake fake;
	struct ef800_persistence persistence;
	struct ef800_composition_service service;
	struct ef800_composition_profile first;
	struct ef800_composition_profile second;
	static const char *const one[] = { "ONE1" };
	static const char *const two[] = { "TWO2" };

	ef800_persistence_fake_init(&fake);
	zassert_ok(ef800_persistence_init(&persistence, &fake.backend), NULL);
	profile_with_tokens(&first, UINT32_MAX, one, 1U);
	profile_with_tokens(&second, 0U, two, 1U);
	write_profile(&persistence, EF800_COMPOSITION_SLOT_A, &first);
	write_profile(&persistence, EF800_COMPOSITION_SLOT_B, &second);
	zassert_ok(ef800_composition_service_init(&service, &persistence,
		&empty_catalog), NULL);
	zassert_equal(service.active_slot, 1, NULL);
	zassert_equal(service.active.stored.generation, 0U, NULL);

	profile_with_tokens(&first, 5U, one, 1U);
	profile_with_tokens(&second, 5U, two, 1U);
	write_profile(&persistence, EF800_COMPOSITION_SLOT_A, &first);
	write_profile(&persistence, EF800_COMPOSITION_SLOT_B, &second);
	zassert_ok(ef800_composition_service_init(&service, &persistence,
		&empty_catalog), NULL);
	zassert_equal(service.load_status, EF800_COMPOSITION_LOAD_CONFLICT, NULL);
	zassert_equal(service.active.stored.token_count, 0U, NULL);
}

ZTEST(composition_service, test_write_and_readback_failures_keep_predecessor)
{
	struct controlled_backend controlled;
	struct ef800_persistence persistence;
	struct ef800_composition_service service;
	struct ef800_composition_service restarted;
	static const char *const first[] = { "FIRST1" };
	static const char *const second[] = { "SECOND2" };

	controlled_init(&controlled, &persistence);
	zassert_ok(ef800_composition_service_init(&service, &persistence,
		&empty_catalog), NULL);
	zassert_ok(ef800_composition_service_stage_replace(&service, first, 1U), NULL);
	zassert_ok(ef800_composition_service_commit(&service), NULL);
	zassert_ok(ef800_composition_service_init(&service, &persistence,
		&empty_catalog), NULL);
	zassert_ok(ef800_composition_service_stage_replace(&service, second, 1U), NULL);
	controlled.fail_write = true;
	zassert_equal(ef800_composition_service_commit(&service), -EIO, NULL);
	/* cppcheck-suppress redundantAssignment -- reset fault injection before later recovery checks */
	controlled.fail_write = false;
	zassert_ok(ef800_composition_service_init(&restarted, &persistence,
		&empty_catalog), NULL);
	zassert_equal(restarted.active.stored.generation, 1U, NULL);
	zassert_equal(strcmp(restarted.active.stored.tokens[0], "FIRST1"), 0, NULL);

	zassert_ok(ef800_composition_service_stage_replace(&restarted, second, 1U), NULL);
	controlled.corrupt_slot_b_read_once = true;
	zassert_true(ef800_composition_service_commit(&restarted) < 0, NULL);
}

ZTEST(composition_service, test_torn_record_recovers_previous_valid_slot)
{
	struct controlled_backend controlled;
	struct ef800_persistence persistence;
	struct ef800_composition_service service;
	struct ef800_composition_service restarted;
	static const char *const first[] = { "FIRST1" };
	static const char *const second[] = { "SECOND2" };

	controlled_init(&controlled, &persistence);
	zassert_ok(ef800_composition_service_init(&service, &persistence,
		&empty_catalog), NULL);
	zassert_ok(ef800_composition_service_stage_replace(&service, first, 1U), NULL);
	zassert_ok(ef800_composition_service_commit(&service), NULL);
	zassert_ok(ef800_composition_service_init(&service, &persistence,
		&empty_catalog), NULL);
	zassert_ok(ef800_composition_service_stage_replace(&service, second, 1U), NULL);
	controlled.torn_write = true;
	zassert_equal(ef800_composition_service_commit(&service), -EIO, NULL);
	/* cppcheck-suppress redundantAssignment -- allow recovery after torn-write injection */
	controlled.torn_write = false;
	zassert_ok(ef800_composition_service_init(&restarted, &persistence,
		&empty_catalog), NULL);
	zassert_equal(restarted.active.stored.generation, 1U, NULL);
	zassert_equal(strcmp(restarted.active.stored.tokens[0], "FIRST1"), 0, NULL);
}

static int unavailable_read(void *context, enum ef800_persistence_root root,
	const char *name, void *buffer, size_t capacity, size_t *length)
{
	ARG_UNUSED(context); ARG_UNUSED(root); ARG_UNUSED(name); ARG_UNUSED(buffer);
	ARG_UNUSED(capacity); ARG_UNUSED(length);
	return -ENOTSUP;
}

static int unavailable_write(void *context, enum ef800_persistence_root root,
	const char *name, const void *buffer, size_t length)
{
	ARG_UNUSED(context); ARG_UNUSED(root); ARG_UNUSED(name); ARG_UNUSED(buffer);
	ARG_UNUSED(length);
	return -ENOTSUP;
}

static int unavailable_stat(void *context, enum ef800_persistence_root root,
	const char *name, struct ef800_persistence_entry *entry)
{
	ARG_UNUSED(context); ARG_UNUSED(root); ARG_UNUSED(name); ARG_UNUSED(entry);
	return -ENOTSUP;
}

static int unavailable_list(void *context, enum ef800_persistence_root root,
	ef800_persistence_list_callback callback, void *callback_context)
{
	ARG_UNUSED(context); ARG_UNUSED(root); ARG_UNUSED(callback);
	ARG_UNUSED(callback_context);
	return -ENOTSUP;
}

static int unavailable_remove(void *context, enum ef800_persistence_root root,
	const char *name)
{
	ARG_UNUSED(context); ARG_UNUSED(root); ARG_UNUSED(name);
	return -ENOTSUP;
}

ZTEST(composition_service, test_unavailable_storage_is_nonfatal)
{
	static const struct ef800_persistence_backend backend = {
		.read = unavailable_read,
		.read_range = NULL,
		.write = unavailable_write,
		.append = unavailable_write,
		.stat = unavailable_stat,
		.list = unavailable_list,
		.remove = unavailable_remove,
	};
	struct ef800_persistence persistence = { .backend = &backend };
	struct ef800_composition_service service;

	/* The composition service only needs read/write; production persistence
	 * initialization still requires the complete backend contract. */
	zassert_ok(ef800_composition_service_init(&service, &persistence,
		&empty_catalog), NULL);
	zassert_equal(service.load_status, EF800_COMPOSITION_LOAD_UNAVAILABLE, NULL);
	zassert_equal(service.active.effective_count, 0U, NULL);
}

ZTEST_SUITE(composition_encoding, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(composition_catalog, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(composition_service, NULL, NULL, NULL, NULL, NULL);
