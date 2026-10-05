/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_CALCULATION_SERVICE_H
#define EF800_CALCULATION_SERVICE_H

#include <stddef.h>
#include <stdint.h>

typedef int (*ef800_workspace_snapshot_fn)(void *context, char *destination,
                                           size_t capacity, size_t *length);
typedef int (*ef800_calculation_transform_fn)(void *context,
                                              const char *snapshot,
                                              size_t snapshot_length,
                                              int32_t offset_mm, char *output,
                                              size_t capacity, size_t *length);

struct ef800_calculation_service {
    ef800_workspace_snapshot_fn snapshot;
    ef800_calculation_transform_fn transform;
    void *context;
};

int ef800_calculation_service_run(
    const struct ef800_calculation_service *service, int32_t offset_mm,
    char *snapshot, size_t snapshot_capacity, char *output,
    size_t output_capacity, size_t *output_length);

#endif
