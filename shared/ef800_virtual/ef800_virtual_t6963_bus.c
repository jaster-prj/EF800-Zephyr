/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_virtual_t6963_bus.h"

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>

#include "ef800_virtual_gpio_emul.h"
#include "ef800_virtual_profile.h"
#include "t6963_emulator.h"

#define USER_NODE DT_PATH(zephyr_user)
#define DATA_COUNT 8U

static const struct gpio_dt_spec data_pins[DATA_COUNT] = {
	GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, data_gpios, 0),
	GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, data_gpios, 1),
	GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, data_gpios, 2),
	GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, data_gpios, 3),
	GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, data_gpios, 4),
	GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, data_gpios, 5),
	GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, data_gpios, 6),
	GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, data_gpios, 7),
};
static const struct gpio_dt_spec cd = GPIO_DT_SPEC_GET(USER_NODE, cd_gpios);
static const struct gpio_dt_spec wr = GPIO_DT_SPEC_GET(USER_NODE, wr_gpios);
static const struct gpio_dt_spec rd = GPIO_DT_SPEC_GET(USER_NODE, rd_gpios);
static const struct gpio_dt_spec ce = GPIO_DT_SPEC_GET(USER_NODE, ce_gpios);

static struct t6963_emulator emulator;
static uint8_t frame[TLX711A_FRAME_SIZE];
static uint8_t memory[8192];
static uint8_t previous_ce;

static int pin_level(const struct gpio_dt_spec *spec)
{
	return gpio_pin_get_dt(spec);
}

static uint8_t data_value(void)
{
	uint8_t value = 0U;

	for (uint8_t index = 0U; index < DATA_COUNT; ++index) {
		if (pin_level(&data_pins[index]) > 0) {
			value |= BIT(index);
		}
	}
	return value;
}

static void put_data_value(uint8_t value)
{
	for (uint8_t index = 0U; index < DATA_COUNT; ++index) {
		(void)ef800_virtual_gpio_input_set(data_pins[index].port,
			BIT(data_pins[index].pin),
			(value & BIT(index)) != 0U ? BIT(data_pins[index].pin) : 0U);
	}
}

static void gpio_output_changed(const struct device *port,
	gpio_port_pins_t changed, gpio_port_value_t values, void *context)
{
	uint8_t current_ce;

	ARG_UNUSED(port);
	ARG_UNUSED(changed);
	ARG_UNUSED(values);
	ARG_UNUSED(context);
	current_ce = (uint8_t)(pin_level(&ce) > 0);
	if (current_ce == previous_ce) {
		return;
	}
	previous_ce = current_ce;

	if (current_ce == 0U && pin_level(&rd) == 0) {
		put_data_value(!ef800_virtual_profile_available(6U) ? 0U :
		(pin_level(&cd) != 0 ?
			t6963_emulator_read_status(&emulator) :
			t6963_emulator_read_data(&emulator)));
	} else if (current_ce == 0U && pin_level(&wr) == 0) {
		uint8_t value = data_value();
		if (pin_level(&cd) != 0) {
			t6963_emulator_command(&emulator, value);
		} else {
			t6963_emulator_data(&emulator, value);
		}
		t6963_emulator_frame(&emulator, frame, sizeof(frame));
	}
}

int ef800_virtual_t6963_frame_snapshot(uint8_t *output, size_t capacity)
{
	if (output == NULL || capacity < sizeof(frame)) {
		return -EINVAL;
	}
	memcpy(output, frame, sizeof(frame));
	return 0;
}

static int t6963_bus_init(void)
{
	struct t6963_emulator_config config = {
		.memory_size = sizeof(memory),
		.hardware_columns = 40U,
		.text_rows = 8U,
		.font_width = 6U,
		.text_area = 40U,
		.graphics_area = 40U,
	};
	(void)t6963_emulator_init_config(&emulator, &config, memory, sizeof(memory),
		frame, sizeof(frame));
	t6963_emulator_frame(&emulator, frame, sizeof(frame));
	previous_ce = (uint8_t)(pin_level(&ce) > 0);
	return ef800_virtual_gpio_output_callback_set(gpio_output_changed, NULL);
}

SYS_INIT(t6963_bus_init, APPLICATION, 0);
