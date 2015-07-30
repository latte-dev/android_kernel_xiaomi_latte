/*
 * devpolicy_mgr.c: Intel USB Power Delivery Device Manager Policy Driver
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. Seee the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Author: Albin B <albin.bala.krishnan@intel.com>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/err.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/extcon.h>
#include <linux/usb_typec_phy.h>
#include "devpolicy_mgr.h"
#include "policy_engine.h"

static struct power_cap spcaps[] = {
	{
		.mv = VIN_5V,
		.ma = ICHRG_P5A,
	},
	{
		.mv = VIN_5V,
		.ma = ICHRG_1P5A,
	},
	{
		.mv = VIN_5V,
		.ma = ICHRG_3A,
	},
	{
		.mv = VIN_9V,
		.ma = ICHRG_1P5A,
	},
	{
		.mv = VIN_9V,
		.ma = ICHRG_3A,
	},
	{
		.mv = VIN_12V,
		.ma = ICHRG_1A,
	},
	{
		.mv = VIN_12V,
		.ma = ICHRG_3A,
	},
};

ATOMIC_NOTIFIER_HEAD(devpolicy_mgr_notifier);
EXPORT_SYMBOL_GPL(devpolicy_mgr_notifier);

static inline struct power_supply *dpm_get_psy(enum psy_type type)
{
	struct class_dev_iter iter;
	struct device *dev;
	static struct power_supply *psy;

	class_dev_iter_init(&iter, power_supply_class, NULL, NULL);
	while ((dev = class_dev_iter_next(&iter))) {
		psy = (struct power_supply *)dev_get_drvdata(dev);
		if ((type == PSY_TYPE_BATTERY && IS_BATTERY(psy)) ||
			(type == PSY_TYPE_CHARGER && IS_CHARGER(psy))) {
			class_dev_iter_exit(&iter);
			return psy;
		}
	}
	class_dev_iter_exit(&iter);

	return NULL;
}

/* Reading the state of charge value of the battery */
static inline int dpm_read_soc(int *soc)
{
	struct power_supply *psy;
	union power_supply_propval val;
	int ret;

	psy = dpm_get_psy(PSY_TYPE_BATTERY);
	if (!psy)
		return -EINVAL;

	ret = psy->get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &val);
	if (!ret)
		*soc = val.intval;

	return ret;
}

static int dpm_get_max_srcpwr_cap(struct devpolicy_mgr *dpm,
				struct power_cap *cap)
{
	cap->mv = VBUS_5V;
	cap->ma = IBUS_1A;
	return 0;
}

static int dpm_get_max_snkpwr_cap(struct devpolicy_mgr *dpm,
					struct power_cap *cap)
{
	cap->mv = VIN_12V;
	cap->ma = ICHRG_3A;
	return 0;
}

static int dpm_get_source_power_cap(struct devpolicy_mgr *dpm,
					struct power_cap *cap)
{
	int val;

	val = typec_get_host_current(dpm->phy);
	if (val < 0) {
		pr_err("DPM: Unable to get the host current from phy\n");
		return val;
	}

	if (val == TYPEC_CURRENT_USB)
		/* setting 900mA source current in case of USB, as
		 * typec connector is capable of supporting USB3.0 */
		cap->ma = IBUS_0P9A;
	else
		cap->ma = val;

	cap->mv = VBUS_5V;

	return 0;
}

static enum batt_soc_status dpm_get_batt_status(struct devpolicy_mgr *dpm)
{
	int soc;

	if (dpm_read_soc(&soc)) {
		pr_err("DPM: Error in getting soc\n");
		return -ENODATA;
	} else {
		pr_debug("DPM: capacity = %d\n", soc);
	}

	if (IS_BATT_SOC_FULL(soc))
		return BATT_SOC_FULL;
	else if (IS_BATT_SOC_GOOD(soc))
		return BATT_SOC_GOOD;
	else if (IS_BATT_SOC_MID2(soc))
		return BATT_SOC_MID2;
	else if (IS_BATT_SOC_MID1(soc))
		return BATT_SOC_MID1;
	else if (IS_BATT_SOC_LOW(soc))
		return BATT_SOC_LOW;
	else if (IS_BATT_SOC_DEAD(soc))
		return BATT_SOC_DEAD;
	else
		return BATT_SOC_UNKNOWN;
}

static int dpm_get_sink_power_cap(struct devpolicy_mgr *dpm,
					struct power_cap *cap)
{
	enum batt_soc_status sts;

	/* if the battery capacity is >= 80% of cap, 5V, 3A
	 * <80% of cpacity 12V, 3A.
	 */
	sts = dpm_get_batt_status(dpm);
	switch (sts) {
	case BATT_SOC_FULL:
	case BATT_SOC_GOOD:
		cap->mv = VIN_5V;
		break;
	case BATT_SOC_MID2:
	case BATT_SOC_MID1:
	case BATT_SOC_LOW:
	case BATT_SOC_DEAD:
		cap->mv = VIN_12V;
		break;
	case BATT_SOC_UNKNOWN:
	default:
		return -EINVAL;
	}
	cap->ma = ICHRG_3A;

	return 0;
}

static int dpm_get_sink_power_caps(struct devpolicy_mgr *dpm,
					struct power_caps *caps)
{

	if (spcaps == NULL)
		return -ENODATA;

	caps->pcap = spcaps;
	caps->n_cap = ARRAY_SIZE(spcaps);
	return 0;
}

static int dpm_set_charger_state(struct power_supply *psy, bool state)
{
	union power_supply_propval val;
	int ret;

	/* setting charger state enable/disable */
	val.intval = state;
	ret = psy->set_property(psy, POWER_SUPPLY_PROP_ENABLE_CHARGER, &val);
	if (ret < 0)
		return ret;

	power_supply_changed(psy);
	return 0;
}

static int dpm_set_charger_mode(struct devpolicy_mgr *dpm,
					enum charger_mode mode)
{
	int ret = 0;
	struct power_supply *psy;

	mutex_lock(&dpm->charger_lock);

	psy = dpm_get_psy(PSY_TYPE_CHARGER);
	if (!psy) {
		mutex_unlock(&dpm->charger_lock);
		return -EINVAL;
	}

	switch (mode) {
	case CHARGER_MODE_SET_HZ:
		ret = dpm_set_charger_state(psy, false);
		break;
	case CHARGER_MODE_ENABLE:
		ret = dpm_set_charger_state(psy, true);
		break;
	default:
		break;
	}

	mutex_unlock(&dpm->charger_lock);

	return ret;
}

static int dpm_update_current_lim(struct devpolicy_mgr *dpm,
					int ilim)
{
	int ret = 0;
	struct power_supply *psy;
	union power_supply_propval val;

	mutex_lock(&dpm->charger_lock);

	psy = dpm_get_psy(PSY_TYPE_CHARGER);
	if (!psy) {
		mutex_unlock(&dpm->charger_lock);
		return -EINVAL;
	}

	/* reading current inlimit value */
	ret = psy->get_property(psy, POWER_SUPPLY_PROP_INLMT, &val);
	if (ret < 0) {
		pr_err("DPM: Unable to get the current limit (%d)\n", ret);
		goto error;
	}

	if (val.intval != ilim) {
		val.intval = ilim;
		ret = psy->set_property(psy, POWER_SUPPLY_PROP_INLMT, &val);
		if (ret < 0) {
			pr_err("DPM: Unable to set the current limit (%d)\n",
					ret);
			goto error;
		}
		power_supply_changed(psy);
	}

error:
	mutex_unlock(&dpm->charger_lock);
	return ret;

}

static int dpm_get_sink_pr_swap_status(struct devpolicy_mgr *dpm)
{
	enum batt_soc_status sts;
	int ret;

	/* if the battery capacity is >= 50% returns 1,
	 * else 0 or error code
	 */
	sts = dpm_get_batt_status(dpm);
	switch (sts) {
	case BATT_SOC_FULL:
	case BATT_SOC_GOOD:
	case BATT_SOC_MID2:
		ret = 1;
		break;
	case BATT_SOC_MID1:
	case BATT_SOC_LOW:
	case BATT_SOC_DEAD:
		ret = 0;
		break;
	case BATT_SOC_UNKNOWN:
	default:
		return -EINVAL;
	}

	return ret;
}

static int dpm_is_pr_swapped(struct devpolicy_mgr *dpm,
					enum pwr_role prole)
{
	if (prole == POWER_ROLE_SINK)
		return dpm_get_sink_pr_swap_status(dpm);

	return 0;
}

static int dpm_get_min_current(struct devpolicy_mgr *dpm,
					int *ma)
{
	/* FIXME: this can be store it from and taken back when needed */
	*ma = ICHRG_P5A;

	return 0;
}

static enum cable_state dpm_get_cable_state(struct devpolicy_mgr *dpm,
					enum cable_type type)
{
	if (type == CABLE_TYPE_PROVIDER)
		return dpm->provider_state;
	else
		return dpm->consumer_state;
}

static void dpm_notify_policy_evt(struct devpolicy_mgr *dpm,
					enum devpolicy_mgr_events evt)
{
	if (dpm && dpm->pe && dpm->pe->ops && dpm->pe->ops->notify_dpm_evt)
		dpm->pe->ops->notify_dpm_evt(dpm->pe, evt);

}

static void dpm_set_pu_pd(struct devpolicy_mgr *dpm, bool pu_pd)
{
	if (dpm && dpm->phy && dpm->phy->set_pu_pd)
		dpm->phy->set_pu_pd(dpm->phy, pu_pd);
}

static void dpm_update_data_role(struct devpolicy_mgr *dpm,
				enum data_role drole)
{
	enum data_role cur_drole;
	char *cbl_type = NULL;
	bool cbl_state = false;

	mutex_lock(&dpm->role_lock);
	cur_drole = dpm->cur_drole;
	if (cur_drole == drole)
		goto drole_err;
	switch (drole) {
	case DATA_ROLE_SWAP:
		if (cur_drole == DATA_ROLE_DFP) {
			/* Role swap from DFP to UFP, Send DFP disconnect */
			cbl_type = "USB-Host";
			cbl_state = CABLE_DETACHED;
		} else if (cur_drole == DATA_ROLE_UFP) {
			/* Role swap from UFP to DFP, Send UFP disconnect */
			cbl_type = "USB";
			cbl_state = CABLE_DETACHED;
		} else {
			pr_warn("DPM:%s:DR_SWAP cann't be processed\n",
					__func__);
			goto drole_err;
		}
		break;
	case DATA_ROLE_UFP:
		/* Send UFP connect */
		cbl_type = "USB";
		cbl_state = CABLE_ATTACHED;
		break;

	case DATA_ROLE_DFP:
		/* Send DFP connect */
		cbl_type = "USB-Host";
		cbl_state = CABLE_ATTACHED;
		break;

	case DATA_ROLE_NONE:
		if (cur_drole == DATA_ROLE_SWAP)
			cur_drole = dpm->prev_drole;
		if (cur_drole == DATA_ROLE_DFP) {
			cbl_type = "USB-Host";
			cbl_state = CABLE_DETACHED;
		} else if (cur_drole == DATA_ROLE_UFP) {
			cbl_type = "USB";
			cbl_state = CABLE_DETACHED;
		} else
			goto drole_err;
		break;
	default:
		pr_debug("DPM:%s: unknown data role!!\n", __func__);
		goto drole_err;
	}
	dpm->prev_drole = dpm->cur_drole;
	dpm->cur_drole = drole;
	typec_notify_cable_state(dpm->phy, cbl_type, cbl_state);

drole_err:
	mutex_unlock(&dpm->role_lock);
}

static void dpm_update_power_role(struct devpolicy_mgr *dpm,
				enum pwr_role prole)
{
	enum pwr_role cur_prole;
	enum pwr_role prev_prole;
	char *cbl_type = NULL;
	bool cbl_state = false;
	bool set_pu_pd = false;
	bool pu_pd = false;

	mutex_lock(&dpm->role_lock);
	cur_prole = dpm->cur_prole;
	prev_prole = dpm->prev_prole;
	if (cur_prole == prole)
		goto update_prole_err;

	switch (prole) {
	case POWER_ROLE_SWAP:
		if (cur_prole == POWER_ROLE_SOURCE) {
			dpm->provider_state = CABLE_DETACHED;
			/* Role swap from SRC to SNK, Send SRC disconnect */
			cbl_type = "USB_TYPEC_SRC";
			cbl_state = CABLE_DETACHED;
			/* Pull-Down the CC line */
			set_pu_pd = true;
			pu_pd = false;
		} else if (cur_prole == POWER_ROLE_SINK) {
			dpm->consumer_state = CABLE_DETACHED;
			/* Role swap from SNK to SRC, Send SNK disconnect */
			cbl_type = "USB_TYPEC_SNK";
			cbl_state = CABLE_DETACHED;
			/* PR SWAP from SNK to SRC.
			 * Pull-Up the CC line
			 */
			set_pu_pd = true;
			pu_pd = true;
		} else {
			pr_warn("DPM:%s:PR_SWAP cann't be processed\n",
					__func__);
			goto update_prole_err;
		}
		break;
	case POWER_ROLE_SINK:
		if (cur_prole == POWER_ROLE_SWAP
			&& prev_prole == POWER_ROLE_SINK) {
			/* PR swap from SINK to SRC failed.
			 * Pull-Down the CC line.
			 */
			set_pu_pd = true;
			pu_pd = false;
		} else if (cur_prole == POWER_ROLE_SOURCE
				&& prev_prole == POWER_ROLE_SWAP) {
			/* During PR SWAP from SNK to SRC, after source
			 * is enabled, the other device failed to switch
			 * to sink and did send PS_RDY ontime. So switch
			 * back from src to snk.
			 */
			set_pu_pd = true;
			pu_pd = false;
			dpm->provider_state = CABLE_DETACHED;
			typec_notify_cable_state(dpm->phy, "USB_TYPEC_SRC",
						CABLE_DETACHED);
		}
		dpm->consumer_state = CABLE_ATTACHED;
		/* Send SNK connect */
		cbl_type = "USB_TYPEC_SNK";
		cbl_state = CABLE_ATTACHED;
		break;

	case POWER_ROLE_SOURCE:
		if (cur_prole == POWER_ROLE_SWAP
			&& prev_prole == POWER_ROLE_SOURCE) {
			/* PR SWAP from SRC to SNK failed and falling
			 * back to SRC, Pull-Up the CC line
			 */
			set_pu_pd = true;
			pu_pd = true;
		}
		dpm->provider_state = CABLE_ATTACHED;
		/* Send SRC connect */
		cbl_type = "USB_TYPEC_SRC";
		cbl_state = CABLE_ATTACHED;
		break;

	case POWER_ROLE_NONE:
		/* Cable disconnected */
		if (cur_prole == POWER_ROLE_SOURCE) {
			cbl_type = "USB_TYPEC_SRC";
			cbl_state = CABLE_DETACHED;
		} else if (cur_prole == POWER_ROLE_SINK) {
			cbl_type = "USB_TYPEC_SNK";
			cbl_state = CABLE_DETACHED;
		}
		break;
	default:
		pr_debug("DPM:%s: unknown pwr role!!\n", __func__);
		goto update_prole_err;
	}
	dpm->prev_prole = cur_prole;
	dpm->cur_prole = prole;
	typec_notify_cable_state(dpm->phy, cbl_type, cbl_state);
	if (set_pu_pd)
		dpm_set_pu_pd(dpm, pu_pd);
update_prole_err:
	mutex_unlock(&dpm->role_lock);

}

static int dpm_set_display_port_state(struct devpolicy_mgr *dpm,
					enum cable_state state,
					enum typec_dp_cable_type type)
{
	mutex_lock(&dpm->role_lock);
	dpm->phy->dp_type = type;
	if (dpm->dp_state != state) {
		dpm->dp_state = state;
		typec_notify_cable_state(dpm->phy,
			"USB_TYPEC_DP_SOURCE", state);
	}
	mutex_unlock(&dpm->role_lock);
	return 0;
}

static void dpm_cable_worker(struct work_struct *work)
{
	struct devpolicy_mgr *dpm =
		container_of(work, struct devpolicy_mgr, cable_event_work);
	struct cable_event *evt, *tmp;
	unsigned long flags;
	struct list_head new_list;

	if (list_empty(&dpm->cable_event_queue))
		return;

	INIT_LIST_HEAD(&new_list);
	spin_lock_irqsave(&dpm->cable_event_queue_lock, flags);
	list_replace_init(&dpm->cable_event_queue, &new_list);
	spin_unlock_irqrestore(&dpm->cable_event_queue_lock, flags);

	list_for_each_entry_safe(evt, tmp, &new_list, node) {
		pr_debug("DPM:%s: Cable type=%s - %s\n", __func__,
			((evt->cbl_type == CABLE_TYPE_CONSUMER) ? "Consumer" :
			((evt->cbl_type == CABLE_TYPE_PROVIDER) ? "Provider" :
			"Unknown")),
			evt->cbl_state ? "Connected" : "Disconnected");

		mutex_lock(&dpm->role_lock);
		if (evt->cbl_type == CABLE_TYPE_CONSUMER
			&& evt->cbl_state != dpm->consumer_state) {
			dpm->consumer_state = evt->cbl_state;
			if (evt->cbl_state == CABLE_ATTACHED) {
				dpm_notify_policy_evt(dpm,
					DEVMGR_EVENT_UFP_CONNECTED);
			} else if (evt->cbl_state == CABLE_DETACHED) {
				dpm_notify_policy_evt(dpm,
					DEVMGR_EVENT_UFP_DISCONNECTED);
			}

		} else if (evt->cbl_type == CABLE_TYPE_PROVIDER
			&& evt->cbl_state != dpm->provider_state) {
			dpm->provider_state = evt->cbl_state;
			if (evt->cbl_state == CABLE_ATTACHED) {
				dpm_notify_policy_evt(dpm,
					DEVMGR_EVENT_DFP_CONNECTED);
			} else if (evt->cbl_state == CABLE_DETACHED) {
				dpm_notify_policy_evt(dpm,
					DEVMGR_EVENT_DFP_DISCONNECTED);
			}
		} else
			pr_debug("DPM: consumer/provider state not changed\n");

		mutex_unlock(&dpm->role_lock);
		list_del(&evt->node);
		kfree(evt);
	}
}

static int dpm_consumer_cable_event(struct notifier_block *nblock,
						unsigned long event,
						void *param)
{
	struct devpolicy_mgr *dpm = container_of(nblock,
						struct devpolicy_mgr,
						consumer_nb);
	struct extcon_dev *edev = param;
	struct cable_event *evt;

	if (!edev)
		return NOTIFY_DONE;

	evt = kzalloc(sizeof(*evt), GFP_ATOMIC);
	if (!evt) {
		pr_err("DPM: failed to allocate memory for cable event\n");
		return NOTIFY_DONE;
	}

	evt->cbl_type = CABLE_TYPE_CONSUMER;
	evt->cbl_state = extcon_get_cable_state(edev, CABLE_CONSUMER);
	pr_debug("DPM: extcon notification evt Consumer - %s\n",
			evt->cbl_state ? "Connected" : "Disconnected");

	spin_lock(&dpm->cable_event_queue_lock);
	list_add_tail(&evt->node, &dpm->cable_event_queue);
	spin_unlock(&dpm->cable_event_queue_lock);

	queue_work(system_nrt_wq, &dpm->cable_event_work);
	return NOTIFY_OK;
}

static int dpm_provider_cable_event(struct notifier_block *nblock,
						unsigned long event,
						void *param)
{
	struct devpolicy_mgr *dpm = container_of(nblock,
						struct devpolicy_mgr,
						provider_nb);
	struct extcon_dev *edev = param;
	struct cable_event *evt;

	if (!edev)
		return NOTIFY_DONE;

	evt = kzalloc(sizeof(*evt), GFP_ATOMIC);
	if (!evt) {
		pr_err("DPM: failed to allocate memory for cable event\n");
		return NOTIFY_DONE;
	}

	evt->cbl_type = CABLE_TYPE_PROVIDER;
	evt->cbl_state = extcon_get_cable_state(edev, CABLE_PROVIDER);
	pr_debug("DPM: extcon notification evt Provider - %s\n",
			evt->cbl_state ? "Connected" : "Disconnected");

	spin_lock(&dpm->cable_event_queue_lock);
	list_add_tail(&evt->node, &dpm->cable_event_queue);
	spin_unlock(&dpm->cable_event_queue_lock);

	queue_work(system_nrt_wq, &dpm->cable_event_work);
	return NOTIFY_OK;
}

static void dpm_trigger_role_swap(struct devpolicy_mgr *dpm,
			enum role_type rtype)
{
	switch (rtype) {
	case ROLE_TYPE_DATA:
		pr_debug("DPM:%s: Triggering data role swap\n", __func__);
		dpm_notify_policy_evt(dpm,
			DEVMGR_EVENT_DR_SWAP);
		break;
	case ROLE_TYPE_POWER:
		pr_debug("DPM:%s: Triggering power role swap\n", __func__);
		dpm_notify_policy_evt(dpm,
			DEVMGR_EVENT_PR_SWAP);
		break;
	default:
		pr_warn("DPM:%s: Invalid role type\n", __func__);
	}
}

#define PD_DEV_ATTR(_name)					\
{								\
	.attr = { .name = #_name },				\
	.show = dpm_pd_sysfs_show_property,			\
	.store = dpm_pd_sysfs_store_property,			\
}

#define PD_SYSFS_ROLE_TEXT_NONE "none"
#define PD_SYSFS_ROLE_TEXT_SINK "sink"
#define PD_SYSFS_ROLE_TEXT_SRC "source"
#define PD_SYSFS_ROLE_TEXT_USB "device"
#define PD_SYSFS_ROLE_TEXT_HOST "host"
#define PD_SYSFS_ROLE_TEXT_MAX_LEN	8

static ssize_t dpm_pd_sysfs_show_property(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t dpm_pd_sysfs_store_property(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count);
static umode_t dpm_pd_sysfs_attr_is_visible(struct kobject *kobj,
		struct attribute *attr, int attrno);

enum pd_sysfs_entries {
	PD_DEV_SYSFS_NAME,
	PD_DEV_SYSFS_PROLE,
	PD_DEV_SYSFS_DROLE,
};

static char *pd_dev_sysfs_strs[] = {
	"dev_name",
	"power_role",
	"data_role",
};

/* Order and name should be same as pd_dev_sysfs_strs.*/
static struct device_attribute
pd_dev_attrs[ARRAY_SIZE(pd_dev_sysfs_strs)] = {
	PD_DEV_ATTR(dev_name),
	PD_DEV_ATTR(power_role),
	PD_DEV_ATTR(data_role),
};

static struct attribute *
__pd_attrs[ARRAY_SIZE(pd_dev_attrs) + 1];

static struct attribute_group pd_attr_group = {
	.attrs = __pd_attrs,
	.is_visible = dpm_pd_sysfs_attr_is_visible,
};

static const struct attribute_group *pd_attr_groups[] = {
	&pd_attr_group,
	NULL,
};

static enum pwr_role dpm_str_to_prole(const char *str, int cnt)
{
	enum pwr_role prole = POWER_ROLE_NONE;

	if (!strncmp(str, PD_SYSFS_ROLE_TEXT_SINK, cnt))
		prole = POWER_ROLE_SINK;
	else if (!strncmp(str, PD_SYSFS_ROLE_TEXT_SRC, cnt))
		prole = POWER_ROLE_SOURCE;

	return prole;
}

static enum data_role dpm_str_to_drole(const char *str, int cnt)
{
	enum data_role drole = DATA_ROLE_NONE;

	if (!strncmp(str, PD_SYSFS_ROLE_TEXT_USB, cnt))
		drole = DATA_ROLE_UFP;
	else if (!strncmp(str, PD_SYSFS_ROLE_TEXT_HOST, cnt))
		drole = DATA_ROLE_DFP;

	return drole;
}

static void dpm_prole_to_str(enum pwr_role prole, char *str)
{
	int max_len = sizeof(str);

	switch (prole) {
	case POWER_ROLE_SINK:
		strncpy(str, PD_SYSFS_ROLE_TEXT_SINK, max_len);
		break;
	case POWER_ROLE_SOURCE:
		strncpy(str, PD_SYSFS_ROLE_TEXT_SRC, max_len);
		break;
	default:
		strncpy(str, PD_SYSFS_ROLE_TEXT_NONE, max_len);
	}
}

static void dpm_drole_to_str(enum data_role drole, char *str)
{
	int max_len = sizeof(str);

	switch (drole) {
	case DATA_ROLE_UFP:
		strncpy(str, PD_SYSFS_ROLE_TEXT_USB, max_len);
		break;
	case DATA_ROLE_DFP:
		strncpy(str, PD_SYSFS_ROLE_TEXT_HOST, max_len);
		break;
	default:
		strncpy(str, PD_SYSFS_ROLE_TEXT_NONE, max_len);
	}
}

static ssize_t dpm_pd_sysfs_show_property(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	ssize_t cnt = 0;
	int role;
	struct devpolicy_mgr *dpm = dev_get_drvdata(dev);
	const ptrdiff_t off  = attr - pd_dev_attrs;
	char role_str[PD_SYSFS_ROLE_TEXT_MAX_LEN];

	if (!dpm)
		return 0;

	switch (off) {
	case PD_DEV_SYSFS_NAME:
		cnt = snprintf(buf, PD_SYSFS_ROLE_TEXT_MAX_LEN,
					"%s\n", dev_name(dev));
		break;
	case PD_DEV_SYSFS_PROLE:
		mutex_lock(&dpm->role_lock);
		if (dpm->cur_prole == POWER_ROLE_SWAP)
			role = dpm->prev_prole;
		else
			role = dpm->cur_prole;
		mutex_unlock(&dpm->role_lock);

		dpm_prole_to_str(role, role_str);
		cnt = snprintf(buf, PD_SYSFS_ROLE_TEXT_MAX_LEN,
					"%s\n", role_str);
		break;

	case PD_DEV_SYSFS_DROLE:
		mutex_lock(&dpm->role_lock);
		if (dpm->cur_drole == DATA_ROLE_SWAP)
			role = dpm->prev_drole;
		else
			role = dpm->cur_drole;
		mutex_unlock(&dpm->role_lock);

		dpm_drole_to_str(role, role_str);
		cnt = snprintf(buf, PD_SYSFS_ROLE_TEXT_MAX_LEN,
					"%s\n", role_str);
		break;

	default:
		dev_warn(dev, "%s: Invalid attribute\n", __func__);
	}
	return cnt;
}

static ssize_t dpm_pd_sysfs_store_property(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	enum pwr_role req_prole, cur_prole;
	enum data_role req_drole, cur_drole;
	struct devpolicy_mgr *dpm = dev_get_drvdata(dev);
	const ptrdiff_t off = attr - pd_dev_attrs;

	if (!dpm)
		return 0;

	switch (off) {
	case PD_DEV_SYSFS_NAME:
		/* set name not supported */
		break;
	case PD_DEV_SYSFS_PROLE:
		req_prole = dpm_str_to_prole(buf, count - 1);
		mutex_lock(&dpm->role_lock);
		cur_prole = dpm->cur_prole;
		mutex_unlock(&dpm->role_lock);
		dev_dbg(dev, "%s:power role to set=%d\n", __func__, req_prole);
		if (((cur_prole == POWER_ROLE_SINK)
			&& (req_prole == POWER_ROLE_SOURCE))
			|| ((cur_prole == POWER_ROLE_SOURCE)
			&& (req_prole == POWER_ROLE_SINK))) {
			/* Trigger power role swap. */
			dpm_trigger_role_swap(dpm, ROLE_TYPE_POWER);
		}
		break;
	case PD_DEV_SYSFS_DROLE:
		req_drole = dpm_str_to_drole(buf, count - 1);
		mutex_lock(&dpm->role_lock);
		cur_drole = dpm->cur_drole;
		mutex_unlock(&dpm->role_lock);
		dev_dbg(dev, "%s:data role to set=%d\n", __func__, req_drole);
		if (((cur_drole == DATA_ROLE_UFP)
			&& (req_drole == DATA_ROLE_DFP))
			|| ((cur_drole == DATA_ROLE_DFP)
			&& (req_drole == DATA_ROLE_UFP))) {
			/* Trigger power role swap. */
			dpm_trigger_role_swap(dpm, ROLE_TYPE_DATA);
		}
		break;
	default:
		dev_warn(dev, "%s: Invalid attribute\n", __func__);
		break;
	}

	return count;
}

static umode_t dpm_pd_sysfs_attr_is_visible(struct kobject *kobj,
					   struct attribute *attr,
					   int attrno)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	umode_t mode = S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR;

	switch (attrno) {
	case PD_DEV_SYSFS_NAME:
		mode = S_IRUSR | S_IRGRP | S_IROTH;
		break;
	case PD_DEV_SYSFS_PROLE:
	case PD_DEV_SYSFS_DROLE:
		mode = S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR;
		break;
	default:
		dev_err(dev, "%s: Invalid attrno=%d\n", __func__, attrno);
		break;
	}
	return mode;
}

void dpm_pd_sysfs_init_attrs(struct device_type *dev_type)
{
	int i;

	dev_type->groups = pd_attr_groups;

	for (i = 0; i < ARRAY_SIZE(pd_dev_attrs); i++)
		__pd_attrs[i] = &pd_dev_attrs[i].attr;
}

static struct device_type pd_dev_type;
static void dpm_pd_dev_release(struct device *dev)
{
	struct devpolicy_mgr *dpm = dev_get_drvdata(dev);

	kfree(dev);
	dpm->pd_dev = NULL;
}

static int dpm_register_pd_class_dev(struct devpolicy_mgr *dpm)
{
	struct device *dev;
	struct typec_phy *phy = dpm->phy;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	device_initialize(dev);
	dpm_pd_sysfs_init_attrs(&pd_dev_type);

	dev->class = power_delivery_class;
	dev->type = &pd_dev_type;
	dev->parent = phy->dev;
	dev->release = dpm_pd_dev_release;
	dev_set_drvdata(dev, dpm);
	dpm->pd_dev = dev;

	ret = dev_set_name(dev, "%s-%s", phy->label, "pd");
	if (ret) {
		pr_err("DPM:%s: Failed to set drv name, ret=%d\n",
				__func__, ret);
		goto dev_add_failed;
	}

	ret = device_add(dev);
	if (ret) {
		pr_err("DPM:%s: Failed to add pd class dev, ret=%d\n",
				__func__, ret);
		goto dev_add_failed;
	}

	return 0;

dev_add_failed:
	put_device(dev);
	return ret;
}

static void dpm_unregister_pd_class_dev(struct devpolicy_mgr *dpm)
{
	struct device *dev;

	dev = dpm->pd_dev;

	if (WARN(!dev, "DPM: Null device\n"))
		return;

	device_del(dev);
	put_device(dev);
}

static struct dpm_interface interface = {
	.get_max_srcpwr_cap = dpm_get_max_srcpwr_cap,
	.get_max_snkpwr_cap = dpm_get_max_snkpwr_cap,
	.get_source_power_cap = dpm_get_source_power_cap,
	.get_sink_power_cap = dpm_get_sink_power_cap,
	.get_sink_power_caps = dpm_get_sink_power_caps,
	.get_cable_state = dpm_get_cable_state,
	.set_charger_mode = dpm_set_charger_mode,
	.update_current_lim = dpm_update_current_lim,
	.get_min_current = dpm_get_min_current,
	.update_data_role = dpm_update_data_role,
	.update_power_role = dpm_update_power_role,
	.is_pr_swapped = dpm_is_pr_swapped,
	.set_display_port_state = dpm_set_display_port_state,
};

struct devpolicy_mgr *dpm_register_syspolicy(struct typec_phy *phy,
				struct pd_policy *policy)
{
	int ret;
	struct devpolicy_mgr *dpm;

	if (!phy) {
		pr_err("DPM: No typec phy!\n");
		return ERR_PTR(-ENODEV);
	}

	dpm = kzalloc(sizeof(struct devpolicy_mgr), GFP_KERNEL);
	if (!dpm) {
		pr_err("DPM: mem alloc failed\n");
		return ERR_PTR(-ENOMEM);
	}

	dpm->phy = phy;
	dpm->policy = policy;
	dpm->interface = &interface;
	INIT_LIST_HEAD(&dpm->cable_event_queue);
	INIT_WORK(&dpm->cable_event_work, dpm_cable_worker);
	spin_lock_init(&dpm->cable_event_queue_lock);
	mutex_init(&dpm->role_lock);
	mutex_init(&dpm->charger_lock);

	/* register for extcon notifier */
	dpm->consumer_nb.notifier_call = dpm_consumer_cable_event;
	dpm->provider_nb.notifier_call = dpm_provider_cable_event;
	ret = extcon_register_interest(&dpm->consumer_cable_nb,
						NULL,
						CABLE_CONSUMER,
						&dpm->consumer_nb);
	if (ret < 0) {
		pr_err("DPM: failed to register notifier for Consumer (%d)\n",
						ret);
		goto error0;
	}

	ret = extcon_register_interest(&dpm->provider_cable_nb,
						NULL,
						CABLE_PROVIDER,
						&dpm->provider_nb);
	if (ret < 0) {
		pr_err("DPM: failed to register notifier for Provider\n");
		goto error1;
	}

	ret = protocol_bind_dpm(dpm->phy);
	if (ret < 0) {
		pr_err("DPM: failed in binding protocol\n");
		goto error2;
	}

	ret = policy_engine_bind_dpm(dpm);
	if (ret < 0) {
		pr_err("DPM: failed in binding policy engine\n");
		goto error3;
	}

	ret = dpm_register_pd_class_dev(dpm);
	if (ret) {
		pr_err("DPM: Unable to register pd class dev\n");
		goto pd_dev_reg_fail;
	}

	return dpm;

pd_dev_reg_fail:
	policy_engine_unbind_dpm(dpm);
error3:
	protocol_unbind_dpm(dpm->phy);
error2:
	extcon_unregister_interest(&dpm->provider_cable_nb);
error1:
	extcon_unregister_interest(&dpm->consumer_cable_nb);
error0:
	kfree(dpm);
	return NULL;
}
EXPORT_SYMBOL(dpm_register_syspolicy);

void dpm_unregister_syspolicy(struct devpolicy_mgr *dpm)
{
	if (dpm) {
		dpm_unregister_pd_class_dev(dpm);
		policy_engine_unbind_dpm(dpm);
		protocol_unbind_dpm(dpm->phy);
		extcon_unregister_interest(&dpm->provider_cable_nb);
		extcon_unregister_interest(&dpm->consumer_cable_nb);
		kfree(dpm);
	}
}
EXPORT_SYMBOL(dpm_unregister_syspolicy);

MODULE_AUTHOR("Albin B <albin.bala.krishnan@intel.com>");
MODULE_DESCRIPTION("PD Device Policy Manager");
MODULE_LICENSE("GPL v2");
