/*
 * policy_engine.c: Intel USB Power Delivery Policy Engine Driver
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
#include <linux/usb_typec_phy.h>
#include <linux/random.h>
#include "policy_engine.h"

static void pe_dump_header(struct pd_pkt_header *header);
static void pe_dump_data_msg(struct pd_packet *pkt);

static inline
struct policy *pe_get_active_src_or_snk_policy(struct list_head *head)
{
	struct policy *p = NULL;

	list_for_each_entry(p, head, list) {
		if (p && ((p->type == POLICY_TYPE_SINK)
			|| (p->type == POLICY_TYPE_SOURCE))) {
			if (p->state == POLICY_STATE_ONLINE)
				return p;
		}
	}

	return NULL;
}

static inline
struct policy *pe_get_running_policy(struct list_head *head)
{
	struct policy *p = NULL;

	list_for_each_entry(p, head, list) {
		if (p && (p->state == POLICY_STATE_ONLINE)
			&& (p->status == POLICY_STATUS_RUNNING))
				return p;
	}

	return NULL;
}

static inline bool pe_is_policy_active(struct policy *p)
{
	return (p->state == POLICY_STATE_ONLINE) ? true : false;
}

static inline bool pe_is_policy_running(struct policy *p)
{
	return (p->status == POLICY_STATUS_RUNNING) ? true : false;
}

static struct policy *pe_get_policy(struct policy_engine *pe,
					enum policy_type type)
{
	struct policy *p = NULL;

	list_for_each_entry(p, &pe->policy_list, list) {
		if (p && (p->type == type))
			return p;
	}
	return NULL;
}

static int policy_engine_process_data_msg(struct policy_engine *pe,
				enum pe_event evt, struct pd_packet *pkt)
{
	struct policy *p = NULL;
	int ret = 0;

	pr_debug("PE: %s Data msg received evt - %d\n", __func__, evt);
	switch (evt) {
	case PE_EVT_RCVD_SRC_CAP:
	case PE_EVT_RCVD_REQUEST:
	case PE_EVT_RCVD_BIST:
	case PE_EVT_RCVD_SNK_CAP:
		p = pe_get_active_src_or_snk_policy(&pe->policy_list);
		break;
	case PE_EVT_RCVD_VDM:
		p = pe_get_policy(pe, POLICY_TYPE_DISPLAY);
		if (!p) {
			pr_err("PE: No display pe to forward VDM msgs\n");
			break;
		}
		if (!pe_is_policy_active(p)) {
			pr_err("PE: DispPE not active to forward VDM msgs\n");
			p = NULL;
		}
		break;
	default:
		pr_warn("PE: %s invalid data msg, event=%d\n", __func__, evt);
		pe_dump_data_msg(pkt);
	}

	if (p && p->rcv_pkt)
		ret = p->rcv_pkt(p, pkt, evt);
	else
		ret = -ENODEV;
	return ret;
}

static int policy_engine_process_ctrl_msg(struct policy_engine *pe,
				enum pe_event evt, struct pd_packet *pkt)
{
	struct policy *p = NULL;
	int ret = 0;

	pr_debug("PE: %s Ctrl msg received evt - %d\n", __func__, evt);
	switch (evt) {
	case PE_EVT_RCVD_GOODCRC:
		p = pe_get_running_policy(&pe->policy_list);
		if (!p)
			pr_err("PE: No running policy to forward GCRC msgs\n");
		break;
	case PE_EVT_RCVD_GOTOMIN:
	case PE_EVT_RCVD_ACCEPT:
	case PE_EVT_RCVD_REJECT:
	case PE_EVT_RCVD_PING:
	case PE_EVT_RCVD_PS_RDY:
	case PE_EVT_RCVD_GET_SRC_CAP:
	case PE_EVT_RCVD_GET_SINK_CAP:
	case PE_EVT_RCVD_DR_SWAP:
	case PE_EVT_RCVD_PR_SWAP:
	case PE_EVT_RCVD_VCONN_SWAP:
	case PE_EVT_RCVD_WAIT:
		p = pe_get_active_src_or_snk_policy(&pe->policy_list);
		if (!p)
			pr_err("PE: No active policy to forward Ctrl msgs\n");
		break;
	default:
		pr_warn("PE: %s Not a valid ctrl msg to process, event=%d\n",
				__func__, evt);
		pe_dump_header(&pkt->header);
	}
	if (p && p->rcv_pkt)
		ret = p->rcv_pkt(p, pkt, evt);
	else
		ret = -ENODEV;
	return ret;
}

static void pe_dump_header(struct pd_pkt_header *header)
{
#ifdef DBG
	if (!header) {
		pr_err("PE: No Header information available...\n");
		return;
	}
	pr_info("========== POLICY ENGINE: HEADER INFO ==========\n");
	pr_info("PE: Message Type - 0x%x\n", header->msg_type);
	pr_info("PE: Reserved B4 - 0x%x\n", header->rsvd_a);
	pr_info("PE: Port Data Role - 0x%x\n", header->data_role);
	pr_info("PE: Specification Revision - 0x%x\n", header->rev_id);
	pr_info("PE: Port Power Role - 0x%x\n", header->pwr_role);
	pr_info("PE: Message ID - 0x%x\n", header->msg_id);
	pr_info("PE: Number of Data Objects - 0x%x\n", header->num_data_obj);
	pr_info("PE: Reserved B15 - 0x%x\n", header->rsvd_b);
	pr_info("=============================================");
#endif /* DBG */
}

static void pe_dump_data_msg(struct pd_packet *pkt)
{
#ifdef DBG
	int num_data_objs = PD_MSG_NUM_DATA_OBJS(&pkt->header);
	unsigned int data_buf[num_data_objs];
	int i;

	memset(data_buf, 0, num_data_objs);
	memcpy(data_buf, &pkt->data_obj, PD_MSG_LEN(&pkt->header));

	for (i = 0; i < num_data_objs; i++) {
		pr_info("PE: Data Message - data[%d]: 0x%08x\n",
					i+1, data_buf[i]);
	}
#endif /* DBG */
}

static int pe_fwdcmd_to_policy(struct policy_engine *pe, enum pe_event evt)
{
	struct policy *p;
	int ret = 0;

	p = pe_get_active_src_or_snk_policy(&pe->policy_list);
	if (!p) {
		pr_err("PE: No Active policy!\n");
		return -EINVAL;
	}

	if (p && p->rcv_cmd) {
		p->rcv_cmd(p, evt);
	} else {
		pr_err("PE: Unable to find send cmd\n");
		ret = -ENODEV;
	}

	return ret;
}

static int policy_engine_process_cmd(struct policy_engine *pe,
				enum pe_event evt)
{
	int ret = 0;

	pr_debug("PE: %s - cmd %d\n", __func__, evt);
	switch (evt) {
	case PE_EVT_RCVD_HARD_RESET:
	case PE_EVT_RCVD_HARD_RESET_COMPLETE:
		ret = pe_fwdcmd_to_policy(pe, evt);
		if (ret < 0)
			pr_err("PE: Error in handling cmd\n");
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static inline void policy_prot_update_data_role(struct policy_engine *pe,
				enum data_role drole)
{
	if (pe && pe->prot && pe->prot->policy_update_data_role)
		pe->prot->policy_update_data_role(pe->prot, drole);
}

static inline void policy_prot_update_power_role(struct policy_engine *pe,
				enum pwr_role prole)
{
	if (pe && pe->prot && pe->prot->policy_update_power_role)
		pe->prot->policy_update_power_role(pe->prot, prole);
}

static int pe_get_srcpwr_cap(struct policy_engine *pe,
					struct power_cap *cap)
{
	if (pe && pe->dpm)
		return devpolicy_get_srcpwr_cap(pe->dpm, cap);

	return -ENODEV;
}

static int pe_get_snkpwr_cap(struct policy_engine *pe,
					struct power_cap *cap)
{
	if (pe && pe->dpm)
		return devpolicy_get_snkpwr_cap(pe->dpm, cap);

	return -ENODEV;
}

static int pe_get_snkpwr_caps(struct policy_engine *pe,
					struct power_caps *caps)
{
	if (pe && pe->dpm)
		return devpolicy_get_snkpwr_caps(pe->dpm, caps);

	return -ENODEV;
}

static int pe_get_max_snkpwr_cap(struct policy_engine *pe,
					struct power_cap *cap)
{
	if (pe && pe->dpm)
		return devpolicy_get_max_snkpwr_cap(pe->dpm, cap);

	return -ENODEV;
}

static enum data_role pe_get_data_role(struct policy_engine *pe)
{
	enum data_role drole;

	mutex_lock(&pe->pe_lock);
	drole = pe->cur_drole;
	mutex_unlock(&pe->pe_lock);
	return drole;
}

static enum pwr_role pe_get_power_role(struct policy_engine *pe)
{
	enum pwr_role prole;

	mutex_lock(&pe->pe_lock);
	prole = pe->cur_prole;
	mutex_unlock(&pe->pe_lock);
	return prole;
}

static int pe_set_data_role(struct policy_engine *pe, enum data_role role)
{
	mutex_lock(&pe->pe_lock);
	if (pe->cur_drole == role)
		goto set_drole_out;

	pe->cur_drole = role;
	policy_dpm_update_data_role(pe, role);
	/* If role swap, no need to update protocol */
	if (role != DATA_ROLE_SWAP) {
		/* Update the protocol */
		policy_prot_update_data_role(pe, role);
	}

set_drole_out:
	mutex_unlock(&pe->pe_lock);
	return 0;
}

static int pe_set_power_role(struct policy_engine *pe, enum pwr_role role)
{
	mutex_lock(&pe->pe_lock);
	if (pe->cur_prole == role)
		goto set_prole_out;

	pe->cur_prole = role;
	policy_dpm_update_power_role(pe, role);
	/* If role swap, no need to update protocol */
	if (role != POWER_ROLE_SWAP) {
		/* Update the protocol */
		policy_prot_update_power_role(pe, role);
	}

set_prole_out:
	mutex_unlock(&pe->pe_lock);
	return 0;
}

static int pe_set_charger_mode(struct policy_engine *pe, enum charger_mode mode)
{
	if (pe && pe->dpm)
		return devpolicy_set_charger_mode(pe->dpm, mode);

	return -ENODEV;
}

static int pe_update_charger_ilim(struct policy_engine *pe, int ilim)
{
	if (pe && pe->dpm)
		return devpolicy_update_current_limit(pe->dpm, ilim);

	return -ENODEV;
}

static int pe_get_min_snk_current(struct policy_engine *pe, int *ma)
{
	if (pe && pe->dpm)
		return devpolicy_get_min_snk_current(pe->dpm, ma);

	return -ENODEV;
}

static int pe_is_pr_swap_support(struct policy_engine *pe, enum pwr_role prole)
{
	if (pe && pe->dpm)
		return devpolicy_is_pr_swap_support(pe->dpm, prole);

	return -ENODEV;
}

static enum cable_state pe_get_cable_state(struct policy_engine *pe,
						enum cable_type type)
{
	if (pe && pe->dpm)
		return devpolicy_get_cable_state(pe->dpm, type);

	return -ENODEV;
}

static bool pe_get_pd_state(struct policy_engine *pe)
{
	return pe->is_pd_connected;
}

static int pe_set_pd_state(struct policy_engine *pe, bool state)
{
	mutex_lock(&pe->pe_lock);
	pe->is_pd_connected = state;
	mutex_unlock(&pe->pe_lock);
	return 0;
}

static int pe_start_policy(struct policy_engine *pe, enum policy_type type)

{
	struct policy *p;

	p = pe_get_policy(pe, type);
	if (!p) {
		pr_err("PE: Unable to get %d policy\n", type);
		return -ENODEV;
	}

	if (p->state != POLICY_STATE_ONLINE)
		p->start(p);
	else {
		pr_warn("PE: policy %d is already active!!!\n", type);
		return -EINVAL;
	}

	return 0;
}

static int pe_stop_policy(struct policy_engine *pe, enum policy_type type)

{
	struct policy *p;

	p = pe_get_policy(pe, type);
	if (!p) {
		pr_err("PE: Unable to get %d policy\n", type);
		return -ENODEV;
	}

	if (p->state == POLICY_STATE_ONLINE)
		p->stop(p);
	else {
		pr_warn("PE: policy %d is not active!!!\n", type);
		return -EINVAL;
	}

	return 0;
}

static int pe_switch_policy(struct policy_engine *pe,
				enum policy_type start_policy_type)
{
	enum policy_type stop_policy_type;
	int ret;

	if (start_policy_type == POLICY_TYPE_SOURCE)
		stop_policy_type =  POLICY_TYPE_SINK;
	else if (start_policy_type == POLICY_TYPE_SINK)
		stop_policy_type = POLICY_TYPE_SOURCE;
	else
		return -EINVAL;

	ret = pe_stop_policy(pe, stop_policy_type);
	if (ret < 0)
		return ret;

	return pe_start_policy(pe, start_policy_type);
}

static int pe_send_packet(struct policy_engine *pe, void *data, int len,
				u8 msg_type, enum pe_event evt)
{
	int ret = 0;

	if (!pe_get_pd_state(pe)) {
		pr_debug("PE:%s: Not sending pkt, evt=%d\n", __func__, evt);
		ret = -EINVAL;
		goto snd_pkt_err;
	}

	switch (evt) {
	case PE_EVT_SEND_GOTOMIN:
	case PE_EVT_SEND_ACCEPT:
	case PE_EVT_SEND_REJECT:
	case PE_EVT_SEND_PING:
	case PE_EVT_SEND_PS_RDY:
	case PE_EVT_SEND_GET_SRC_CAP:
	case PE_EVT_SEND_GET_SINK_CAP:
	case PE_EVT_SEND_DR_SWAP:
	case PE_EVT_SEND_PR_SWAP:
	case PE_EVT_SEND_VCONN_SWAP:
	case PE_EVT_SEND_WAIT:
	case PE_EVT_SEND_SRC_CAP:
	case PE_EVT_SEND_REQUEST:
	case PE_EVT_SEND_BIST:
	case PE_EVT_SEND_SNK_CAP:
	case PE_EVT_SEND_VDM:
	case PE_EVT_SEND_HARD_RESET:
	case PE_EVT_SEND_PROTOCOL_RESET:
	case PE_EVT_SEND_SOFT_RESET:
		break;
	default:
		ret = -EINVAL;
		goto snd_pkt_err;
	}

	/* Send the pd_packet to protocol directly to request
	 * sink power cap */
	pr_debug("PE:%s: Sending pkt, evt=%d\n", __func__, evt);
	if (pe && pe->prot && pe->prot->policy_fwd_pkt)
		pe->prot->policy_fwd_pkt(pe->prot, msg_type, data, len);

snd_pkt_err:
	return ret;
}

static struct policy *__pe_find_policy(struct list_head *list,
						enum policy_type type)
{
	struct policy  *p = NULL;

	list_for_each_entry(p, list, list) {
		if (p && p->type != type)
			continue;
		return p;
	}

	return ERR_PTR(-ENODEV);
}

static void pe_policy_status_changed(struct policy_engine *pe, int policy_type,
				int status)
{
	struct policy *p;

	if (!pe)
		return;
	/* Handle the source policy status change */
	if ((policy_type == POLICY_TYPE_SOURCE)
		&& ((status == POLICY_STATUS_SUCCESS)
		|| (status == POLICY_STATUS_FAIL))) {
		p = pe_get_policy(pe, POLICY_TYPE_DISPLAY);
		/* Start the display policy */
		if (!p) {
			pr_err("PE: %s No Display policy found\n", __func__);
			return;
		}
		if (p->start) {
			pr_info("PE: %s Stating disp policy\n", __func__);
			p->start(p);
		}
	}
}

static void pe_init_policy(struct work_struct *work)
{
	struct policy_engine *pe = container_of(work, struct policy_engine,
							policy_init_work);

	struct pd_policy *supported_policy = pe->supported_policies;
	struct policy *policy;
	int i;

	for (i = 0; i < supported_policy->num_policies; i++) {
		switch (supported_policy->policies[i]) {
		case POLICY_TYPE_SINK:
			policy = sink_port_policy_init(pe);
			if (IS_ERR_OR_NULL(policy)) {
				pr_err("PE: %s unable to init SINK_POLICY\n",
								__func__);
				continue;
			}
			list_add_tail(&policy->list, &pe->policy_list);
			break;
		case POLICY_TYPE_SOURCE:
			policy = src_pe_init(pe);
			if (IS_ERR_OR_NULL(policy)) {
				pr_err("PE: %s unable to init SOURCE_POLICY\n",
								__func__);
				continue;
			}
			list_add_tail(&policy->list, &pe->policy_list);
			pr_debug("PE: %s Successfuly init source pe\n",
					__func__);
			break;
		case POLICY_TYPE_DISPLAY:
			policy = disp_pe_init(pe);
			if (IS_ERR_OR_NULL(policy)) {
				pr_err("PE: %s unable to init DOSPLAY_POLICY\n",
								__func__);
				continue;
			}
			list_add_tail(&policy->list, &pe->policy_list);
			pr_debug("PE: %s Successfuly init display pe\n",
					__func__);
			break;
		default:
			/* invalid, dont add it to policy */
			pr_err("PE: Unknown policy type %d\n",
				supported_policy->policies[i]);
			break;
		}
	}

	return;
}

static int pe_fwdreq_to_policy(struct policy_engine *pe,
					enum pe_event evt)
{
	struct policy *p;
	int ret = 0;

	p = pe_get_active_src_or_snk_policy(&pe->policy_list);
	if (!p) {
		pr_err("PE: No Active policy!\n");
		return -EINVAL;
	}

	if (p && p->rcv_request) {
		p->rcv_request(p, evt);
	} else {
		pr_err("PE: Unable to send request\n");
		ret = -ENODEV;
	}

	return ret;
}

static int pe_send_role_swap_request(struct policy_engine *pe,
					enum pe_event evt)
{
	int ret = 0;

	pr_debug("PE: %s - request %d\n", __func__, evt);
	switch (evt) {
	case PE_EVT_SEND_PR_SWAP:
	case PE_EVT_SEND_DR_SWAP:
		ret = pe_fwdreq_to_policy(pe, evt);
		if (ret < 0)
			pr_err("PE: Error in handling request\n");
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static void pe_handle_dpm_event(struct policy_engine *pe,
					enum devpolicy_mgr_events evt)
{
	struct policy *p;
	enum pe_event pevt = PE_EVT_SEND_NONE;

	pr_info("PE: %s event - %d\n", __func__, evt);
	switch (evt) {
	case DEVMGR_EVENT_UFP_CONNECTED:
		pe_set_power_role(pe, POWER_ROLE_SINK);
		pe_set_data_role(pe, DATA_ROLE_UFP);
		/* Start sink policy */
		p = pe_get_policy(pe, POLICY_TYPE_SINK);
		if (!p) {
			pr_err("PE: No SINK policy to start on UFP connect\n");
			break;
		}
		if (p->state != POLICY_STATE_ONLINE)
			p->start(p);
		else
			pr_warn("PE: SINK policy is already active!!!\n");
		break;
	case DEVMGR_EVENT_DFP_CONNECTED:
		pe_set_power_role(pe, POWER_ROLE_SOURCE);
		pe_set_data_role(pe, DATA_ROLE_DFP);
		/* Start source policy.
		 * Display pe should be started after source pe complete.
		 */
		p = pe_get_policy(pe, POLICY_TYPE_SOURCE);
		if (!p) {
			pr_err("PE: No SOURCE policy to start on DFP connect\n");
			break;
		}
		if (p->state != POLICY_STATE_ONLINE)
			p->start(p);
		else
			pr_warn("PE: SOURCE policy is already active!!!\n");
		break;

	case DEVMGR_EVENT_UFP_DISCONNECTED:
	case DEVMGR_EVENT_DFP_DISCONNECTED:
		if (pe->cur_prole == POWER_ROLE_SWAP) {
			/* This disconnect event is due to pwr role swap.
			 * Hence ignore it.
			 */
			 pr_info("PE:%s: Disconnect evt during role swap\n",
					__func__);
			 break;
		}
		pe_set_power_role(pe, POWER_ROLE_NONE);
		pe_set_data_role(pe, DATA_ROLE_NONE);
		/* Stop all active policies */
		list_for_each_entry(p, &pe->policy_list, list) {
			if (p && (p->state == POLICY_STATE_ONLINE))
				p->stop(p);
		}
		break;
	case DEVMGR_EVENT_PR_SWAP:
		pevt = PE_EVT_SEND_PR_SWAP;
		break;
	case DEVMGR_EVENT_DR_SWAP:
		pevt = PE_EVT_SEND_DR_SWAP;
		break;
	default:
		pr_err("PE: %s Unknown dpm event=%d\n",
			__func__, evt);
	}

	if (pevt != PE_EVT_SEND_NONE)
		pe_send_role_swap_request(pe, pevt);
}

static void pe_dpm_evt_worker(struct work_struct *work)
{
	struct policy_engine *pe =
		container_of(work, struct policy_engine, dpm_evt_work);
	struct pe_dpm_evt *evt, *tmp;
	struct list_head new_list;

	if (list_empty(&pe->dpm_evt_queue))
		return;

	mutex_lock(&pe->dpm_evt_lock);
	list_replace_init(&pe->dpm_evt_queue, &new_list);
	mutex_unlock(&pe->dpm_evt_lock);

	list_for_each_entry_safe(evt, tmp, &new_list, node) {
		pe_handle_dpm_event(pe, evt->evt);
		kfree(evt);
	}
}

static int pe_dpm_notification(struct policy_engine *pe,
				enum devpolicy_mgr_events evt)
{
	struct pe_dpm_evt *dpm_evt;

	dpm_evt = kzalloc(sizeof(*dpm_evt), GFP_KERNEL);
	if (!dpm_evt) {
		pr_err("PE: failed to allocate memory for dpm event\n");
		return -ENOMEM;
	}

	dpm_evt->evt = evt;

	mutex_lock(&pe->dpm_evt_lock);
	list_add_tail(&dpm_evt->node, &pe->dpm_evt_queue);
	mutex_unlock(&pe->dpm_evt_lock);
	queue_work(system_nrt_wq, &pe->dpm_evt_work);

	return 0;
}




static struct pe_operations ops = {
	.get_snkpwr_cap = pe_get_snkpwr_cap,
	.get_snkpwr_caps = pe_get_snkpwr_caps,
	.get_srcpwr_cap = pe_get_srcpwr_cap,
	.get_max_snkpwr_cap = pe_get_max_snkpwr_cap,
	.get_data_role = pe_get_data_role,
	.get_power_role = pe_get_power_role,
	.set_data_role = pe_set_data_role,
	.set_power_role = pe_set_power_role,
	.set_charger_mode = pe_set_charger_mode,
	.update_charger_ilim = pe_update_charger_ilim,
	.get_min_snk_current = pe_get_min_snk_current,
	.is_pr_swap_support = pe_is_pr_swap_support,
	.send_packet = pe_send_packet,
	.get_cable_state = pe_get_cable_state,
	.get_pd_state = pe_get_pd_state,
	.set_pd_state = pe_set_pd_state,
	.switch_policy = pe_switch_policy,
	.process_data_msg = policy_engine_process_data_msg,
	.process_ctrl_msg = policy_engine_process_ctrl_msg,
	.process_cmd = policy_engine_process_cmd,
	.policy_status_changed = pe_policy_status_changed,
	.notify_dpm_evt = pe_dpm_notification,
};

int policy_engine_bind_dpm(struct devpolicy_mgr *dpm)
{
	int retval;
	struct policy_engine *pe;

	if (!dpm)
		return -EINVAL;

	pe = devm_kzalloc(dpm->phy->dev, sizeof(struct policy_engine),
				GFP_KERNEL);
	if (!pe)
		return -ENOMEM;

	pe->ops = &ops;
	pe->dpm = dpm;
	pe->supported_policies = dpm->policy;

	retval = protocol_bind_pe(pe);
	if (retval) {
		pr_err("PE: failed to bind pe to protocol\n");
		retval = -EINVAL;
		goto bind_error;
	}
	pe->cur_drole = DATA_ROLE_NONE;
	pe->cur_prole = POWER_ROLE_NONE;
	INIT_WORK(&pe->policy_init_work, pe_init_policy);

	INIT_WORK(&pe->dpm_evt_work, pe_dpm_evt_worker);
	INIT_LIST_HEAD(&pe->dpm_evt_queue);
	mutex_init(&pe->dpm_evt_lock);

	mutex_init(&pe->pe_lock);
	INIT_LIST_HEAD(&pe->policy_list);
	dpm->pe = pe;

	schedule_work(&pe->policy_init_work);

	return 0;

bind_error:
	kfree(pe);
	return retval;
}
EXPORT_SYMBOL_GPL(policy_engine_bind_dpm);

void policy_engine_unbind_dpm(struct devpolicy_mgr *dpm)
{
	struct policy_engine *pe = dpm->pe;
	struct policy *p;
	struct pe_dpm_evt *evt, *tmp;
	struct pd_policy *supported_policy;
	struct list_head tmp_list;
	int i;

	mutex_lock(&pe->pe_lock);
	/* remove the pe ops to avoid further external
	 * notifications and callbacks.
	 */
	pe->ops = NULL;

	/* Exit all sub policy engines */
	supported_policy = pe->supported_policies;
	for (i = 0; i < supported_policy->num_policies; i++) {
		p = __pe_find_policy(&pe->policy_list,
					supported_policy->policies[i]);
		p->exit(p);
	}
	/* Unbind from protocol layer */
	protocol_unbind_pe(pe);
	mutex_unlock(&pe->pe_lock);

	/* Clear dpm event list */
	mutex_lock(&pe->dpm_evt_lock);
	if (list_empty(&pe->dpm_evt_queue)) {
		mutex_unlock(&pe->dpm_evt_lock);
		goto unbind_dpm_out;
	}

	list_replace_init(&pe->dpm_evt_queue, &tmp_list);
	mutex_unlock(&pe->dpm_evt_lock);

	list_for_each_entry_safe(evt, tmp, &tmp_list, node) {
		/* Free the event */
		kfree(evt);
	}

unbind_dpm_out:
	kfree(pe);
}
EXPORT_SYMBOL_GPL(policy_engine_unbind_dpm);

MODULE_AUTHOR("Albin B <albin.bala.krishnan@intel.com>");
MODULE_DESCRIPTION("PD Policy Engine Core");
MODULE_LICENSE("GPL v2");
