/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ef800_virtual_display.h"
#include "ef800_virtual_frontpanel.h"
#include "ef800_virtual_keyboard.h"
#include "ef800_virtual_keyboard_controller.h"
#include "ef800_virtual_t6963_bus.h"

LOG_MODULE_REGISTER(ef800_virtual_frontpanel_bridge, LOG_LEVEL_INF);

static struct ef800_virtual_display display;
static struct ef800_virtual_frontpanel frontpanel;
static struct ef800_virtual_keyboard keyboard;
static uint8_t last_frame[EF800_VIRTUAL_DISPLAY_FRAME_SIZE];
static uint32_t sequence;

static void bridge_thread(void *arg1, void *arg2, void *arg3)
{
	uint8_t frame[EF800_VIRTUAL_DISPLAY_FRAME_SIZE];
	uint8_t bytes[64];

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	while (true) {
		if (ef800_virtual_t6963_frame_snapshot(frame, sizeof(frame)) == 0 &&
			memcmp(last_frame, frame, sizeof(frame)) != 0) {
			memcpy(last_frame, frame, sizeof(frame));
			memcpy(display.frame, frame, sizeof(frame));
			display.sequence = ++sequence;
			display.dirty = true;
		}
		size_t length = ef800_virtual_keyboard_read(&keyboard, bytes, sizeof(bytes));
		if (length != 0U) {
			(void)ef800_virtual_keyboard_controller_send(bytes, length);
		}
		k_msleep(5U);
	}
}

static int frontpanel_bridge_init(void)
{
	ef800_virtual_display_init(&display);
	ef800_virtual_keyboard_init(&keyboard);
	if (ef800_virtual_frontpanel_start(&frontpanel, &display, &keyboard, 8800U) != 0) {
		LOG_WRN("virtual front panel unavailable; continuing headless");
	} else {
		LOG_INF("production LCD front panel listening on port 8800");
	}
	return 0;
}

SYS_INIT(frontpanel_bridge_init, APPLICATION, 1);
K_THREAD_DEFINE(ef800_virtual_frontpanel_bridge_thread, 2048, bridge_thread,
	NULL, NULL, NULL, 7, 0, 0);
