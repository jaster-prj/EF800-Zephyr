/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Read and filter MainApplication battery ADC samples as SOC readings.
 */
#ifndef EF800_MAIN_BATTERY_ADC_H
#define EF800_MAIN_BATTERY_ADC_H

#include <stdbool.h>

#include "ef800_battery_soc_store.h"
#include "ef800_battery_adc_ring.h"
#include "ef800_max17043.h"

struct ef800_main_battery_adc {
	struct ef800_battery_soc_store *store;
	struct ef800_battery_adc_ring ring;
	bool store_ready;
	bool adc_ready;
};

/** @brief Bind the calibration store and configure the battery ADC channel.
 * @param battery Adapter state to initialize.
 * @param store Calibration table store used to convert voltage to SOC.
 * @param store_ready True when the calibration store is available.
 * @return 0 when ADC setup succeeds; negative errno on invalid input/setup error.
 */
int ef800_main_battery_adc_init(struct ef800_main_battery_adc *battery,
	struct ef800_battery_soc_store *store, bool store_ready);
/** @brief Sample, filter, and convert battery voltage into gauge status.
 * @param context Pointer to initialized battery ADC adapter state.
 * @param status Receives SOC percent and voltage in microvolts.
 * @return 0 on conversion; negative errno if ADC, calibration, or profile fails.
 */
int ef800_main_battery_adc_read(void *context,
	struct ef800_max17043_status *status);

#endif /* EF800_MAIN_BATTERY_ADC_H */
