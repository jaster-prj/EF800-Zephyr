/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Install a validated main-firmware update using the SRAM staging area.
 */
#ifndef EF800_BOOT_UPDATE_H_
#define EF800_BOOT_UPDATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct flash_area;

/**
 * @brief Install a validated, staged EF800 Application image.
 *
 * @details Prefers `/DATA:/fwu/EF800_APP.pkg`, whose envelope, L496 target,
 * load address, minimum coordinator version, header/payload CRCs, vector
 * table, and partition bounds are validated before erase. When no package is
 * staged, the legacy `/DATA:/fwu/EF800_FW.bin` bootstrap path remains
 * available for the one-time transition; subsequent releases use the package
 * path. The payload is copied in bounded chunks; no full image-sized RAM
 * buffer is required. A staged file is removed only after flash readback and
 * installed-image validation succeed.
 *
 * @param main_area Open flash area for the EF800 Application partition.
 * @param main_address Authorized load address of that partition.
 * @param sram_address Start of valid SRAM used to validate the initial stack
 *        pointer in the candidate vector table.
 * @param sram_size Size in bytes of the valid SRAM range beginning at
 *        @p sram_address.
 * @return 0 after installation and verification; -ENOENT if neither update
 *         file exists; -EINVAL/-EBADMSG for invalid image data; or a negative
 *         errno for filesystem, validation, or flash-operation failures.
 */
int ef800_boot_install_update(const struct flash_area *main_area,
				      uintptr_t main_address,
				      uintptr_t sram_address, size_t sram_size);

#endif /* EF800_BOOT_UPDATE_H_ */
