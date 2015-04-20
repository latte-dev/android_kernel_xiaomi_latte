/*
 * The Industrial I/O core, software trigger functions
 *
 * Copyright (c) 2015 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/slab.h>

#include <linux/iio/sw_trigger.h>

static LIST_HEAD(iio_trigger_types_list);
static DEFINE_RWLOCK(iio_trigger_types_lock);

static
struct iio_sw_trigger_type *iio_find_sw_trigger_type(char *name, unsigned len)
{
	struct iio_sw_trigger_type *t = NULL, *iter;

	list_for_each_entry(iter, &iio_trigger_types_list, list)
		if (!strncmp(iter->name, name, len)) {
			t = iter;
			break;
		}

	return t;
}

int iio_register_sw_trigger_type(struct iio_sw_trigger_type *t)
{
	struct iio_sw_trigger_type *iter;
	int ret = 0;

	write_lock(&iio_trigger_types_lock);
	iter = iio_find_sw_trigger_type(t->name, strlen(t->name));
	if (iter)
		ret = -EBUSY;
	else
		list_add_tail(&t->list, &iio_trigger_types_list);
	write_unlock(&iio_trigger_types_lock);
	return ret;
}
EXPORT_SYMBOL(iio_register_sw_trigger_type);

int iio_unregister_sw_trigger_type(struct iio_sw_trigger_type *t)
{
	struct iio_sw_trigger_type *iter;
	int ret = 0;

	write_lock(&iio_trigger_types_lock);
	iter = iio_find_sw_trigger_type(t->name, strlen(t->name));
	if (!iter)
		ret = -EINVAL;
	else
		list_del(&t->list);
	write_unlock(&iio_trigger_types_lock);

	return ret;
}
EXPORT_SYMBOL(iio_unregister_sw_trigger_type);

static
struct iio_sw_trigger_type *iio_get_sw_trigger_type(char *name)
{
	struct iio_sw_trigger_type *t;

	read_lock(&iio_trigger_types_lock);
	t = iio_find_sw_trigger_type(name, strlen(name));
	if (t && !try_module_get(t->owner))
		t = NULL;
	read_unlock(&iio_trigger_types_lock);

	return t;
}

struct iio_sw_trigger *iio_sw_trigger_create(char *type, char *name)
{
	struct iio_sw_trigger *t;
	struct iio_sw_trigger_type *tt;

	tt = iio_get_sw_trigger_type(type);
	if (!tt) {
		pr_err("Invalid trigger type: %s\n", type);
		return ERR_PTR(-EINVAL);
	}
	t = tt->ops->probe(name);
	if (IS_ERR(t))
		goto out_module_put;

	return t;
out_module_put:
	module_put(tt->owner);
	return t;
}
EXPORT_SYMBOL(iio_sw_trigger_create);

void iio_sw_trigger_destroy(struct iio_sw_trigger *t)
{
	struct iio_sw_trigger_type *tt = t->trigger_type;

	tt->ops->remove(t);
	module_put(tt->owner);
}
EXPORT_SYMBOL(iio_sw_trigger_destroy);
