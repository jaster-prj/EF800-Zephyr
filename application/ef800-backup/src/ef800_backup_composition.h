/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Read composition-backed LCD contrast mode for BackupApplication.
 */
#ifndef EF800_BACKUP_COMPOSITION_H_
#define EF800_BACKUP_COMPOSITION_H_

#include "ef800_lcd_contrast.h"

/** @brief Resolve the effective LCD contrast mode from the active composition.
 * @param mode Receives the selected mode on success.
 * @return 0 on success; negative errno if composition data is unavailable/invalid.
 */
int ef800_backup_composition_lcd_contrast_mode(
	enum ef800_lcd_contrast_mode *mode);

#endif /* EF800_BACKUP_COMPOSITION_H_ */
