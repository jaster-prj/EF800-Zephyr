/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_main_usb_msc.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>

#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/usb/usbd.h>

#define EF800_MAIN_DISK_NAME "EF800"
#define EF800_MAIN_USB_UDC_NODE DT_NODELABEL(usbotg_fs)

BUILD_ASSERT(DT_NODE_HAS_STATUS(EF800_MAIN_USB_UDC_NODE, okay),
		     "MainApplication USB MSC requires an enabled USB controller");

USBD_DEVICE_DEFINE(ef800_main_usbd,
		   DEVICE_DT_GET(EF800_MAIN_USB_UDC_NODE),
		   CONFIG_EF800_USB_VID, CONFIG_EF800_USB_PID);
USBD_DESC_LANG_DEFINE(ef800_main_lang);
USBD_DESC_MANUFACTURER_DEFINE(ef800_main_manufacturer, "EF800");
USBD_DESC_PRODUCT_DEFINE(ef800_main_product, "EF800");
USBD_DESC_CONFIG_DEFINE(ef800_main_config_desc, "EF800 MSD");
USBD_CONFIGURATION_DEFINE(ef800_main_config, USB_SCD_SELF_POWERED, 125,
				  &ef800_main_config_desc);
USBD_DEFINE_MSC_LUN(qspi_main, EF800_MAIN_DISK_NAME, "EF800", "Application Disk",
			   "1.00");

static bool usb_msc_active;

int ef800_main_usb_msc_start(void)
{
	int ret;

	if (usb_msc_active) {
		return 0;
	}
	ret = disk_access_init(EF800_MAIN_DISK_NAME);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_add_descriptor(&ef800_main_usbd, &ef800_main_lang);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_add_descriptor(&ef800_main_usbd, &ef800_main_manufacturer);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_add_descriptor(&ef800_main_usbd, &ef800_main_product);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_add_configuration(&ef800_main_usbd, USBD_SPEED_FS,
					     &ef800_main_config);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_register_all_classes(&ef800_main_usbd, USBD_SPEED_FS, 1, NULL);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_init(&ef800_main_usbd);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_enable(&ef800_main_usbd);
	if (ret == 0) {
		usb_msc_active = true;
	}
	return ret;
}

int ef800_main_usb_msc_stop(void)
{
	int ret;
	int shutdown_ret;

	if (!usb_msc_active) {
		return 0;
	}
	/* Disable the controller before shutdown so the MSC worker stops issuing
	 * endpoint traffic while the QSPI disk ownership is transferred. */
	ret = usbd_disable(&ef800_main_usbd);
	if (ret != 0 && ret != -EALREADY) {
		return ret;
	}
	shutdown_ret = usbd_shutdown(&ef800_main_usbd);
	if (shutdown_ret != 0) {
		return shutdown_ret;
	}
	usb_msc_active = false;
	return 0;
}

bool ef800_main_usb_msc_is_active(void)
{
	return usb_msc_active;
}
