/*
 * intel_wcove_gpio.c - Intel WhiskeyCove GPIO(VBUS/VCONN/VCHRG) Control Driver
 *
 * Copyright (C) 2015 Intel Corporation
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Author: Albin B <albin.bala.krishnan@intel.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/extcon.h>
#include <linux/gpio.h>
#include <linux/acpi.h>
#include <linux/power_supply.h>
#include "../power/power_supply_charger.h"
#include <linux/power/bq24192_charger.h>

#define WCOVE_GPIO_VCHGIN	"vchgin_desc"
#define WCOVE_GPIO_OTG		"otg_desc"
#define WCOVE_GPIO_VCONN	"vconn_desc"

#define MAX_UPDATE_VBUS_RETRY_COUNT	3
#define VBUS_UPDATE_DIFFERED_DELAY	100

struct wcove_gpio_info {
	struct platform_device *pdev;
	struct notifier_block nb;
	struct extcon_specific_cable_nb otg_cable_obj;
	struct gpio_desc *gpio_vchgrin;
	struct gpio_desc *gpio_otg;
	struct gpio_desc *gpio_vconn;
	struct list_head gpio_queue;
	struct work_struct gpio_work;
	struct mutex lock;
	spinlock_t gpio_queue_lock;
};

struct wcove_gpio_event {
	struct list_head node;
	bool is_src_connected;
};

static struct wcove_gpio_info *wc_info;
static inline struct power_supply *wcove_gpio_get_psy_charger(void)
{
	struct class_dev_iter iter;
	struct device *dev;
	struct power_supply *pst;

	class_dev_iter_init(&iter, power_supply_class, NULL, NULL);
	while ((dev = class_dev_iter_next(&iter))) {
		pst = dev_get_drvdata(dev);
		if (IS_CHARGER(pst)) {
			class_dev_iter_exit(&iter);
			return pst;
		}
	}
	class_dev_iter_exit(&iter);
	return NULL;
}

static int wcgpio_set_charger_state(struct wcove_gpio_info *info,
						bool state)
{
	struct power_supply *psy;

	psy = wcove_gpio_get_psy_charger();
	if (psy == NULL) {
		dev_err(&info->pdev->dev, "Unable to get psy for charger\n");
		return -ENODEV;
	}
	return set_ps_int_property(psy, POWER_SUPPLY_PROP_ENABLE_CHARGER, state);
}

static int wcgpio_update_vbus_state(struct wcove_gpio_info *info, bool state)
{
	int ret;

	mutex_lock(&info->lock);
	if (state) {
		/* put charger into HiZ mode */
		ret = wcgpio_set_charger_state(info, false);
		if (ret == 0) {
			ret = bq24192_vbus_enable();
			if (ret)
				dev_warn(&info->pdev->dev,
					"Error in VBUS enable %d", ret);
		} else
			ret = -EAGAIN;
	} else {
		ret = bq24192_vbus_disable();
		if (ret)
			dev_warn(&info->pdev->dev,
				"Error in VBUS disable %d", ret);
	}
	mutex_unlock(&info->lock);

	return ret;
}

int wcgpio_set_vbus_state(bool state)
{
	int ret;
	if (!wc_info)
		return -EINVAL;
	ret = wcgpio_update_vbus_state(wc_info, state);

	/* enable/disable vbus based on the provider(source) event */
	if (!ret) {
		dev_info(&wc_info->pdev->dev, "%s: VBUS=%d\n",
						__func__, state);
		gpiod_set_value_cansleep(wc_info->gpio_otg, state);
	}
	return ret;
}
EXPORT_SYMBOL(wcgpio_set_vbus_state);

static void wcgpio_ctrl_worker(struct work_struct *work)
{
	struct wcove_gpio_info *info =
		container_of(work, struct wcove_gpio_info, gpio_work);
	struct wcove_gpio_event *evt, *tmp;
	unsigned long flags;
	struct list_head new_list;
	int retry_count;
	int ret;

        if (list_empty(&info->gpio_queue))
                return;

	spin_lock_irqsave(&info->gpio_queue_lock, flags);
        list_replace_init(&info->gpio_queue, &new_list);
	spin_unlock_irqrestore(&info->gpio_queue_lock, flags);

	list_for_each_entry_safe(evt, tmp, &new_list, node) {
		dev_info(&info->pdev->dev,
				"%s:%d state=%d\n", __FILE__, __LINE__,
				evt->is_src_connected);
		retry_count = 0;

		do {
			/* loop is to update the vbus state in case fails, try
			 * again upto max retry count */
			ret = wcgpio_update_vbus_state(info,
					evt->is_src_connected);
			if (ret != -EAGAIN)
				break;

			msleep(VBUS_UPDATE_DIFFERED_DELAY);
		} while (++retry_count <= MAX_UPDATE_VBUS_RETRY_COUNT);

		if (ret < 0) {
			dev_warn(&info->pdev->dev,
				"Error in update vbus state (%d)\n", ret);
		}

		/* enable/disable vbus based on the provider(source) event */
		gpiod_set_value_cansleep(info->gpio_otg,
						evt->is_src_connected);

		/* FIXME: vchrgin GPIO is not setting here to select
		 * Wireless Charging */
		list_del(&evt->node);
		kfree(evt);
	}
}

static int wcgpio_check_events(struct wcove_gpio_info *info,
					struct extcon_dev *edev)
{
	struct wcove_gpio_event *evt;

	if (!edev)
		return -EIO;

	evt = kzalloc(sizeof(*evt), GFP_ATOMIC);
	if (!evt) {
		dev_err(&info->pdev->dev,
			"failed to allocate memory for SDP/OTG event\n");
		return -ENOMEM;
	}

	evt->is_src_connected = extcon_get_cable_state(edev, "USB_TYPEC_SRC");
	dev_info(&info->pdev->dev,
			"[extcon notification] evt: Provider - %s\n",
			evt->is_src_connected ? "Connected" : "Disconnected");

	INIT_LIST_HEAD(&evt->node);
	spin_lock(&info->gpio_queue_lock);
	list_add_tail(&evt->node, &info->gpio_queue);
	spin_unlock(&info->gpio_queue_lock);

	schedule_work(&info->gpio_work);
	return 0;
}

static int wcgpio_event_handler(struct notifier_block *nblock,
					unsigned long event, void *param)
{
	int ret = 0;
	struct wcove_gpio_info *info =
			container_of(nblock, struct wcove_gpio_info, nb);
	struct extcon_dev *edev = param;

	ret = wcgpio_check_events(info, edev);

	if (ret < 0)
		return NOTIFY_DONE;

	return NOTIFY_OK;
}

static void check_initial_events(struct wcove_gpio_info *info)
{
	struct extcon_dev *edev;

	edev = extcon_get_extcon_dev("usb-typec");

	wcgpio_check_events(info, edev);
}

static int wcove_gpio_probe(struct platform_device *pdev)
{
	struct wcove_gpio_info *info;
	int ret;

	info = devm_kzalloc(&pdev->dev,
			sizeof(struct wcove_gpio_info), GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "kzalloc failed\n");
		ret = -ENOMEM;
		goto error;
	}

	info->pdev = pdev;
	platform_set_drvdata(pdev, info);
	mutex_init(&info->lock);
	INIT_LIST_HEAD(&info->gpio_queue);
	INIT_WORK(&info->gpio_work, wcgpio_ctrl_worker);
	spin_lock_init(&info->gpio_queue_lock);

	info->nb.notifier_call = wcgpio_event_handler;
	ret = extcon_register_interest(&info->otg_cable_obj, NULL,
						"USB_TYPEC_SRC",
						&info->nb);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to register extcon notifier for otg\n");
		goto error;
	}

	/* FIXME: hardcoding of the index 0, 1 & 2 should fix when upstreaming.
	 * However ACPI _DSD is not support in Gmin yet and we need to live
	 * with it.
	 */
	info->gpio_vchgrin = devm_gpiod_get_index(&pdev->dev,
					WCOVE_GPIO_VCHGIN, 0);
	if (IS_ERR(info->gpio_vchgrin)) {
		dev_err(&pdev->dev, "Can't request gpio_vchgrin\n");
		ret = PTR_ERR(info->gpio_vchgrin);
		goto error_gpio;
	}

	info->gpio_otg = devm_gpiod_get_index(&pdev->dev,
					WCOVE_GPIO_OTG, 1);
	if (IS_ERR(info->gpio_otg)) {
		dev_err(&pdev->dev, "Can't request gpio_otg\n");
		ret = PTR_ERR(info->gpio_otg);
		goto error_gpio;
	}

	info->gpio_vconn = devm_gpiod_get_index(&pdev->dev,
					WCOVE_GPIO_VCONN, 2);
	if (IS_ERR(info->gpio_vconn)) {
		dev_err(&pdev->dev, "Can't request gpio_vconn\n");
		ret = PTR_ERR(info->gpio_vconn);
		goto error_gpio;
	}

	ret = gpiod_direction_output(info->gpio_vchgrin, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot configure vchgrin-gpio %d\n", ret);
		goto error_gpio;
	}

	ret = gpiod_direction_output(info->gpio_otg, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot configure otg-gpio %d\n", ret);
		goto error_gpio;
	}

	ret = gpiod_direction_output(info->gpio_vconn, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot configure vconn-gpio %d\n", ret);
		goto error_gpio;
	}
	dev_dbg(&pdev->dev, "wcove gpio probed\n");

	check_initial_events(info);

	/* Enable vconn always to typec chip */
	gpiod_set_value_cansleep(info->gpio_vconn, 1);
	wc_info = info;

	return 0;

error_gpio:
	extcon_unregister_interest(&info->otg_cable_obj);
error:
	return ret;
}

static int wcove_gpio_remove(struct platform_device *pdev)
{
	struct wcove_gpio_info *info =  dev_get_drvdata(&pdev->dev);

	if (info)
		extcon_unregister_interest(&info->otg_cable_obj);

	return 0;
}

static int wcove_gpio_suspend(struct device *dev)
{
	dev_dbg(dev, "%s called\n", __func__);
	return 0;
}

static int wcove_gpio_resume(struct device *dev)
{
	dev_dbg(dev, "%s called\n", __func__);
	return 0;
}

static int wcove_gpio_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "%s called\n", __func__);
	return 0;
}

static int wcove_gpio_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "%s called\n", __func__);
	return 0;
}

static int wcove_gpio_runtime_idle(struct device *dev)
{
	dev_dbg(dev, "%s called\n", __func__);
	return 0;
}

#ifdef CONFIG_PM_SLEEP

static const struct dev_pm_ops wcove_gpio_driver_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(wcove_gpio_suspend,
				wcove_gpio_resume)

	SET_RUNTIME_PM_OPS(wcove_gpio_runtime_suspend,
				wcove_gpio_runtime_resume,
				wcove_gpio_runtime_idle)
};
#endif /* CONFIG_PM_SLEEP */

static struct acpi_device_id wcove_gpio_acpi_ids[] = {
	{"GPTC0001"},
	{}
};
MODULE_DEVICE_TABLE(acpi, wcove_gpio_acpi_ids);

static struct platform_device_id wcove_gpio_device_ids[] = {
	{"gptc0001", 0},
	{},
};

static struct platform_driver wcove_gpio_driver = {
	.driver = {
		.name = "gptc0001",
		.owner = THIS_MODULE,
		.acpi_match_table = ACPI_PTR(wcove_gpio_acpi_ids),
#ifdef CONFIG_PM_SLEEP
		.pm = &wcove_gpio_driver_pm_ops,
#endif /* CONFIG_PM_SLEEP */
	},
	.probe = wcove_gpio_probe,
	.remove = wcove_gpio_remove,
	.id_table = wcove_gpio_device_ids,
};

static int __init wcove_gpio_init(void)
{
	int ret;
	ret =  platform_driver_register(&wcove_gpio_driver);
	return ret;
}
late_initcall(wcove_gpio_init);

static void __exit wcove_gpio_exit(void)
{
	platform_driver_unregister(&wcove_gpio_driver);
}
module_exit(wcove_gpio_exit)

MODULE_AUTHOR("Albin B<albin.bala.krishnan@intel.com>");
MODULE_DESCRIPTION("Intel Whiskey Cove GPIO Driver");
MODULE_LICENSE("GPL");
