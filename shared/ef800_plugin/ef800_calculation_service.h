/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Run a bounded calculation transform over a workspace snapshot. */
#ifndef EF800_CALCULATION_SERVICE_H
#define EF800_CALCULATION_SERVICE_H

#include <stddef.h>
#include <stdint.h>

typedef int (*ef800_calculation_snapshot_fn)(void *context, char *destination,
		size_t capacity, size_t *length);
typedef int (*ef800_calculation_transform_fn)(void *context,
		const char *snapshot, size_t snapshot_length, char *output,
		size_t capacity, size_t *length);

struct ef800_calculation_service {
	ef800_calculation_snapshot_fn snapshot;
	ef800_calculation_transform_fn transform;
	void *context;
};

/** @brief Acquire a snapshot, transform it, and return bounded output text.
 * @param service Snapshot and transform callbacks with shared context.
 * @param snapshot Buffer receiving source snapshot.
 * @param snapshot_capacity Snapshot buffer capacity.
 * @param output Buffer receiving transformed result.
 * @param output_capacity Result buffer capacity.
 * @param output_length Receives result length.
 * @return 0 on successful transform; negative errno on callback or bounds failure.
 */
int ef800_calculation_service_run(
	const struct ef800_calculation_service *service, char *snapshot,
	size_t snapshot_capacity, char *output, size_t output_capacity,
	size_t *output_length);

#endif
