/* SPDX-License-Identifier: Apache-2.0 */
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

void ef800_virtual_log_store_record(struct ef800_virtual_log_store *store,
					    uint8_t severity, const uint8_t *text,
					    size_t length);
bool ef800_virtual_log_store_pop(struct ef800_virtual_log_store *store,
					 struct ef800_virtual_log_entry *entry);
size_t ef800_virtual_log_store_snapshot(struct ef800_virtual_log_store *store,
						struct ef800_virtual_log_entry *entries,
						size_t count, uint32_t *last_sequence);

#endif
