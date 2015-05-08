/*
 * Industrial I/O configfs bits
 *
 * Copyright (c) 2015 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/configfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/slab.h>

#include <linux/iio/iio.h>
#include <linux/iio/sw_trigger.h>

#define MAX_NAME_LEN 32

static struct config_group *trigger_make_group(struct config_group *group,
					       const char *name)
{
	char *type_name;
	char *trigger_name;
	char buf[MAX_NAME_LEN];
	struct iio_sw_trigger *t;

	snprintf(buf, MAX_NAME_LEN, "%s", name);

	/* group name should have the form <trigger-type>-<trigger-name> */
	type_name = buf;
	trigger_name = strchr(buf, '-');
	if (!trigger_name) {
		WARN_ONCE(1, "Unable to locate '-' in %s. Use <type>-<name>.\n",
			  buf);
		return ERR_PTR(-EINVAL);
	}

	/* replace - with \0, this nicely separates the two strings */
	*trigger_name = '\0';
	trigger_name++;

	t = iio_sw_trigger_create(type_name, trigger_name);
	if (IS_ERR(t))
		return ERR_CAST(t);

	config_item_set_name(&t->group.cg_item, name);

	return &t->group;
}

static void trigger_drop_group(struct config_group *group,
			       struct config_item *item)
{
	struct iio_sw_trigger *t = to_iio_sw_trigger(item);

	iio_sw_trigger_destroy(t);
	config_item_put(item);
}

static struct configfs_group_operations triggers_ops = {
	.make_group	= &trigger_make_group,
	.drop_item	= &trigger_drop_group,
};

static struct config_item_type iio_triggers_group_type = {
	.ct_group_ops = &triggers_ops,
	.ct_owner       = THIS_MODULE,
};

static struct config_group iio_triggers_group = {
	.cg_item = {
		.ci_namebuf = "triggers",
		.ci_type = &iio_triggers_group_type,
	},
};

static struct config_group *iio_root_default_groups[] = {
	&iio_triggers_group,
	NULL
};

static struct config_item_type iio_root_group_type = {
	.ct_owner       = THIS_MODULE,
};

static struct configfs_subsystem iio_configfs_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "iio",
			.ci_type = &iio_root_group_type,
		},
		.default_groups = iio_root_default_groups,
	},
	.su_mutex = __MUTEX_INITIALIZER(iio_configfs_subsys.su_mutex),
};

static int __init iio_configfs_init(void)
{
	config_group_init(&iio_triggers_group);
	config_group_init(&iio_configfs_subsys.su_group);

	return configfs_register_subsystem(&iio_configfs_subsys);
}
module_init(iio_configfs_init);

static void __exit iio_configfs_exit(void)
{
	configfs_unregister_subsystem(&iio_configfs_subsys);
}
module_exit(iio_configfs_exit);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com>");
MODULE_DESCRIPTION("Industrial I/O configfs support");
MODULE_LICENSE("GPL v2");
