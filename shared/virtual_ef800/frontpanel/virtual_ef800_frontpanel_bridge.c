/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "virtual_ef800_display.h"
#include "virtual_ef800_frontpanel.h"
#include "virtual_ef800_keyboard.h"
#include "virtual_ef800_keyboard_controller.h"
#include "virtual_ef800_t6963_bus.h"

LOG_MODULE_REGISTER(virtual_ef800_frontpanel_bridge, LOG_LEVEL_INF);

static struct virtual_ef800_display display;
static struct virtual_ef800_frontpanel frontpanel;
static struct virtual_ef800_keyboard keyboard;
static uint8_t last_frame[VIRTUAL_EF800_DISPLAY_FRAME_SIZE];
static uint32_t sequence;

static void bridge_thread(void *arg1, void *arg2, void *arg3)
{
	uint8_t frame[VIRTUAL_EF800_DISPLAY_FRAME_SIZE];
	uint8_t bytes[64];

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	while (true) {
		if (virtual_ef800_t6963_frame_snapshot(frame, sizeof(frame)) == 0 &&
			memcmp(last_frame, frame, sizeof(frame)) != 0) {
			memcpy(last_frame, frame, sizeof(frame));
			memcpy(display.frame, frame, sizeof(frame));
			display.sequence = ++sequence;
			display.dirty = true;
		}
		size_t length = virtual_ef800_keyboard_read(&keyboard, bytes, sizeof(bytes));
		if (length != 0U) {
			(void)virtual_ef800_keyboard_controller_send(bytes, length);
		}
		k_msleep(5U);
	}
}

static int frontpanel_bridge_init(void)
{
	virtual_ef800_display_init(&display);
	virtual_ef800_keyboard_init(&keyboard);
	if (virtual_ef800_frontpanel_start(&frontpanel, &display, &keyboard, 8800U) != 0) {
		LOG_WRN("virtual front panel unavailable; continuing headless");
	} else {
		LOG_INF("production LCD front panel listening on port 8800");
	}
	return 0;
}

SYS_INIT(frontpanel_bridge_init, APPLICATION, 1);
K_THREAD_DEFINE(virtual_ef800_frontpanel_bridge_thread, 2048, bridge_thread,
	NULL, NULL, NULL, 7, 0, 0);
