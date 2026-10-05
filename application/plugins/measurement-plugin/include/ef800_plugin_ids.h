/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PLUGIN_IDS_H
#define EF800_PLUGIN_IDS_H

#include "plugin_manager/plugin_abi.h"

#define EF800_APPLICATION_UUID_BYTES \
    0x65u, 0x66u, 0x38u, 0x30u, 0x30u, 0x2du, 0x61u, 0x70u, \
    0x70u, 0x2du, 0x76u, 0x31u, 0x00u, 0x00u, 0x00u, 0x01u
#define EF800_MEASUREMENT_INTERFACE_UUID_BYTES \
    0x65u, 0x66u, 0x38u, 0x30u, 0x30u, 0x2du, 0x6du, 0x65u, \
    0x61u, 0x73u, 0x2du, 0x76u, 0x31u, 0x00u, 0x00u, 0x01u
#define EF800_PORT_SERVICE_UUID_BYTES \
    0x65u, 0x66u, 0x38u, 0x30u, 0x30u, 0x2du, 0x70u, 0x6fu, \
    0x72u, 0x74u, 0x2du, 0x76u, 0x31u, 0x00u, 0x00u, 0x01u

static const pm_uuid_t EF800_APPLICATION_ID = {{EF800_APPLICATION_UUID_BYTES}};
static const pm_uuid_t EF800_MEASUREMENT_INTERFACE_ID =
    {{EF800_MEASUREMENT_INTERFACE_UUID_BYTES}};
static const pm_uuid_t EF800_PORT_SERVICE_ID = {{EF800_PORT_SERVICE_UUID_BYTES}};

#define EF800_MEASUREMENT_INTERFACE_VERSION 1u
#define EF800_PORT_SERVICE_VERSION 1u
#define EF800_PLUGIN_ID_ELTA3 2u
#define EF800_PLUGIN_ID_FTS3XX 3u

#endif
