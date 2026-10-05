/* SPDX-License-Identifier: Apache-2.0 */
#include "calculation_service.h"

#include <errno.h>

int ef800_calculation_service_run(
    const struct ef800_calculation_service *service, int32_t offset_mm,
    char *snapshot, size_t snapshot_capacity, char *output,
    size_t output_capacity, size_t *output_length)
{
    size_t snapshot_length = 0u;
    int status;

    if (service == NULL || service->snapshot == NULL ||
        service->transform == NULL || snapshot == NULL || output == NULL ||
        output_length == NULL || snapshot_capacity == 0u ||
        output_capacity == 0u) {
        return -EINVAL;
    }
    *output_length = 0u;
    status = service->snapshot(service->context, snapshot, snapshot_capacity,
                               &snapshot_length);
    if (status != 0) {
        return status;
    }
    if (snapshot_length > snapshot_capacity) {
        return -EOVERFLOW;
    }
    return service->transform(service->context, snapshot, snapshot_length,
                              offset_mm, output, output_capacity,
                              output_length);
}
