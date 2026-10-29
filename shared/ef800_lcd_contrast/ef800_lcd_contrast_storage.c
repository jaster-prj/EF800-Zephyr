/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_lcd_contrast_storage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/sys/util.h>

#define LCD_CONTRAST_DUTY_PATH "/EF800:/.configuration/contrast"
#define LCD_CONTRAST_FILE_BUFFER_SIZE 16U

static int read_record(const char *path, char *buffer, size_t capacity,
		       size_t *length)
{
	struct fs_file_t file;
	ssize_t amount;
	int ret;

	if (path == NULL || buffer == NULL || capacity < 2U || length == NULL) {
		return -EINVAL;
	}
	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_READ);
	if (ret != 0) {
		return ret;
	}
	amount = fs_read(&file, buffer, capacity - 1U);
	ret = fs_close(&file);
	if (amount < 0) {
		return (int)amount;
	}
	if (ret != 0) {
		return ret;
	}
	if ((size_t)amount == capacity - 1U) {
		return -E2BIG;
	}
	*length = (size_t)amount;
	while (*length > 0U && (buffer[*length - 1U] == '\n' ||
		buffer[*length - 1U] == '\r' || buffer[*length - 1U] == ' ' ||
		buffer[*length - 1U] == '\t')) {
		--(*length);
	}
	buffer[*length] = '\0';
	return 0;
}

static int write_record(const char *path, const char *record, size_t length)
{
	struct fs_file_t file;
	ssize_t amount;
	int ret;

	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		return ret;
	}
	amount = fs_write(&file, record, length);
	ret = fs_close(&file);
	if (amount < 0) {
		return (int)amount;
	}
	if ((size_t)amount != length) {
		return -EIO;
	}
	return ret;
}

int ef800_lcd_contrast_storage_load_duty(void *context,
					uint8_t *duty_percent)
{
	char buffer[LCD_CONTRAST_FILE_BUFFER_SIZE];
	size_t length;
	int ret;

	ARG_UNUSED(context);
	ret = read_record(LCD_CONTRAST_DUTY_PATH, buffer, sizeof(buffer), &length);
	if (ret != 0) {
		return ret;
	}
	return ef800_lcd_contrast_parse_duty(buffer, length, duty_percent);
}

int ef800_lcd_contrast_storage_save_duty(void *context,
					uint8_t duty_percent)
{
	char buffer[4];
	int length;

	ARG_UNUSED(context);
	if (duty_percent > EF800_LCD_CONTRAST_MAX_PERCENT) {
		return -ERANGE;
	}
	length = snprintk(buffer, sizeof(buffer), "%u\n", duty_percent);
	if (length < 0 || (size_t)length >= sizeof(buffer)) {
		return -EIO;
	}
	return write_record(LCD_CONTRAST_DUTY_PATH, buffer, (size_t)length);
}
