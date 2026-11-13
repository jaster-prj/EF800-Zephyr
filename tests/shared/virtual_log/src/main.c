/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include <zephyr/ztest.h>

#include "virtual_ef800_log.h"

ZTEST(shared_virtual_log, test_queue_is_bounded_and_sequences_records)
{
	struct virtual_ef800_log_store store = {0};
	struct virtual_ef800_log_entry entry;

	for (uint32_t sequence = 0U; sequence < VIRTUAL_EF800_LOG_QUEUE_COUNT + 8U;
		sequence++) {
		virtual_ef800_log_store_record(&store, VIRTUAL_EF800_LOG_INFO,
			(const uint8_t *)"record", 6U);
	}
	zassert_equal(store.queue_length, VIRTUAL_EF800_LOG_QUEUE_COUNT, NULL);
	zassert_true(virtual_ef800_log_store_pop(&store, &entry), NULL);
	zassert_equal(entry.sequence, 9U, NULL);
	zassert_equal(store.queue_length, VIRTUAL_EF800_LOG_QUEUE_COUNT - 1U, NULL);
}

ZTEST(shared_virtual_log, test_history_snapshot_marks_replay_and_bounds_text)
{
	struct virtual_ef800_log_store store = {0};
	struct virtual_ef800_log_entry entries[VIRTUAL_EF800_LOG_HISTORY_COUNT];
	uint32_t last_sequence;
	const uint8_t long_text[VIRTUAL_EF800_LOG_MAX_TEXT + 4U] = {0};
	size_t count;

	virtual_ef800_log_store_record(&store, VIRTUAL_EF800_LOG_WARNING,
		long_text, sizeof(long_text));
	count = virtual_ef800_log_store_snapshot(&store, entries,
		VIRTUAL_EF800_LOG_HISTORY_COUNT, &last_sequence);
	zassert_equal(count, 1U, NULL);
	zassert_equal(last_sequence, 1U, NULL);
	zassert_true((entries[0].flags & VIRTUAL_EF800_LOG_REPLAY) != 0U, NULL);
	zassert_true((entries[0].flags & VIRTUAL_EF800_LOG_TRUNCATED) != 0U, NULL);
	zassert_equal(entries[0].text_length, VIRTUAL_EF800_LOG_MAX_TEXT, NULL);
	zassert_mem_equal(entries[0].text, long_text, entries[0].text_length, NULL);
}

ZTEST(shared_virtual_log, test_replay_removes_duplicates_and_sequence_continues)
{
	struct virtual_ef800_log_store store = {0};
	struct virtual_ef800_log_entry entries[VIRTUAL_EF800_LOG_HISTORY_COUNT];
	struct virtual_ef800_log_entry entry;
	uint32_t last_sequence;

	virtual_ef800_log_store_record(&store, VIRTUAL_EF800_LOG_INFO,
		(const uint8_t *)"one", 3U);
	virtual_ef800_log_store_record(&store, VIRTUAL_EF800_LOG_INFO,
		(const uint8_t *)"two", 3U);
	zassert_equal(virtual_ef800_log_store_snapshot(&store, entries,
		VIRTUAL_EF800_LOG_HISTORY_COUNT, &last_sequence), 2U, NULL);
	virtual_ef800_log_store_record(&store, VIRTUAL_EF800_LOG_INFO,
		(const uint8_t *)"three", 5U);
	zassert_true(virtual_ef800_log_store_pop(&store, &entry), NULL);
	zassert_equal(entry.sequence, 3U, NULL);
	zassert_equal(last_sequence, 2U, NULL);
}

ZTEST_SUITE(shared_virtual_log, NULL, NULL, NULL, NULL, NULL);
