/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_console.h"

#include <errno.h>
#include <zephyr/devicetree.h>

#if defined(CONFIG_USBD_CDC_ACM_CLASS) && DT_HAS_COMPAT_STATUS_OKAY(zephyr_cdc_acm_uart)
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/usb/usbd.h>

#define EF800_USB_UDC_NODE DT_NODELABEL(usbotg_fs)

BUILD_ASSERT(DT_NODE_HAS_STATUS(EF800_USB_UDC_NODE, okay),
	"DebugConsole requires an enabled USB device controller");

USBD_DEVICE_DEFINE(ef800_debug_usbd, DEVICE_DT_GET(EF800_USB_UDC_NODE),
	CONFIG_EF800_USB_VID, CONFIG_EF800_USB_PID);
USBD_DESC_LANG_DEFINE(ef800_debug_lang);
USBD_DESC_MANUFACTURER_DEFINE(ef800_debug_manufacturer, "EF800");
USBD_DESC_PRODUCT_DEFINE(ef800_debug_product, "EF800 DebugConsole");
USBD_DESC_CONFIG_DEFINE(ef800_debug_config_desc, "DebugConsole CDC");
USBD_CONFIGURATION_DEFINE(ef800_debug_config, 0, 125,
	&ef800_debug_config_desc);

static bool debug_usb_started;
static const char *const debug_only_blocklist[] = {
	"msc_0",
	NULL,
};

int ef800_console_usb_start(void)
{
	int ret;

	if (debug_usb_started) {
		return 0;
	}
	ret = usbd_add_descriptor(&ef800_debug_usbd, &ef800_debug_lang);
	if (ret != 0) return ret;
	ret = usbd_add_descriptor(&ef800_debug_usbd, &ef800_debug_manufacturer);
	if (ret != 0) return ret;
	ret = usbd_add_descriptor(&ef800_debug_usbd, &ef800_debug_product);
	if (ret != 0) return ret;
	ret = usbd_add_configuration(&ef800_debug_usbd, USBD_SPEED_FS,
		&ef800_debug_config);
	if (ret != 0) return ret;
	ret = usbd_register_all_classes(&ef800_debug_usbd, USBD_SPEED_FS, 1,
		debug_only_blocklist);
	if (ret != 0) return ret;
	ret = usbd_init(&ef800_debug_usbd);
	if (ret != 0) return ret;
	ret = usbd_enable(&ef800_debug_usbd);
	if (ret == 0) {
		debug_usb_started = true;
	}
	return ret;
}

#else

int ef800_console_usb_start(void)
{
	return -ENOTSUP;
}

#endif
