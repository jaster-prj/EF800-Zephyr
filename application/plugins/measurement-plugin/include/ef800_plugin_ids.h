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

#define EF800_PLUGIN_METADATA_UUID_BYTES \
    0x65u, 0x66u, 0x38u, 0x30u, 0x30u, 0x2du, 0x6du, 0x65u, \
    0x74u, 0x61u, 0x2du, 0x76u, 0x31u, 0x00u, 0x00u, 0x01u
#define EF800_PLUGIN_METADATA_VERSION 2u
#define EF800_PLUGIN_HARDWARE_SERIAL (1u << 0)

struct ef800_plugin_metadata {
    uint8_t metadata_id[16];
    uint16_t version;
    uint16_t size;
    uint32_t required_hardware;
    uint32_t measurement_ui_version;
    uint32_t measurement_ui_size;
    uint32_t measurement_actions;
    uint32_t serial_baudrate;
    uint8_t serial_data_bits;
    uint8_t serial_parity;
    uint8_t serial_stop_bits;
    uint8_t serial_flow_control;
};

static const pm_uuid_t EF800_APPLICATION_ID = {{EF800_APPLICATION_UUID_BYTES}};
static const pm_uuid_t EF800_MEASUREMENT_INTERFACE_ID =
    {{EF800_MEASUREMENT_INTERFACE_UUID_BYTES}};
static const pm_uuid_t EF800_PORT_SERVICE_ID = {{EF800_PORT_SERVICE_UUID_BYTES}};

#define EF800_MEASUREMENT_INTERFACE_VERSION 1u
#define EF800_PORT_SERVICE_VERSION 1u
#define EF800_PLUGIN_ID_ELTA3 2u
#define EF800_PLUGIN_ID_FTS3XX 3u

#endif
