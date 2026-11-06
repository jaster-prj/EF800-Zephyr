/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_PLUGIN_METADATA_H
#define EF800_PLUGIN_METADATA_H

#include <stdint.h>

#define EF800_PLUGIN_METADATA_UUID_BYTES \
	0x65u, 0x66u, 0x38u, 0x30u, 0x30u, 0x2du, 0x6du, 0x65u, \
	0x74u, 0x61u, 0x2du, 0x76u, 0x31u, 0x00u, 0x00u, 0x01u
#define EF800_PLUGIN_METADATA_VERSION 3u

#define EF800_PLUGIN_EXECUTION_LEGACY_DIRECT 0u
#define EF800_PLUGIN_EXECUTION_USER_WORKER 1u

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
	uint32_t execution_model;
	uint32_t isolation_abi_version;
	uint32_t target_architecture;
	uint32_t required_worker_services;
	uint32_t minimum_mailbox_size;
	uint32_t minimum_plugin_ram_size;
	uint32_t minimum_worker_stack_size;
	uint32_t text_alignment;
	uint32_t rodata_alignment;
	uint32_t data_alignment;
};


_Static_assert(sizeof(struct ef800_plugin_metadata) == 84U,
	"EF800 plugin metadata v3 size is part of the package ABI");

#endif /* EF800_PLUGIN_METADATA_H */
