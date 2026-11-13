/* SPDX-License-Identifier: Apache-2.0 */
#include "virtual_ef800_log.h"

#include <string.h>

static void history_append_locked(struct virtual_ef800_log_store *store,
					  const struct virtual_ef800_log_entry *entry)
{
	while (store->history_length != 0U &&
		(store->history_length == VIRTUAL_EF800_LOG_HISTORY_COUNT ||
		 store->history_bytes + entry->text_length > VIRTUAL_EF800_LOG_HISTORY_BYTES)) {
		store->history_bytes -= store->history[0].text_length;
		memmove(&store->history[0], &store->history[1],
			(store->history_length - 1U) * sizeof(store->history[0]));
		store->history_length--;
		store->gap_pending = true;
	}
	store->history[store->history_length++] = *entry;
	store->history_bytes += entry->text_length;
}

void virtual_ef800_log_store_record(struct virtual_ef800_log_store *store,
					    uint8_t severity, const uint8_t *text,
					    size_t length)
{
	struct virtual_ef800_log_entry entry = {0};
	k_spinlock_key_t key;

	if (store == NULL || text == NULL || severity > VIRTUAL_EF800_LOG_PANIC) {
		return;
	}
	entry.severity = severity;
	entry.text_length = (uint16_t)MIN(length, sizeof(entry.text));
	entry.flags = length > sizeof(entry.text) ?
		VIRTUAL_EF800_LOG_TRUNCATED : 0U;
	memcpy(entry.text, text, entry.text_length);
	key = k_spin_lock(&store->lock);
	entry.sequence = ++store->sequence;
	if (store->gap_pending) {
		entry.flags |= VIRTUAL_EF800_LOG_GAP;
		store->gap_pending = false;
	}
	history_append_locked(store, &entry);
	if (store->queue_length == VIRTUAL_EF800_LOG_QUEUE_COUNT) {
		store->queue_head = (store->queue_head + 1U) % VIRTUAL_EF800_LOG_QUEUE_COUNT;
		store->queue_length--;
		store->gap_pending = true;
	}
	store->queue[(store->queue_head + store->queue_length) %
		VIRTUAL_EF800_LOG_QUEUE_COUNT] = entry;
	store->queue_length++;
	k_spin_unlock(&store->lock, key);
}

bool virtual_ef800_log_store_pop(struct virtual_ef800_log_store *store,
					 struct virtual_ef800_log_entry *entry)
{
	k_spinlock_key_t key;

	if (store == NULL || entry == NULL) {
		return false;
	}
	key = k_spin_lock(&store->lock);
	if (store->queue_length == 0U) {
		k_spin_unlock(&store->lock, key);
		return false;
	}
	*entry = store->queue[store->queue_head];
	store->queue_head = (store->queue_head + 1U) % VIRTUAL_EF800_LOG_QUEUE_COUNT;
	store->queue_length--;
	k_spin_unlock(&store->lock, key);
	return true;
}

size_t virtual_ef800_log_store_snapshot(struct virtual_ef800_log_store *store,
						struct virtual_ef800_log_entry *entries,
						size_t count, uint32_t *last_sequence)
{
	k_spinlock_key_t key;
	size_t length;

	if (store == NULL || entries == NULL || count == 0U) {
		return 0U;
	}
	key = k_spin_lock(&store->lock);
	length = MIN(store->history_length, count);
	memcpy(entries, store->history, length * sizeof(entries[0]));
	if (last_sequence != NULL) {
		*last_sequence = length == 0U ? 0U : entries[length - 1U].sequence;
	}
	for (size_t index = 0U; index < length; ++index) {
		entries[index].flags |= VIRTUAL_EF800_LOG_REPLAY;
	}
	while (store->queue_length != 0U && last_sequence != NULL &&
		store->queue[store->queue_head].sequence <= *last_sequence) {
		store->queue_head = (store->queue_head + 1U) % VIRTUAL_EF800_LOG_QUEUE_COUNT;
		store->queue_length--;
	}
	k_spin_unlock(&store->lock, key);
	return length;
}
