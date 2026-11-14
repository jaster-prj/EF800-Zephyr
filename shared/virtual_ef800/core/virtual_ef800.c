/* SPDX-License-Identifier: Apache-2.0 */
#include "virtual_ef800.h"

#include <string.h>

void virtual_ef800_status_init(struct virtual_ef800_status *status)
{
	if (status == NULL) {
		return;
	}
	memset(status, 0, sizeof(*status));
	status->initialized = true;
	status->storage_available = true;
	status->rtc_available = true;
	status->serial_peers_available = true;
	status->battery_soc_percent = -1;
	status->battery_voltage_uv = 0U;
	status->serial_delay_ms = 0;
	status->serial_error = 0;
}

void virtual_ef800_status_set_battery(struct virtual_ef800_status *status,
					       int32_t soc_percent,
					       uint32_t voltage_uv,
					       bool available)
{
	if (status == NULL) {
		return;
	}
	status->battery_available = available;
	status->battery_soc_percent = soc_percent;
	status->battery_voltage_uv = voltage_uv;
}

void virtual_ef800_status_set_serial_fault(struct virtual_ef800_status *status,
						   int32_t delay_ms,
						   int32_t error,
						   bool available)
{
	if (status == NULL) {
		return;
	}
	status->serial_peers_available = available;
	status->serial_delay_ms = delay_ms < 0 ? 0 : delay_ms;
	status->serial_error = error;
}

const char *virtual_ef800_capabilities(void)
{
	return "lcd=virtual keypad=virtual storage=deterministic rtc=deterministic "
		"battery=optional serial=scripted usb=unsupported qspi=unsupported";
}
