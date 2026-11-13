/* SPDX-License-Identifier: Apache-2.0 */
/** @file @brief Connect virtual GPIO signals to the T6963 emulator bus. */
#ifndef VIRTUAL_EF800_T6963_BUS_H
#define VIRTUAL_EF800_T6963_BUS_H

#include <stddef.h>
#include <stdint.h>

/** @brief Copy the latest emulated LCD frame into a caller buffer.
 * @param frame Destination framebuffer.
 * @param capacity Destination capacity in bytes.
 * @return Frame size on success; negative errno if capacity is too small/unavailable.
 */
int virtual_ef800_t6963_frame_snapshot(uint8_t *frame, size_t capacity);

#endif
