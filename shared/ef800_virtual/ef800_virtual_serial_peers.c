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
 uint8_t command[16];
 size_t command_length;
	uint8_t pending_command[16];
	size_t pending_command_length;
	struct k_work_delayable work;
};

static struct virtual_peer peers[PEER_COUNT] = {
	/* EF800 Port 1 is UART3; Port 2 is USART1. */
	{ .uart = DEVICE_DT_GET(DT_NODELABEL(usart3)),
		.response = {'A', '\r', '\n'}, .response_length = 3U, .available = true },
	{ .uart = DEVICE_DT_GET(DT_NODELABEL(usart1)),
		.response = {'1','2','3','4',' ','0','4','5','2','7','0','0',' ',
			'0','9','0','0','0','0','0','\r','\n'},
		.response_length = 22U, .available = true },
};

static void peer_work(struct k_work *work)
{
	struct virtual_peer *peer = CONTAINER_OF(work, struct virtual_peer, work.work);

	if (!peer->available || peer->error != 0 || peer->response_length == 0U) {
		return;
	}
	if (peer == &peers[0] && peer->pending_command_length >= 3U &&
		peer->pending_command[0] == 'S' && peer->pending_command[1] == '1' &&
		peer->pending_command[2] == '4') {
		static const uint8_t acknowledge[] = {'A', '\r', '\n'};
		(void)uart_emul_put_rx_data(peer->uart, acknowledge,
			sizeof(acknowledge));
	} else if (peer == &peers[0] && peer->pending_command_length >= 1U &&
		peer->pending_command[0] == 'B') {
		static const uint8_t elta3_measurement[] =
			"#48 0 1.234; 100.0000; 50.5000\r\n";
		(void)uart_emul_put_rx_data(peer->uart, elta3_measurement,
			sizeof(elta3_measurement) - 1U);
	} else if (peer == &peers[0] && peer->pending_command_length >= 1U &&
		peer->pending_command[0] == 'Q') {
		/* Elta3's Q command ends a readout and has no response. */
	} else {
		(void)uart_emul_put_rx_data(peer->uart, peer->response,
			peer->response_length);
	}
	peer->pending_command_length = 0U;
}

static void peer_tx_ready(const struct device *dev, size_t size, void *context)
{
	struct virtual_peer *peer = context;
	uint8_t bytes[PEER_SCRIPT_SIZE];
	uint32_t length;

	ARG_UNUSED(size);
	length = uart_emul_get_tx_data(dev, bytes, sizeof(bytes));
	for (uint32_t index = 0U; index < length; ++index) {
		if (peer->command_length < sizeof(peer->command)) {
			peer->command[peer->command_length++] = bytes[index];
		}
		if (bytes[index] == '\n') {
			memcpy(peer->pending_command, peer->command, peer->command_length);
			peer->pending_command_length = peer->command_length;
			peer->transaction_count++;
			if (peer->available) {
				k_work_reschedule(&peer->work, K_MSEC(MAX(peer->delay_ms, 0)));
			}
			peer->command_length = 0U;
		}
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
	peer->command_length = 0U;
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
