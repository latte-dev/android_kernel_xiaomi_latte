/**
 * The industrial I/O periodic hrtimer trigger driver
 *
 * Copyright (C) Intuitive Aerial AB
 * Written by Marten Svanfeldt, marten@intuitiveaerial.com
 * Copyright (C) 2012, Analog Device Inc.
 *	Author: Lars-Peter Clausen <lars@metafoo.de>
 * Copyright (C) 2015, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/hrtimer.h>

#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/sw_trigger.h>

/* default sampling frequency - 100Hz */
#define HRTIMER_DEFAULT_SAMPLING_FREQUENCY 100

struct iio_hrtimer_info {
	struct iio_sw_trigger swt;
	struct hrtimer timer;
	unsigned long sampling_frequency;
	ktime_t period;
};

#ifdef CONFIG_CONFIGFS_FS
struct iio_hrtimer_info *to_iio_hrtimer_info(struct config_item *item)
{
	return item ? container_of(to_iio_sw_trigger(item),
				   struct iio_hrtimer_info, swt) : NULL;
}

CONFIGFS_ATTR_STRUCT(iio_hrtimer_info);

#define IIO_HRTIMER_INFO_ATTR(_name, _mode, _show, _store) \
	struct iio_hrtimer_info_attribute iio_hrtimer_attr_##_name = \
	__CONFIGFS_ATTR(_name, _mode, _show, _store)

static
ssize_t iio_hrtimer_info_show_sampling_frequency(struct iio_hrtimer_info *info,
						 char *page)
{
	return snprintf(page, PAGE_SIZE, "%lu\n", info->sampling_frequency);
}

static
ssize_t iio_hrtimer_info_store_sampling_frequency(struct iio_hrtimer_info *info,
						  const char *page,
						  size_t count)
{
	unsigned long val;
	int ret;

	ret = kstrtoul(page, 10, &val);
	if (ret)
		return ret;

	if (!val || val > NSEC_PER_SEC)
		return -EINVAL;

	info->sampling_frequency = val;
	info->period = ktime_set(0, NSEC_PER_SEC / val);

	return count;
}

IIO_HRTIMER_INFO_ATTR(sampling_frequency, S_IRUGO | S_IWUSR,
		      iio_hrtimer_info_show_sampling_frequency,
		      iio_hrtimer_info_store_sampling_frequency);

static struct configfs_attribute *iio_hrtimer_attrs[] = {
	&iio_hrtimer_attr_sampling_frequency.attr,
	NULL
};

CONFIGFS_ATTR_OPS(iio_hrtimer_info);
static struct configfs_item_operations iio_hrtimer_ops = {
	.show_attribute		= iio_hrtimer_info_attr_show,
	.store_attribute	= iio_hrtimer_info_attr_store,
};

static struct config_item_type iio_hrtimer_type = {
	.ct_item_ops	= &iio_hrtimer_ops,
	.ct_attrs	= iio_hrtimer_attrs,
	.ct_owner	= THIS_MODULE,
};

#endif /* CONFIGFS_FS */

static enum hrtimer_restart iio_hrtimer_trig_handler(struct hrtimer *timer)
{
	struct iio_hrtimer_info *info;

	info = container_of(timer, struct iio_hrtimer_info, timer);

	hrtimer_forward_now(timer, info->period);
	iio_trigger_poll(info->swt.trigger, 0);

	return HRTIMER_RESTART;
}

static int iio_trig_hrtimer_set_state(struct iio_trigger *trig, bool state)
{
	struct iio_hrtimer_info *trig_info;

	trig_info = iio_trigger_get_drvdata(trig);

	if (state)
		hrtimer_start(&trig_info->timer, trig_info->period,
			      HRTIMER_MODE_REL);
	else
		hrtimer_cancel(&trig_info->timer);

	return 0;
}

static const struct iio_trigger_ops iio_hrtimer_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = iio_trig_hrtimer_set_state,
};

static struct iio_sw_trigger *iio_trig_hrtimer_probe(const char *name)
{
	struct iio_hrtimer_info *trig_info;
	int ret;

	trig_info = kzalloc(sizeof(*trig_info), GFP_KERNEL);
	if (!trig_info)
		return ERR_PTR(-ENOMEM);

	trig_info->swt.trigger = iio_trigger_alloc("%s", name);
	if (!trig_info->swt.trigger) {
		ret = -ENOMEM;
		goto err_free_trig_info;
	}

	iio_trigger_set_drvdata(trig_info->swt.trigger, trig_info);
	trig_info->swt.trigger->ops = &iio_hrtimer_trigger_ops;

	hrtimer_init(&trig_info->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	trig_info->timer.function = iio_hrtimer_trig_handler;

	trig_info->sampling_frequency = HRTIMER_DEFAULT_SAMPLING_FREQUENCY;
	trig_info->period = ktime_set(0, NSEC_PER_SEC /
				      trig_info->sampling_frequency);

	ret = iio_trigger_register(trig_info->swt.trigger);
	if (ret)
		goto err_free_trigger;

	iio_config_group_init_type_name(&trig_info->swt.group, name,
					&iio_hrtimer_type);
	return &trig_info->swt;
err_free_trigger:
	iio_trigger_free(trig_info->swt.trigger);
err_free_trig_info:
	kfree(trig_info);

	return ERR_PTR(ret);
}

static int iio_trig_hrtimer_remove(struct iio_sw_trigger *swt)
{
	struct iio_hrtimer_info *trig_info;

	trig_info = iio_trigger_get_drvdata(swt->trigger);

	hrtimer_cancel(&trig_info->timer);

	iio_trigger_unregister(swt->trigger);
	iio_trigger_free(swt->trigger);
	kfree(trig_info);

	return 0;
}

struct iio_sw_trigger_ops iio_trig_hrtimer_ops = {
	.probe		= iio_trig_hrtimer_probe,
	.remove		= iio_trig_hrtimer_remove,
};

struct iio_sw_trigger_type iio_trig_hrtimer = {
	.name = "hrtimer",
	.owner = THIS_MODULE,
	.ops = &iio_trig_hrtimer_ops,
};

module_iio_sw_trigger_driver(iio_trig_hrtimer);

MODULE_AUTHOR("Marten Svanfeldt <marten@intuitiveaerial.com>");
MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com>");
MODULE_DESCRIPTION("Periodic hrtimer trigger for the IIO subsystem");
MODULE_LICENSE("GPL v2");
