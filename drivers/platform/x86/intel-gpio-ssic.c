/* intel-gpio-ssic.c - Intel SSIC GPIO Driver
 *
 * Copyright (C) 2015 Intel Corp.
 *
 * Author: Konrad Leszczynski <konrad.leszczynski@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/usb.h>
#include <linux/gpio/consumer.h>
#include <linux/debugfs.h>
#include <../../drivers/usb/core/hub.h>

#define SSIC_GPIO_WAKEUP "ssic_wakeup"
#define SSIC_ADR_SHIFT 18

struct gpio_ssic {
	spinlock_t	lock;

	/* SSIC PHY platform device */
	struct device	*dev;
	/* USB controller owning the SSIC port */
	struct acpi_device	*controller;

	/* Platform specific */
	struct gpio_desc *gpio_wakeup;
	bool	irq_enabled;
	int		irq_wakeup;
	int		port_num;

	/* Entry for debugfs */
	struct dentry *root;

	/* Roothub and usb device associated with the SSIC port */
	struct usb_device		*rhdev;
	struct usb_device		*udev;

	/* Notifier to register for usb device ADD, REMOVE events */
	struct notifier_block		usb_nb;
};

static inline bool is_ssic_roothub(struct gpio_ssic *ssic,
				struct usb_device *udev)
{
	acpi_handle handle;
	struct acpi_device *device;

	/* MUST be Super Speed HUB */
	if (udev->parent || udev->speed != USB_SPEED_SUPER)
		return false;

	/* The acpi_device of the bus controller must be the same as
	 * ssic->controller
	 */
	handle = ACPI_HANDLE(udev->bus->controller);
	if (!handle)
		return false;

	if (acpi_bus_get_device(handle, &device))
		return false;

	return device == ssic->controller;
}

static inline bool is_ssic_dev(struct gpio_ssic *ssic,
				struct usb_device *udev)
{
	return udev->parent &&
		udev->portnum == ssic->port_num;
}

static irqreturn_t gpio_ssic_wakeup_irq(int irq, void *__ssic)
{
	struct gpio_ssic *ssic = __ssic;
	struct usb_device *udev;

	dev_dbg(ssic->dev, "ssic wakeup irq\n");

	udev = usb_get_dev(ssic->udev);

	if (!udev) {
		dev_err(ssic->dev, "bogus wakeup irq\n");
		return IRQ_NONE;
	}

	usb_lock_device(udev);
	usb_autoresume_device(udev);
	usb_unlock_device(udev);
	usb_put_dev(udev);

	return IRQ_HANDLED;
}

static int gpio_ssic_request_irq(struct gpio_ssic *ssic)
{
	struct device *dev = ssic->dev;
	int ret;

	if (ssic->irq_enabled)
		return -EBUSY;

	ret = devm_request_threaded_irq(dev, ssic->irq_wakeup, NULL,
			gpio_ssic_wakeup_irq,
			IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			SSIC_GPIO_WAKEUP, ssic);
	if (ret) {
		dev_err(dev, "failed to request irq %d)\n", ret);
		return ret;
	}

	/* Set irq_enabled flag */
	ssic->irq_enabled = true;
	return 0;
}

static void gpio_ssic_release_irq(struct gpio_ssic *ssic)
{
	struct device *dev = ssic->dev;

	if (!ssic->irq_enabled)
		return;

	devm_free_irq(dev, ssic->irq_wakeup, ssic);

	/* Clear irq_enabled flag */
	ssic->irq_enabled = false;
}

static void gpio_ssic_dev_add(struct gpio_ssic *ssic,
				struct usb_device *udev)
{
	unsigned long	flags;

	if (is_ssic_roothub(ssic, udev)) {
		spin_lock_irqsave(&ssic->lock, flags);
		ssic->rhdev = usb_get_dev(udev);
		spin_unlock_irqrestore(&ssic->lock, flags);
		dev_dbg(ssic->dev, "ssic roothub added\n");
	} else if (is_ssic_dev(ssic, udev)) {
		spin_lock_irqsave(&ssic->lock, flags);
		ssic->udev = usb_get_dev(udev);
		spin_unlock_irqrestore(&ssic->lock, flags);
		dev_dbg(ssic->dev, "ssic device added\n");

		/* Request irq enabling */
		gpio_ssic_request_irq(ssic);
	}
}

static void gpio_ssic_dev_remove(struct gpio_ssic *ssic,
				struct usb_device *udev)
{
	unsigned long	flags;

	if (is_ssic_roothub(ssic, udev)) {
		spin_lock_irqsave(&ssic->lock, flags);
		ssic->rhdev = NULL;
		spin_unlock_irqrestore(&ssic->lock, flags);
		usb_put_dev(udev);
		dev_dbg(ssic->dev, "ssic roothub removed\n");
	} else if (is_ssic_dev(ssic, udev)) {
		/* Request irq disabling */
		gpio_ssic_release_irq(ssic);

		spin_lock_irqsave(&ssic->lock, flags);
		ssic->udev = NULL;
		spin_unlock_irqrestore(&ssic->lock, flags);
		usb_put_dev(udev);
		dev_dbg(ssic->dev, "ssic device removed\n");
	}
}

static int gpio_ssic_notify(struct notifier_block *self,
			unsigned long action, void *dev)
{
	struct gpio_ssic *ssic = container_of(self,
				struct gpio_ssic, usb_nb);

	switch (action) {
	case USB_DEVICE_ADD:
		gpio_ssic_dev_add(ssic, dev);
		break;
	case USB_DEVICE_REMOVE:
		gpio_ssic_dev_remove(ssic, dev);
		break;
	}

	return NOTIFY_OK;
}

static int gpio_ssic_init_gpios(struct gpio_ssic *ssic)
{
	int ret;
	struct device *dev = ssic->dev;

	ssic->gpio_wakeup = devm_gpiod_get_index(dev, SSIC_GPIO_WAKEUP, 0);
	if (IS_ERR(ssic->gpio_wakeup)) {
		dev_err(dev, "can't request gpio_wakeup\n");
		return PTR_ERR(ssic->gpio_wakeup);
	}

	ret = gpiod_direction_input(ssic->gpio_wakeup);
	if (ret) {
		dev_err(dev, "can't configure as input (%d)\n", ret);
		return ret;
	}

	ret = gpiod_to_irq(ssic->gpio_wakeup);
	if (ret < 0) {
		dev_err(dev, "can't get valid irq num (%d)\n", ret);
		return ret;
	}
	ssic->irq_wakeup = ret;

	/* Initialize the spinlock */
	spin_lock_init(&ssic->lock);

	/* Set flags' initial state */
	ssic->irq_enabled = false;

	return 0;
}

static int gpio_ssic_wakeup_connect(struct seq_file *s, void *unused)
{
	struct gpio_ssic	*ssic = s->private;
	return gpio_ssic_wakeup_irq(0, ssic);
}

static int gpio_ssic_interrupt_open(struct inode *inode, struct file *file)
{
	return single_open(file, gpio_ssic_wakeup_connect, inode->i_private);
}

static const struct file_operations gpio_ssic_debugfs_fops = {
	.owner			= THIS_MODULE,
	.open			= gpio_ssic_interrupt_open,
	.write			= NULL,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int gpio_ssic_debugfs_init(struct gpio_ssic *ssic)
{
	struct dentry		*root;
	struct dentry		*file;
	int			ret;

	root = debugfs_create_dir("intel-gpio-ssic", NULL);
	if (!root) {
		ret = -ENOMEM;
		goto err0;
	}

	ssic->root = root;

	file = debugfs_create_file("interrupt", S_IRUGO, root,
		ssic, &gpio_ssic_debugfs_fops);
	if (!file) {
		ret = -ENOMEM;
		goto err1;
	}

	return 0;

err1:
	debugfs_remove_recursive(ssic->root);

err0:
	return ret;
}

static void gpio_ssic_debugfs_exit(struct gpio_ssic *ssic)
{
	debugfs_remove_recursive(ssic->root);
}

static int gpio_ssic_probe(struct platform_device *pdev)
{
	struct gpio_ssic *ssic;
	struct device *dev = &pdev->dev;
	struct acpi_device *adev;
	acpi_status status;
	unsigned long long addr;
	int ret;

	adev = ACPI_COMPANION(&pdev->dev);
	if (!adev) {
		dev_err(dev, "no acpi device\n");
		return -ENODEV;
	}

	ssic = devm_kzalloc(dev, sizeof(*ssic), GFP_KERNEL);
	if (!ssic) {
		dev_err(dev, "ssic alloc failed");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, ssic);
	ssic->dev = dev;

	/* Find the acpi_device of the USB controller */
	if (!adev->parent || !adev->parent->parent ||
		!adev->parent->parent->parent) {
		dev_err(dev, "can't get controller\n");
		return -ENODEV;
	}
	ssic->controller = adev->parent->parent->parent;

	/* Get port number */
	status = acpi_evaluate_integer(adev->parent->handle, METHOD_NAME__ADR,
				NULL, &addr);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "can't get port address\n");
		return -EINVAL;
	}
	ssic->port_num = addr >> SSIC_ADR_SHIFT;

	/* Initialize the GPIO */
	ret = gpio_ssic_init_gpios(ssic);
	if (ret) {
		dev_err(dev, "init gpios failed (%d)\n", ret);
		return ret;
	}

	/* Add a USB notifier */
	ssic->usb_nb.notifier_call = gpio_ssic_notify;
	usb_register_notify(&ssic->usb_nb);

	dev_info(dev, "intel ssic driver probe succeeded\n");
	gpio_ssic_debugfs_init(ssic);

	return 0;
}

static int gpio_ssic_remove(struct platform_device *pdev)
{
	struct gpio_ssic *ssic = platform_get_drvdata(pdev);

	usb_unregister_notify(&ssic->usb_nb);
	usb_put_dev(ssic->rhdev);
	usb_put_dev(ssic->udev);

	gpio_ssic_debugfs_exit(ssic);
	return 0;
}

static void gpio_ssic_shutdown(struct platform_device *pdev)
{
	struct gpio_ssic *ssic = platform_get_drvdata(pdev);

	if (ssic->irq_enabled)
		gpio_ssic_release_irq(ssic);
}

static const struct acpi_device_id gpio_ssic_acpi_ids[] = {
	{"SSP0001", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, ssic_device_ids);

static struct platform_driver gpio_ssic_driver = {
	.driver = {
		.name = "intel-gpio-ssic",
		.owner = THIS_MODULE,
		.acpi_match_table = ACPI_PTR(gpio_ssic_acpi_ids),
	},
	.probe = gpio_ssic_probe,
	.remove = gpio_ssic_remove,
	.shutdown = gpio_ssic_shutdown,
};

static int __init gpio_ssic_init(void)
{
	return platform_driver_register(&gpio_ssic_driver);
}
fs_initcall(gpio_ssic_init);

static void __exit gpio_ssic_exit(void)
{
	platform_driver_unregister(&gpio_ssic_driver);
}
module_exit(gpio_ssic_exit);

MODULE_AUTHOR("Konrad Leszczynski <konrad.leszczynski@intel.com>");
MODULE_DESCRIPTION("Intel SSIC GPIO Driver");
MODULE_LICENSE("GPL");
