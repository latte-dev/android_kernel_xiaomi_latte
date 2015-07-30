/*
 * usb_typec_detect.c: usb type-c cable detecton driver
 *
 * Copyright (C) 2014 Intel Corporation
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
 * Author: Kannappan, R <r.kannappan@intel.com>
 * Author: Albin B <albin.bala.krishnan@intel.com>
 */

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/wait.h>
#include <linux/usb/phy.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/usb_typec_phy.h>
#include "usb_typec_detect.h"

#define CC_OPEN(x)		(x == USB_TYPEC_CC_VRD_UNKNOWN)
#define CC_RD(x)		(x > USB_TYPEC_CC_VRA)
#define CC_RA(x)		(x == USB_TYPEC_CC_VRA)

/* Typec spec1.1 CC debounce time is 250ms */
#define TYPEC_DRPLOCK_TIMEOUT 250
#define MAX_DRP_TOGGLING 10

#define TYPEC_CABLE_USB		"USB"
#define TYPEC_CABLE_USB_HOST	"USB-Host"
#define TYPEC_CABLE_USB_SNK	"USB_TYPEC_SNK"
#define TYPEC_CABLE_USB_SRC	"USB_TYPEC_SRC"
#define TYPEC_CABLE_USB_DP_SRC	"USB_TYPEC_DP_SOURCE"

enum typec_cable_type {
	E_TYPEC_CABLE_UNKNOWN,
	E_TYPEC_CABLE_USB,
	E_TYPEC_CABLE_USB_HOST,
	E_TYPEC_CABLE_USB_SNK,
	E_TYPEC_CABLE_USB_SRC,
	E_TYPEC_CABLE_DP_SRC,
};

static int detect_check_valid_ufp(struct typec_detect *detect,
		struct typec_cc_psy *cc1_psy,
		struct typec_cc_psy *cc2_psy);
static void detect_update_ufp_state(struct typec_detect *detect,
		struct typec_cc_psy *cc1_psy,
		struct typec_cc_psy *cc2_psy);


static const char *pd_extcon_cable[] = {
	TYPEC_CABLE_USB,
	TYPEC_CABLE_USB_HOST,
	TYPEC_CABLE_USB_SNK,
	TYPEC_CABLE_USB_SRC,
	TYPEC_CABLE_USB_DP_SRC,
	NULL,
};

static LIST_HEAD(typec_detect_list);
static DEFINE_SPINLOCK(slock);

static struct typec_detect *get_typec_detect(struct typec_phy *phy)
{
	struct typec_detect *detect;

	spin_lock(&slock);
	list_for_each_entry(detect, &typec_detect_list, list) {
		if (!strncmp(detect->phy->label, phy->label, MAX_LABEL_SIZE)) {
			spin_unlock(&slock);
			return detect;
		}
	}
	spin_unlock(&slock);

	return NULL;
}
static int get_chrgcur_from_rd(struct typec_detect *detect,
				enum  typec_cc_level use_rd)
{
	int ma;

	switch (use_rd) {
	case USB_TYPEC_CC_VRD_USB:
		/* On UFP connect, if the pull-up is USB, then
		 * set inlinit as zero here. Once enumeration is
		 * completed by usb, otg driver will set the inlimit.
		 */
		if (detect->state == DETECT_STATE_ATTACHED_UFP)
			ma = TYPEC_CURRENT_UNKNOWN;
		else
			ma = TYPEC_CURRENT_USB;
		break;
	case USB_TYPEC_CC_VRD_1500:
		ma = TYPEC_CURRENT_1500;
		break;
	case USB_TYPEC_CC_VRD_3000:
		ma = TYPEC_CURRENT_3000;
		break;
	default:
		ma = TYPEC_CURRENT_UNKNOWN;
	}

	return ma;
}

static enum typec_cable_type typec_detect_cable_name_to_type(char *name)
{
	enum typec_cable_type type;

	if (!strcmp(name, TYPEC_CABLE_USB_SNK))
		type = E_TYPEC_CABLE_USB_SNK;
	else if (!strcmp(name, TYPEC_CABLE_USB_SRC))
		type = E_TYPEC_CABLE_USB_SRC;
	else if (!strcmp(name, TYPEC_CABLE_USB))
		type = E_TYPEC_CABLE_USB;
	else if (!strcmp(name, TYPEC_CABLE_USB_HOST))
		type = E_TYPEC_CABLE_USB_HOST;
	else if (!strcmp(name, TYPEC_CABLE_USB_DP_SRC))
		type = E_TYPEC_CABLE_DP_SRC;
	else
		type = E_TYPEC_CABLE_UNKNOWN;

	return type;
}

static int typec_detect_send_psy_notification(struct typec_detect *detect,
					bool chrg_status)
{
	struct typec_cc_psy cc_psy;
	int ret;
	struct power_supply_cable_props cable_props = {0};

	if (chrg_status) {

		ret = typec_measure_cc(detect->phy,
					detect->valid_cc, &cc_psy, 0);
		if (ret < 0) {
			dev_warn(detect->phy->dev,
					"%s: Error(%d) measuring cc2\n",
					__func__, ret);
			return ret;
		}
		dev_dbg(detect->phy->dev, "%s: Measured v_rd=%d\n",
				__func__, cc_psy.v_rd);

		cable_props.ma = get_chrgcur_from_rd(detect, cc_psy.v_rd);
		cable_props.chrg_evt =
				POWER_SUPPLY_CHARGER_EVENT_CONNECT;
		cable_props.chrg_type =
				POWER_SUPPLY_CHARGER_TYPE_USB_TYPEC;
	} else {
		cable_props.chrg_evt =
				POWER_SUPPLY_CHARGER_EVENT_DISCONNECT;
		cable_props.chrg_type =
				POWER_SUPPLY_CHARGER_TYPE_USB_TYPEC;
		cable_props.ma = 0;
	}

	dev_dbg(detect->phy->dev, "%s: Notifying PSY, evt= %d\n",
				__func__, cable_props.chrg_evt);
	/* notify power supply */
	atomic_notifier_call_chain(&power_supply_notifier,
						PSY_CABLE_EVENT,
						&cable_props);
	return 0;
}

static void typec_detect_notify_extcon(struct typec_detect *detect,
						char *type, bool state)
{
	bool notify_otg = false;
	int otg_evt;
	enum typec_cable_type cbl_type;

	dev_dbg(detect->phy->dev, "%s: type = %s state = %d\n",
				 __func__, type, state);
	cbl_type = typec_detect_cable_name_to_type(type);
	mutex_lock(&detect->lock);

	switch (cbl_type) {
	case E_TYPEC_CABLE_USB_SNK:
		if (detect->snk_state == state)
			break;
		detect->snk_state = state;
		/* send notification to power supply framework */
		typec_detect_send_psy_notification(detect, state);
		if (state)
			detect->state = DETECT_STATE_ATTACHED_UFP;
		else
			detect->state = DETECT_STATE_UNATTACHED_UFP;
		break;

	case E_TYPEC_CABLE_USB_SRC:
		if (detect->src_state == state)
			break;

		detect->src_state = state;
		if (state)
			detect->state = DETECT_STATE_ATTACHED_DFP;
		else
			detect->state = DETECT_STATE_UNATTACHED_DFP;
		break;

	case E_TYPEC_CABLE_USB_HOST:
		if (detect->usb_host_state == state)
			break;

		detect->usb_host_state = state;
		/* Send ID notification to USB subsystem so that
		 *  system will switch host mode of operation.
		 */
		notify_otg = true;
		if (state)
			otg_evt = USB_EVENT_ID;
		else
			otg_evt = USB_EVENT_NONE;
		break;

	case E_TYPEC_CABLE_USB:
		if (detect->usb_state == state)
			break;

		detect->usb_state = state;
		/* Send ID notification to USB subsystem so that
		 *  system will switch device mode of operation.
		 */
		notify_otg = true;
		if (state)
			otg_evt = USB_EVENT_VBUS;
		else
			otg_evt = USB_EVENT_NONE;
		break;

	case E_TYPEC_CABLE_DP_SRC:
		break;

	default:
		goto notify_ext_err;
	}

	extcon_set_cable_state(detect->edev, type, state);

	if (notify_otg)
		atomic_notifier_call_chain(&detect->otg->notifier,
						otg_evt, NULL);
	if (!detect->snk_state && !detect->src_state
		&& !detect->usb_state && !detect->usb_host_state) {
		/* If phy supports drp toggle then enble toggle
		 * as cable disconnected.
		 */
		detect->state = DETECT_STATE_UNATTACHED_DRP;
		if (detect->phy->support_drp_toggle
			&& detect->phy->enable_detection)
			detect->phy->enable_detection(detect->phy, true);
	}

notify_ext_err:
	mutex_unlock(&detect->lock);
}

void typec_notify_cable_state(struct typec_phy *phy, char *type, bool state)
{
	struct typec_detect *detect;

	detect = get_typec_detect(phy);
	if (detect)
		typec_detect_notify_extcon(detect, type, state);
}
EXPORT_SYMBOL_GPL(typec_notify_cable_state);

static int detect_kthread(void *data)
{
	struct typec_detect *detect = (struct typec_detect *)data;
	struct typec_phy *phy;
	struct typec_cc_psy cc1_psy = {USB_TYPEC_CC_VRD_UNKNOWN,
					TYPEC_CURRENT_UNKNOWN};
	struct typec_cc_psy cc2_psy = {USB_TYPEC_CC_VRD_UNKNOWN,
					TYPEC_CURRENT_UNKNOWN};

	if (!detect) {
		pr_err("%s: no detect found", __func__);
		return 0;
	}

	phy = detect->phy;

	do {
		detect->timer_evt = TIMER_EVENT_NONE;
		wait_event(detect->wq, detect->timer_evt);
		cancel_work_sync(&detect->dfp_work);

		if (detect->timer_evt == TIMER_EVENT_QUIT)
			break;


		/*
		 * try the toggling logic for 5secs
		 * if we cant resolve, it means nothing connected
		 * make the phy to wakeup only on CC change.
		 */
		if (++detect->drp_counter > MAX_DRP_TOGGLING) {
			mutex_lock(&detect->lock);
			detect->drp_counter = 0;
			del_timer_sync(&detect->drp_timer); /* disable timer */
			detect->state = DETECT_STATE_UNATTACHED_DRP;
			typec_switch_mode(phy, TYPEC_MODE_DRP);
			mutex_unlock(&detect->lock);
			continue;
		}


		if (detect->state == DETECT_STATE_UNATTACHED_UFP) {
			if (detect_check_valid_ufp(detect,
				&cc1_psy, &cc2_psy) && detect->got_vbus) {
				detect_update_ufp_state(detect, &cc1_psy,
								&cc2_psy);
				continue;
			} else
				mod_timer(&detect->drp_timer,
					jiffies + msecs_to_jiffies(50));
		}

		if (detect->state == DETECT_STATE_UNATTACHED_DFP ||
			detect->state == DETECT_STATE_UNATTACHED_DRP) {
			mutex_lock(&detect->lock);
			detect->state = DETECT_STATE_UNATTACHED_UFP;
			typec_switch_mode(phy, TYPEC_MODE_UFP);
			mutex_unlock(&detect->lock);
			/* next state start from VALID VBUS */
		} else if (detect->state == DETECT_STATE_UNATTACHED_UFP) {
			mutex_lock(&detect->lock);
			detect->state = DETECT_STATE_UNATTACHED_DFP;
			typec_set_host_current(phy, TYPEC_CURRENT_USB);
			typec_switch_mode(phy, TYPEC_MODE_DFP);
			mutex_unlock(&detect->lock);
			schedule_work(&detect->dfp_work);
		}
	} while (true);

	return 0;
}

static enum typec_cc_pin get_active_cc(struct typec_cc_psy *cc1,
		struct typec_cc_psy *cc2)
{
	int ret = 0;

	if (CC_RD(cc1->v_rd) && (CC_OPEN(cc2->v_rd) || CC_RA(cc2->v_rd)))
		ret = TYPEC_PIN_CC1;
	else if (CC_RD(cc2->v_rd) && (CC_OPEN(cc1->v_rd) || CC_RA(cc1->v_rd)))
		ret = TYPEC_PIN_CC2;

	return ret;
}

/*
 * return 1 on VBUS presence (UFP detected),
 * 0 on measurement sucess,
 * -ERR on measuremet failure
 */
static int detect_measure_cc(struct typec_detect *detect, enum typec_cc_pin pin,
		struct typec_cc_psy *cc_psy, bool *found)
{
	int ret;

	ret = typec_measure_cc(detect->phy, pin, cc_psy, msecs_to_jiffies(3));
	if (ret >= 0) {
		*found = true;
		ret = 0;
	}
	mutex_lock(&detect->lock);
	if (detect->got_vbus) {
		ret = detect->got_vbus;
		mutex_unlock(&detect->lock);
		dev_err(detect->phy->dev, "%s:exiting got vbus cc%d\n",
				__func__, pin);
		return ret;
	}
	mutex_unlock(&detect->lock);
	return ret;
}

static void detect_dfp_work(struct work_struct *work)
{
	struct typec_detect *detect =
		container_of(work, struct typec_detect, dfp_work);
	bool cc1_found = false;
	bool cc2_found = false;
	int ret;
	enum typec_cc_pin use_cc = 0;
	struct typec_phy *phy = detect->phy;
	struct typec_cc_psy cc1 = {0, 0};
	struct typec_cc_psy cc2 = {0, 0};

	mutex_lock(&detect->lock);
	if (detect->state != DETECT_STATE_UNATTACHED_DFP || detect->got_vbus) {
		mutex_unlock(&detect->lock);
		return;
	}
	mutex_unlock(&detect->lock);

	/* If phy supports drp toggle then disable toggle
	 * as the detection completed.
	 */
	if (phy->support_drp_toggle
		&& phy->enable_detection)
		phy->enable_detection(phy, false);

	ret = detect_measure_cc(detect, TYPEC_PIN_CC1, &cc1, &cc1_found);
	/* if vbus is received due to the UFP attachment, then break worker */
	if (ret > 0)
		return;

	ret = detect_measure_cc(detect, TYPEC_PIN_CC2, &cc2, &cc2_found);
	/* if vbus is received due to the UFP attachment, then break worker */
	if (ret > 0)
		return;

	dev_dbg(detect->phy->dev,
		"cc1_found = %d cc2_found = %d unattach dfp cc1 = %d, cc2 = %d",
		cc1_found, cc2_found, cc1.v_rd, cc2.v_rd);

	if (cc1_found && cc2_found) {
		if (((CC_RA(cc1.v_rd) || (CC_OPEN(cc1.v_rd)))
				&& CC_RD(cc2.v_rd)) ||
			(CC_RD(cc1.v_rd) && (CC_RA(cc2.v_rd) ||
					CC_OPEN(cc2.v_rd)))) {
			if (!phy->support_drp_toggle) /* disable timer */
				del_timer_sync(&detect->drp_timer);
			mutex_lock(&detect->lock);
			detect->state = DETECT_STATE_ATTACH_DFP_DRP_WAIT;
			mutex_unlock(&detect->lock);

			usleep_range(100000, 150000);
			mutex_lock(&detect->lock);
			/* cable detach could have happened during this time */
			if (detect->state != DETECT_STATE_ATTACH_DFP_DRP_WAIT) {
				mutex_unlock(&detect->lock);
				return;
			}
			detect->state = DETECT_STATE_ATTACHED_DFP;
			detect->drp_counter = 0;
			use_cc = get_active_cc(&cc1, &cc2);
			detect->valid_cc = use_cc;
			mutex_unlock(&detect->lock);
			typec_setup_cc(phy, use_cc, TYPEC_STATE_ATTACHED_DFP);

			/* enable VBUS */
			typec_detect_notify_extcon(detect,
						TYPEC_CABLE_USB_SRC, true);
			if (!detect->is_pd_capable)
				typec_detect_notify_extcon(detect,
						TYPEC_CABLE_USB_HOST, true);

			typec_enable_autocrc(detect->phy, true);
			return;

		} else if (CC_RA(cc1.v_rd) && CC_RA(cc2.v_rd)) {
			mutex_lock(&detect->lock);
			detect->state = DETECT_STATE_ATTACHED_DFP;
			detect->drp_counter = 0;
			mutex_unlock(&detect->lock);
			/* TODO: Need to set the phy state */
			if (!phy->support_drp_toggle) /* disable timer */
				del_timer_sync(&detect->drp_timer);
			/* Audio Accessory. */
			/* next state Attached UFP based on VBUS */
			dev_info(detect->phy->dev, "Audio Accessory Detected");
			return;
		} else if (CC_RD(cc1.v_rd) && CC_RD(cc2.v_rd)) {
			mutex_lock(&detect->lock);
			detect->state = DETECT_STATE_ATTACHED_DFP;
			detect->drp_counter = 0;
			mutex_unlock(&detect->lock);
			if (!phy->support_drp_toggle) /* disable timer */
				del_timer_sync(&detect->drp_timer);
			/* Debug Accessory */
			/* next state Attached UFP based on VBUS */
			dev_info(detect->phy->dev, "Debug Accessory Detected");
			return;
		}
	}
	if (!phy->support_drp_toggle)
		schedule_work(&detect->dfp_work);
	else
		typec_switch_mode(phy, TYPEC_MODE_DRP);
}

static void detect_drp_timer(unsigned long data)
{
	struct typec_detect *detect = (struct typec_detect *)data;
	struct typec_phy *phy;

	pr_debug("running %s\n", __func__);

	phy = detect->phy;
	if (!phy) {
		pr_err("%s: no valid phy registered", __func__);
		return;
	}
	if (phy->support_drp_toggle)
		return;

	detect->timer_evt = TIMER_EVENT_PROCESS;
	wake_up(&detect->wq);
	mod_timer(&detect->drp_timer, jiffies + msecs_to_jiffies(50));
}

static void detect_lock_ufp_work(struct work_struct *work)
{
	struct typec_detect *detect = container_of(work, struct typec_detect,
					lock_ufp_work);
	struct typec_phy *phy;
	int ret;
	/* tDRPLock - 100 to 150ms */
	unsigned long timeout = msecs_to_jiffies(TYPEC_DRPLOCK_TIMEOUT);

	phy = detect->phy;
	typec_switch_mode(detect->phy, TYPEC_MODE_UFP);
	ret = wait_for_completion_timeout(&detect->lock_ufp_complete, timeout);
	if (ret == 0) {
		mutex_lock(&detect->lock);
		if (detect->state == DETECT_STATE_LOCK_UFP) {
			detect->state = DETECT_STATE_UNATTACHED_DRP;
			typec_switch_mode(detect->phy, TYPEC_MODE_DRP);
		}
		mutex_unlock(&detect->lock);
	}
	/* got vbus, goto attached ufp */

	return;
}

static void detect_update_ufp_state(struct typec_detect *detect,
		struct typec_cc_psy *cc1_psy,
		struct typec_cc_psy *cc2_psy)
{
	struct typec_phy *phy = detect->phy;

	mutex_lock(&detect->lock);
	detect->state = DETECT_STATE_ATTACHED_UFP;
	mutex_unlock(&detect->lock);
	/* If phy supports drp toggle then disable toggle
	 * as the detection completed.
	 */
	if (phy->support_drp_toggle
		&& phy->enable_detection)
		phy->enable_detection(phy, false);


	typec_detect_notify_extcon(detect,
				TYPEC_CABLE_USB_SNK, true);
	if (!detect->is_pd_capable)
		typec_detect_notify_extcon(detect,
				TYPEC_CABLE_USB, true);
	typec_enable_autocrc(detect->phy, true);

}

static int detect_check_valid_ufp(struct typec_detect *detect,
		struct typec_cc_psy *cc1_psy,
		struct typec_cc_psy *cc2_psy)
{
	struct typec_phy *phy;
	enum typec_cc_pin use_cc = 0;
	int ret;

	phy = detect->phy;

	if (!phy->support_drp_toggle)
		del_timer_sync(&detect->drp_timer); /* disable timer */
	cancel_work_sync(&detect->drp_work);
	cancel_work_sync(&detect->dfp_work);

	if (detect->state == DETECT_STATE_ATTACHED_DFP)
		goto end;
	else if (!phy->support_drp_toggle &&
			(detect->state == DETECT_STATE_UNATTACHED_DFP ||
			detect->state == DETECT_STATE_UNATTACHED_DRP)) {
		mutex_lock(&detect->lock);
		typec_switch_mode(phy, TYPEC_MODE_UFP);
		mutex_unlock(&detect->lock);
	}

	ret = typec_measure_cc(phy, TYPEC_PIN_CC1, cc1_psy, 0);
	if (ret < 0) {
		dev_warn(detect->phy->dev,
				"%s: Error(%d) measuring cc1\n",
				__func__, ret);
		cc1_psy->v_rd = USB_TYPEC_CC_VRD_UNKNOWN;
		cc1_psy->cur = TYPEC_CURRENT_UNKNOWN;
	}

	ret = typec_measure_cc(phy, TYPEC_PIN_CC2, cc2_psy, 0);
	if (ret < 0) {
		dev_warn(detect->phy->dev,
				"%s: Error(%d) measuring cc2\n",
				__func__, ret);
		cc2_psy->v_rd = USB_TYPEC_CC_VRD_UNKNOWN;
		cc2_psy->cur = TYPEC_CURRENT_UNKNOWN;
	}

	dev_info(detect->phy->dev, "evt_vbus cc1 = %d, cc2 = %d",
					cc1_psy->v_rd, cc2_psy->v_rd);

	if (!phy->support_drp_toggle) {
		/* try another time? */
		if (CC_OPEN(cc1_psy->v_rd) || CC_RA(cc1_psy->v_rd)) {
			ret = typec_measure_cc(phy, TYPEC_PIN_CC1,
					cc1_psy, 0);
			if (ret < 0) {
				dev_warn(detect->phy->dev,
					"%s: Error(%d) measuring cc1\n",
					__func__, ret);
				cc1_psy->v_rd = USB_TYPEC_CC_VRD_UNKNOWN;
				cc1_psy->cur = TYPEC_CURRENT_UNKNOWN;
			}
		}

		if (CC_OPEN(cc2_psy->v_rd) || CC_RA(cc2_psy->v_rd)) {
			ret = typec_measure_cc(phy, TYPEC_PIN_CC2,
					cc2_psy, 0);
			if (ret < 0) {
				dev_warn(detect->phy->dev,
					"%s: Error(%d) measuring cc2\n",
						__func__, ret);
				cc2_psy->v_rd = USB_TYPEC_CC_VRD_UNKNOWN;
				cc2_psy->cur = TYPEC_CURRENT_UNKNOWN;
			}
		}
		dev_info(detect->phy->dev, "evt_vbus cc1 = %d cc2 = %d",
					cc1_psy->v_rd, cc2_psy->v_rd);
	}

	use_cc = get_active_cc(cc1_psy, cc2_psy);

	if (use_cc) {
		/* valid cc found; UFP_ATTACHED */

		mutex_lock(&detect->lock);
		detect->valid_cc = use_cc;
		mutex_unlock(&detect->lock);
		typec_setup_cc(phy, use_cc, TYPEC_STATE_ATTACHED_UFP);
		return true;
	}
end:
	return false;
}

static void detect_drp_work(struct work_struct *work)
{
	struct typec_detect *detect = container_of(work, struct typec_detect,
					drp_work);
	struct typec_phy *phy;

	phy = detect->phy;
	if (phy->support_drp_toggle)
		return;

	dev_info(detect->phy->dev, "EVNT DRP");
	mutex_lock(&detect->lock);
	detect->state = DETECT_STATE_UNATTACHED_DRP;
	mutex_unlock(&detect->lock);
	/* start the timer now */
	if (!timer_pending(&detect->drp_timer))
		mod_timer(&detect->drp_timer, jiffies + msecs_to_jiffies(1));
}

static inline void detect_check_ufp(struct typec_detect *detect)
{
	struct typec_phy *phy;
	struct typec_cc_psy cc1_psy = {USB_TYPEC_CC_VRD_UNKNOWN,
					TYPEC_CURRENT_UNKNOWN};
	struct typec_cc_psy cc2_psy = {USB_TYPEC_CC_VRD_UNKNOWN,
					TYPEC_CURRENT_UNKNOWN};

	phy = detect->phy;

	if (phy->support_drp_toggle) {
		if (detect_check_valid_ufp(detect, &cc1_psy, &cc2_psy))
			detect_update_ufp_state(detect, &cc1_psy, &cc2_psy);
		else
			typec_switch_mode(phy, TYPEC_MODE_DRP);
	}
}

static void update_phy_state(struct work_struct *work)
{
	struct typec_phy *phy;
	struct typec_detect *detect;
	struct power_supply_cable_props cable_props = {0};
	int state;

	detect = container_of(work, struct typec_detect, phy_ntf_work);
	phy = detect->phy;

	switch (detect->event) {
	case TYPEC_EVENT_VBUS:
		mutex_lock(&detect->lock);
		detect->got_vbus = true;
		detect->drp_counter = 0;
		state = detect->state;
		if (state == DETECT_STATE_LOCK_UFP)
			complete(&detect->lock_ufp_complete);
		mutex_unlock(&detect->lock);
		detect_check_ufp(detect);
		break;
	case TYPEC_EVENT_NONE:
		dev_dbg(phy->dev, "EVENT NONE: state = %d", detect->state);
		mutex_lock(&detect->lock);
		detect->got_vbus = false;
		/* setup Switches0 Setting */
		detect->drp_counter = 0;
		detect->valid_cc = 0;
		if (!phy->support_drp_toggle)
			typec_setup_cc(phy, 0, TYPEC_STATE_UNATTACHED_UFP);
		mutex_unlock(&detect->lock);

		if (detect->state == DETECT_STATE_ATTACHED_UFP) {
			dev_dbg(phy->dev, "%s: UFP Disconnected, state=%d",
				__func__, detect->state);
			typec_detect_notify_extcon(detect,
						TYPEC_CABLE_USB_SNK, false);
			if (!detect->is_pd_capable)
				typec_detect_notify_extcon(detect,
						TYPEC_CABLE_USB, false);
			/* notify power supply */
			cable_props.chrg_evt =
				POWER_SUPPLY_CHARGER_EVENT_DISCONNECT;
			cable_props.chrg_type =
				POWER_SUPPLY_CHARGER_TYPE_USB_TYPEC;
			cable_props.ma = 0;
			atomic_notifier_call_chain(&power_supply_notifier,
							PSY_CABLE_EVENT,
							&cable_props);
			typec_enable_autocrc(detect->phy, false);
			mutex_lock(&detect->lock);
			detect->state = DETECT_STATE_UNATTACHED_DRP;
			mutex_unlock(&detect->lock);
		} else if (detect->state == DETECT_STATE_ATTACHED_DFP) {
			/* state = DFP; disable VBUS */
			typec_detect_notify_extcon(detect,
						TYPEC_CABLE_USB_SRC, false);
			if (!detect->is_pd_capable)
				typec_detect_notify_extcon(detect,
						TYPEC_CABLE_USB_HOST, false);

			typec_enable_autocrc(detect->phy, false);

			reinit_completion(&detect->lock_ufp_complete);
			mutex_lock(&detect->lock);
			detect->state = DETECT_STATE_LOCK_UFP;
			mutex_unlock(&detect->lock);
			queue_work(detect->wq_lock_ufp,
					&detect->lock_ufp_work);
			break;
		}
		break;
	default:
		dev_err(detect->phy->dev, "unknown event %d", detect->event);
	}
}

static int typec_handle_phy_ntf(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct typec_phy *phy;
	struct typec_detect *detect =
		container_of(nb, struct typec_detect, nb);
	int handled = NOTIFY_OK;

	phy = detect->phy;
	if (!phy)
		return NOTIFY_BAD;

	switch (event) {
	case TYPEC_EVENT_VBUS:
	case TYPEC_EVENT_NONE:
		detect->event = event;
		/* Do not enable drp toggle here as this EVENT_NONE
		 * could be due to pwr role swap.
		 */
		schedule_work(&detect->phy_ntf_work);
		break;
	case TYPEC_EVENT_DFP:
		detect->state = DETECT_STATE_UNATTACHED_DFP;
		schedule_work(&detect->dfp_work);
		break;
	case TYPEC_EVENT_DRP:
		schedule_work(&detect->drp_work);
		break;
	default:
		handled = NOTIFY_DONE;
	}
	return handled;
}

static int detect_otg_notifier(struct notifier_block *nb, unsigned long event,
				void *param)
{
	return NOTIFY_DONE;
}

static void detect_remove(struct typec_detect *detect)
{
	struct typec_phy *phy;
	if (!detect)
		return;

	phy = detect->phy;
	cancel_work_sync(&detect->phy_ntf_work);
	cancel_work_sync(&detect->dfp_work);
	if (!phy->support_drp_toggle)
		del_timer_sync(&detect->drp_timer);
	detect->timer_evt = TIMER_EVENT_QUIT;
	wake_up(&detect->wq);

	if (detect->otg) {
		usb_unregister_notifier(detect->otg, &detect->otg_nb);
		usb_put_phy(detect->otg);
	}
	if (detect->edev)
		extcon_dev_unregister(detect->edev);
	kfree(detect);
}

int typec_bind_detect(struct typec_phy *phy)
{
	struct typec_detect *detect;
	int ret;

	detect = kzalloc(sizeof(struct typec_detect), GFP_KERNEL);

	if (!detect) {
		pr_err("typec fsm: no memory");
		return -ENOMEM;
	}

	if (!phy) {
		pr_err("%s: no valid phy provided", __func__);
		return -EINVAL;
	}

	detect->phy = phy;
	if (phy->is_pd_capable)
		detect->is_pd_capable = phy->is_pd_capable(phy);
	detect->nb.notifier_call = typec_handle_phy_ntf;

	ret = typec_register_notifier(phy, &detect->nb);
	if (ret  < 0) {
		dev_err(phy->dev, "unable to register notifier");
		goto error;
	}

	init_waitqueue_head(&detect->wq);

	INIT_WORK(&detect->phy_ntf_work, update_phy_state);
	INIT_WORK(&detect->dfp_work, detect_dfp_work);
	INIT_WORK(&detect->drp_work, detect_drp_work);

	if (!phy->support_drp_toggle)
		setup_timer(&detect->drp_timer, detect_drp_timer,
				(unsigned long)detect);

	detect->detect_kthread = kthread_run(detect_kthread, detect, "detect");
	detect->state = DETECT_STATE_UNATTACHED_DRP;

	detect->otg = usb_get_phy(USB_PHY_TYPE_USB2);
	if (IS_ERR_OR_NULL(detect->otg)) {
		detect->otg = NULL;
		ret = -EINVAL;
		goto error;
	} else {
		detect->otg_nb.notifier_call = detect_otg_notifier;
		ret = usb_register_notifier(detect->otg, &detect->otg_nb);
		if (ret < 0)
			goto error;
	}
	mutex_init(&detect->lock);
	detect->wq_lock_ufp = create_singlethread_workqueue("wq_lock_ufp");
	INIT_WORK(&detect->lock_ufp_work, detect_lock_ufp_work);
	init_completion(&detect->lock_ufp_complete);

	detect->edev = devm_kzalloc(phy->dev, sizeof(struct extcon_dev),
					GFP_KERNEL);

	if (!detect->edev) {
		ret = -ENOMEM;
		goto error;
	}
	detect->edev->name = "usb-typec";
	detect->edev->supported_cable = pd_extcon_cable;
	ret = extcon_dev_register(detect->edev);
	if (ret) {
		devm_kfree(phy->dev, detect->edev);
		goto error;
	}

	list_add_tail(&detect->list, &typec_detect_list);
	return 0;

error:
	detect_remove(detect);
	return ret;
}

int typec_unbind_detect(struct typec_phy *phy)
{
	struct typec_detect *detect, *temp;

	spin_lock(&slock);
	list_for_each_entry_safe(detect, temp, &typec_detect_list, list) {
		if (!strncmp(detect->phy->label, phy->label, MAX_LABEL_SIZE)) {
			list_del(&detect->list);
			detect_remove(detect);
		}
	}
	spin_unlock(&slock);

	return 0;
}
