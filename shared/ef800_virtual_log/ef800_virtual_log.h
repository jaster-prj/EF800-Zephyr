/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Buffer, replay, and transmit structured virtual logs. */
#ifndef EF800_VIRTUAL_LOG_H
#define EF800_VIRTUAL_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "ef800_virtual_protocol.h"

struct ef800_virtual_log_entry {
	uint8_t severity;
	uint8_t flags;
	uint32_t sequence;
	uint16_t text_length;
	uint8_t text[EF800_VIRTUAL_LOG_MAX_TEXT];
};

struct ef800_virtual_log_store {
	struct ef800_virtual_log_entry queue[EF800_VIRTUAL_LOG_QUEUE_COUNT];
	size_t queue_head;
	size_t queue_length;
	struct ef800_virtual_log_entry history[EF800_VIRTUAL_LOG_HISTORY_COUNT];
	size_t history_length;
	size_t history_bytes;
	uint32_t sequence;
	bool gap_pending;
	struct k_spinlock lock;
};

/** @brief Append a log record to bounded live queue and replay history.
 * @param store Log store to update.
 * @param severity Log severity level.
 * @param text Message bytes.
 * @param length Message byte count; oversized messages are truncated.
 * @return None.
 */
void ef800_virtual_log_store_record(struct ef800_virtual_log_store *store,
					    uint8_t severity, const uint8_t *text,
					    size_t length);

/** @brief Remove the next queued record for transmission.
 * @param store Log store.
 * @param entry Receives the record.
 * @return True when a record was available.
 */
bool ef800_virtual_log_store_pop(struct ef800_virtual_log_store *store,
					 struct ef800_virtual_log_entry *entry);

/** @brief Copy recent history records in sequence order.
 * @param store Log store.
 * @param entries Destination record array.
 * @param count Maximum records to copy.
 * @param last_sequence Receives sequence of newest copied record.
 * @return Number of records copied.
 */
size_t ef800_virtual_log_store_snapshot(struct ef800_virtual_log_store *store,
						struct ef800_virtual_log_entry *entries,
						size_t count, uint32_t *last_sequence);

#endif
