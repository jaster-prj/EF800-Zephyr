/* SPDX-License-Identifier: Apache-2.0 */

#include "ef800_main_usb_mtp.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#if !defined(CONFIG_BOARD_NATIVE_SIM)
#include <stm32l4xx_ll_bus.h>
#include <stm32l4xx_ll_crs.h>
#endif
#include <zephyr/usb/class/usbd_mtp.h>
#include <zephyr/usb/usbd.h>

#define EF800_MAIN_USB_UDC_NODE DT_NODELABEL(usbotg_fs)

BUILD_ASSERT(DT_NODE_HAS_STATUS(EF800_MAIN_USB_UDC_NODE, okay),
	"MainApplication USB MTP requires an enabled USB controller");

USBD_DEVICE_DEFINE(ef800_main_mtp_usbd,
	DEVICE_DT_GET(EF800_MAIN_USB_UDC_NODE),
	CONFIG_EF800_USB_VID, CONFIG_EF800_USB_PID);
USBD_DESC_LANG_DEFINE(ef800_main_mtp_lang);
USBD_DESC_MANUFACTURER_DEFINE(ef800_main_mtp_manufacturer, "EF800");
USBD_DESC_PRODUCT_DEFINE(ef800_main_mtp_product, "EF800");
USBD_DESC_CONFIG_DEFINE(ef800_main_mtp_config_desc, "EF800 MTP");
USBD_CONFIGURATION_DEFINE(ef800_main_mtp_config, USB_SCD_SELF_POWERED, 125,
	&ef800_main_mtp_config_desc);

USBD_MTP_DEFINE_INSTANCE(ef800_main,
	"EF800", "EF800", "1.0", "EF800000000000000000000000000001",
	USBD_MTP_STORAGE_ENTRY_NAMED("/DATA:/data", "Data", false),
	USBD_MTP_STORAGE_ENTRY_NAMED("/DATA:/plugins", "Plugins", false),
	USBD_MTP_STORAGE_ENTRY_NAMED("/DATA:/fwu", "FWU", false));

static void configure_mtp_device_descriptor(void)
{
	/* EF800 exposes one MTP/PTP interface, not an IAD composite device.
	 * Linux MTP hosts reject the composite class tuple that Windows tolerates. */
	fs_desc_ef800_main_mtp_usbd.bDeviceClass = 0;
	fs_desc_ef800_main_mtp_usbd.bDeviceSubClass = 0;
	fs_desc_ef800_main_mtp_usbd.bDeviceProtocol = 0;
	fs_desc_ef800_main_mtp_usbd.bMaxPacketSize0 = 64;
#if USBD_SUPPORTS_HIGH_SPEED
	hs_desc_ef800_main_mtp_usbd.bDeviceClass = 0;
	hs_desc_ef800_main_mtp_usbd.bDeviceSubClass = 0;
	hs_desc_ef800_main_mtp_usbd.bDeviceProtocol = 0;
	hs_desc_ef800_main_mtp_usbd.bMaxPacketSize0 = 64;
#endif
}

static const char *const ef800_main_mtp_blocklist[] = {
	"cdc_acm_0",
	"msc_0",
	NULL,
};

static bool usb_mtp_active;
static bool usb_mtp_configured;

static void configure_usb_clock_recovery(void)
{
#if !defined(CONFIG_BOARD_NATIVE_SIM) && defined(CRS)
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_CRS);
	LL_CRS_SetSyncSignalSource(LL_CRS_SYNC_SOURCE_USB);
	LL_CRS_EnableAutoTrimming();
	LL_CRS_EnableFreqErrorCounter();
#endif
}

int ef800_main_usb_mtp_start(void)
{
	int ret;

	if (usb_mtp_active) {
		return 0;
	}
	configure_mtp_device_descriptor();
	configure_usb_clock_recovery();
	if (!usb_mtp_configured) {
		ret = usbd_add_descriptor(&ef800_main_mtp_usbd, &ef800_main_mtp_lang);
		if (ret != 0) {
			return ret;
		}
		ret = usbd_add_descriptor(&ef800_main_mtp_usbd,
			&ef800_main_mtp_manufacturer);
		if (ret != 0) {
			return ret;
		}
		ret = usbd_add_descriptor(&ef800_main_mtp_usbd, &ef800_main_mtp_product);
		if (ret != 0) {
			return ret;
		}
		ret = usbd_add_configuration(&ef800_main_mtp_usbd, USBD_SPEED_FS,
			&ef800_main_mtp_config);
		if (ret != 0) {
			return ret;
		}
		usb_mtp_configured = true;
	}
	ret = usbd_register_all_classes(&ef800_main_mtp_usbd, USBD_SPEED_FS,
		1, ef800_main_mtp_blocklist);
	if (ret != 0) {
		return ret;
	}
	ret = usbd_init(&ef800_main_mtp_usbd);
	if (ret != 0) {
		return ret;
	}
	/* Allow the EF800 MCP23017 USB mux and the FS PHY routing to settle before
	 * raising the pull-up. Linux xHCI is less tolerant of the first control
	 * transfer racing this hardware transition than Windows. */
	k_msleep(100);
	ret = usbd_enable(&ef800_main_mtp_usbd);
	if (ret == 0) {
		usb_mtp_active = true;
	}
	return ret;
}

int ef800_main_usb_mtp_stop(void)
{
	int ret;

	if (!usb_mtp_active) {
		return 0;
	}
	ret = usbd_disable(&ef800_main_mtp_usbd);
	if (ret != 0 && ret != -EALREADY) {
		return ret;
	}
	ret = usbd_shutdown(&ef800_main_mtp_usbd);
	if (ret == 0) {
		usb_mtp_active = false;
	}
	return ret;
}

bool ef800_main_usb_mtp_is_active(void)
{
	return usb_mtp_active;
}
