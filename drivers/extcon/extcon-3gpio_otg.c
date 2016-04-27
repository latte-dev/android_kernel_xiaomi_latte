/*
 * Virtual USB OTG Port driver controlled by 3 gpios
 *
 * Copyright (c) 2014, Intel Corporation.
 * Author: David Cohen <david.a.cohen@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/acpi.h>
#include <linux/extcon.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#define DRV_NAME	"usb_otg_port"
#define VBUS_CTRL_CDEV_NAME		"vbus_control"

enum vbus_states {
	VBUS_ENABLE,
	VBUS_DISABLE,
	MAX_VBUSCTRL_STATES,
};

enum vup_modes {
	VUP_HOST,
	VUP_PERIPHERAL,
};

struct vuport {
	struct device *dev;
	struct gpio_desc *gpio_vbus_en;
	struct gpio_desc *gpio_usb_id;
	struct gpio_desc *gpio_usb_mux;
	int usb_id_value;
	int vbus_state;
	struct thermal_cooling_device *vbus_cdev;

	struct mutex mutex_port;
	struct extcon_dev edev;
};

/* vbus control cooling device callbacks */
static int vbus_get_max_state(struct thermal_cooling_device *tcd,
				unsigned long *state)
{
	*state = MAX_VBUSCTRL_STATES;
	return 0;
}

static int vbus_get_cur_state(struct thermal_cooling_device *tcd,
				unsigned long *state)
{
	struct vuport *vup = tcd->devdata;

	mutex_lock(&vup->mutex_port);
	*state = vup->vbus_state;
	mutex_unlock(&vup->mutex_port);

	return 0;
}

static int vbus_set_cur_state(struct thermal_cooling_device *tcd,
					unsigned long new_state)
{
	struct vuport *vup = tcd->devdata;

	if (new_state >= MAX_VBUSCTRL_STATES || new_state < 0) {
		dev_err(vup->dev, "Invalid vbus control state: %ld\n",
				new_state);
		return -EINVAL;
	}

	dev_info(vup->dev, "%s: id_short=%d\n", __func__,
			vup->usb_id_value);
	/**
	 * set gpio directly only when the ID_GND and want to change the state
	 * from previous state (vbus enable/disable).
	 */
	mutex_lock(&vup->mutex_port);
	if ((vup->usb_id_value == VUP_HOST) && (vup->vbus_state != new_state) &&
			!IS_ERR(vup->gpio_vbus_en)) {
		gpiod_direction_output(vup->gpio_vbus_en, !new_state);
	}

	vup->vbus_state = new_state;
	mutex_unlock(&vup->mutex_port);

	return 0;
}

static struct thermal_cooling_device_ops psy_vbuscd_ops = {
	.get_max_state = vbus_get_max_state,
	.get_cur_state = vbus_get_cur_state,
	.set_cur_state = vbus_set_cur_state,
};

static inline int register_cooling_device(struct vuport *vup)
{
	vup->vbus_cdev = thermal_cooling_device_register(
				(char *)VBUS_CTRL_CDEV_NAME,
				vup,
				&psy_vbuscd_ops);
	if (IS_ERR(vup->vbus_cdev)) {
		dev_err(vup->dev,
			"Error registering cooling device vbus_control\n");
		return PTR_ERR(vup->vbus_cdev);
	}

	dev_dbg(vup->dev, "cooling device register success for %s\n",
				VBUS_CTRL_CDEV_NAME);
	return 0;
}

static const char *vuport_extcon_cable[] = {
	[0] = "USB-Host",
	NULL,
};

/*
 * If id == 1, USB port should be set to peripheral
 * if id == 0, USB port should be set to host
 *
 * Peripheral: set USB mux to peripheral and disable VBUS
 * Host: set USB mux to host and enable VBUS
 */
static void vuport_set_port(struct vuport *vup, int id)
{
	int mux_val = id;
	int vbus_val = !id & !vup->vbus_state;

	if (!IS_ERR(vup->gpio_usb_mux))
		gpiod_direction_output(vup->gpio_usb_mux, mux_val);

	if (!IS_ERR(vup->gpio_vbus_en))
		gpiod_direction_output(vup->gpio_vbus_en, vbus_val);

	vup->usb_id_value = id;
}

static void vuport_do_usb_id(struct vuport *vup)
{
	int id = gpiod_get_value(vup->gpio_usb_id);

	mutex_lock(&vup->mutex_port);

	/* Nothing changed? Do nothing. */
	if (id == vup->usb_id_value) {
		mutex_unlock(&vup->mutex_port);
		return;
	}

	dev_info(vup->dev, "USB PORT ID: %s\n", id ? "PERIPHERAL" : "HOST");

	/*
	 * id == 1: PERIPHERAL
	 * id == 0: HOST
	 */
	vuport_set_port(vup, id);

	/*
	 * id == 0: HOST connected
	 * id == 1: Host disconnected
	 */
	extcon_set_cable_state(&vup->edev, "USB-Host", !id);

	mutex_unlock(&vup->mutex_port);
}

static irqreturn_t vuport_thread_isr(int irq, void *priv)
{
	struct vuport *vup = priv;
	vuport_do_usb_id(vup);
	return IRQ_HANDLED;
}

static irqreturn_t vuport_isr(int irq, void *priv)
{
	return IRQ_WAKE_THREAD;
}

#define VUPORT_GPIO_USB_ID	0
#define VUPORT_GPIO_VBUS_EN	1
#define VUPORT_GPIO_USB_MUX	2
static int vuport_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vuport *vup;
	int ret;

	vup = devm_kzalloc(dev, sizeof(*vup), GFP_KERNEL);
	if (!vup) {
		dev_err(dev, "cannot allocate private data\n");
		return -ENOMEM;
	}
	vup->dev = dev;
	mutex_init(&vup->mutex_port);

	vup->gpio_usb_id = devm_gpiod_get_index(dev, "id", VUPORT_GPIO_USB_ID);
	if (IS_ERR(vup->gpio_usb_id)) {
		dev_err(dev, "cannot request USB ID GPIO: ret = %ld\n",
			PTR_ERR(vup->gpio_usb_id));
		return PTR_ERR(vup->gpio_usb_id);
	}

	ret = gpiod_direction_input(vup->gpio_usb_id);
	if (ret < 0) {
		dev_err(dev, "cannot set input direction to USB ID GPIO: ret = %d\n",
			ret);
		return ret;
	}

	vup->gpio_vbus_en = devm_gpiod_get_index(dev, "vbus en",
						 VUPORT_GPIO_VBUS_EN);
	if (IS_ERR(vup->gpio_vbus_en))
		dev_info(dev, "cannot request VBUS EN GPIO, skipping it.\n");
	else
		ret = register_cooling_device(vup);

	vup->gpio_usb_mux = devm_gpiod_get_index(dev, "usb mux",
						 VUPORT_GPIO_USB_MUX);
	if (IS_ERR(vup->gpio_usb_mux))
		dev_info(dev, "cannot request USB USB MUX, skipping it.\n");

	/* register extcon device */
	vup->edev.dev.parent = dev;
	vup->edev.supported_cable = vuport_extcon_cable;
	ret = extcon_dev_register(&vup->edev);
	if (ret < 0) {
		dev_err(dev, "failed to register extcon device: ret = %d\n",
			ret);
		return ret;
	}

	/*
	 * We start with unknown value. The fist usb id operation will result
	 * in an extcon event regardless the initial cable state. From the
	 * second usb id operation and on, only actual id value changes will
	 * result in extcon event.
	 */
	vup->usb_id_value = -1;

	ret = devm_request_threaded_irq(dev, gpiod_to_irq(vup->gpio_usb_id),
					vuport_isr, vuport_thread_isr,
					IRQF_SHARED | IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING,
					dev_name(dev), vup);
	if (ret < 0) {
		dev_err(dev, "cannot request IRQ for USB ID GPIO: ret = %d\n",
			ret);
		goto irq_err;
	}
	vuport_do_usb_id(vup);

	platform_set_drvdata(pdev, vup);

	dev_info(dev, "driver successfully probed\n");

	return 0;

irq_err:
	extcon_dev_unregister(&vup->edev);

	return ret;
}

static int vuport_remove(struct platform_device *pdev)
{
	struct vuport *vup = platform_get_drvdata(pdev);
	extcon_dev_unregister(&vup->edev);
	return 0;
}

static struct acpi_device_id vuport_acpi_match[] = {
	{ "INT3496" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, vuport_acpi_match);

#ifdef CONFIG_PM_SLEEP

static void vuport_complete(struct device *dev)
{
	struct vuport *vup = dev_get_drvdata(dev);

	/*
	 * In case a micro A cable was plugged in while device was sleeping,
	 * we missed the interrupt. We need to poll usb id gpio when waking the
	 * driver to detect the missed event.
	 * We use 'complete' callback to give time to all extcon listeners to
	 * resume before we send new events.
	 */
	vuport_do_usb_id(vup);
}

static const struct dev_pm_ops vuport_pm_ops = {
	.complete = vuport_complete,
};

#endif

static struct platform_driver vuport_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.acpi_match_table = ACPI_PTR(vuport_acpi_match),
#ifdef CONFIG_PM_SLEEP
		.pm = &vuport_pm_ops,
#endif
	},
	.probe = vuport_probe,
	.remove = vuport_remove,
};

static int __init vuport_init(void)
{
	return platform_driver_register(&vuport_driver);
}
fs_initcall(vuport_init);

static void __exit vuport_exit(void)
{
	platform_driver_unregister(&vuport_driver);
}
module_exit(vuport_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Cohen <david.a.cohen@linux.intel.com>");
