/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>

#include "ef800_watchdog.h"
#include "ef800_console.h"

static K_THREAD_STACK_DEFINE(watchdog_stack, 512);
static struct k_thread watchdog_thread;

static void backup_watchdog_task(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		(void)ef800_watchdog_feed();
		k_msleep(250U);
	}
}

static void backup_watchdog_start(void)
{
	if (ef800_watchdog_start(1000U) != 0) {
		return;
	}
	(void)k_thread_create(&watchdog_thread, watchdog_stack,
				      K_THREAD_STACK_SIZEOF(watchdog_stack),
				      backup_watchdog_task, NULL, NULL, NULL,
				      K_PRIO_PREEMPT(2), 0, K_NO_WAIT);
}
#if CONFIG_EF800_BACKUP_USB_MSC
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/usb/usbd.h>
#endif

#if CONFIG_EF800_BACKUP_USB_MSC
#define EF800_DISK_NAME "EF800"

#define EF800_USB_UDC_NODE DT_NODELABEL(usbotg_fs)

BUILD_ASSERT(DT_NODE_HAS_STATUS(EF800_USB_UDC_NODE, okay),
		     "EF800 recovery requires an enabled USB device controller");

USBD_DEVICE_DEFINE(ef800_usbd,
		   DEVICE_DT_GET(EF800_USB_UDC_NODE),
		   CONFIG_EF800_USB_VID, CONFIG_EF800_USB_PID);

USBD_DESC_LANG_DEFINE(ef800_lang);
USBD_DESC_MANUFACTURER_DEFINE(ef800_manufacturer, "EF800");
USBD_DESC_PRODUCT_DEFINE(ef800_product, "EF800 Recovery");
USBD_DESC_CONFIG_DEFINE(ef800_config_desc, "Recovery MSC");

static const uint8_t ef800_config_attributes =
	(IS_ENABLED(CONFIG_EF800_USB_SELF_POWERED) ? USB_SCD_SELF_POWERED : 0U);

USBD_CONFIGURATION_DEFINE(ef800_config,
			  ef800_config_attributes,
			  125,
			  &ef800_config_desc);

USBD_DEFINE_MSC_LUN(qspi, EF800_DISK_NAME, "EF800", "Recovery Disk", "1.00");

static int ef800_usb_start(void)
{
	int rc;

	rc = usbd_add_descriptor(&ef800_usbd, &ef800_lang);
	if (rc < 0) {
		return rc;
	}
	rc = usbd_add_descriptor(&ef800_usbd, &ef800_manufacturer);
	if (rc < 0) {
		return rc;
	}
	rc = usbd_add_descriptor(&ef800_usbd, &ef800_product);
	if (rc < 0) {
		return rc;
	}
	rc = usbd_add_configuration(&ef800_usbd, USBD_SPEED_FS, &ef800_config);
	if (rc < 0) {
		return rc;
	}
	rc = usbd_register_all_classes(&ef800_usbd, USBD_SPEED_FS, 1, NULL);
	if (rc < 0) {
		return rc;
	}
	rc = usbd_init(&ef800_usbd);
	if (rc < 0) {
		return rc;
	}

	return usbd_enable(&ef800_usbd);
}
#endif /* CONFIG_EF800_BACKUP_USB_MSC */

int main(void)
{
	(void)ef800_console_init(true);
	printk("ef800 backup application: console=%u\r\n",
	       ef800_console_get_target());
	backup_watchdog_start();
#if CONFIG_EF800_BACKUP_USB_MSC
	const struct device *const udc = DEVICE_DT_GET(EF800_USB_UDC_NODE);
	int rc;

	if (!device_is_ready(udc)) {
		return 0;
	}

	/* MSC owns the disk. Do not call fs_mount() in this application. */
	(void)disk_access_init(EF800_DISK_NAME);
	(void)disk_access_status(EF800_DISK_NAME);

	rc = ef800_usb_start();
	if (rc < 0) {
		return 0;
	}
#endif

	while (true) {
		k_sleep(K_FOREVER);
	}
}
