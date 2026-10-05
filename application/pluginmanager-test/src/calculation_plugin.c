/* SPDX-License-Identifier: Apache-2.0 */
#include "calculation_plugin.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>

#include "ef800_plugin_ids.h"

static struct ef800_calculation_instance calculation_instance;
static bool active;

/* Prototype format: signed millimetre readings, one per line. The host
 * controls the snapshot and any file write; the plugin sees only buffers. */
static int transform(void *instance, const char *snapshot, size_t length,
                     int32_t offset_mm, char *output, size_t capacity,
                     size_t *written)
{
    size_t pos = 0u;
    size_t used = 0u;

    if (!active || instance != &calculation_instance || snapshot == NULL ||
        output == NULL || written == NULL) return -EINVAL;
    *written = 0u;
    while (pos < length) {
        int64_t value = 0;
        int sign = 1;
        bool digits = false;
        int count;

        if (snapshot[pos] == '-') {
            sign = -1;
            ++pos;
        }
        while (pos < length && snapshot[pos] >= '0' && snapshot[pos] <= '9') {
            digits = true;
            value = value * 10 + snapshot[pos++] - '0';
            if (value > (int64_t)INT32_MAX + 1) return -ERANGE;
        }
        if (!digits || (pos < length && snapshot[pos] != '\n')) return -EINVAL;
        if (pos < length) ++pos;
        value = value * sign + offset_mm;
        if (value < INT32_MIN || value > INT32_MAX) return -ERANGE;
        if (used >= capacity) return -EFBIG;
        count = snprintf(output + used, capacity - used, "%ld\n", (long)value);
        if (count < 0 || (size_t)count >= capacity - used) return -EFBIG;
        used += (size_t)count;
    }
    *written = used;
    return 0;
}

static const struct ef800_calculation_api api = {
    .abi_version = 1u,
    .struct_size = sizeof(struct ef800_calculation_api),
    .transform = transform,
};

static int create(const struct pm_service_table *services, void **instance)
{
    (void)services;
    if (instance == NULL) return PM_EINVAL;
    if (active) return PM_EBUSY;
    calculation_instance.api = &api;
    active = true;
    *instance = &calculation_instance;
    return PM_OK;
}

static int destroy(void *instance)
{
    if (!active || instance != &calculation_instance) return PM_EINVAL;
    active = false;
    calculation_instance.api = NULL;
    return PM_OK;
}

const struct pm_plugin_descriptor ef800_offset_calculation_descriptor = {
    .framework_abi_version = PM_FRAMEWORK_ABI_VERSION,
    .descriptor_size = sizeof(struct pm_plugin_descriptor),
    .application_id = {{EF800_APPLICATION_UUID_BYTES}},
    .interface_id = {{EF800_CALCULATION_INTERFACE_UUID_BYTES}},
    .interface_version = 1u,
    .plugin_id = EF800_CALCULATION_PLUGIN_ID,
    .name = "Offset calculation",
    .version = "1.0.0",
    .create = create,
    .destroy = destroy,
};
