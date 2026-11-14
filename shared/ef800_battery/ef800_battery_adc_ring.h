/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Maintain a bounded moving average of battery ADC samples. */

#ifndef EF800_BATTERY_ADC_RING_H_
#define EF800_BATTERY_ADC_RING_H_

#include <stdint.h>

#define EF800_BATTERY_ADC_RING_CAPACITY 8U

struct ef800_battery_adc_ring {
	uint16_t samples[EF800_BATTERY_ADC_RING_CAPACITY];
	uint32_t sum;
	uint8_t count;
	uint8_t next;
};

/** @brief Clear all samples and reset ring indices.
 * @param ring Filter state to reset.
 * @return None.
 */
void ef800_battery_adc_ring_reset(struct ef800_battery_adc_ring *ring);

/** @brief Add a sample and return the current arithmetic moving average.
 * @param ring Filter state to update.
 * @param sample New raw ADC sample.
 * @param average Receives the average of currently retained samples.
 * @return 0 on update; negative errno for invalid pointers.
 */
int ef800_battery_adc_ring_push(struct ef800_battery_adc_ring *ring,
	uint16_t sample, uint16_t *average);

#endif /* EF800_BATTERY_ADC_RING_H_ */
