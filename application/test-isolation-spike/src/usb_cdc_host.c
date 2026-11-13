/* SPDX-License-Identifier: Apache-2.0 */

#include "usb_cdc_host.h"

#include <errno.h>
#include <zephyr/kernel.h>

#if CONFIG_TEST_ISOLATION_SPIKE_USB_CDC_TEST

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/usb/usbd.h>

#define SPIKE_USB_VID 0x2fe3U
#define SPIKE_USB_PID 0x8001U

static const struct device *const cdc_uart =
	DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

USBD_DEVICE_DEFINE(spike_usbd,
	DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
	SPIKE_USB_VID, SPIKE_USB_PID);

USBD_DESC_LANG_DEFINE(spike_lang);
USBD_DESC_MANUFACTURER_DEFINE(spike_manufacturer, "EF800 development spike");
USBD_DESC_PRODUCT_DEFINE(spike_product, "EF800 isolation CDC test");
USBD_DESC_CONFIG_DEFINE(spike_config_desc, "CDC ACM");

USBD_CONFIGURATION_DEFINE(spike_fs_config, 0, 100, &spike_config_desc);

static const char *const class_blocklist[] = {
	"dfu_dfu",
	NULL,
};

static void usb_message_callback(struct usbd_context *const context,
				const struct usbd_msg *message)
{
	if (usbd_can_detect_vbus(context)) {
		if (message->type == USBD_MSG_VBUS_READY) {
			(void)usbd_enable(context);
		} else if (message->type == USBD_MSG_VBUS_REMOVED) {
			(void)usbd_disable(context);
		}
	}
}

int spike_usb_cdc_host_init(void)
{
	int ret;

	if (!device_is_ready(cdc_uart)) {
		return -ENODEV;
	}

	ret = usbd_add_descriptor(&spike_usbd, &spike_lang);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_add_descriptor(&spike_usbd, &spike_manufacturer);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_add_descriptor(&spike_usbd, &spike_product);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_add_configuration(&spike_usbd, USBD_SPEED_FS,
				     &spike_fs_config);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_register_all_classes(&spike_usbd, USBD_SPEED_FS, 1,
					class_blocklist);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_device_set_code_triple(&spike_usbd, USBD_SPEED_FS,
					 USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	if (ret != 0) {
		return ret;
	}
	usbd_self_powered(&spike_usbd, false);

	ret = usbd_msg_register_cb(&spike_usbd, usb_message_callback);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_init(&spike_usbd);
	if (ret != 0) {
		return ret;
	}

	if (!usbd_can_detect_vbus(&spike_usbd)) {
		ret = usbd_enable(&spike_usbd);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

int spike_usb_cdc_host_read(uint8_t *byte)
{
	unsigned char value;
	int ret;

	if (byte == NULL) {
		return -EINVAL;
	}
	ret = uart_poll_in(cdc_uart, &value);
	if (ret == 0) {
		*byte = value;
		return 0;
	}

	return -EAGAIN;
}

void spike_usb_cdc_host_write(const uint8_t *data, size_t length)
{
	if (data == NULL) {
		return;
	}

	for (size_t i = 0; i < length; ++i) {
		uart_poll_out(cdc_uart, data[i]);
	}
}

#else

int spike_usb_cdc_host_init(void)
{
	return -ENOTSUP;
}

int spike_usb_cdc_host_read(uint8_t *byte)
{
	ARG_UNUSED(byte);
	return -ENOTSUP;
}

void spike_usb_cdc_host_write(const uint8_t *data, size_t length)
{
	ARG_UNUSED(data);
	ARG_UNUSED(length);
}

#endif
