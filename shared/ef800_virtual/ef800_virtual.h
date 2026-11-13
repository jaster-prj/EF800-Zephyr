/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Track virtual-device capabilities and injected test conditions. */
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

/** @brief Initialize virtual status to unavailable/default values.
 * @param status Status structure to initialize.
 * @return None.
 */
void ef800_virtual_status_init(struct ef800_virtual_status *status);

/** @brief Return the static capability document for the virtual device.
 * @return NUL-terminated capability text.
 */
const char *ef800_virtual_capabilities(void);

/** @brief Update virtual battery readings and availability.
 * @param status Status structure to update.
 * @param soc_percent State of charge, or an unavailable sentinel.
 * @param voltage_uv Battery voltage in microvolts.
 * @param available True when values represent an available battery.
 * @return None.
 */
void ef800_virtual_status_set_battery(struct ef800_virtual_status *status,
					       int32_t soc_percent,
					       uint32_t voltage_uv,
					       bool available);

/** @brief Configure serial peer delay/error injection and availability.
 * @param status Status structure to update.
 * @param delay_ms Injected response delay.
 * @param error Injected error code.
 * @param available Whether serial peers are available.
 * @return None.
 */
void ef800_virtual_status_set_serial_fault(struct ef800_virtual_status *status,
						   int32_t delay_ms,
						   int32_t error,
						   bool available);

#endif
