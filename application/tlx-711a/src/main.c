/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "lcd_bus.h"
#include "t6963_emulator.h"
#include "transport.h"
#include "lcd_bus.h"

static struct t6963_emulator emulator;
static uint8_t emulator_memory[8192];
static uint8_t frame[TLX711A_FRAME_SIZE];
static uint8_t last_frame[TLX711A_FRAME_SIZE];
static volatile bool frame_pending;
static volatile bool frame_sending;
static bool frame_sent;
static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

static void emulator_init(void)
{
	static const struct t6963_emulator_config config = {
		.memory_size = sizeof(emulator_memory),
		.hardware_columns = 40U,
		.text_rows = 8U,
		.font_width = 6U,
		.text_area = 40U,
		.graphics_area = 40U,
	};

	(void)t6963_emulator_init_config(&emulator, &config, emulator_memory,
		sizeof(emulator_memory), frame, sizeof(frame));
}

static int led_init(void)
{
	if (!device_is_ready(status_led.port)) {
		return -ENODEV;
	}
	return gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
}

static void led_set(bool on)
{
	(void)gpio_pin_set_dt(&status_led, on);
}

static void error_blink(uint32_t on_ms, uint32_t off_ms)
{
	led_set(true);
	k_msleep(on_ms);
	led_set(false);
	k_msleep(off_ms);
}

static void lcd_transaction(bool command, uint8_t value)
{
	if (command) t6963_emulator_command(&emulator, value);
	else t6963_emulator_data(&emulator, value);
	frame_pending = true;
}

static void lcd_reset(void)
{
	emulator_init();
	frame_pending = true;
}

static uint8_t lcd_read(bool command)
{
	return command ? t6963_emulator_read_status(&emulator)
		       : t6963_emulator_read_data(&emulator);
}

static size_t frame_nonzero_bytes(const uint8_t *value, size_t length)
{
	size_t count = 0U;
	for (size_t index = 0U; index < length; ++index) {
		if (value[index] != 0U) ++count;
	}
	return count;
}

int main(void)
{
	int ret;

	ret = led_init();
	if (ret != 0) {
		return ret;
	}
	emulator_init();
	/* The CDC device is initialized by Zephyr before main(). */
	ret = transport_init();
	if (ret != 0) {
		transport_send_info("ERROR: transport init failed");
		while (true) {
			error_blink(100, 900);
		}
	}
	ret = lcd_bus_init(lcd_transaction, lcd_read, lcd_reset);
	if (ret != 0) {
		transport_send_info("ERROR: LCD bus init failed");
		while (true) {
			error_blink(300, 300);
		}
	}
	transport_send_info("TLX-711A emulator ready");
	uint32_t last_ce_count = 0U;
	uint32_t last_write_count = 0U;
	uint32_t last_read_count = 0U;
	uint32_t last_wr_edge_count = 0U;
	uint32_t reported_write_count = 0U;
	int last_font_width = 0;
	while (true) {
		int font_width = lcd_bus_font_width();
		if (font_width > 0 && font_width != last_font_width) {
			char message[96];
			last_font_width = font_width;
			t6963_emulator_set_font_width(&emulator, (uint8_t)font_width);
			frame_pending = true;
			snprintk(message, sizeof(message),
				"FS=%d font width=%d text area=%d graphics area=%d",
				font_width == 6 ? 1 : 0, font_width,
				240 / font_width, 240 / font_width);
			transport_send_info(message);
		}
		led_set(true);
		if (frame_pending) {
			unsigned int key = irq_lock();
			frame_pending = false;
			frame_sending = true;
			irq_unlock(key);
		t6963_emulator_frame(&emulator, frame, sizeof(frame));
			if (!frame_sent || memcmp(frame, last_frame, sizeof(frame)) != 0) {
				transport_send_frame(frame, sizeof(frame));
				memcpy(last_frame, frame, sizeof(frame));
				frame_sent = true;
				transport_send_info("framebuffer updated");
				char message[128];
				snprintk(message, sizeof(message),
					"framebuffer nonzero bytes=%u",
					(unsigned int)frame_nonzero_bytes(frame, sizeof(frame)));
				transport_send_info(message);
				struct t6963_memory_stats stats;
				t6963_emulator_memory_stats(&emulator, &stats);
				snprintk(message, sizeof(message),
					"text nonzero=%u range=0x%04x-0x%04x graphics nonzero=%u range=0x%04x-0x%04x",
					(unsigned int)stats.text_nonzero,
					(unsigned int)stats.text_first_address,
					(unsigned int)stats.text_last_address,
					(unsigned int)stats.graphics_nonzero,
					(unsigned int)stats.graphics_first_address,
					(unsigned int)stats.graphics_last_address);
				transport_send_info(message);
			}
			key = irq_lock();
			frame_sending = false;
			bool again = frame_pending;
			irq_unlock(key);
			if (again) continue;
		}
		if (lcd_bus_ce_count() != last_ce_count ||
		    lcd_bus_write_count() != last_write_count ||
		    lcd_bus_read_count() != last_read_count ||
		    lcd_bus_wr_edge_count() != last_wr_edge_count) {
			char message[96];
			last_ce_count = lcd_bus_ce_count();
			last_write_count = lcd_bus_write_count();
			last_read_count = lcd_bus_read_count();
			last_wr_edge_count = lcd_bus_wr_edge_count();
			snprintk(message, sizeof(message),
				"bus ce=%u wr_edges=%u writes=%u reads=%u",
				(unsigned int)last_ce_count,
				(unsigned int)last_wr_edge_count,
				(unsigned int)last_write_count,
				(unsigned int)last_read_count);
			transport_send_info(message);
		}
		if (lcd_bus_write_count() != reported_write_count) {
			char message[96];
			reported_write_count = lcd_bus_write_count();
			snprintk(message, sizeof(message), "lcd write %s value=0x%02x",
				lcd_bus_last_write_command() ? "command" : "data",
				(unsigned int)lcd_bus_last_write_value());
			transport_send_info(message);
		}
		k_msleep(250);
		led_set(false);
		k_msleep(250);
	}
}
