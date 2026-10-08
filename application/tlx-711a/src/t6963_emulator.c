/* SPDX-License-Identifier: Apache-2.0 */
#include "t6963_emulator.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "cgrom_0101.h"

#define CMD_SET_CURSOR_POINTER 0x21U
#define CMD_SET_OFFSET 0x22U
#define CMD_SET_ADDRESS 0x24U
#define CMD_SET_TEXT_HOME 0x40U
#define CMD_SET_TEXT_AREA 0x41U
#define CMD_SET_GRAPHICS_HOME 0x42U
#define CMD_SET_GRAPHICS_AREA 0x43U
#define CMD_AUTO_WRITE 0xB0U
#define CMD_AUTO_READ 0xB1U
#define CMD_AUTO_RESET 0xB2U
#define CMD_DATA_WRITE 0xC0U
#define CMD_DATA_READ 0xC1U
#define CMD_DATA_WRITE_DECREMENT 0xC2U
#define CMD_DATA_READ_DECREMENT 0xC3U
#define CMD_DATA_WRITE_FIXED 0xC4U
#define CMD_DATA_READ_FIXED 0xC5U
#define CMD_SCREEN_PEEK 0xE0U
#define CMD_SCREEN_COPY 0xE8U

#define MODE_OR 0U
#define MODE_EXOR 1U
#define MODE_AND 3U
#define MODE_ATTRIBUTE 4U
#define MODE_EXTERNAL_CG BIT(3)

static uint8_t default_memory[8192];
static uint8_t default_frame[TLX711A_FRAME_SIZE];

static uint8_t ram_read(const struct t6963_emulator *emu, uint16_t address);

static uint16_t address_wrap(const struct t6963_emulator *emu, int32_t address)
{
	int32_t size = (int32_t)emu->memory_size;

	address %= size;
	if (address < 0) {
		address += size;
	}
	return (uint16_t)address;
}

static void mark_dirty(struct t6963_emulator *emu, uint16_t x0, uint16_t y0,
		uint16_t x1, uint16_t y1)
{
	if (emu->framebuffer_width == 0U || emu->framebuffer_height == 0U) {
		return;
	}
	x0 = MIN(x0, (uint16_t)(emu->framebuffer_width - 1U));
	y0 = MIN(y0, (uint16_t)(emu->framebuffer_height - 1U));
	x1 = MIN(x1, (uint16_t)(emu->framebuffer_width - 1U));
	y1 = MIN(y1, (uint16_t)(emu->framebuffer_height - 1U));
	if (x0 > x1 || y0 > y1) {
		return;
	}
	if (!emu->dirty) {
		emu->dirty_x0 = x0;
		emu->dirty_y0 = y0;
		emu->dirty_x1 = x1;
		emu->dirty_y1 = y1;
		emu->dirty = true;
		return;
	}
	emu->dirty_x0 = MIN(emu->dirty_x0, x0);
	emu->dirty_y0 = MIN(emu->dirty_y0, y0);
	emu->dirty_x1 = MAX(emu->dirty_x1, x1);
	emu->dirty_y1 = MAX(emu->dirty_y1, y1);
}

static void mark_all_dirty(struct t6963_emulator *emu)
{
	mark_dirty(emu, 0U, 0U, emu->framebuffer_width - 1U,
		emu->framebuffer_height - 1U);
}

static void mark_text_cell(struct t6963_emulator *emu, uint16_t column,
	uint16_t row)
{
	mark_dirty(emu, column * emu->font_width, row * 8U,
		(column + 1U) * emu->font_width - 1U, (row + 1U) * 8U - 1U);
}

static void mark_memory_change(struct t6963_emulator *emu, uint16_t address)
{
	uint32_t text_size = (uint32_t)emu->text_area * emu->config.text_rows;
	uint32_t graphics_size = (uint32_t)emu->graphics_area *
		emu->framebuffer_height;
	uint16_t relative;

	if (address >= emu->text_home && address < emu->text_home + text_size) {
		relative = (uint16_t)(address - emu->text_home);
		mark_text_cell(emu, relative % emu->text_area,
			relative / emu->text_area);
		return;
	}
	if (address >= emu->graphics_home &&
		address < emu->graphics_home + graphics_size) {
		relative = (uint16_t)(address - emu->graphics_home);
		uint16_t column = relative % emu->graphics_area;
		uint16_t row = relative / emu->graphics_area;
		if (emu->mode == MODE_ATTRIBUTE) {
			mark_text_cell(emu, column, row / 8U);
		} else {
			mark_dirty(emu, column * emu->font_width, row,
				(column + 1U) * emu->font_width - 1U, row);
		}
		return;
	}
	/* A CG byte can be used by multiple visible text cells. */
	uint16_t cg_base = (uint16_t)((emu->offset & 0x1FU) << 11);
	if (address >= cg_base && address < cg_base + 0x800U) {
		uint8_t character = (uint8_t)((address - cg_base) / 8U);
		for (uint16_t row = 0U; row < emu->config.text_rows; ++row) {
			for (uint16_t column = 0U; column < emu->text_area; ++column) {
				uint16_t text_address = (uint16_t)(emu->text_home +
					row * emu->text_area + column);
				if (ram_read(emu, text_address) == character) {
					mark_text_cell(emu, column, row);
				}
			}
		}
	}
}

static uint8_t ram_read(const struct t6963_emulator *emu, uint16_t address)
{
	return emu->memory[address_wrap(emu, address)];
}

static void ram_write(struct t6963_emulator *emu, uint16_t address, uint8_t value)
{
	uint16_t wrapped = address_wrap(emu, address);

	if (emu->memory[wrapped] == value) {
		return;
	}
	emu->memory[wrapped] = value;
	mark_memory_change(emu, wrapped);
}

static void advance_address(struct t6963_emulator *emu)
{
	emu->address = address_wrap(emu, (int32_t)emu->address + emu->address_step);
}

static uint8_t glyph_row(const struct t6963_emulator *emu, uint8_t character,
		uint8_t row)
{
	uint16_t base;

	if (row >= 8U) {
		return 0U;
	}
	if ((emu->mode & MODE_EXTERNAL_CG) == 0U && character < 0x80U) {
		return cgrom_0101[character][row];
	}
	base = (uint16_t)(((uint16_t)(emu->offset & 0x1FU) << 11) +
		(uint16_t)character * 8U);
	return ram_read(emu, (uint16_t)(base + row));
}

static bool text_bit(const struct t6963_emulator *emu, uint16_t x, uint16_t y,
		uint8_t *attribute)
{
	uint16_t column = x / emu->font_width;
	uint16_t row = y / 8U;
	uint16_t address;
	uint8_t character;
	uint8_t glyph;
	uint8_t bit;

	if (column >= emu->text_area || row >= emu->config.text_rows) {
		if (attribute != NULL) {
			*attribute = 0U;
		}
		return false;
	}
	address = (uint16_t)(emu->text_home + row * emu->text_area + column);
	character = ram_read(emu, address);
	if (attribute != NULL) {
		uint16_t attr_address = (uint16_t)(emu->graphics_home +
			row * emu->graphics_area + column);
		*attribute = ram_read(emu, attr_address);
	}
	glyph = glyph_row(emu, character, (uint8_t)(y % 8U));
	bit = (uint8_t)(emu->font_width - 1U - (x % emu->font_width));
	return (glyph & BIT(bit)) != 0U;
}

static bool graphics_bit(const struct t6963_emulator *emu, uint16_t x,
		uint16_t y)
{
	uint16_t address;
	uint8_t bit;

	if (x >= emu->framebuffer_width || y >= emu->framebuffer_height ||
		emu->graphics_area == 0U) {
		return false;
	}
	address = (uint16_t)(emu->graphics_home +
		y * emu->graphics_area + x / emu->font_width);
	bit = (uint8_t)(emu->font_width - 1U - (x % emu->font_width));
	return (ram_read(emu, address) & BIT(bit)) != 0U;
}

static bool composed_pixel(const struct t6963_emulator *emu, uint16_t x,
		uint16_t y)
{
	uint8_t attribute = 0U;
	bool text = text_bit(emu, x, y, &attribute);
	bool graphics = graphics_bit(emu, x, y);

	if ((emu->display_mode & BIT(2)) == 0U) {
		text = false;
	}
	if ((emu->display_mode & BIT(3)) == 0U || emu->mode == MODE_ATTRIBUTE) {
		graphics = false;
	}
	if (emu->mode == MODE_ATTRIBUTE) {
		switch (attribute & 0x0FU) {
		case 0x05U:
			text = !text;
			break;
		case 0x03U:
			text = false;
			break;
		case 0x08U:
		case 0x0DU:
		case 0x0BU:
			if (emu->blink_phase) {
				text = false;
			} else if ((attribute & 0x0FU) == 0x0DU) {
				text = !text;
			} else if ((attribute & 0x0FU) == 0x0BU) {
				text = false;
			}
			break;
		default:
			break;
		}
	}
	switch (emu->mode & 0x07U) {
	case MODE_EXOR:
		text = text ^ graphics;
		break;
	case MODE_AND:
		text = text & graphics;
		break;
	default:
		text = text | graphics;
		break;
	}
	if ((emu->display_mode & BIT(1)) != 0U &&
		x / emu->font_width == emu->cursor_x &&
		y / 8U == emu->cursor_y &&
		y % 8U >= 8U - emu->cursor_pattern) {
		if ((emu->display_mode & BIT(0)) == 0U || !emu->blink_phase) {
			text = true;
		}
	}
	return text;
}

static void framebuffer_set(struct t6963_emulator *emu, uint16_t x,
		uint16_t y, bool value)
{
	size_t index;
	uint8_t mask;
	uint8_t old;

	index = (size_t)y * ((emu->framebuffer_width + 7U) / 8U) + x / 8U;
	mask = BIT(x % 8U);
	old = emu->framebuffer[index];
	if (value) {
		emu->framebuffer[index] |= mask;
	} else {
		emu->framebuffer[index] &= (uint8_t)~mask;
	}
	if (old != emu->framebuffer[index]) {
		emu->framebuffer_changed = true;
	}
}

static void render_dirty(struct t6963_emulator *emu)
{
	if (!emu->dirty) {
		return;
	}
	for (uint16_t y = emu->dirty_y0; y <= emu->dirty_y1; ++y) {
		for (uint16_t x = emu->dirty_x0; x <= emu->dirty_x1; ++x) {
			framebuffer_set(emu, x, y, composed_pixel(emu, x, y));
		}
	}
	emu->dirty = false;
}

static void write_data_byte(struct t6963_emulator *emu, uint8_t data)
{
	ram_write(emu, emu->address, data);
	advance_address(emu);
}

static void set_word(uint16_t *value, const uint8_t *data)
{
	*value = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static bool address_in_graphics(const struct t6963_emulator *emu)
{
	uint32_t size = (uint32_t)emu->graphics_area * emu->framebuffer_height;
	uint32_t address = emu->address;
	uint32_t home = emu->graphics_home;

	return address >= home && address < home + size;
}

static uint8_t displayed_byte(const struct t6963_emulator *emu)
{
	uint16_t relative = (uint16_t)(emu->address - emu->graphics_home);
	uint16_t y = relative / emu->graphics_area;
	uint16_t column = relative % emu->graphics_area;
	uint8_t result = 0U;

	for (uint8_t bit = 0U; bit < emu->font_width; ++bit) {
		uint16_t x = (uint16_t)(column * emu->font_width + bit);
		if (x < emu->framebuffer_width && composed_pixel(emu, x, y)) {
			result |= BIT(emu->font_width - 1U - bit);
		}
	}
	return result;
}

int t6963_emulator_init_config(struct t6963_emulator *emu,
		const struct t6963_emulator_config *config,
		uint8_t *memory, size_t memory_size,
		uint8_t *framebuffer, size_t framebuffer_size)
{
	uint16_t width;
	uint16_t height;
	size_t required_frame;

	if (emu == NULL || config == NULL || memory == NULL || framebuffer == NULL ||
		memory_size != config->memory_size || memory_size < 2048U ||
		memory_size > 65536U || config->hardware_columns == 0U ||
		config->text_rows == 0U || config->font_width < 5U ||
		config->font_width > 8U || config->text_area == 0U ||
		config->graphics_area == 0U ||
		(config->hardware_columns != 32U && config->hardware_columns != 40U &&
		 config->hardware_columns != 64U && config->hardware_columns != 80U)) {
		return -EINVAL;
	}
	width = (uint16_t)(config->hardware_columns * config->font_width);
	height = (uint16_t)(config->text_rows * 8U);
	required_frame = (size_t)((width + 7U) / 8U) * height;
	if (framebuffer_size < required_frame ||
		config->text_area > UINT8_MAX || config->graphics_area > UINT8_MAX) {
		return -EINVAL;
	}
	memset(emu, 0, sizeof(*emu));
	emu->config = *config;
	emu->memory = memory;
	emu->memory_size = memory_size;
	emu->framebuffer = framebuffer;
	emu->framebuffer_size = framebuffer_size;
	emu->framebuffer_width = width;
	emu->framebuffer_height = height;
	emu->text_area = config->text_area;
	emu->graphics_area = config->graphics_area;
	emu->font_width = config->font_width;
	emu->address_step = 1;
	emu->cursor_pattern = 1U;
	memset(memory, 0, memory_size);
	memset(framebuffer, 0, framebuffer_size);
	mark_all_dirty(emu);
	return 0;
}

void t6963_emulator_init(struct t6963_emulator *emu)
{
	struct t6963_emulator_config config = {
		.memory_size = sizeof(default_memory),
		.hardware_columns = 40U,
		.text_rows = 8U,
		.font_width = 6U,
		.text_area = 40U,
		.graphics_area = 40U,
	};

	(void)t6963_emulator_init_config(emu, &config, default_memory,
		sizeof(default_memory), default_frame, sizeof(default_frame));
}

void t6963_emulator_set_font_width(struct t6963_emulator *emu, uint8_t width)
{
	if (emu == NULL || width < 5U || width > 8U) {
		return;
	}
	emu->font_width = width;
	emu->framebuffer_width = (uint16_t)(emu->config.hardware_columns * width);
	emu->text_area = (uint8_t)emu->config.hardware_columns;
	emu->graphics_area = (uint8_t)emu->config.hardware_columns;
	mark_all_dirty(emu);
}

void t6963_emulator_data(struct t6963_emulator *emu, uint8_t data)
{
	if (emu->auto_mode == 1U) {
		write_data_byte(emu, data);
		return;
	}
	if (emu->pending_count < ARRAY_SIZE(emu->pending_data)) {
		emu->pending_data[emu->pending_count++] = data;
	}
}

static void execute_bit_command(struct t6963_emulator *emu, uint8_t command)
{
	uint8_t bit = command & 0x07U;
	uint8_t value = ram_read(emu, emu->address);

	if ((command & BIT(3)) != 0U) {
		value |= BIT(bit);
	} else {
		value &= (uint8_t)~BIT(bit);
	}
	ram_write(emu, emu->address, value);
	mark_all_dirty(emu);
}

void t6963_emulator_command(struct t6963_emulator *emu, uint8_t command)
{
	if (emu == NULL || emu->memory == NULL) {
		return;
	}
	if (emu->auto_mode != 0U && command != CMD_AUTO_RESET) {
		return;
	}
	if (command == CMD_SET_CURSOR_POINTER && emu->pending_count >= 2U) {
		mark_dirty(emu, (uint16_t)emu->cursor_x * emu->font_width,
			(uint16_t)emu->cursor_y * 8U,
			(uint16_t)(emu->cursor_x + 1U) * emu->font_width - 1U,
			(uint16_t)(emu->cursor_y + 1U) * 8U - 1U);
		emu->cursor_x = emu->pending_data[0] & 0x7FU;
		emu->cursor_y = emu->pending_data[1] & 0x1FU;
		mark_dirty(emu, (uint16_t)emu->cursor_x * emu->font_width,
			(uint16_t)emu->cursor_y * 8U,
			(uint16_t)(emu->cursor_x + 1U) * emu->font_width - 1U,
			(uint16_t)(emu->cursor_y + 1U) * 8U - 1U);
	} else if (command == CMD_SET_OFFSET && emu->pending_count >= 1U) {
		emu->offset = emu->pending_data[0] & 0x1FU;
		mark_all_dirty(emu);
	} else if (command == CMD_SET_ADDRESS && emu->pending_count >= 2U) {
		set_word(&emu->address, emu->pending_data);
	} else if (command == CMD_SET_TEXT_HOME && emu->pending_count >= 2U) {
		set_word(&emu->text_home, emu->pending_data);
		mark_all_dirty(emu);
	} else if (command == CMD_SET_TEXT_AREA && emu->pending_count >= 1U) {
		emu->text_area = emu->pending_data[0];
		mark_all_dirty(emu);
	} else if (command == CMD_SET_GRAPHICS_HOME && emu->pending_count >= 2U) {
		set_word(&emu->graphics_home, emu->pending_data);
		mark_all_dirty(emu);
	} else if (command == CMD_SET_GRAPHICS_AREA && emu->pending_count >= 1U) {
		emu->graphics_area = emu->pending_data[0];
		mark_all_dirty(emu);
	} else if ((command & 0xF0U) == 0x80U) {
		emu->mode = command & 0x0FU;
		mark_all_dirty(emu);
	} else if ((command & 0xF0U) == 0x90U) {
		emu->display_mode = command & 0x0FU;
		mark_all_dirty(emu);
	} else if ((command & 0xF8U) == 0xA0U) {
		emu->cursor_pattern = (command & 0x07U) + 1U;
		mark_all_dirty(emu);
	} else if (command == CMD_AUTO_WRITE) {
		emu->auto_mode = 1U;
		emu->address_step = 1;
	} else if (command == CMD_AUTO_READ) {
		emu->auto_mode = 2U;
		emu->address_step = 1;
	} else if (command == CMD_AUTO_RESET) {
		emu->auto_mode = 0U;
		emu->data_read_pending = false;
	} else if ((command == CMD_DATA_WRITE || command == CMD_DATA_WRITE_DECREMENT ||
		command == CMD_DATA_WRITE_FIXED) && emu->pending_count >= 1U) {
		emu->address_step = command == CMD_DATA_WRITE_DECREMENT ? -1 :
			(command == CMD_DATA_WRITE_FIXED ? 0 : 1);
		write_data_byte(emu, emu->pending_data[0]);
	} else if (command == CMD_DATA_READ || command == CMD_DATA_READ_DECREMENT ||
		command == CMD_DATA_READ_FIXED) {
		emu->address_step = command == CMD_DATA_READ_DECREMENT ? -1 :
			(command == CMD_DATA_READ_FIXED ? 0 : 1);
		emu->data_read_pending = true;
	} else if (command == CMD_SCREEN_PEEK) {
		emu->screen_error = !address_in_graphics(emu);
		if (!emu->screen_error) {
			emu->read_latch = displayed_byte(emu);
			emu->read_latch_valid = true;
			emu->data_read_pending = true;
		}
	} else if (command == CMD_SCREEN_COPY) {
		if (!address_in_graphics(emu) || emu->config.dual_scan) {
			emu->screen_error = true;
		} else {
			ram_write(emu, emu->address, displayed_byte(emu));
			mark_all_dirty(emu);
		}
	} else if ((command & 0xF0U) == 0xF0U) {
		execute_bit_command(emu, command);
	}
	emu->pending_count = 0U;
}

uint8_t t6963_emulator_read_status(const struct t6963_emulator *emu)
{
	uint8_t status = BIT(0) | BIT(1) | BIT(5);

	if (emu == NULL) {
		return 0U;
	}
	if (emu->auto_mode == 1U) {
		status = BIT(3) | BIT(5);
	} else if (emu->auto_mode == 2U) {
		status = BIT(2) | BIT(5);
	}
	if (emu->screen_error) {
		status |= BIT(6);
	}
	if (emu->display_mode != 0U) {
		status |= BIT(7);
	}
	return status;
}

uint8_t t6963_emulator_read_data(struct t6963_emulator *emu)
{
	uint8_t value;

	if (emu == NULL || emu->memory == NULL) {
		return 0U;
	}
	if (emu->auto_mode == 2U) {
		value = ram_read(emu, emu->address);
		advance_address(emu);
		return value;
	}
	if (!emu->data_read_pending) {
		return 0U;
	}
	value = emu->screen_error ? 0U :
		(emu->read_latch_valid ? emu->read_latch : ram_read(emu, emu->address));
	if (!emu->read_latch_valid) {
		advance_address(emu);
	}
	emu->read_latch = 0U;
	emu->read_latch_valid = false;
	emu->data_read_pending = false;
	return value;
}

void t6963_emulator_frame(struct t6963_emulator *emu, uint8_t *frame,
		size_t capacity)
{
	if (emu == NULL || frame == NULL || capacity <
		(size_t)((emu->framebuffer_width + 7U) / 8U) * emu->framebuffer_height) {
		return;
	}
	render_dirty(emu);
	memcpy(frame, emu->framebuffer,
		(size_t)((emu->framebuffer_width + 7U) / 8U) * emu->framebuffer_height);
}

bool t6963_emulator_frame_changed(const struct t6963_emulator *emu)
{
	return emu != NULL && emu->framebuffer_changed;
}

void t6963_emulator_clear_frame_changed(struct t6963_emulator *emu)
{
	if (emu != NULL) {
		emu->framebuffer_changed = false;
	}
}

void t6963_emulator_set_blink_phase(struct t6963_emulator *emu, bool phase)
{
	if (emu != NULL && emu->blink_phase != phase) {
		emu->blink_phase = phase;
		mark_all_dirty(emu);
	}
}

void t6963_emulator_memory_stats(const struct t6963_emulator *emu,
		struct t6963_memory_stats *stats)
{
	uint16_t address;
	uint32_t graphics_size;

	if (emu == NULL || stats == NULL) {
		return;
	}
	memset(stats, 0, sizeof(*stats));
	stats->text_first_address = UINT16_MAX;
	stats->graphics_first_address = UINT16_MAX;
	for (uint16_t row = 0U; row < emu->config.text_rows; ++row) {
		for (uint16_t column = 0U; column < emu->text_area; ++column) {
			address = (uint16_t)(emu->text_home + row * emu->text_area + column);
			if (ram_read(emu, address) == 0U) continue;
			stats->text_nonzero++;
			stats->text_first_address = MIN(stats->text_first_address, address);
			stats->text_last_address = MAX(stats->text_last_address, address);
		}
	}
	graphics_size = (uint32_t)emu->graphics_area * emu->framebuffer_height;
	for (uint32_t index = 0U; index < graphics_size; ++index) {
		address = (uint16_t)(emu->graphics_home + index);
		if (ram_read(emu, address) == 0U) continue;
		stats->graphics_nonzero++;
		stats->graphics_first_address = MIN(stats->graphics_first_address, address);
		stats->graphics_last_address = MAX(stats->graphics_last_address, address);
	}
}
