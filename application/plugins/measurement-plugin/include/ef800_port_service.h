/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PORT_SERVICE_H
#define EF800_PORT_SERVICE_H

#include <stdint.h>

#define EF800_PORT_API_VERSION 1u

struct ef800_port_api {
    uint32_t abi_version;
    uint32_t struct_size;
    void *context;
    int32_t (*write)(void *context, const uint8_t *data, uint32_t length);
    int32_t (*read)(void *context, uint8_t *data, uint32_t capacity,
                    int32_t timeout_ms);
    uint32_t reserved[4];
};

#endif
