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
#include "pd_policy.h"
#include "protocol.h"

static struct power_cap default_snk_pwr_caps[] = {
	{
		.mv = VIN_5V,
		.ma = ICHRG_3A,
		.psy_type = DPM_PSY_TYPE_FIXED,
	},
	{
		.mv = VIN_12V,
		.ma = ICHRG_3A,
		.psy_type = DPM_PSY_TYPE_FIXED,
	},
};

static struct power_cap default_src_pwr_caps[] = {
	{
		.mv = VIN_5V,
		.ma = IBUS_0P9A,
		.psy_type = DPM_PSY_TYPE_FIXED,
	},
};

ATOMIC_NOTIFIER_HEAD(devpolicy_mgr_notifier);
EXPORT_SYMBOL_GPL(devpolicy_mgr_notifier);

struct dpm_cable_state {
	struct list_head node;
	char *cbl_type;
	bool cbl_state;
};


static int dpm_handle_psy_notification(struct notifier_block *nb,
				   unsigned long event, void *data)
{
	struct devpolicy_mgr *dpm = container_of(nb, struct devpolicy_mgr,
					psy_nb);
	struct power_supply *psy;

	if (data == NULL)
		return NOTIFY_DONE;
	pr_debug("DPM: PSY Event=%lu\n", event);
	psy = data;
	if (IS_BATTERY(psy) &&
		event == PSY_EVENT_PROP_CHANGED)
		schedule_work(&dpm->psy_work);

	return NOTIFY_OK;
}


static struct power_supply *dpm_get_psy(struct devpolicy_mgr *dpm,
							enum psy_type type)
{
	struct class_dev_iter iter;
	struct device *dev;
	struct power_supply *psy;
	bool found = false;

	if (type == PSY_TYPE_CHARGER && dpm->charger_psy)
		return dpm->charger_psy;

	if (type == PSY_TYPE_BATTERY && dpm->battery_psy)
		return dpm->battery_psy;

	class_dev_iter_init(&iter, power_supply_class, NULL, NULL);
	while ((dev = class_dev_iter_next(&iter))) {
		psy = (struct power_supply *)dev_get_drvdata(dev);
		if (type == PSY_TYPE_BATTERY && IS_BATTERY(psy)) {
			dpm->battery_psy = psy;
			found = true;
			break;
		}
		if (type == PSY_TYPE_CHARGER && IS_CHARGER(psy)) {
			dpm->charger_psy = psy;
			found = true;
			break;
		}
	}
	class_dev_iter_exit(&iter);

	if (found)
		return psy;

	return NULL;
}

static void dpm_psy_worker(struct work_struct *work)
{
	struct devpolicy_mgr *dpm = container_of(work, struct devpolicy_mgr,
							psy_work);
	struct power_supply *psy;
	union power_supply_propval val;
	int ret;

	psy = dpm_get_psy(dpm, PSY_TYPE_BATTERY);
	if (!psy)
		return;
	ret = psy->get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &val);
	if (ret) {
		pr_err("DPM: Failed to read battery soc\n");
		return;
	}
	dpm->battery_capacity = val.intval;
	pr_debug("DPM: battery_capacity=%d\n", dpm->battery_capacity);
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

	soc = dpm->battery_capacity;
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

	if (default_snk_pwr_caps == NULL)
		return -ENODATA;

	caps->pcap = default_snk_pwr_caps;
	caps->n_cap = ARRAY_SIZE(default_snk_pwr_caps);
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

	return 0;
}

static int dpm_set_charger_mode(struct devpolicy_mgr *dpm,
					enum charger_mode mode)
{
	int ret = 0;
	struct power_supply *psy;

	mutex_lock(&dpm->charger_lock);

	psy = dpm_get_psy(dpm, PSY_TYPE_CHARGER);
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

static int dpm_update_charger(struct devpolicy_mgr *dpm,
					int ilim, int query)
{
	struct power_supply_cable_props cable_props = {0};
	int evt;
	int ma;

	evt = (ilim != 0) ? POWER_SUPPLY_CHARGER_EVENT_CONNECT :
				POWER_SUPPLY_CHARGER_EVENT_DISCONNECT;


	if (query) {
		ma = typec_get_host_current(dpm->phy);

		if (ma < 0 || ma == TYPEC_CURRENT_USB)
			/* setting 900mA source current in case of USB, as
			 * typec connector is capable of supporting USB3.0 */
			ma = IBUS_0P9A;
	} else
		ma = ilim;

	cable_props.ma = ma;
	cable_props.chrg_evt = evt;
	cable_props.chrg_type =
			POWER_SUPPLY_CHARGER_TYPE_USB_TYPEC;

	pr_debug("DPM: calling psy with evt %d cur %d\n", evt, ma);

	atomic_notifier_call_chain(&power_supply_notifier,
						PSY_CABLE_EVENT,
						&cable_props);
	return 0;
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

static inline int dpm_is_vconn_swapped(struct devpolicy_mgr *dpm)
{
	/* vconn swap can be supported regardless of data/power role */
	return true;
}

static int dpm_is_pr_swapped(struct devpolicy_mgr *dpm,
					enum pwr_role prole)
{
	int ret = 0;

	if (prole == POWER_ROLE_SINK)
		ret = dpm_get_sink_pr_swap_status(dpm);
	else if (prole == POWER_ROLE_SOURCE)
		ret = true;
	return ret;
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
	if (dpm && dpm->p)
		pe_notify_dpm_evt(dpm->p, evt);

}

static void dpm_notify_cable_state_worker(struct work_struct *work)
{
	struct devpolicy_mgr *dpm =
		container_of(work, struct devpolicy_mgr, cable_notify_work);

	struct dpm_cable_state *cbl, *tmp;
	struct list_head new_list;

	mutex_lock(&dpm->cable_notify_lock);
	if (list_empty(&dpm->cable_notify_list)) {
		mutex_unlock(&dpm->cable_notify_lock);
		return;
	}

	list_replace_init(&dpm->cable_notify_list, &new_list);
	mutex_unlock(&dpm->cable_notify_lock);

	list_for_each_entry_safe(cbl, tmp, &new_list, node) {

		typec_notify_cable_state(dpm->phy,
					cbl->cbl_type, cbl->cbl_state);
		kfree(cbl);
	}
}

static int dpm_notify_cable_state(struct devpolicy_mgr *dpm,
					char *cbl_type, bool cbl_state)
{
	struct dpm_cable_state *cbl;

	cbl = kzalloc(sizeof(struct dpm_cable_state), GFP_KERNEL);
	if (!cbl) {
		pr_err("DPM:%s: Failed to allocate memory for cbl\n",
					__func__);
		return -ENOMEM;
	}

	cbl->cbl_type = cbl_type;
	cbl->cbl_state = cbl_state;

	mutex_lock(&dpm->cable_notify_lock);
	list_add_tail(&cbl->node, &dpm->cable_notify_list);
	mutex_unlock(&dpm->cable_notify_lock);

	schedule_work(&dpm->cable_notify_work);
	return 0;
}

/*
 * dpm_update_vconn_state will get called to update the dpm's vconn state,
 * to expose the current vconn state to the outside world.
 */
static void dpm_update_vconn_state(struct devpolicy_mgr *dpm,
				enum vconn_state vcstate)
{
	mutex_lock(&dpm->role_lock);
	if (dpm->cur_vcstate == vcstate) {
		pr_warn("DPM: vconn is already in %d state\n", vcstate);
		mutex_unlock(&dpm->role_lock);
		return;
	}

	dpm->cur_vcstate = vcstate;
	pr_debug("DPM: vconn state updated to %d\n", vcstate);
	mutex_unlock(&dpm->role_lock);
}

static int dpm_set_vconn_state(struct devpolicy_mgr *dpm,
					enum vconn_state vcstate)
{
	int ret = -EINVAL;

	if (dpm && dpm->phy && IS_VCSTATE_VALID(vcstate)) {
		if (vcstate == VCONN_NONE || vcstate == VCONN_SINK)
			ret = typec_enable_vconn(dpm->phy, false);
		else if (vcstate == VCONN_SOURCE)
			ret = typec_enable_vconn(dpm->phy, true);

		if (ret < 0)
			pr_err("DPM: Unable to enable/disable vconn %d\n", ret);
		else
			dpm_update_vconn_state(dpm, vcstate);
	} else {
		pr_warn("DPM: Invalid input to enable/disable vconn state %d\n",
				vcstate);
	}

	return ret;
}

static bool dpm_get_vconn_state(struct devpolicy_mgr *dpm)
{
	if (dpm && dpm->phy)
		return typec_is_vconn_enabled(dpm->phy);

	return false;
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
	case DATA_ROLE_UFP:
		if (cur_drole == DATA_ROLE_DFP) {
			/* Role swap from DFP to UFP, Send DFP disconnect */
			dpm_notify_cable_state(dpm, "USB-Host",
							CABLE_DETACHED);
		}
		/* Send UFP connect */
		cbl_type = "USB";
		cbl_state = CABLE_ATTACHED;
		break;

	case DATA_ROLE_DFP:
		if (cur_drole == DATA_ROLE_UFP) {
			/* Role swap from UFP to DFP, Send UFP disconnect */
			dpm_notify_cable_state(dpm, "USB",
						CABLE_DETACHED);
		}
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
		} else {
			pr_err("DMP:%s: Unexpected cur_drole=%d\n", __func__,
						cur_drole);
			goto drole_err;
		}
		break;
	default:
		pr_debug("DPM:%s: unknown data role!!\n", __func__);
		goto drole_err;
	}
	dpm->prev_drole = dpm->cur_drole;
	dpm->cur_drole = drole;
	dpm_notify_cable_state(dpm, cbl_type, cbl_state);

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
		} else if (cur_prole == POWER_ROLE_SINK) {
			dpm->consumer_state = CABLE_DETACHED;
			/* Role swap from SNK to SRC, Send SNK disconnect */
			cbl_type = "USB_TYPEC_SNK";
			cbl_state = CABLE_DETACHED;
		} else {
			pr_warn("DPM:%s:PR_SWAP cann't be processed\n",
					__func__);
			goto update_prole_err;
		}
		typec_set_swap_state(dpm->phy, true);
		break;
	case POWER_ROLE_SINK:
		dpm->consumer_state = CABLE_ATTACHED;
		/* Send SNK connect */
		cbl_type = "USB_TYPEC_SNK";
		cbl_state = CABLE_ATTACHED;
		break;

	case POWER_ROLE_SOURCE:
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
	if (dpm->prev_prole == POWER_ROLE_SWAP)
		typec_set_swap_state(dpm->phy, false);
	dpm->prev_prole = cur_prole;
	dpm->cur_prole = prole;

	if (cbl_type != NULL)
		dpm_notify_cable_state(dpm, cbl_type, cbl_state);

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
		dpm_notify_cable_state(dpm,
			"USB_TYPEC_DP_SOURCE", state);
	}
	mutex_unlock(&dpm->role_lock);
	return 0;
}

void dpm_handle_phy_event(struct typec_phy *phy,
				enum typec_phy_dpm_evts evt)
{
	enum devpolicy_mgr_events dpm_evt = DEVMGR_EVENT_NONE;

	if (!phy || !phy->proto || !phy->proto->p || !phy->proto->p->dpm)
		return;

	switch (evt) {
	case PHY_DPM_EVENT_VBUS_ON:
		dpm_evt = DEVMGR_EVENT_VBUS_ON;
		break;
	case PHY_DPM_EVENT_VBUS_OFF:
		dpm_evt = DEVMGR_EVENT_VBUS_OFF;
		break;
	default:
		pr_info("DPM:%s: Unknown phy event=%d", __func__, evt);
	}

	if (dpm_evt != DEVMGR_EVENT_NONE)
		dpm_notify_policy_evt(phy->proto->p->dpm, dpm_evt);

}
EXPORT_SYMBOL(dpm_handle_phy_event);

static void dpm_handle_ext_cable_event(struct devpolicy_mgr *dpm,
					struct cable_event *evt)
{
	enum devpolicy_mgr_events dpm_evt = DEVMGR_EVENT_NONE;
	enum pwr_role prole;
	enum data_role drole;
	enum vconn_state vcstate;

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
			dpm_evt = DEVMGR_EVENT_UFP_CONNECTED;
			drole = DATA_ROLE_UFP;
			prole = POWER_ROLE_SINK;
			vcstate = VCONN_SINK;
		} else if (evt->cbl_state == CABLE_DETACHED) {
			dpm_evt = DEVMGR_EVENT_UFP_DISCONNECTED;
			drole = DATA_ROLE_NONE;
			prole = POWER_ROLE_NONE;
			vcstate = VCONN_NONE;
		} else
			pr_warn("DPM: %s: Unknown consumer state=%d\n",
					__func__, evt->cbl_state);

	} else if (evt->cbl_type == CABLE_TYPE_PROVIDER
			&& evt->cbl_state != dpm->provider_state) {
			dpm->provider_state = evt->cbl_state;
		if (evt->cbl_state == CABLE_ATTACHED) {
			dpm_evt = DEVMGR_EVENT_DFP_CONNECTED;
			drole = DATA_ROLE_DFP;
			prole = POWER_ROLE_SOURCE;
			vcstate = VCONN_SOURCE;
		} else if (evt->cbl_state == CABLE_DETACHED) {
			dpm_evt = DEVMGR_EVENT_DFP_DISCONNECTED;
			drole = DATA_ROLE_NONE;
			prole = POWER_ROLE_NONE;
			vcstate = VCONN_NONE;
		} else
			pr_warn("DPM: %s: Unknown consumer state=%d\n",
				__func__, evt->cbl_state);
	} else
		pr_debug("DPM: consumer/provider state not changed\n");


	/* Notify policy engine on valid event*/
	if (dpm_evt != DEVMGR_EVENT_NONE) {
		dpm->prev_drole = dpm->cur_drole;
		dpm->cur_drole = drole;

		dpm->prev_prole = dpm->cur_prole;
		dpm->cur_prole = prole;

		dpm->cur_vcstate = vcstate;
		mutex_unlock(&dpm->role_lock);

		dpm_notify_policy_evt(dpm, dpm_evt);
	} else
		mutex_unlock(&dpm->role_lock);
}

static void dpm_cable_worker(struct work_struct *work)
{
	struct devpolicy_mgr *dpm =
		container_of(work, struct devpolicy_mgr, cable_event_work);
	struct cable_event *evt;
	unsigned long flags;

	spin_lock_irqsave(&dpm->cable_event_queue_lock, flags);
	while (!list_empty(&dpm->cable_event_queue)) {
		evt = list_first_entry(&dpm->cable_event_queue,
				struct cable_event, node);
		list_del(&evt->node);
		spin_unlock_irqrestore(&dpm->cable_event_queue_lock, flags);
		/* Handle the event */
		pr_debug("DPM: %s: Processing event\n", __func__);
		dpm_handle_ext_cable_event(dpm, evt);
		kfree(evt);

		spin_lock_irqsave(&dpm->cable_event_queue_lock, flags);
	}
	spin_unlock_irqrestore(&dpm->cable_event_queue_lock, flags);

}

static int dpm_consumer_cable_event(struct notifier_block *nblock,
						unsigned long event,
						void *param)
{
	struct devpolicy_mgr *dpm = container_of(nblock,
						struct devpolicy_mgr,
						consumer_nb);
	struct extcon_dev *edev = param;
	struct cable_event *evt, *tmp;
	struct list_head new_list;

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

	/* If event disconnect flush the previous
	 * events as cable is disconnected */
	if (evt->cbl_state == CABLE_DETACHED)
		list_replace_init(&dpm->cable_event_queue, &new_list);
	else
		INIT_LIST_HEAD(&new_list);

	list_add_tail(&evt->node, &dpm->cable_event_queue);
	spin_unlock(&dpm->cable_event_queue_lock);

	/* Free all the previous events*/
	if (!list_empty(&new_list)) {
		list_for_each_entry_safe(evt, tmp, &new_list, node) {
			/* Free the event*/
			kfree(evt);
		}
	}

	schedule_work(&dpm->cable_event_work);
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
	struct cable_event *evt, *tmp;
	struct list_head new_list;

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

	/* If event disconnect flush the previous
	 * events as cable is disconnected */
	if (evt->cbl_state == CABLE_DETACHED)
		list_replace_init(&dpm->cable_event_queue, &new_list);
	else
		INIT_LIST_HEAD(&new_list);

	list_add_tail(&evt->node, &dpm->cable_event_queue);
	spin_unlock(&dpm->cable_event_queue_lock);

	/* Free all the previous events*/
	if (!list_empty(&new_list)) {
		list_for_each_entry_safe(evt, tmp, &new_list, node) {
			/* Free the event*/
			kfree(evt);
		}
	}

	schedule_work(&dpm->cable_event_work);
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
	case ROLE_TYPE_VCONN:
		pr_debug("DPM: %s Triggering vconn swap\n", __func__);
		dpm_notify_policy_evt(dpm, DEVMGR_EVENT_VCONN_SWAP);
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
	PD_DEV_SYSFS_VCONN,
};

static char *pd_dev_sysfs_strs[] = {
	"dev_name",
	"power_role",
	"data_role",
	"vconn",
};

/* Order and name should be same as pd_dev_sysfs_strs.*/
static struct device_attribute
pd_dev_attrs[ARRAY_SIZE(pd_dev_sysfs_strs)] = {
	PD_DEV_ATTR(dev_name),
	PD_DEV_ATTR(power_role),
	PD_DEV_ATTR(data_role),
	PD_DEV_ATTR(vconn),
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

static enum vconn_state dpm_str_to_vcstate(const char *str, int cnt)
{
	enum vconn_state vcstate = VCONN_NONE;

	if (!strncmp(str, PD_SYSFS_ROLE_TEXT_SINK, cnt))
		vcstate = VCONN_SINK;
	else if (!strncmp(str, PD_SYSFS_ROLE_TEXT_SRC, cnt))
		vcstate = VCONN_SOURCE;

	return vcstate;
}

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

static void dpm_vcstate_to_str(enum vconn_state vcstate, char *str)
{
	int max_len = sizeof(str);

	switch (vcstate) {
	case VCONN_SINK:
		strncpy(str, PD_SYSFS_ROLE_TEXT_SINK, max_len);
		break;
	case VCONN_SOURCE:
		strncpy(str, PD_SYSFS_ROLE_TEXT_SRC, max_len);
		break;
	default:
		strncpy(str, PD_SYSFS_ROLE_TEXT_NONE, max_len);
	}
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
	case PD_DEV_SYSFS_VCONN:
		mutex_lock(&dpm->role_lock);
		role = dpm->cur_vcstate;
		mutex_unlock(&dpm->role_lock);

		dpm_vcstate_to_str(role, role_str);
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
	enum vconn_state req_vcstate, cur_vcstate;
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
	case PD_DEV_SYSFS_VCONN:
		req_vcstate = dpm_str_to_vcstate(buf, count - 1);
		mutex_lock(&dpm->role_lock);
		cur_vcstate = dpm->cur_vcstate;
		mutex_unlock(&dpm->role_lock);
		dev_dbg(dev, "vconn state to set %d\n", req_vcstate);
		if (cur_vcstate != req_vcstate &&
			req_vcstate != VCONN_NONE &&
			(cur_vcstate == VCONN_SOURCE ||
			 cur_vcstate == VCONN_SINK)) {
			/* Trigger vconn swap. */
			dpm_trigger_role_swap(dpm, ROLE_TYPE_VCONN);
		} else {
			dev_warn(dev,
				"%s Can't request VCS in state %d for state %d",
				__func__, cur_vcstate, req_vcstate);
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
	case PD_DEV_SYSFS_VCONN:
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

static void dpm_clear_notify_list(struct devpolicy_mgr *dpm)
{
	struct dpm_cable_state *cbl, *tmp;

	mutex_lock(&dpm->cable_notify_lock);
	if (list_empty(&dpm->cable_notify_list)) {
		mutex_unlock(&dpm->cable_notify_lock);
		return;
	}

	/* As this clearing list is on exit, temp list not required */
	list_for_each_entry_safe(cbl, tmp, &dpm->cable_notify_list, node) {
		/*Free the event*/
		kfree(cbl);
	}
	INIT_LIST_HEAD(&dpm->cable_notify_list);
	mutex_unlock(&dpm->cable_notify_lock);
}

static void dpm_load_default_pd_config(struct devpolicy_mgr *dpm)
{
	struct pd_platfrom_config *conf = &dpm->plat_conf;

	conf->num_snk_pwr_caps = ARRAY_SIZE(default_snk_pwr_caps);
	if (conf->num_snk_pwr_caps > MAX_SNK_PWR_CAPS)
		conf->num_snk_pwr_caps = MAX_SNK_PWR_CAPS;
	memcpy(conf->src_pwr_caps, default_snk_pwr_caps,
		(sizeof(struct power_cap)) * conf->num_snk_pwr_caps);

	conf->num_src_pwr_caps = ARRAY_SIZE(default_src_pwr_caps);
	if (conf->num_src_pwr_caps > MAX_SRC_PWR_CAPS)
		conf->num_src_pwr_caps = MAX_SRC_PWR_CAPS;
	memcpy(conf->snk_pwr_caps, default_src_pwr_caps,
		(sizeof(struct power_cap)) * conf->num_src_pwr_caps);

	conf->usb_dev_supp = 1;
	conf->usb_host_supp = 1;
	conf->dfp_modal_op_supp = 1;
	conf->ufp_modal_op_supp = 0;
	conf->vconn_req = 0;
	conf->vbus_req = 1;
	conf->usb_suspend_supp = 1;
	conf->dual_data_role = 1;
	conf->dual_pwr_role = 1;
	conf->ext_pwrd = 0;
	conf->psy_type = DPM_PSY_TYPE_FIXED;
	conf->product_type = PRODUCT_TYPE_PERIPHERAL;
	conf->vendor_id = 0x8086; /* Intel VID */
	conf->usb_product_id = 0x1234;
	conf->usb_bcd_device_id = 0x0010;
	conf->test_id = 0x0;
	conf->hw_ver = 0;
	conf->fw_ver = 0;
	conf->vconn_pwr_req = 0;
	conf->usb_ss_signaling = USB_SS_USB_3P1_GEN1;
}

static void dpm_init_pd_platform_config(struct devpolicy_mgr *dpm)
{
	/* TODO: Get platform configuration from acpi */
	dpm_load_default_pd_config(dpm);
}

static struct dpm_interface interface = {
	.get_max_srcpwr_cap = dpm_get_max_srcpwr_cap,
	.get_max_snkpwr_cap = dpm_get_max_snkpwr_cap,
	.get_source_power_cap = dpm_get_source_power_cap,
	.get_sink_power_cap = dpm_get_sink_power_cap,
	.get_sink_power_caps = dpm_get_sink_power_caps,
	.get_cable_state = dpm_get_cable_state,
	.get_vconn_state = dpm_get_vconn_state,
	.set_vconn_state = dpm_set_vconn_state,
	.set_charger_mode = dpm_set_charger_mode,
	.update_charger = dpm_update_charger,
	.get_min_current = dpm_get_min_current,
	.update_data_role = dpm_update_data_role,
	.update_power_role = dpm_update_power_role,
	.is_pr_swapped = dpm_is_pr_swapped,
	.is_vconn_swapped = dpm_is_vconn_swapped,
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

	INIT_WORK(&dpm->cable_notify_work, dpm_notify_cable_state_worker);
	mutex_init(&dpm->cable_notify_lock);
	INIT_LIST_HEAD(&dpm->cable_notify_list);
	dpm_init_pd_platform_config(dpm);

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

	dpm->psy_nb.notifier_call = dpm_handle_psy_notification;
	ret = power_supply_reg_notifier(&dpm->psy_nb);
	if (ret) {
		pr_err("DPM: Unable to register psy\n");
		goto pd_dev_reg_fail;
	}
	INIT_WORK(&dpm->psy_work, dpm_psy_worker);
	schedule_work(&dpm->psy_work);

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
		dpm_clear_notify_list(dpm);
		dpm_unregister_pd_class_dev(dpm);
		policy_engine_unbind_dpm(dpm);
		protocol_unbind_dpm(dpm->phy);
		extcon_unregister_interest(&dpm->provider_cable_nb);
		extcon_unregister_interest(&dpm->consumer_cable_nb);
		power_supply_unreg_notifier(&dpm->psy_nb);
		kfree(dpm);
	}
}
EXPORT_SYMBOL(dpm_unregister_syspolicy);

MODULE_AUTHOR("Albin B <albin.bala.krishnan@intel.com>");
MODULE_DESCRIPTION("PD Device Policy Manager");
MODULE_LICENSE("GPL v2");
