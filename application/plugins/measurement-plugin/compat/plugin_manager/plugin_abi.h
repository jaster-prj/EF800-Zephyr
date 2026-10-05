/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PLUGIN_MANAGER_ABI_H
#define EF800_PLUGIN_MANAGER_ABI_H

#include <stddef.h>
#include <stdint.h>

#define PM_FRAMEWORK_ABI_VERSION 1u
#define PM_UUID_SIZE 16u

enum pm_status {
    PM_OK = 0,
    PM_EINVAL = -22,
    PM_ENOENT = -2,
    PM_ENOMEM = -12,
    PM_ENOTSUP = -95,
    PM_EBUSY = -16,
    PM_EOVERFLOW = -75,
    PM_EPROTO = -71,
    PM_EIO = -5,
};

typedef struct pm_uuid {
    uint8_t bytes[PM_UUID_SIZE];
} pm_uuid_t;

struct pm_service {
    pm_uuid_t service_id;
    uint32_t version;
    const void *vtable;
};

struct pm_service_table {
    size_t count;
    const struct pm_service *items;
};

#endif
