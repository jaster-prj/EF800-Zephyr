/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>

#include "ef800_plugin_ids.h"
#include "plugin_runtime.h"

#ifndef EF800_PLUGIN_ID
#error EF800_PLUGIN_ID is required
#endif
#if EF800_PLUGIN_ID == 2u
#define EF800_PLUGIN_NAME "EF800 ELTA3"
#elif EF800_PLUGIN_ID == 3u
#define EF800_PLUGIN_NAME "EF800 FTS3XX"
#else
#error Unsupported EF800 measurement plugin ID
#endif
#define EF800_PLUGIN_VERSION "1.0.0"

struct ef800_wire_descriptor {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t application_id[16];
    uint8_t interface_id[16];
    uint32_t interface_version;
    uint32_t plugin_id;
    uint32_t required_count;
    uint32_t required_offset;
    uint32_t name_offset;
    uint32_t name_size;
    uint32_t version_offset;
    uint32_t version_size;
    uint32_t create_address;
    uint32_t destroy_address;
    uint8_t required_service[16];
    char name[sizeof(EF800_PLUGIN_NAME)];
    char plugin_version[sizeof(EF800_PLUGIN_VERSION)];
};

__attribute__((section(".rodata.pm_descriptor"), used))
const struct ef800_wire_descriptor pm_plugin_get_descriptor = {
    0x53444d50u, 1u, 80u,
    {EF800_APPLICATION_UUID_BYTES},
    {EF800_MEASUREMENT_INTERFACE_UUID_BYTES},
    EF800_MEASUREMENT_INTERFACE_VERSION, EF800_PLUGIN_ID,
    1u, 80u, 96u, sizeof(EF800_PLUGIN_NAME),
    96u + sizeof(EF800_PLUGIN_NAME), sizeof(EF800_PLUGIN_VERSION),
    (uint32_t)(uintptr_t)pm_plugin_create,
    (uint32_t)(uintptr_t)pm_plugin_destroy,
    {EF800_PORT_SERVICE_UUID_BYTES}, EF800_PLUGIN_NAME, EF800_PLUGIN_VERSION
};
