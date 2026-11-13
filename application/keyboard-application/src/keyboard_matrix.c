/* SPDX-License-Identifier: Apache-2.0 */

#include "keyboard_matrix.h"

#include <errno.h>
#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#include "keyboard_protocol.h"

#define KB_NODE DT_PATH(keyboard)
#define KB_DEBOUNCE_MS 40U
#define KB_SCAN_PERIOD_MS 2U
#define KB_DEBOUNCE_SCANS (KB_DEBOUNCE_MS / KB_SCAN_PERIOD_MS)

static const struct gpio_dt_spec rows[KB_MATRIX_ROWS] = {
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 0),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 1),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 2),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 3),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 4),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 5),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 6),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, row_gpios, 7),
};

static const struct gpio_dt_spec columns[KB_MATRIX_COLS] = {
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, column_gpios, 0),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, column_gpios, 1),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, column_gpios, 2),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, column_gpios, 3),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, column_gpios, 4),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, column_gpios, 5),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, column_gpios, 6),
};

static const struct gpio_dt_spec special_keys[KB_SPECIAL_KEYS] = {
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, special_gpios, 0),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, special_gpios, 1),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, special_gpios, 2),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, special_gpios, 3),
	GPIO_DT_SPEC_GET_BY_IDX(KB_NODE, special_gpios, 4),
};

int ef800_keyboard_matrix_init(void)
{
	for (size_t index = 0U; index < ARRAY_SIZE(rows); ++index) {
		if (!gpio_is_ready_dt(&rows[index])) {
			return -ENODEV;
		}
		int result = gpio_pin_configure_dt(&rows[index], GPIO_INPUT);

		if (result != 0) {
			return result;
		}
	}
	for (size_t index = 0U; index < ARRAY_SIZE(columns); ++index) {
		if (!gpio_is_ready_dt(&columns[index])) {
			return -ENODEV;
		}
		int result = gpio_pin_configure_dt(&columns[index], GPIO_INPUT);

		if (result != 0) {
			return result;
		}
	}
	for (size_t index = 0U; index < ARRAY_SIZE(special_keys); ++index) {
		if (!gpio_is_ready_dt(&special_keys[index])) {
			return -ENODEV;
		}
		int result = gpio_pin_configure_dt(&special_keys[index], GPIO_INPUT);

		if (result != 0) {
			return result;
		}
	}
	return 0;
}

static int read_key_state(uint64_t *state)
{
	int value;

	*state = 0U;
	for (size_t column = 0U; column < ARRAY_SIZE(columns); ++column) {
		value = gpio_pin_configure_dt(&columns[column], GPIO_INPUT);
		if (value != 0) {
			return value;
		}
	}
	for (size_t column = 0U; column < ARRAY_SIZE(columns); ++column) {
		value = gpio_pin_configure_dt(&columns[column], GPIO_OUTPUT_ACTIVE);
		if (value != 0) {
			return value;
		}
		for (size_t row = 0U; row < ARRAY_SIZE(rows); ++row) {
			value = gpio_pin_get_dt(&rows[row]);
			if (value < 0) {
				(void)gpio_pin_configure_dt(&columns[column], GPIO_INPUT);
				return value;
			}
			if (value != 0) {
				*state |= UINT64_C(1) <<
					(column * KB_MATRIX_ROWS + row);
			}
		}
		(void)gpio_pin_configure_dt(&columns[column], GPIO_INPUT);
	}
	for (size_t index = 0U; index < ARRAY_SIZE(special_keys); ++index) {
		value = gpio_pin_get_dt(&special_keys[index]);
		if (value < 0) {
			return value;
		}
		if (value != 0) {
			*state |= UINT64_C(1) <<
				(KB_MATRIX_ROWS * KB_MATRIX_COLS + index);
		}
	}
	return 0;
}

int ef800_keyboard_matrix_scan(struct kb_matrix_debounce_state *state,
			       bool enabled, struct kb_matrix_key_event *events,
			       size_t event_capacity, size_t *event_count)
{
	uint64_t current_keys;
	int result;

	if (state == NULL || event_count == NULL ||
		(events == NULL && event_capacity != 0U)) {
		return -EINVAL;
	}
	*event_count = 0U;
	if (!enabled) {
		return 0;
	}
	result = read_key_state(&current_keys);
	if (result != 0) {
		return result;
	}
	return kb_matrix_debounce_update(state, current_keys, KB_DEBOUNCE_SCANS,
		events, event_capacity, event_count);
}
