/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_VIRTUAL_H
#define EF800_VIRTUAL_H

#include <stdbool.h>
#include <stdint.h>

struct ef800_virtual_status {
	bool initialized;
	bool front_panel_connected;
	bool storage_available;
	bool rtc_available;
	bool battery_available;
	bool serial_peers_available;
	int32_t battery_soc_percent;
	uint32_t battery_voltage_uv;
	int32_t serial_delay_ms;
	int32_t serial_error;
};

void ef800_virtual_status_init(struct ef800_virtual_status *status);
const char *ef800_virtual_capabilities(void);
void ef800_virtual_status_set_battery(struct ef800_virtual_status *status,
					       int32_t soc_percent,
					       uint32_t voltage_uv,
					       bool available);
void ef800_virtual_status_set_serial_fault(struct ef800_virtual_status *status,
						   int32_t delay_ms,
						   int32_t error,
						   bool available);

#endif
