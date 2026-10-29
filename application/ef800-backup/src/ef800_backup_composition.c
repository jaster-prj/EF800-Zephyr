/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_backup_composition.h"

#include <errno.h>
#include <string.h>

#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ef800_composition.h"

LOG_MODULE_REGISTER(ef800_backup_composition, LOG_LEVEL_INF);

#define EF800_BACKUP_COMPOSITION_PATH "/EF800:/.configuration/"

struct backup_composition_candidate {
	struct ef800_composition_profile profile;
	bool valid;
	int error;
};

static const struct ef800_composition_catalog_entry lcd_contrast_entries[] = {
	{ "LCDCONTR0", "LCDCONTR", EF800_LCD_CONTRAST_STATIC, true, true },
	{ "LCDCONTR1", "LCDCONTR", EF800_LCD_CONTRAST_PWM, false, true },
};
static const struct ef800_composition_catalog lcd_contrast_catalog = {
	.entries = lcd_contrast_entries,
	.entry_count = ARRAY_SIZE(lcd_contrast_entries),
};
static uint8_t composition_record[EF800_COMPOSITION_RECORD_MAX + 1U];
static struct backup_composition_candidate candidates[2];
static struct ef800_composition_snapshot resolved_snapshot;

static int read_candidate(const char *name,
			 struct backup_composition_candidate *candidate)
{
	struct fs_file_t file;
	struct fs_dirent entry;
	char path[96];
	ssize_t length;
	int ret;

	*candidate = (struct backup_composition_candidate){0};
	ret = snprintk(path, sizeof(path), "%s%s", EF800_BACKUP_COMPOSITION_PATH,
		name);
	if (ret < 0 || (size_t)ret >= sizeof(path)) {
		candidate->error = -ENAMETOOLONG;
		return candidate->error;
	}
	ret = fs_stat(path, &entry);
	if (ret != 0) {
		candidate->error = ret;
		return ret;
	}
	if (entry.type != FS_DIR_ENTRY_FILE) {
		candidate->error = -EISDIR;
		return candidate->error;
	}
	if (entry.size > EF800_COMPOSITION_RECORD_MAX) {
		candidate->error = -E2BIG;
		return candidate->error;
	}
	fs_file_t_init(&file);
	ret = fs_open(&file, path, FS_O_READ);
	if (ret != 0) {
		candidate->error = ret;
		return ret;
	}
	length = fs_read(&file, composition_record, sizeof(composition_record));
	ret = fs_close(&file);
	if (length < 0) {
		candidate->error = (int)length;
		return candidate->error;
	}
	if (ret != 0) {
		candidate->error = ret;
		return ret;
	}
	if ((size_t)length != entry.size) {
		candidate->error = -EIO;
		return candidate->error;
	}
	ret = ef800_composition_profile_decode(composition_record, (size_t)length,
		&candidate->profile);
	candidate->error = ret;
	candidate->valid = ret == 0;
	return ret;
}

int ef800_backup_composition_lcd_contrast_mode(
	enum ef800_lcd_contrast_mode *mode)
{
	static const char *const slot_names[] = {
		EF800_COMPOSITION_SLOT_A,
		EF800_COMPOSITION_SLOT_B,
	};
	struct ef800_composition_profile selected = {0};
	int selected_slot = -1;
	int ret;

	if (mode == NULL) {
		return -EINVAL;
	}
	*mode = EF800_LCD_CONTRAST_STATIC;
	for (size_t index = 0U; index < ARRAY_SIZE(candidates); ++index) {
		(void)read_candidate(slot_names[index], &candidates[index]);
	}
	ret = ef800_composition_profile_select(&candidates[0].profile,
		candidates[0].valid, &candidates[1].profile, candidates[1].valid,
		&selected, &selected_slot);
	if (ret == -EBADMSG) {
		LOG_WRN("Composition slots conflict; using LCDCONTR0 default");
	} else if (ret != 0 && ret != -ENOENT) {
		LOG_WRN("Composition slots unavailable; using LCDCONTR0 default: %d", ret);
	}
	ret = ef800_composition_resolve(&selected, &lcd_contrast_catalog,
		&resolved_snapshot);
	if (ret != 0) {
		LOG_WRN("LCD contrast composition invalid; using LCDCONTR0 default: %d", ret);
		return 0;
	}
	for (size_t index = 0U; index < resolved_snapshot.effective_count; ++index) {
		const struct ef800_composition_catalog_entry *entry =
			resolved_snapshot.effective[index].catalog_entry;

		if (entry != NULL && strcmp(entry->group, "LCDCONTR") == 0) {
			*mode = entry->selection == EF800_LCD_CONTRAST_PWM ?
				EF800_LCD_CONTRAST_PWM : EF800_LCD_CONTRAST_STATIC;
			return 0;
		}
	}
	return 0;
}
