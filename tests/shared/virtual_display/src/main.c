/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>

#include "ef800_virtual_display.h"
#include "t6963_emulator.h"

static struct t6963_emulator_config ef800_config = {
	.memory_size = 8192U,
	.hardware_columns = 40U,
	.text_rows = 8U,
	.font_width = 6U,
	.text_area = 40U,
	.graphics_area = 40U,
};

static void set_address(struct t6963_emulator *emu, uint16_t address)
{
	t6963_emulator_data(emu, (uint8_t)address);
	t6963_emulator_data(emu, (uint8_t)(address >> 8));
	t6963_emulator_command(emu, 0x24U);
}

ZTEST(shared_virtual_display, test_pixel_bit_order_and_bounds)
{
	struct ef800_virtual_display display;

	ef800_virtual_display_init(&display);
	zassert_ok(ef800_virtual_display_set_pixel(&display, 0U, 0U, true), NULL);
	zassert_ok(ef800_virtual_display_set_pixel(&display, 7U, 0U, true), NULL);
	zassert_ok(ef800_virtual_display_set_pixel(&display, 8U, 0U, true), NULL);
	zassert_equal(display.frame[0], 0x81U, NULL);
	zassert_equal(display.frame[1], 0x01U, NULL);
	zassert_equal(ef800_virtual_display_set_pixel(&display, 240U, 0U, true),
		-EINVAL, NULL);
}

ZTEST(shared_virtual_display, test_snapshot_sequence_and_acknowledgement)
{
	struct ef800_virtual_display display;
	uint8_t frame[EF800_VIRTUAL_DISPLAY_FRAME_SIZE];
	uint32_t sequence;

	ef800_virtual_display_init(&display);
	zassert_true(ef800_virtual_display_is_dirty(&display), NULL);
	zassert_ok(ef800_virtual_display_snapshot(&display, frame, sizeof(frame),
		&sequence), NULL);
	zassert_equal(sequence, ef800_virtual_display_sequence(&display), NULL);
	ef800_virtual_display_acknowledge(&display, sequence - 1U);
	zassert_true(ef800_virtual_display_is_dirty(&display), NULL);
	ef800_virtual_display_acknowledge(&display, sequence);
	zassert_false(ef800_virtual_display_is_dirty(&display), NULL);
	zassert_equal(ef800_virtual_display_snapshot(&display, frame,
		sizeof(frame) - 1U, &sequence), -EINVAL, NULL);
}

ZTEST(shared_virtual_display, test_clear_and_structured_text_rows)
{
	struct ef800_virtual_display display;

	ef800_virtual_display_init(&display);
	zassert_ok(ef800_virtual_display_write_line(&display, 0U, "Main menu"), NULL);
	zassert_mem_equal(display.text[0], "Main menu", 9U, NULL);
	zassert_equal(display.text[0][9], '\0', NULL);
	zassert_equal(ef800_virtual_display_write_line(&display, 8U, "bad"),
		-EINVAL, NULL);
	zassert_ok(ef800_virtual_display_set_pixel(&display, 3U, 3U, true), NULL);
	ef800_virtual_display_clear(&display);
	zassert_equal(display.frame[0], 0U, NULL);
	for (size_t index = 0U; index < EF800_VIRTUAL_DISPLAY_TEXT_COLUMNS; ++index) {
		zassert_equal(display.text[0][index], ' ', NULL);
	}
}

ZTEST(shared_virtual_display, test_t6963_ef800_packed_graphics_pixel)
{
	static uint8_t memory[8192];
	static uint8_t framebuffer[TLX711A_FRAME_SIZE];
	struct t6963_emulator emu;

	zassert_ok(t6963_emulator_init_config(&emu, &ef800_config, memory,
		sizeof(memory), framebuffer, sizeof(framebuffer)), NULL);
	/* Program the same layout as the production EF800 initialization. */
	t6963_emulator_data(&emu, 0x40U);
	t6963_emulator_data(&emu, 0x01U);
	t6963_emulator_command(&emu, 0x42U);
	set_address(&emu, 0x0140U);
	t6963_emulator_data(&emu, BIT(5));
	t6963_emulator_command(&emu, 0xC0U);
	t6963_emulator_command(&emu, 0x98U);
	t6963_emulator_frame(&emu, framebuffer, sizeof(framebuffer));
	zassert_true((framebuffer[0] & BIT(0)) != 0U, NULL);
}

ZTEST(shared_virtual_display, test_t6963_configurable_64k_address_space)
{
	static uint8_t memory[65536];
	static uint8_t framebuffer[4096];
	struct t6963_emulator_config config = {
		.memory_size = sizeof(memory),
		.hardware_columns = 64U,
		.text_rows = 8U,
		.font_width = 8U,
		.text_area = 64U,
		.graphics_area = 64U,
	};
	struct t6963_emulator emu;

	zassert_ok(t6963_emulator_init_config(&emu, &config, memory, sizeof(memory),
		framebuffer, sizeof(framebuffer)), NULL);
	set_address(&emu, 0xFFFFU);
	t6963_emulator_data(&emu, 0xA5U);
	t6963_emulator_command(&emu, 0xC0U);
	zassert_equal(memory[0xFFFFU], 0xA5U, NULL);
	zassert_equal(emu.address, 0U, NULL);
}

ZTEST(shared_virtual_display, test_t6963_repeated_write_does_not_change_frame)
{
	static uint8_t memory[8192];
	static uint8_t framebuffer[TLX711A_FRAME_SIZE];
	struct t6963_emulator emu;

	zassert_ok(t6963_emulator_init_config(&emu, &ef800_config, memory,
		sizeof(memory), framebuffer, sizeof(framebuffer)), NULL);
	set_address(&emu, 0x0140U);
	t6963_emulator_data(&emu, 0U);
	t6963_emulator_command(&emu, 0xC0U);
	t6963_emulator_frame(&emu, framebuffer, sizeof(framebuffer));
	t6963_emulator_clear_frame_changed(&emu);
	set_address(&emu, 0x0140U);
	t6963_emulator_data(&emu, 0U);
	t6963_emulator_command(&emu, 0xC0U);
	t6963_emulator_frame(&emu, framebuffer, sizeof(framebuffer));
	zassert_false(t6963_emulator_frame_changed(&emu), NULL);
}

ZTEST(shared_virtual_display, test_t6963_text_custom_attribute_and_cursor_rendering)
{
	static uint8_t memory[8192];
	static uint8_t framebuffer[TLX711A_FRAME_SIZE];
	struct t6963_emulator emu;

	zassert_ok(t6963_emulator_init_config(&emu, &ef800_config, memory,
		sizeof(memory), framebuffer, sizeof(framebuffer)), NULL);
	/* Internal CG ROM text is rendered into the packed one-bit framebuffer. */
	set_address(&emu, 0U);
	t6963_emulator_data(&emu, (uint8_t)('A' - ' '));
	t6963_emulator_command(&emu, 0xC0U);
	t6963_emulator_command(&emu, 0x94U);
	t6963_emulator_frame(&emu, framebuffer, sizeof(framebuffer));
	zassert_true(t6963_emulator_frame_changed(&emu), NULL);

	/* A custom character in external CG RAM is visible through the same path. */
	t6963_emulator_clear_frame_changed(&emu);
	t6963_emulator_data(&emu, 0x03U);
	t6963_emulator_command(&emu, 0x22U);
	set_address(&emu, 0x1800U);
	t6963_emulator_data(&emu, BIT(5));
	t6963_emulator_command(&emu, 0xC0U);
	set_address(&emu, 0U);
	t6963_emulator_data(&emu, 0x00U);
	t6963_emulator_command(&emu, 0xC0U);
	t6963_emulator_command(&emu, 0x88U);
	t6963_emulator_frame(&emu, framebuffer, sizeof(framebuffer));
	zassert_true((framebuffer[0] & BIT(0)) != 0U, NULL);

	/* Attribute reverse and the one-line cursor invalidate only display output. */
	set_address(&emu, 0x0140U);
	t6963_emulator_data(&emu, 0x05U);
	t6963_emulator_command(&emu, 0xC0U);
	t6963_emulator_data(&emu, 0U);
	t6963_emulator_data(&emu, 0U);
	t6963_emulator_command(&emu, 0x21U);
	t6963_emulator_command(&emu, 0x84U);
	t6963_emulator_data(&emu, 0U);
	t6963_emulator_data(&emu, 0U);
	t6963_emulator_command(&emu, 0x21U);
	t6963_emulator_command(&emu, 0x96U);
	t6963_emulator_frame(&emu, framebuffer, sizeof(framebuffer));
	zassert_true(t6963_emulator_frame_changed(&emu), NULL);
}

ZTEST_SUITE(shared_virtual_display, NULL, NULL, NULL, NULL, NULL);
