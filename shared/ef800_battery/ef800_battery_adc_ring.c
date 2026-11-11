/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_battery_adc_ring.h"

#include <errno.h>
#include <string.h>

void ef800_battery_adc_ring_reset(struct ef800_battery_adc_ring *ring)
{
	if (ring != NULL) {
		memset(ring, 0, sizeof(*ring));
	}
}

int ef800_battery_adc_ring_push(struct ef800_battery_adc_ring *ring,
	uint16_t sample, uint16_t *average)
{
	if (ring == NULL || average == NULL) {
		return -EINVAL;
	}

	if (ring->count == EF800_BATTERY_ADC_RING_CAPACITY) {
		ring->sum -= ring->samples[ring->next];
	} else {
		++ring->count;
	}

	ring->samples[ring->next] = sample;
	ring->sum += sample;
	ring->next = (uint8_t)((ring->next + 1U) %
		EF800_BATTERY_ADC_RING_CAPACITY);
	*average = (uint16_t)((ring->sum + ring->count / 2U) / ring->count);

	return 0;
}
