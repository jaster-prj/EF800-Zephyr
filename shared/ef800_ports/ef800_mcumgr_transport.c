/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_mcumgr_transport.h"

#if defined(CONFIG_MCUMGR) && defined(CONFIG_MCUMGR_GRP_FS)

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/mgmt/mcumgr/transport/serial.h>
#include <zephyr/mgmt/mcumgr/transport/smp.h>
#include <mgmt/mcumgr/transport/smp_internal.h>

#include "ef800_console.h"
#include "ef800_shell_transport.h"

#define EF800_MCUMGR_MTU 256U

static struct smp_transport transport;
static struct mcumgr_serial_rx_ctxt rx_context;
static bool active;
static struct k_work rx_work;

static int mcumgr_output(struct net_buf *buffer)
{
	int result = ef800_console_write(buffer->data, buffer->len);

	smp_packet_free(buffer);
	return result < 0 ? result : 0;
}

static uint16_t mcumgr_get_mtu(const struct net_buf *buffer)
{
	ARG_UNUSED(buffer);
	return EF800_MCUMGR_MTU;
}

static void mcumgr_rx_work(struct k_work *work)
{
	uint8_t buffer[64];
	int result;

	ARG_UNUSED(work);
	while (active) {
		result = ef800_console_read(buffer);
		if (result != 0) {
			break;
		}
		struct net_buf *request = mcumgr_serial_process_frag(&rx_context,
			buffer, 1U);
		if (request != NULL) {
			smp_rx_req(&transport, request);
		}
	}
}

static void mcumgr_poll(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	if (active) {
		k_work_submit(&rx_work);
	}
}

K_TIMER_DEFINE(mcumgr_timer, mcumgr_poll, NULL);

int ef800_mcumgr_transport_init(void)
{
	int result;

	transport.functions.output = mcumgr_output;
	transport.functions.get_mtu = mcumgr_get_mtu;
	result = smp_transport_init(&transport);
	if (result != 0) {
		return result;
	}
	k_work_init(&rx_work, mcumgr_rx_work);
	active = true;
	ef800_shell_set_binary_mode(true);
	k_timer_start(&mcumgr_timer, K_MSEC(1U), K_MSEC(1U));
	return 0;
}

void ef800_mcumgr_transport_stop(void)
{
	if (!active) {
		return;
	}
	active = false;
	k_timer_stop(&mcumgr_timer);
	(void)k_work_cancel(&rx_work);
	ef800_shell_set_binary_mode(false);
}

#else

int ef800_mcumgr_transport_init(void)
{
	return -ENOTSUP;
}

void ef800_mcumgr_transport_stop(void)
{
}

#endif
