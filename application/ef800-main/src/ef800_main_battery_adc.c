/* SPDX-License-Identifier: Apache-2.0 */
#include "ef800_main_battery_adc.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>

#ifdef CONFIG_BOARD_NATIVE_SIM
#include <zephyr/drivers/adc/adc_emul.h>
#include "ef800_virtual_profile.h"
#endif

#include "ef800_battery_adc_ring.h"
#include "ef800_battery_soc.h"

static const struct adc_dt_spec battery_adc =
	ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), battery);

int ef800_main_battery_adc_init(struct ef800_main_battery_adc *battery,
	struct ef800_battery_soc_store *store, bool store_ready)
{
	int result;

	if (battery == NULL || store == NULL) {
		return -EINVAL;
	}
	*battery = (struct ef800_main_battery_adc){
		.store = store,
		.store_ready = store_ready,
	};
	result = adc_channel_setup_dt(&battery_adc);
	if (result == 0) {
		battery->adc_ready = true;
	}
	return result;
}

int ef800_main_battery_adc_read(void *context,
	struct ef800_max17043_status *status)
{
	struct ef800_main_battery_adc *battery = context;
	struct ef800_battery_soc_table table;
	struct adc_sequence sequence = {0};
	int16_t raw_sample;
	int32_t pin_mv;
	uint16_t filtered_pin_mv;
#ifdef CONFIG_BOARD_NATIVE_SIM
	uint16_t pin_mv_input;
#endif
	int result;

	if (battery == NULL || status == NULL || !battery->adc_ready ||
		!battery->store_ready) {
		return -ENODEV;
	}
	result = ef800_battery_soc_store_get_table(battery->store, &table);
	if (result != 0) {
		return result;
	}
#ifdef CONFIG_BOARD_NATIVE_SIM
	if (!ef800_virtual_profile_available(4U)) {
		return -ENODEV;
	}
	result = ef800_battery_pin_mv_from_soc(&table,
		ef800_virtual_profile_battery_soc(), &pin_mv_input);
	if (result != 0) {
		return result;
	}
	result = adc_emul_const_value_set(battery_adc.dev,
		battery_adc.channel_id, pin_mv_input);
	if (result != 0) {
		return result;
	}
#endif
	result = adc_sequence_init_dt(&battery_adc, &sequence);
	if (result != 0) {
		return result;
	}
	sequence.buffer = &raw_sample;
	sequence.buffer_size = sizeof(raw_sample);
	result = adc_read_dt(&battery_adc, &sequence);
	if (result != 0) {
		return result;
	}
	pin_mv = raw_sample;
	result = adc_raw_to_millivolts_dt(&battery_adc, &pin_mv);
	if (result != 0 || pin_mv < 0 ||
		pin_mv > (int32_t)EF800_BATTERY_SOC_ADC_MAX_MV) {
		return result != 0 ? result : -ERANGE;
	}
	result = ef800_battery_adc_ring_push(&battery->ring,
		(uint16_t)pin_mv, &filtered_pin_mv);
	if (result != 0) {
		return result;
	}
	result = ef800_battery_soc_from_pin_mv(&table, filtered_pin_mv,
		&status->soc_percent);
	if (result == 0) {
		status->voltage_uv = (uint32_t)filtered_pin_mv * 1000U;
	}
	return result;
}
