/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_CALCULATION_PLUGIN_H
#define EF800_CALCULATION_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#include "plugin_manager/plugin_abi.h"

#define EF800_CALCULATION_INTERFACE_UUID_BYTES \
    0x65u, 0x66u, 0x38u, 0x30u, 0x30u, 0x2du, 0x63u, 0x61u, \
    0x6cu, 0x63u, 0x2du, 0x76u, 0x31u, 0x00u, 0x00u, 0x01u
#define EF800_CALCULATION_PLUGIN_ID 100u

struct ef800_calculation_api {
    uint32_t abi_version;
    uint32_t struct_size;
    int (*transform)(void *instance, const char *snapshot, size_t length,
                     int32_t offset_mm, char *output, size_t capacity,
                     size_t *written);
};

struct ef800_calculation_instance {
    const struct ef800_calculation_api *api;
};

extern const struct pm_plugin_descriptor ef800_offset_calculation_descriptor;

#endif
