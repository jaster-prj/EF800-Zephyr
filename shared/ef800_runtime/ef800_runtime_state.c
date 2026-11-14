/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_runtime_state.h"

#include <errno.h>

#include "ef800_app.h"

static void sample_rtc(struct ef800_runtime_state *state, uint32_t now)
{
	struct rtc_time time;
	int ret = ef800_app_rtc_get_local_time(&time);

	k_mutex_lock(&state->lock, K_FOREVER);
	state->snapshot.rtc_error = ret;
	state->snapshot.rtc_updated_ms = now;
	if (ret == 0) {
		state->snapshot.local_time = time;
		state->snapshot.rtc_status = EF800_RUNTIME_SAMPLE_VALID;
		state->snapshot.rtc_has_value = true;
	} else {
		state->snapshot.rtc_status = EF800_RUNTIME_SAMPLE_ERROR;
	}
	++state->snapshot.sequence;
	k_mutex_unlock(&state->lock);
}

static void sample_battery(struct ef800_runtime_state *state, uint32_t now)
{
	struct ef800_max17043_status battery;
	int ret = state->battery_reader == NULL ? -ENODEV :
		state->battery_reader(state->battery_reader_context, &battery);

	k_mutex_lock(&state->lock, K_FOREVER);
	state->snapshot.battery_error = ret;
	state->snapshot.battery_updated_ms = now;
	if (ret == 0) {
		state->snapshot.battery_soc_percent = battery.soc_percent;
		state->snapshot.battery_voltage_uv = battery.voltage_uv;
		state->snapshot.battery_status = EF800_RUNTIME_SAMPLE_VALID;
		state->snapshot.battery_has_value = true;
		state->battery_consecutive_failures = 0U;
	} else {
		if (state->snapshot.battery_has_value) {
			if (state->battery_consecutive_failures < UINT8_MAX) {
				++state->battery_consecutive_failures;
			}
			state->snapshot.battery_status =
				state->battery_consecutive_failures >= 3U ?
				EF800_RUNTIME_SAMPLE_UNAVAILABLE : EF800_RUNTIME_SAMPLE_ERROR;
			if (state->battery_consecutive_failures >= 3U) {
				state->snapshot.battery_has_value = false;
			}
		} else {
			state->snapshot.battery_status = EF800_RUNTIME_SAMPLE_UNAVAILABLE;
		}
	}
	++state->snapshot.sequence;
	k_mutex_unlock(&state->lock);
}

static void sample_work_handler(struct k_work *work)
{
	struct ef800_runtime_state *state = CONTAINER_OF(work,
		struct ef800_runtime_state, sample_work.work);
	uint32_t now = k_uptime_get_32();

	sample_rtc(state, now);
	if ((int32_t)(now - state->next_battery_sample_ms) >= 0) {
		ef800_runtime_state_refresh_battery(state);
		state->next_battery_sample_ms =
			now + CONFIG_EF800_APPLICATION_RUNTIME_BATTERY_PERIOD_MS;
	}
	if (state->started) {
		(void)k_work_reschedule(&state->sample_work,
			K_MSEC(CONFIG_EF800_APPLICATION_RUNTIME_RTC_PERIOD_MS));
	}
}

void ef800_runtime_state_init(struct ef800_runtime_state *state,
			      ef800_runtime_battery_reader_t battery_reader,
			      void *battery_reader_context)
{
	if (state == NULL) {
		return;
	}

	*state = (struct ef800_runtime_state){
		.battery_reader = battery_reader,
		.battery_reader_context = battery_reader_context,
	};
	k_mutex_init(&state->lock);
	k_work_init_delayable(&state->sample_work, sample_work_handler);
}

void ef800_runtime_state_start(struct ef800_runtime_state *state)
{
	if (state == NULL) {
		return;
	}
	state->started = true;
	state->next_battery_sample_ms = 0U;
	(void)k_work_reschedule(&state->sample_work, K_NO_WAIT);
}

void ef800_runtime_state_stop(struct ef800_runtime_state *state)
{
	if (state == NULL) {
		return;
	}
	state->started = false;
	(void)k_work_cancel_delayable(&state->sample_work);
}

void ef800_runtime_state_refresh_rtc(struct ef800_runtime_state *state)
{
	if (state == NULL) {
		return;
	}
	sample_rtc(state, k_uptime_get_32());
}

void ef800_runtime_state_refresh_battery(struct ef800_runtime_state *state)
{
	if (state == NULL) {
		return;
	}
	sample_battery(state, k_uptime_get_32());
}

int ef800_runtime_state_snapshot(struct ef800_runtime_state *state,
				 struct ef800_runtime_snapshot *snapshot)
{
	if (state == NULL || snapshot == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&state->lock, K_FOREVER);
	*snapshot = state->snapshot;
	k_mutex_unlock(&state->lock);
	return 0;
}
