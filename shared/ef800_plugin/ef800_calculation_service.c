/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_calculation_service.h"

#include <errno.h>

int ef800_calculation_service_run(
	const struct ef800_calculation_service *service, char *snapshot,
	size_t snapshot_capacity, char *output, size_t output_capacity,
	size_t *output_length)
{
	size_t snapshot_length = 0U;
	int ret;

	if (service == NULL || service->snapshot == NULL ||
		service->transform == NULL || snapshot == NULL || output == NULL ||
		output_length == NULL || snapshot_capacity == 0U || output_capacity == 0U) {
		return -EINVAL;
	}
	*output_length = 0U;
	ret = service->snapshot(service->context, snapshot, snapshot_capacity,
		&snapshot_length);
	if (ret != 0) {
		return ret;
	}
	if (snapshot_length > snapshot_capacity) {
		return -EOVERFLOW;
	}
	ret = service->transform(service->context, snapshot, snapshot_length,
		output, output_capacity, output_length);
	if (ret == 0 && *output_length > output_capacity) {
		return -EOVERFLOW;
	}
	return ret;
}
