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

static struct dpm_interface interface = {
	.get_max_srcpwr_cap = dpm_get_max_srcpwr_cap,
	.get_max_snkpwr_cap = dpm_get_max_snkpwr_cap,
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

	return dpm;

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
