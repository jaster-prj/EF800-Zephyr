/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_battery_soc_store.h"

#include <errno.h>
#include <string.h>

#define STORE_RECORD_VERSION 1U
#define STORE_HEADER_SIZE 10U
#define STORE_CRC_SIZE 4U

static const char store_magic[4] = { 'B', 'S', 'O', 'C' };
static const char *const store_slot_names[] = {
	EF800_BATTERY_SOC_STORE_SLOT_A,
	EF800_BATTERY_SOC_STORE_SLOT_B,
};

struct store_candidate {
	struct ef800_battery_soc_table table;
	uint32_t generation;
	int error;
	bool valid;
};

static void write_u32_be(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)(value >> 24U);
	data[1] = (uint8_t)(value >> 16U);
	data[2] = (uint8_t)(value >> 8U);
	data[3] = (uint8_t)value;
}

static uint32_t read_u32_be(const uint8_t *data)
{
	return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
		((uint32_t)data[2] << 8U) | (uint32_t)data[3];
}

static bool tables_equal(const struct ef800_battery_soc_table *left,
	const struct ef800_battery_soc_table *right)
{
	if (left->version != right->version || left->chemistry != right->chemistry ||
	    left->point_count != right->point_count) {
		return false;
	}
	for (size_t index = 0U; index < left->point_count; ++index) {
		if (left->points[index].pin_mv != right->points[index].pin_mv ||
		    left->points[index].soc_percent != right->points[index].soc_percent) {
			return false;
		}
	}
	return true;
}

static int decode_record(const uint8_t *record, size_t length,
	struct store_candidate *candidate)
{
	uint8_t hex_length;
	size_t expected_length;
	uint32_t stored_crc;
	int ret;

	if (record == NULL || candidate == NULL || length < STORE_HEADER_SIZE +
	    EF800_BATTERY_SOC_TABLE_MIN_POINTS * 6U + STORE_CRC_SIZE ||
	    length > EF800_BATTERY_SOC_STORE_RECORD_MAX ||
	    memcmp(record, store_magic, sizeof(store_magic)) != 0 ||
	    record[4] != STORE_RECORD_VERSION) {
		return -EINVAL;
	}
	hex_length = record[9];
	if (hex_length > EF800_BATTERY_SOC_TABLE_MAX_HEX_LENGTH ||
	    (hex_length & 1U) != 0U) {
		return -EINVAL;
	}
	expected_length = STORE_HEADER_SIZE + (size_t)hex_length + STORE_CRC_SIZE;
	if (length != expected_length) {
		return -EINVAL;
	}
	stored_crc = read_u32_be(&record[expected_length - STORE_CRC_SIZE]);
	if (stored_crc != ef800_battery_soc_crc32(record,
		expected_length - STORE_CRC_SIZE)) {
		return -EBADMSG;
	}
	ret = ef800_battery_soc_table_decode_hex((const char *)&record[STORE_HEADER_SIZE],
		hex_length, &candidate->table);
	if (ret != 0) {
		return ret;
	}
	candidate->generation = read_u32_be(&record[5]);
	candidate->valid = true;
	candidate->error = 0;
	return 0;
}

static int read_candidate(struct ef800_battery_soc_store *store, int slot,
	struct store_candidate *candidate)
{
	uint8_t record[EF800_BATTERY_SOC_STORE_RECORD_MAX];
	size_t length = 0U;
	int ret;

	*candidate = (struct store_candidate){0};
	ret = ef800_persistence_read(store->persistence,
		EF800_PERSISTENCE_CONFIGURATION, store_slot_names[slot], record,
		sizeof(record), &length);
	if (ret == 0) {
		ret = decode_record(record, length, candidate);
	}
	candidate->error = ret;
	candidate->valid = ret == 0;
	return ret;
}

static int select_newest(const struct store_candidate candidates[2], int *slot)
{
	if (!candidates[0].valid && !candidates[1].valid) {
		*slot = -1;
		return -ENOENT;
	}
	if (candidates[0].valid && !candidates[1].valid) {
		*slot = 0;
		return 0;
	}
	if (!candidates[0].valid && candidates[1].valid) {
		*slot = 1;
		return 0;
	}
	{
		uint32_t difference = candidates[0].generation - candidates[1].generation;

		if (difference == 0U) {
			if (!tables_equal(&candidates[0].table, &candidates[1].table)) {
				return -EIO;
			}
			*slot = 0;
		} else if (difference == 0x80000000U) {
			return -EIO;
		} else {
			*slot = difference < 0x80000000U ? 0 : 1;
		}
	}
	return 0;
}

static void load_candidates(struct ef800_battery_soc_store *store,
	struct store_candidate candidates[2])
{
	(void)read_candidate(store, 0, &candidates[0]);
	(void)read_candidate(store, 1, &candidates[1]);
}

static int encode_record(const struct ef800_battery_soc_table *table,
	uint32_t generation, uint8_t *record, size_t capacity, size_t *length)
{
	char hex[EF800_BATTERY_SOC_TABLE_MAX_HEX_LENGTH + 1U];
	size_t hex_length = 0U;
	size_t record_length;
	uint32_t crc;
	int ret;

	ret = ef800_battery_soc_table_encode_hex(table, hex, sizeof(hex), &hex_length);
	if (ret != 0) {
		return ret;
	}
	record_length = STORE_HEADER_SIZE + hex_length + STORE_CRC_SIZE;
	if (record == NULL || length == NULL || capacity < record_length) {
		return -ENOMEM;
	}
	memcpy(record, store_magic, sizeof(store_magic));
	record[4] = STORE_RECORD_VERSION;
	write_u32_be(&record[5], generation);
	record[9] = (uint8_t)hex_length;
	memcpy(&record[STORE_HEADER_SIZE], hex, hex_length);
	crc = ef800_battery_soc_crc32(record, record_length - STORE_CRC_SIZE);
	write_u32_be(&record[record_length - STORE_CRC_SIZE], crc);
	*length = record_length;
	return 0;
}

static void activate_candidate(struct ef800_battery_soc_store *store,
	const struct store_candidate *candidate, int slot)
{
	store->table = candidate->table;
	store->generation = candidate->generation;
	store->active_slot = slot;
	store->table_available = true;
	store->load_error = 0;
}

int ef800_battery_soc_store_init(struct ef800_battery_soc_store *store,
	struct ef800_persistence *persistence)
{
	struct store_candidate candidates[2];
	int selected_slot;
	int ret;

	if (store == NULL || persistence == NULL) {
		return -EINVAL;
	}
	*store = (struct ef800_battery_soc_store){
		.persistence = persistence,
		.active_slot = -1,
	};
	k_mutex_init(&store->lock);
	load_candidates(store, candidates);
	ret = select_newest(candidates, &selected_slot);
	if (ret == 0) {
		activate_candidate(store, &candidates[selected_slot], selected_slot);
	} else {
		store->load_error = ret;
	}
	return 0;
}

bool ef800_battery_soc_store_has_table(
	struct ef800_battery_soc_store *store)
{
	bool available;

	if (store == NULL) {
		return false;
	}
	k_mutex_lock(&store->lock, K_FOREVER);
	available = store->table_available;
	k_mutex_unlock(&store->lock);
	return available;
}

int ef800_battery_soc_store_get_table(
	struct ef800_battery_soc_store *store,
	struct ef800_battery_soc_table *table)
{
	int ret = 0;

	if (store == NULL || table == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&store->lock, K_FOREVER);
	if (!store->table_available) {
		ret = store->load_error != 0 && store->load_error != -ENOENT ?
			store->load_error : -ENOENT;
	} else {
		*table = store->table;
	}
	k_mutex_unlock(&store->lock);
	return ret;
}

int ef800_battery_soc_store_get_hex(
	struct ef800_battery_soc_store *store, char *hex, size_t capacity,
	size_t *hex_length)
{
	struct ef800_battery_soc_table table;
	int ret;

	if (store == NULL) {
		return -EINVAL;
	}
	ret = ef800_battery_soc_store_get_table(store, &table);
	if (ret != 0) {
		return ret;
	}
	return ef800_battery_soc_table_encode_hex(&table, hex, capacity,
		 hex_length);
}

int ef800_battery_soc_store_set_hex(
	struct ef800_battery_soc_store *store, const char *hex, size_t hex_length)
{
	struct ef800_battery_soc_table table;
	struct store_candidate candidates[2];
	struct store_candidate verified;
	uint8_t record[EF800_BATTERY_SOC_STORE_RECORD_MAX];
	size_t record_length = 0U;
	uint32_t generation;
	int active_slot;
	int target_slot;
	int ret;

	if (store == NULL || store->persistence == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&store->lock, K_FOREVER);
	ret = ef800_battery_soc_table_decode_hex(hex, hex_length, &table);
	if (ret != 0) {
		goto done;
	}
	load_candidates(store, candidates);
	ret = select_newest(candidates, &active_slot);
	if (ret == -EIO) {
		goto done;
	}
	target_slot = active_slot == 0 ? 1 : 0;
	generation = active_slot >= 0 ? candidates[active_slot].generation + 1U : 1U;
	ret = encode_record(&table, generation, record, sizeof(record), &record_length);
	if (ret != 0) {
		goto done;
	}
	ret = ef800_persistence_write(store->persistence,
		EF800_PERSISTENCE_CONFIGURATION, store_slot_names[target_slot], record,
		record_length);
	if (ret == 0) {
		ret = ef800_persistence_sync(store->persistence,
			EF800_PERSISTENCE_CONFIGURATION, store_slot_names[target_slot]);
	}
	if (ret != 0) {
		goto done;
	}
	ret = read_candidate(store, target_slot, &verified);
	if (ret != 0 || verified.generation != generation ||
	    !tables_equal(&verified.table, &table)) {
		ret = ret != 0 ? ret : -EBADMSG;
		goto done;
	}
	activate_candidate(store, &verified, target_slot);
	ret = 0;
done:
	k_mutex_unlock(&store->lock);
	return ret;
}
