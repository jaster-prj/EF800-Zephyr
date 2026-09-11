/* SPDX-License-Identifier: Apache-2.0 */
#include "t6963_emulator.h"

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
#define CMD_DATA_WRITE_DECREMENT 0xC2U
#define CMD_DATA_WRITE_FIXED 0xC4U
#define CMD_DATA_READ 0xC1U
#define CMD_DATA_READ_NO_INCREMENT 0xC5U

static void write_data_byte(struct t6963_emulator *emu, uint8_t data);

static uint8_t glyph_row(uint8_t character, uint8_t row)
{
	if (character >= 0x80U || row >= 8U) return 0U;
	return cgrom_0101[character][row];
}

static void put_word(uint16_t *value, uint8_t byte, uint8_t index)
{
	if (index == 0U) {
		*value = (*value & 0xFF00U) | byte;
	} else {
		*value = (*value & 0x00FFU) | ((uint16_t)byte << 8);
	}
}

void t6963_emulator_init(struct t6963_emulator *emu)
{
	memset(emu, 0, sizeof(*emu));
	emu->text_area = 30U;
	/* EF800 uses 6-pixel character cells: 240 / 6 = 40 bytes per row. */
	emu->graphics_area = 40U;
	emu->font_width = 6U;
	emu->address_step = 1;
}

void t6963_emulator_set_font_width(struct t6963_emulator *emu, uint8_t width)
{
	if (width != 6U && width != 8U) return;
	emu->font_width = width;
	emu->text_area = (uint8_t)(TLX711A_WIDTH / width);
	emu->graphics_area = (uint8_t)(TLX711A_WIDTH / width);
}

void t6963_emulator_memory_stats(const struct t6963_emulator *emu,
				 struct t6963_memory_stats *stats)
{
	uint16_t row;
	uint16_t column;
	uint16_t address;
	uint16_t graphics_bytes = (uint16_t)(TLX711A_HEIGHT * emu->graphics_area);

	memset(stats, 0, sizeof(*stats));
	stats->text_first_address = 0xFFFFU;
	stats->graphics_first_address = 0xFFFFU;

	for (row = 0U; row < 8U; ++row) {
		for (column = 0U; column < emu->text_area; ++column) {
			address = (uint16_t)(emu->text_home + row * emu->text_area + column);
			if (emu->memory[address & 0x1FFFU] == 0U) continue;
			stats->text_nonzero++;
			if (stats->text_first_address == 0xFFFFU ||
			    address < stats->text_first_address) {
				stats->text_first_address = address;
			}
			if (address > stats->text_last_address) stats->text_last_address = address;
		}
	}

	for (address = 0U; address < graphics_bytes; ++address) {
		uint16_t memory_address = (uint16_t)(emu->graphics_home + address);
		if (emu->memory[memory_address & 0x1FFFU] == 0U) continue;
		stats->graphics_nonzero++;
		if (stats->graphics_first_address == 0xFFFFU ||
		    memory_address < stats->graphics_first_address) {
			stats->graphics_first_address = memory_address;
		}
		if (memory_address > stats->graphics_last_address)
			stats->graphics_last_address = memory_address;
	}

}

void t6963_emulator_data(struct t6963_emulator *emu, uint8_t data)
{
	if (emu->auto_write) {
		write_data_byte(emu, data);
		return;
	}
	if (emu->pending_count < 2U) {
		emu->pending_data[emu->pending_count++] = data;
	}
}

void t6963_emulator_command(struct t6963_emulator *emu, uint8_t command)
{
	uint16_t value;

	if (command == CMD_SET_ADDRESS && emu->pending_count >= 2U) {
		value = (uint16_t)emu->pending_data[0] |
			((uint16_t)emu->pending_data[1] << 8);
		emu->address = value;
		emu->pending_count = 0U;
		return;
	}
	if (command == CMD_SET_TEXT_HOME && emu->pending_count >= 2U) {
		put_word(&emu->text_home, emu->pending_data[0], 0U);
		put_word(&emu->text_home, emu->pending_data[1], 1U);
	} else if (command == CMD_SET_GRAPHICS_HOME && emu->pending_count >= 2U) {
		put_word(&emu->graphics_home, emu->pending_data[0], 0U);
		put_word(&emu->graphics_home, emu->pending_data[1], 1U);
	} else if (command == CMD_SET_TEXT_AREA && emu->pending_count >= 1U) {
		emu->text_area = emu->pending_data[0];
	} else if (command == CMD_SET_GRAPHICS_AREA && emu->pending_count >= 1U) {
		emu->graphics_area = emu->pending_data[0];
	} else if (command == CMD_SET_OFFSET && emu->pending_count >= 1U) {
		emu->offset = emu->pending_data[0];
	} else if (command == 0x80U || command == 0x81U || command == 0x82U || command == 0x83U) {
		emu->mode = command;
	} else if ((command & 0xF0U) == 0x90U) {
		emu->display_mode = command;
	} else if (command == CMD_AUTO_WRITE) {
		emu->auto_write = true;
	} else if (command == CMD_AUTO_RESET) {
		emu->auto_write = false;
	} else if ((command == CMD_DATA_WRITE || command == CMD_DATA_WRITE_DECREMENT ||
			command == CMD_DATA_WRITE_FIXED) && emu->pending_count >= 1U) {
		if (command == CMD_DATA_WRITE_DECREMENT) emu->address_step = -1;
		else if (command == CMD_DATA_WRITE_FIXED) emu->address_step = 0;
		else emu->address_step = 1;
		write_data_byte(emu, emu->pending_data[0]);
		emu->auto_write = false;
	} else if (command == CMD_DATA_READ || command == CMD_DATA_READ_NO_INCREMENT) {
		emu->read_increment = command == CMD_DATA_READ;
		emu->data_read_pending = true;
	}
	emu->pending_count = 0U;
}

uint8_t t6963_emulator_read_status(const struct t6963_emulator *emu)
{
	ARG_UNUSED(emu);
	return 0x03U;
}

uint8_t t6963_emulator_read_data(struct t6963_emulator *emu)
{
	uint8_t value = emu->memory[emu->address & 0x1FFFU];
	if (emu->data_read_pending && emu->read_increment) {
		emu->address++;
	}
	emu->data_read_pending = false;
	return value;
}

static void write_data_byte(struct t6963_emulator *emu, uint8_t data)
{
	emu->memory[emu->address & 0x1FFFU] = data;
	emu->address = (uint16_t)(emu->address + emu->address_step);
}

void t6963_emulator_frame(const struct t6963_emulator *emu,
				 uint8_t frame[TLX711A_FRAME_SIZE])
{
	uint16_t x;
	uint16_t y;
	memset(frame, 0, TLX711A_FRAME_SIZE);
	if ((emu->display_mode & BIT(3)) != 0U || emu->display_mode == 0U) {
	for (y = 0U; y < TLX711A_HEIGHT; ++y) {
		for (x = 0U; x < TLX711A_WIDTH; ++x) {
			uint16_t source = (uint16_t)(emu->graphics_home +
				y * emu->graphics_area + x / emu->font_width);
			uint8_t source_column = (uint8_t)(8U - emu->font_width +
				(x % emu->font_width));
			uint8_t source_bit = (uint8_t)(7U - source_column);
			if (source < sizeof(emu->memory) &&
				(emu->memory[source] & BIT(source_bit)) != 0U) {
				frame[y * TLX711A_ROW_BYTES + x / 8U] |= BIT(x % 8U);
			}
		}
	}
	}
	if ((emu->display_mode & BIT(2)) != 0U || emu->display_mode == 0U) {
		for (y = 0U; y < TLX711A_HEIGHT; ++y) {
			uint16_t row = y / 8U;
			uint8_t glyph_row_index = y % 8U;
			for (x = 0U; x < TLX711A_WIDTH; ++x) {
				uint16_t column = x / emu->font_width;
				uint16_t address = (uint16_t)(emu->text_home +
					row * emu->text_area + column);
				uint8_t glyph = emu->memory[address & 0x1FFFU];
				uint8_t glyph_column = (uint8_t)(x % emu->font_width);
				uint8_t source_column = (uint8_t)(8U - emu->font_width + glyph_column);
				uint8_t bit = (uint8_t)(7U - source_column);
				if ((glyph_row(glyph, glyph_row_index) & BIT(bit)) != 0U)
					frame[y * TLX711A_ROW_BYTES + x / 8U] |= BIT(x % 8U);
			}
		}
	}
}
