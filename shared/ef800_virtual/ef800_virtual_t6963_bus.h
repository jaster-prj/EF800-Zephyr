/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EF800_VIRTUAL_T6963_BUS_H
#define EF800_VIRTUAL_T6963_BUS_H

#include <stddef.h>
#include <stdint.h>

int ef800_virtual_t6963_frame_snapshot(uint8_t *frame, size_t capacity);

#endif
