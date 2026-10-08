/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_virtual_serial_peers.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/kernel.h>

#define PEER_COUNT 2U
#define PEER_SCRIPT_SIZE 128U

struct virtual_peer {
	const struct device *uart;
	uint8_t response[PEER_SCRIPT_SIZE];
	size_t response_length;
	int32_t delay_ms;
	int32_t error;
	bool available;
	uint32_t transaction_count;
	struct k_work_delayable work;
};

static struct virtual_peer peers[PEER_COUNT] = {
	{ .uart = DEVICE_DT_GET(DT_NODELABEL(usart1)), .available = true },
	{ .uart = DEVICE_DT_GET(DT_NODELABEL(usart3)), .available = true },
};

static void peer_work(struct k_work *work)
{
	struct virtual_peer *peer = CONTAINER_OF(work, struct virtual_peer, work.work);

	if (!peer->available || peer->error != 0 || peer->response_length == 0U) {
		return;
	}
	if (peer->response_length == 3U && peer->response[0] == 'A' &&
		peer->response[1] == '\r' && peer->response[2] == '\n' &&
		peer->transaction_count > 1U) {
		static const uint8_t elta3_measurement[] =
			"#48 0 1.234; 100.0000; 50.5000\r\n";
		(void)uart_emul_put_rx_data(peer->uart, elta3_measurement,
			sizeof(elta3_measurement) - 1U);
	} else {
		(void)uart_emul_put_rx_data(peer->uart, peer->response,
			peer->response_length);
	}
}

static void peer_tx_ready(const struct device *dev, size_t size, void *context)
{
	struct virtual_peer *peer = context;
	uint8_t discarded[PEER_SCRIPT_SIZE];

	ARG_UNUSED(size);
	(void)uart_emul_get_tx_data(dev, discarded, sizeof(discarded));
	if (peer->available) {
		peer->transaction_count++;
		k_work_reschedule(&peer->work, K_MSEC(MAX(peer->delay_ms, 0)));
	}
}

int ef800_virtual_serial_peer_set(uint8_t port, const uint8_t *response,
	size_t length, int32_t delay_ms, int32_t error, bool available)
{
	struct virtual_peer *peer;

	if (port == 0U || port > PEER_COUNT || length > PEER_SCRIPT_SIZE ||
		(response == NULL && length != 0U)) {
		return -EINVAL;
	}
	peer = &peers[port - 1U];
	memcpy(peer->response, response, length);
	peer->response_length = length;
	peer->transaction_count = 0U;
	peer->delay_ms = delay_ms;
	peer->error = error;
	peer->available = available;
	return 0;
}

static int serial_peers_init(void)
{
	for (size_t index = 0U; index < ARRAY_SIZE(peers); ++index) {
		k_work_init_delayable(&peers[index].work, peer_work);
		if (device_is_ready(peers[index].uart)) {
			uart_emul_callback_tx_data_ready_set(peers[index].uart,
				peer_tx_ready, &peers[index]);
		}
	}
	return 0;
}

SYS_INIT(serial_peers_init, APPLICATION, 0);
