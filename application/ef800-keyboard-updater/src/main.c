/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include "ef800_watchdog.h"
#include "keyboard_updater_transport.h"
#include "keyboard_updater_session.h"

int main(void)
{
	uint8_t incoming[EF800_KB_UPDATE_REQUEST_MAX];
	uint8_t outgoing[EF800_KB_UPDATE_RESPONSE_MAX];
	size_t incoming_length;
	size_t outgoing_length;
	bool incoming_version;
	bool timed_out;
	bool overflow;
	bool reboot_after_response;

	if (ef800_watchdog_inherit() != 0) {
		return -EIO;
	}
	if (ef800_keyboard_updater_transport_init() != 0) {
		return -ENODEV;
	}

	for (;;) {
		if (ef800_keyboard_updater_request_take(incoming, &incoming_length,
				&incoming_version, &timed_out, &overflow)) {
			(void)ef800_keyboard_updater_session_process(incoming, incoming_length,
				incoming_version, timed_out, overflow, outgoing,
				&outgoing_length, &reboot_after_response);
			(void)ef800_keyboard_updater_response_submit(outgoing,
				outgoing_length, reboot_after_response);
		}
		if (ef800_keyboard_updater_reboot_take()) {
			sys_reboot(SYS_REBOOT_COLD);
		}
		(void)ef800_watchdog_feed();
		k_msleep(1U);
	}
}
