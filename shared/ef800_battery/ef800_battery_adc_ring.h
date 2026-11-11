/* SPDX-License-Identifier: Apache-2.0 */

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

void ef800_battery_adc_ring_reset(struct ef800_battery_adc_ring *ring);
int ef800_battery_adc_ring_push(struct ef800_battery_adc_ring *ring,
	uint16_t sample, uint16_t *average);

#endif /* EF800_BATTERY_ADC_RING_H_ */
