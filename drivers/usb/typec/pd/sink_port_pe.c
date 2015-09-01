/*
 * pd_sink_pe.c: Intel USB Power Delivery Sink Port Policy Engine
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
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/kfifo.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include "policy_engine.h"
#include "sink_port_pe.h"

static int snkpe_start(struct sink_port_pe *sink);
static int snkpe_handle_select_capability_state(struct sink_port_pe *sink,
							struct pd_packet *pkt);
static int snkpe_handle_give_snk_cap_state(struct sink_port_pe *sink);
static void sink_port_policy_exit(struct policy *p);

static inline void snkpe_update_state(struct sink_port_pe *sink,
					enum pe_states cur_state)
{
	if (!sink)
		return;

	mutex_lock(&sink->snkpe_state_lock);
	sink->cur_state = cur_state;
	mutex_unlock(&sink->snkpe_state_lock);
}

static void snkpe_reset_params(struct sink_port_pe *sink)
{
	/* By default dual power role supported*/
	sink->pp_is_dual_prole = 1;
	/* By default dual data role supported*/
	sink->pp_is_dual_drole = 1;
	sink->pp_is_ext_pwrd = 0;
}

static int snkpe_get_req_cap(struct sink_port_pe *sink,
					struct pd_packet *pkt,
					struct power_cap *pcap,
					struct req_cap *rcap)
{
	int num_data_obj = PD_MSG_NUM_DATA_OBJS(&pkt->header);
	int i;
	int mv = 0;
	int ma = 0;
	bool is_mv_match = false;

	rcap->cap_mismatch = true;

	for (i = 0; i < num_data_obj; i++) {
		/**
		 * FIXME: should be selected based on the power (V*I) cap.
		 */
		mv = DATA_OBJ_TO_VOLT(pkt->data_obj[i]);
		if (mv == pcap->mv) {
			is_mv_match = true;
			ma = DATA_OBJ_TO_CURRENT(pkt->data_obj[i]);
			if (ma == pcap->ma) {
				rcap->cap_mismatch = false;
				break;
			} else if (ma > pcap->ma) {
				/* if the ma in the pdo is greater than the
				 * required ma, exit from the loop as the pdo
				 * capabilites are in ascending order */
				break;
			}
		} else if (mv > pcap->mv) {
			/* if the mv value in the pdo is greater than the
			 * required mv, exit from the loop as the pdo
			 * capabilites are in ascending order */
			break;
		}
	}

	if (!is_mv_match) {
		rcap->cap_mismatch = false;
		i = 0; /* to select 1st pdo, Vsafe5V */
	}

	if (!rcap->cap_mismatch)
		rcap->obj_pos = i + 1; /* obj pos always starts from 1 */
	else /* if cur is not match, select the previous pdo */
		rcap->obj_pos = i;

	rcap->mv = DATA_OBJ_TO_VOLT(pkt->data_obj[rcap->obj_pos - 1]);
	rcap->ma = DATA_OBJ_TO_CURRENT(pkt->data_obj[rcap->obj_pos - 1]);

	return 0;
}

static int snkpe_create_reqmsg(struct sink_port_pe *sink,
					struct pd_packet *pkt, u32 *data)
{
	struct pd_fixed_var_rdo *rdo = (struct pd_fixed_var_rdo *)data;
	struct req_cap *rcap = &sink->rcap;
	struct power_cap dpm_suggested_cap;
	int ret;

	ret = policy_get_snkpwr_cap(&sink->p, &dpm_suggested_cap);
	if (ret) {
		pr_err("SNKPE: Error in getting max sink pwr cap %d\n",
				ret);
		goto error;
	}

	ret = snkpe_get_req_cap(sink, pkt, &dpm_suggested_cap, rcap);
	if (ret < 0) {
		pr_err("SNKPE: Unable to get the Sink Port PE cap\n");
		goto error;
	}

	rdo->obj_pos = rcap->obj_pos;
	rdo->cap_mismatch = rcap->cap_mismatch;
	rdo->op_cur = CURRENT_TO_DATA_OBJ(rcap->ma);
	rdo->max_cur = rdo->op_cur;

	return 0;

error:
	return ret;
}

static int snkpe_send_pr_swap_accept(struct sink_port_pe *sink)
{
	snkpe_update_state(sink, PE_PRS_SNK_SRC_ACCEPT_PR_SWAP);
	return policy_send_packet(&sink->p, NULL, 0,
					PD_CTRL_MSG_ACCEPT, PE_EVT_SEND_ACCEPT);
}

static int snkpe_send_pr_swap_reject(struct sink_port_pe *sink)
{
	snkpe_update_state(sink, PE_SNK_READY);
	return policy_send_packet(&sink->p, NULL, 0,
				PD_CTRL_MSG_REJECT, PE_EVT_SEND_REJECT);
}

static int snkpe_handle_pr_swap(struct sink_port_pe *sink)
{
	enum pwr_role prole;
	int ret = 0;

	snkpe_update_state(sink, PE_PRS_SNK_SRC_EVALUATE_PR_SWAP);
	/* If port partner is externally powered, power role swap from
	 * sink to source can be rejected.
	 */
	if (sink->pp_is_ext_pwrd || (!sink->pp_is_dual_prole)) {
		pr_info("SNKPE:%s: Not processing PR_SWAP Req\n",
				__func__);
		goto pr_swap_reject;
	}
	prole = policy_get_power_role(&sink->p);
	if (prole <= 0) {
		pr_err("SINKPE: Error in getting power role\n");
		goto pr_swap_reject;
	}

	if (prole != POWER_ROLE_SINK) {
		pr_warn("SNKPE: Current Power Role - %d\n", prole);
		goto pr_swap_reject;
	}
	/* As the request to transition to provider mode, It
	 * will be accepted only if VBAT >= 50% else reject.
	 * returns: 1 - accepted, 0 - rejected or error code.
	 */
	ret = policy_is_pr_swap_support(&sink->p, prole);
	if (ret == 0) {
		pr_warn("SNKPE: Batt cap < 50\n");
		goto pr_swap_reject;
	}

	pr_debug("SNKPE:%s: Accepting pr_swap\n", __func__);
	return snkpe_send_pr_swap_accept(sink);

pr_swap_reject:
	pr_debug("SNKPE:%s: Rejecting pr_swap\n", __func__);
	return snkpe_send_pr_swap_reject(sink);
}

static inline int snkpe_do_prot_reset(struct sink_port_pe *sink)
{
	return	policy_send_packet(&sink->p, NULL, 0, PD_CMD_PROTOCOL_RESET,
					 PE_EVT_SEND_PROTOCOL_RESET);
}

static void snkpe_reinitialize_completion(struct sink_port_pe *sink)
{
	reinit_completion(&sink->wct_complete);
	reinit_completion(&sink->srt_complete);
	reinit_completion(&sink->pstt_complete);
	reinit_completion(&sink->sat_complete);
	reinit_completion(&sink->pssoff_complete);
}

/* This function will set the role to POWER_ROLE_SWAP, disable charging
 * and schedule worker to wait for ps_rdy after accepting the pr_swap.
 */
static int snkpe_handle_pss_transition_to_off(struct sink_port_pe *sink)
{
	int ret;

	snkpe_update_state(sink, PE_PRS_SNK_SRC_TRANSITION_TO_OFF);

	ret = policy_set_power_role(&sink->p, POWER_ROLE_SWAP);
	if (ret < 0) {
		pr_err("SNKPE: Error in setting POWER_ROLE_SWAP (%d)\n", ret);
		goto trans_to_off_err;
	}
	schedule_work(&sink->timer_work);
	return 0;

trans_to_off_err:
	/* Move to PE_SNK_Hard_Reset state */
	snkpe_update_state(sink, PE_SNK_HARD_RESET);
	schedule_work(&sink->timer_work);
	return ret;
}

static void snkpe_handle_send_swap(struct sink_port_pe *sink)
{
	int ret;

	ret = wait_for_completion_timeout(&sink->srt_complete,
				msecs_to_jiffies(TYPEC_SENDER_RESPONSE_TIMER));

	if (ret == 0) {
		pr_warn("SNKPE: %s sender response expired\n", __func__);
		snkpe_update_state(sink, PE_SNK_READY);
		goto send_swap_out;
	}
	/* Either accepr or reject received */
	switch (sink->cur_state) {
	case PE_PRS_SNK_SRC_SEND_PR_SWAP:
		if (sink->last_pkt == PE_EVT_RCVD_ACCEPT) {
			pr_debug("SNKPE:%s: PR_Swap accepted\n", __func__);
			/* PR_SWAP accepted, transition to sink off*/
			snkpe_handle_pss_transition_to_off(sink);
		} else {
			pr_debug("SNKPE:%s: PR_Swap not accepted\n", __func__);
			/* PR_SWAP not accepted, go to ready state*/
			snkpe_update_state(sink, PE_SNK_READY);
		}
		break;
	case PE_EVT_SEND_DR_SWAP:
		/* TODO: handle for data role swap */
		break;
	default:
		pr_warn("SNKPE:%s: unexpected state=%d\n", __func__,
						sink->cur_state);
	}

send_swap_out:
	reinit_completion(&sink->srt_complete);
}

static void snkpe_handle_dr_swap_transition(struct sink_port_pe *sink,
			enum data_role to_role)
{
	int ret;

	if (to_role == DATA_ROLE_UFP)
		snkpe_update_state(sink, PE_DRS_DFP_UFP_CHANGE_TO_UFP);
	else
		snkpe_update_state(sink, PE_DRS_UFP_DFP_CHANGE_TO_DFP);

	pr_debug("SNKPE:%s:Changing data role to %d", __func__, to_role);
	ret = policy_set_data_role(&sink->p, to_role);
	if (ret) {
		pr_err("SNKPE:%s:Failed to change the data role\n", __func__);
		/*Reset pe as role swap failed*/
		/* Move to PE_SNK_Hard_Reset state */
		snkpe_update_state(sink, PE_SNK_HARD_RESET);
		schedule_work(&sink->timer_work);
		return;
	}
	pe_notify_policy_status_changed(&sink->p,
			POLICY_TYPE_SINK, PE_STATUS_CHANGE_DR_CHANGED);
	pr_debug("SNKPE:%s:Data role changed to %d", __func__, to_role);
	snkpe_update_state(sink, PE_SNK_READY);
}

static void snkpe_handle_after_dr_swap_sent(struct sink_port_pe *sink)
{
	unsigned long timeout;
	int ret;

	/* Initialize and run SenderResponseTimer */
	timeout = msecs_to_jiffies(TYPEC_SENDER_RESPONSE_TIMER);
	/* unblock this once Accept msg received by checking the
	 * cur_state */
	ret = wait_for_completion_timeout(&sink->srt_complete, timeout);
	if (ret == 0) {
		pr_err("SNKPE:%s:SRT time expired, Move to READY\n",
					__func__);
		snkpe_update_state(sink, PE_SNK_READY);
		goto dr_sent_error;
	}

	if (sink->last_pkt != PE_EVT_RCVD_ACCEPT) {
		pr_info("SNKPE:%s:DR swap not accepted!!\n", __func__);
		snkpe_update_state(sink, PE_SNK_READY);
		goto dr_sent_error;
	}
	pr_debug("SNKPE:%s:DR swap accepted by port partner\n", __func__);
	if (sink->cur_state == PE_DRS_DFP_UFP_SEND_DR_SWAP)
		snkpe_handle_dr_swap_transition(sink, DATA_ROLE_UFP);
	else if (sink->cur_state == PE_DRS_UFP_DFP_SEND_DR_SWAP)
		snkpe_handle_dr_swap_transition(sink, DATA_ROLE_DFP);
	else
		pr_err("SNKPE:%s:Unexpected state=%d !!!\n",
					__func__, sink->cur_state);

dr_sent_error:
	reinit_completion(&sink->srt_complete);
	return;
}

static int snkpe_handle_trigger_dr_swap(struct sink_port_pe *sink)
{
	enum data_role drole;

	drole = policy_get_data_role(&sink->p);

	if (sink->cur_state != PE_SNK_READY
		|| (drole != DATA_ROLE_UFP && drole != DATA_ROLE_DFP)) {
		pr_warn("SNKPE:%s:Not processing DR_SWAP request in state=%d",
				__func__, sink->cur_state);
		return -EINVAL;
	}

	if (drole == DATA_ROLE_DFP)
		snkpe_update_state(sink, PE_DRS_DFP_UFP_SEND_DR_SWAP);
	else
		snkpe_update_state(sink, PE_DRS_UFP_DFP_SEND_DR_SWAP);
	schedule_work(&sink->timer_work);

	policy_send_packet(&sink->p, NULL, 0,
			PD_CTRL_MSG_DR_SWAP, PE_EVT_SEND_DR_SWAP);

	return 0;
}

static void snkpe_handle_after_dr_swap_accept(struct sink_port_pe *sink)
{
	unsigned long timeout;
	int ret;

	/* Initialize and run SenderResponseTimer */
	timeout = msecs_to_jiffies(TYPEC_SENDER_RESPONSE_TIMER);
	/* unblock this once Accept msg received by checking the
	 * cur_state */
	ret = wait_for_completion_timeout(&sink->srt_complete, timeout);
	if (ret == 0) {
		pr_err("SNKPE:%s:SRT time expired, move to RESET\n", __func__);
		/*Reset pe as role swap failed*/
		snkpe_update_state(sink, PE_SNK_HARD_RESET);
		schedule_work(&sink->timer_work);
		goto swap_accept_error;
	}

	if (sink->cur_state == PE_DRS_DFP_UFP_ACCEPT_DR_SWAP)
		snkpe_handle_dr_swap_transition(sink, DATA_ROLE_UFP);
	else if (sink->cur_state == PE_DRS_UFP_DFP_ACCEPT_DR_SWAP)
		snkpe_handle_dr_swap_transition(sink, DATA_ROLE_DFP);
	else
		pr_err("SNKPE:%s:Unexpected state=%d !!!\n",
				__func__, sink->cur_state);

swap_accept_error:
	reinit_completion(&sink->srt_complete);
}

static void snkpe_handle_rcv_dr_swap(struct sink_port_pe *sink)
{
	enum data_role drole;

	drole = policy_get_data_role(&sink->p);

	if (sink->cur_state != PE_SNK_READY
		|| (drole != DATA_ROLE_UFP && drole != DATA_ROLE_DFP)) {
		pr_debug("SNKPE:%s:Not processing DR_SWAP request in state=%d",
				__func__, sink->cur_state);
		policy_send_packet(&sink->p, NULL, 0,
			PD_CTRL_MSG_REJECT, PE_EVT_SEND_REJECT);
		return;
	}

	if (drole == DATA_ROLE_DFP)
		snkpe_update_state(sink, PE_DRS_DFP_UFP_ACCEPT_DR_SWAP);
	else
		snkpe_update_state(sink, PE_DRS_UFP_DFP_ACCEPT_DR_SWAP);
	schedule_work(&sink->timer_work);

	policy_send_packet(&sink->p, NULL, 0,
			PD_CTRL_MSG_ACCEPT, PE_EVT_SEND_ACCEPT);

}

static void snkpe_received_msg_good_crc(struct sink_port_pe *sink)
{

	switch (sink->cur_state) {
	case PE_SNK_SOFT_RESET:
		if (sink->last_pkt == PE_EVT_RCVD_ACCEPT) {
			snkpe_update_state(sink, PE_SNK_STARTUP);
			schedule_work(&sink->timer_work);
		}
		break;
	case PE_SNK_SELECT_CAPABILITY:
		schedule_work(&sink->timer_work);
		break;
	case PE_PRS_SNK_SRC_ACCEPT_PR_SWAP:
		snkpe_handle_pss_transition_to_off(sink);
		break;
	case PE_PRS_SNK_SRC_SOURCE_ON:
		pr_debug("SNKPE:%s: Notifying power role chnage\n", __func__);
		pe_notify_policy_status_changed(&sink->p,
			POLICY_TYPE_SINK, PE_STATUS_CHANGE_PR_CHANGED);
		break;
	case PE_DRS_DFP_UFP_ACCEPT_DR_SWAP:
	case PE_DRS_UFP_DFP_ACCEPT_DR_SWAP:
		complete(&sink->srt_complete);
		break;
	default:
		pr_debug("SNKPE: Recved GOODCRC in %d state\n",
							sink->cur_state);
		break;
	}
}

static int sink_port_policy_rcv_request(struct policy *p, enum pe_event evt)
{
	struct sink_port_pe *sink = container_of(p,
					struct sink_port_pe, p);

	switch (evt) {
	case PE_EVT_SEND_PR_SWAP:
		if (sink->pp_is_ext_pwrd || (!sink->pp_is_dual_prole)
			|| (sink->cur_state != PE_SNK_READY)) {
			pr_info("SNKPE:%s: Not processing PR_SWAP Req\n",
					__func__);
			break;
		}
		snkpe_update_state(sink, PE_PRS_SNK_SRC_SEND_PR_SWAP);
		policy_send_packet(&sink->p, NULL, 0,
				PD_CTRL_MSG_PR_SWAP, evt);
		/* work schedule for rcv good crc for PR_SWAP and
		 * receive Accept/reject */
		schedule_work(&sink->timer_work);
		break;
	case PE_EVT_SEND_DR_SWAP:
		snkpe_handle_trigger_dr_swap(sink);
		break;
	default:
		break;
	}

	return 0;
}

static void sink_handle_src_cap(struct sink_port_pe *sink,
					struct pd_packet *pkt)
{
	snkpe_update_state(sink, PE_SNK_EVALUATE_CAPABILITY);

	sink->hard_reset_count = 0;
	if (timer_pending(&sink->no_response_timer))
		del_timer_sync(&sink->no_response_timer);
	sink->no_response_timer_expired = false;

	policy_set_pd_state(&sink->p, true);

	snkpe_handle_select_capability_state(sink, pkt);

}

static void sink_handle_accept_reject_wait(struct sink_port_pe *sink,
					enum pe_event evt)
{
	switch (evt) {
	case PE_EVT_RCVD_ACCEPT:
		snkpe_update_state(sink, PE_SNK_TRANSITION_SINK);
		break;
	case PE_EVT_RCVD_REJECT:
	case PE_EVT_RCVD_WAIT:
		snkpe_update_state(sink, PE_SNK_READY);
		break;
	default:
		pr_warn("SNKPE: recvd (%d) in select cap\n", evt);
		goto end;
	}
	schedule_work(&sink->timer_work);
end:
	return;
}

static int snkpe_setup_charging(struct sink_port_pe *sink)
{
	int ret = 0;

	pr_debug("SNKPE:%s In\n", __func__);
	/* Update the charger input current limit */
	ret = policy_update_charger_ilim(&sink->p, sink->rcap.ma);
	if (ret < 0) {
		pr_err("SNKPE: Error in updating charger ilim (%d)\n",
				ret);
		return ret;
	}

	/* Enable charger */
	ret = policy_set_charger_mode(&sink->p, CHRGR_ENABLE);
	if (ret < 0)
		pr_err("SNKPE: Error in enabling charger (%d)\n", ret);
	else
		pr_info("SNKPE: Consumer Policy Negotiation Success!\n");
	return ret;
}

/* This function will read the port partner capabilities and
 * save it for further use.
 */
static void snkpe_read_src_caps(struct sink_port_pe *sink,
					struct pd_packet *pkt)
{
	struct pd_fixed_supply_pdo *pdo =
			(struct pd_fixed_supply_pdo *) &pkt->data_obj[0];

	if (pdo->fixed_supply != SUPPLY_TYPE_FIXED) {
		pr_debug("SNKPE:%s: source is not fixed supply\n",
					__func__);
		return;
	}
	sink->pp_is_dual_prole = pdo->dual_role_pwr;
	sink->pp_is_dual_drole = pdo->data_role_swap;
	sink->pp_is_ext_pwrd = pdo->ext_powered;

	pr_debug("SNKPE:%s:dual_prole=%d, dual_drole=%d, ext_pwrd=%d",
			__func__, sink->pp_is_dual_prole,
			sink->pp_is_dual_drole,	sink->pp_is_ext_pwrd);
}

static int snkpe_handle_select_capability_state(struct sink_port_pe *sink,
							struct pd_packet *pkt)
{
	int ret = 0;
	u32 data = 0;

	/* move the next state PE_SNK_Select_Capability */
	snkpe_update_state(sink, PE_SNK_SELECT_CAPABILITY);

	snkpe_read_src_caps(sink, pkt);
	/* make request message and send to PE -> protocol */
	ret = snkpe_create_reqmsg(sink, pkt, &data);
	if (ret < 0) {
		pr_err("SNKPE: Error in getting message!\n");
		goto error;
	}

	ret = policy_send_packet(&sink->p, &data, 4, PD_DATA_MSG_REQUEST,
							PE_EVT_SEND_REQUEST);
	if (ret < 0) {
		pr_err("SNKPE: Error in sending packet!\n");
		goto error;
	}
	pr_debug("SNKPE: PD_DATA_MSG_REQUEST Sent\n");

	/* Keeping backup to use later if required for wait event and
	 * sink request timer timeout */
	memcpy(&sink->prev_pkt, pkt, sizeof(struct pd_packet));

error:
	return ret;
}

static int snkpe_handle_give_snk_cap_state(struct sink_port_pe *sink)
{
	int ret = 0;
	int i;
	struct power_caps pcaps;
	struct pd_sink_fixed_pdo pdo[MAX_NUM_DATA_OBJ] = { {0} };

	snkpe_update_state(sink, PE_SNK_GIVE_SINK_CAP);

	ret = policy_get_snkpwr_caps(&sink->p, &pcaps);
	if (ret < 0)
		goto error;

	for (i = 0; i < MAX_NUM_DATA_OBJ; i++) {
		if (i >= pcaps.n_cap)
			break;

		pdo[i].max_cur = CURRENT_TO_DATA_OBJ(pcaps.pcap[i].ma);
		pdo[i].volt = (VOLT_TO_DATA_OBJ(pcaps.pcap[i].mv) >>
					SNK_FSPDO_VOLT_SHIFT);
		/* FIXME: get it from dpm once the dpm provides the caps */
		pdo[i].data_role_swap = 1;
		pdo[i].usb_comm = 1;
		pdo[i].ext_powered = 0;
		pdo[i].higher_cap = 0;
		pdo[i].dual_role_pwr = 1;
		pdo[i].supply_type = 0;
	}

	ret = policy_send_packet(&sink->p, pdo, pcaps.n_cap * 4,
					PD_DATA_MSG_SINK_CAP,
					PE_EVT_SEND_SNK_CAP);
	if (ret < 0) {
		pr_err("SNKPE: Error in sending packet!\n");
		goto error;
	}
	pr_debug("SNKPE: PD_DATA_MSG_SINK_CAP sent\n");

error:
	snkpe_update_state(sink, PE_SNK_READY);
	return ret;
}

/* After accepting the pr_swap and disabling the charging
 * (in PE_PRS_SNK_SRC_TRANSITION_TO_OFF state), this function
 * will wait for ps_rdy from source with timeout. On timeout,
 * pe will move to hard reset state. If ps_rdy received on-time
 * then move to source mode.
 */
static void snkpe_handle_pss_transition_off(struct sink_port_pe *sink)
{
	int ret;
	unsigned long timeout;

	/* initialize and run the PSSourceOffTimer */
	timeout = msecs_to_jiffies(TYPEC_PS_SRC_OFF_TIMER);
	/* unblock this once PS_RDY msg received by checking the
	 * cur_state */
	ret = wait_for_completion_timeout(&sink->pssoff_complete,
						timeout);
	if (!ret)
		goto trans_off_err;

	pr_info("SNKPE: Received PS_RDY\n");
	/* Pull-up CC (enable Rp) and Vbus 5V enable */
	ret = policy_set_power_role(&sink->p, POWER_ROLE_SOURCE);
	if (ret)
		goto trans_off_err;

	/* SourceActivityTimer (40 - 50mSec) is not used to monitor source
	 * activity. Assuming the source activity can be done within the time
	 */
	snkpe_update_state(sink, PE_PRS_SNK_SRC_SOURCE_ON);
	policy_send_packet(&sink->p, NULL, 0,
			PD_CTRL_MSG_PS_RDY, PE_EVT_SEND_PS_RDY);

	return;

trans_off_err:
	pr_err("SNKPE: Error in pss_transition_off %d\n", ret);
	/* Move to PE_SNK_Hard_Reset state */
	snkpe_update_state(sink, PE_SNK_HARD_RESET);
	schedule_work(&sink->timer_work);
	return;
}

static void snkpe_handle_startup(struct sink_port_pe *sink)
{
	int ret;
	unsigned long timeout =
		msecs_to_jiffies(TYPEC_SINK_WAIT_CAP_TIMER);

	if (sink->cur_state == PE_SNK_STARTUP ||
		sink->cur_state == PE_SNK_DISCOVERY) {
		snkpe_update_state(sink, PE_SNK_WAIT_FOR_CAPABILITIES);

		/* Initialize and run SinkWaitCapTimer */
		/* unblock this once source cap rcv by checking the cur_state */
		ret = wait_for_completion_timeout(&sink->wct_complete, timeout);
		if (ret == 0) {
			snkpe_update_state(sink, PE_SNK_HARD_RESET);
			schedule_work(&sink->timer_work);
		}
	}

	reinit_completion(&sink->wct_complete);
}

static void snkpe_handle_sink_hard_reset(struct sink_port_pe *sink)
{
	pr_warn("SNKPE: transitioning to hard reset\n");
	/* send hard reset */
	sink->hard_reset_complete = false;
	policy_send_packet(&sink->p, NULL, 0, PD_CMD_HARD_RESET,
			PE_EVT_SEND_HARD_RESET);

	/* increment counter */
	sink->hard_reset_count++;
	/* wait for hardrst complete */
	wait_event(sink->wq, sink->hard_reset_complete);

	if ((sink->last_pkt == PE_EVT_RCVD_HARD_RESET) ||
		(sink->last_pkt == PE_EVT_RCVD_SOFT_RESET)) {
		/* this will move the state to SNK_STARTUP */
		sink->hard_reset_count = 0;
		return;
	}

	if (!timer_pending(&sink->no_response_timer))
		mod_timer(&sink->no_response_timer,
				msecs_to_jiffies(TYPEC_NO_RESPONSE_TIMER));

	snkpe_update_state(sink, PE_SNK_STARTUP);
	schedule_work(&sink->timer_work);
}

static void sink_handle_select_cap(struct sink_port_pe *sink)
{
	int ret;

	sink->resend_cap = false;
	ret = wait_for_completion_timeout(&sink->srt_complete,
				msecs_to_jiffies(TYPEC_SENDER_RESPONSE_TIMER));

	if (ret == 0) {
		pr_warn("SNKPE: %s sender response expired\n", __func__);
		snkpe_update_state(sink, PE_SNK_HARD_RESET);
		schedule_work(&sink->timer_work);
	}
	reinit_completion(&sink->srt_complete);
}

static void sink_handle_ready(struct sink_port_pe *sink)
{
	pr_debug("SNKPE: %s: last_pkt = %d cur_state %d\n", __func__,
			sink->last_pkt, sink->cur_state);
	if (sink->last_pkt == PE_EVT_RCVD_WAIT) {
		pr_info("SNKPE:%s: wait\n", __func__);
		schedule_work(&sink->request_timer);
		goto ready_end;
	}
	if (sink->cur_state == PE_SNK_TRANSITION_SINK)
		snkpe_setup_charging(sink);

ready_end:
	sink->p.status = POLICY_STATUS_SUCCESS;
	snkpe_update_state(sink, PE_SNK_READY);
	pe_notify_policy_status_changed(&sink->p,
			POLICY_TYPE_SINK, PE_STATUS_CHANGE_PD_SUCCESS);
}

static void sink_handle_transition_sink(struct sink_port_pe *sink)
{
	int ret;

	pr_debug("SNKPE:%s: in\n", __func__);
	policy_set_charger_mode(&sink->p, CHRGR_SET_HZ);
	ret = wait_for_completion_timeout(&sink->pstt_complete,
				msecs_to_jiffies(TYPEC_PS_TRANSITION_TIMER));

	if (ret == 0) {
		pr_warn("SNKPE: %s PSTransition expired\n", __func__);
		policy_set_charger_mode(&sink->p, CHRGR_ENABLE);
		snkpe_update_state(sink, PE_SNK_HARD_RESET);
		schedule_work(&sink->timer_work);
		goto trans_sink_end;
	}

	/* PS_RDY received, handle it*/
	sink_handle_ready(sink);

trans_sink_end:
	reinit_completion(&sink->pstt_complete);
}

/* wait for request timer expired to restart the request */
static void sink_request_timer_work(struct work_struct *work)
{
	struct sink_port_pe *sink = container_of(work,
					struct sink_port_pe,
					request_timer);

	if (timer_pending(&sink->snk_request_timer))
		del_timer_sync(&sink->snk_request_timer);

	mod_timer(&sink->snk_request_timer,
				msecs_to_jiffies(TYPEC_SINK_REQUEST_TIMER));

	sink->request_timer_expired = false;

	wait_event(sink->wq_req, sink->request_timer_expired);

	if ((sink->last_pkt != PE_EVT_RCVD_HARD_RESET) ||
		(sink->last_pkt != PE_EVT_RCVD_SOFT_RESET)) {
		snkpe_update_state(sink, PE_SNK_SELECT_CAPABILITY);
		sink->resend_cap = true;
		schedule_work(&sink->timer_work);
	} else
		sink->request_timer_expired = false;
}

static void sink_request_timer(unsigned long data)
{
	struct sink_port_pe *sink = (struct sink_port_pe *) data;

	sink->request_timer_expired = true;
	wake_up(&sink->wq_req);
}

static void sinkpe_no_response_timer(unsigned long data)
{
	struct sink_port_pe *sink_pe = (struct sink_port_pe *) data;

	sink_pe->no_response_timer_expired  = true;

	schedule_work(&sink_pe->timer_work);
}

static void sinkpe_handle_error_recovery(struct sink_port_pe *sink)
{
	snkpe_update_state(sink, ERROR_RECOVERY);
	policy_send_packet(&sink->p, NULL, 0, PD_CMD_PROTOCOL_RESET,
				PE_EVT_SEND_PROTOCOL_RESET);
	sink->no_response_timer_expired = false;
	pr_err("SNKPE: No Response timer expired, going to error recovery\n");
	pe_notify_policy_status_changed(&sink->p,
			POLICY_TYPE_SINK, PE_STATUS_CHANGE_PD_FAIL);
}

/* This is the main task worker for sink pe */
static void snkpe_task_worker(struct work_struct *work)
{
	struct sink_port_pe *sink = container_of(work,
					struct sink_port_pe,
					timer_work);

	if (sink->hard_reset_count > HARD_RESET_COUNT_N &&
		sink->no_response_timer_expired) {
		return sinkpe_handle_error_recovery(sink);
	}

	pr_err("SNKPE: %s: state = %d\n", __func__, sink->cur_state);
	switch (sink->cur_state) {
	case PE_SNK_STARTUP:
	case PE_SNK_DISCOVERY:
		sink->last_pkt = 0;
		if (sink->is_sink_cable_connected)
			snkpe_handle_startup(sink);
		break;
	case PE_SNK_HARD_RESET:
		if (sink->hard_reset_count <= HARD_RESET_COUNT_N)
			snkpe_handle_sink_hard_reset(sink);
		else if (sink->hard_reset_count > HARD_RESET_COUNT_N &&
			sink->no_response_timer_expired)
			return sinkpe_handle_error_recovery(sink);
		break;
	case PE_SNK_SELECT_CAPABILITY:
		if (sink->resend_cap)
			snkpe_handle_select_capability_state(sink,
							&sink->prev_pkt);
		sink_handle_select_cap(sink);
		break;
	case PE_SNK_TRANSITION_SINK:
		sink_handle_transition_sink(sink);
		break;
	case PE_PRS_SNK_SRC_TRANSITION_TO_OFF:
		snkpe_handle_pss_transition_off(sink);
		break;
	case PE_PRS_SNK_SRC_SEND_PR_SWAP:
		snkpe_handle_send_swap(sink);
		break;
	case PE_DRS_DFP_UFP_SEND_DR_SWAP:
	case PE_DRS_UFP_DFP_SEND_DR_SWAP:
		snkpe_handle_after_dr_swap_sent(sink);
		break;
	case PE_DRS_DFP_UFP_ACCEPT_DR_SWAP:
	case PE_DRS_UFP_DFP_ACCEPT_DR_SWAP:
		snkpe_handle_after_dr_swap_accept(sink);
		break;
	default:
		pr_warn("SNKPE: got state %d\n", sink->cur_state);
		break;
	}
}

static void sinkpe_reset_timers(struct sink_port_pe *sink)
{
	del_timer_sync(&sink->no_response_timer);
	del_timer_sync(&sink->snk_request_timer);
}

static void sink_do_reset(struct sink_port_pe *pe)
{
	/* complete the pending timers */
	if (!completion_done(&pe->wct_complete))
		complete(&pe->wct_complete);
	if (!completion_done(&pe->srt_complete))
		complete(&pe->srt_complete);
	if (!completion_done(&pe->pstt_complete))
		complete(&pe->pstt_complete);
	if (!completion_done(&pe->pssoff_complete))
		complete(&pe->pssoff_complete); /* PS Source Off timer */

	if (pe->cur_state == PE_SNK_HARD_RESET) {
		pe->hard_reset_complete = true;
		wake_up(&pe->wq);
	}

	if (timer_pending(&pe->snk_request_timer)) {
		del_timer_sync(&pe->snk_request_timer);
		pe->request_timer_expired = true;
		wake_up(&pe->wq_req);
	}

	if (timer_pending(&pe->no_response_timer))
		del_timer_sync(&pe->no_response_timer);

	snkpe_update_state(pe, PE_SNK_STARTUP);
	schedule_work(&pe->timer_work);
}

static void sink_handle_src_hard_reset(struct sink_port_pe *pe)
{

	sink_do_reset(pe);
	/* restart the no response timer */
	mod_timer(&pe->no_response_timer,
				msecs_to_jiffies(TYPEC_NO_RESPONSE_TIMER));
	pe->hard_reset_count = 0;
}

static int sink_port_policy_rcv_cmd(struct policy *p, enum pe_event evt)
{
	struct sink_port_pe *sink = container_of(p,
					struct sink_port_pe, p);
	int ret = 0;

	switch (evt) {
	case PE_EVT_RCVD_HARD_RESET:
		ret = snkpe_do_prot_reset(sink);
		sink->last_pkt = evt;
		sink_handle_src_hard_reset(sink);
		break;
	case PE_EVT_RCVD_HARD_RESET_COMPLETE:
		pr_err("SNKPE: RCVD PE_EVT_RCVD_HARD_RESET_COMPLETE\n");
		sink->hard_reset_complete = true;
		if (sink->cur_state == PE_SNK_HARD_RESET)
			wake_up(&sink->wq);
		else {
			snkpe_do_prot_reset(sink);
			sink_do_reset(sink);
		}
		break;
	case PE_EVT_RCVD_SOFT_RESET:
		ret = snkpe_do_prot_reset(sink);
		sink->last_pkt = evt;
		sink_do_reset(sink);
		policy_send_packet(&sink->p, NULL, 0,
				PD_CTRL_MSG_ACCEPT, PE_EVT_SEND_ACCEPT);
		snkpe_update_state(sink, PE_SNK_SOFT_RESET);
		break;
	default:
		pr_err("SNKPE: %s evt - %d\n", __func__, evt);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int sink_port_policy_rcv_pkt(struct policy *p, struct pd_packet *pkt,
				enum pe_event evt)
{
	struct sink_port_pe *sink = container_of(p,
					struct sink_port_pe, p);

	pr_debug("SNKPE:%s: received evt=%d\n", __func__, evt);
	if (evt != PE_EVT_RCVD_GOODCRC)
		sink->last_pkt = evt;

	switch (evt) {
	case PE_EVT_RCVD_SRC_CAP:
		complete(&sink->wct_complete);
		/* Process ScrcCap if sink pe is waiting for caps */
		if (sink->cur_state == PE_SNK_WAIT_FOR_CAPABILITIES)
			sink_handle_src_cap(sink, pkt);
		break;
	case PE_EVT_RCVD_GET_SINK_CAP:
		if (sink->cur_state == PE_SNK_READY)
			return snkpe_handle_give_snk_cap_state(sink);
		break;
	case PE_EVT_RCVD_ACCEPT:
	case PE_EVT_RCVD_REJECT:
	case PE_EVT_RCVD_WAIT:
		if (sink->cur_state == PE_SNK_SELECT_CAPABILITY) {
			complete(&sink->srt_complete);
			sink_handle_accept_reject_wait(sink, evt);

		} else if (sink->cur_state == PE_SNK_SEND_SOFT_RESET) {
			/* Move to start-up */
			snkpe_update_state(sink, PE_SNK_STARTUP);

		} else if (sink->cur_state == PE_PRS_SNK_SRC_SEND_PR_SWAP
			|| sink->cur_state == PE_DRS_UFP_DFP_SEND_DR_SWAP
			|| sink->cur_state == PE_DRS_DFP_UFP_SEND_DR_SWAP) {
			/* unblock the waiting thread */
			complete(&sink->srt_complete);
		}
		break;
	case PE_EVT_RCVD_PS_RDY:
		/* Unblock the task worker thread waiting for PS_RDY*/
		if (sink->cur_state == PE_SNK_TRANSITION_SINK)
			complete(&sink->pstt_complete);
		else if (sink->cur_state == PE_PRS_SNK_SRC_TRANSITION_TO_OFF)
			complete(&sink->pssoff_complete);
		else
			pr_warn("SNKPE: Got PSRdy in wrong state=%d\n",
					sink->cur_state);
		break;
	case PE_EVT_RCVD_PING:
		break;
	case PE_EVT_RCVD_GOTOMIN:
		snkpe_update_state(sink, PE_SNK_TRANSITION_SINK);
		schedule_work(&sink->timer_work);
		break;
	case PE_EVT_RCVD_GOODCRC:
		snkpe_received_msg_good_crc(sink);
		break;
	case PE_EVT_RCVD_PR_SWAP:
		/* If not SNK_READY dont send accept or reject*/
		if (sink->cur_state == PE_SNK_READY)
			snkpe_handle_pr_swap(sink);
		else
			pr_debug("SNKPE:%s: PR_Swap rcvd in worng state=%d\n",
					__func__, sink->cur_state);
		break;
	case PE_EVT_RCVD_DR_SWAP:
		snkpe_handle_rcv_dr_swap(sink);
		break;
	default:
		pr_warn("SNKPE: Not intersted in (%d) event\n", evt);
		return -EINVAL;
	}

	return 0;
}

static int snkpe_start(struct sink_port_pe *sink)
{
	enum cable_state sink_cable_state;

	pr_debug("SNKPE: %s\n", __func__);

	/*---------- Start of Sink Port PE --------------*/
	/* get the sink_cable_state, in case of boot with cable */
	snkpe_reset_params(sink);
	sink_cable_state = policy_get_cable_state(&sink->p,
					CABLE_TYPE_CONSUMER);
	if (sink_cable_state < 0) {
		pr_err("SNKPE: Error in getting vbus state!\n");
		return sink_cable_state;
	}
	if (sink_cable_state == CABLE_ATTACHED)
		sink->is_sink_cable_connected = true;
	else
		sink->is_sink_cable_connected = false;

	if (!sink->is_sink_cable_connected) {
		mutex_lock(&sink->snkpe_state_lock);
		sink->cur_state = PE_SNK_STARTUP;
		mutex_unlock(&sink->snkpe_state_lock);
		return snkpe_do_prot_reset(sink);
	}

	/* move the state from PE_SNK_STARTUP to PE_SNK_DISCOVERY */
	snkpe_update_state(sink, PE_SNK_DISCOVERY);

	schedule_work(&sink->timer_work);

	return 0;
}

static inline int sink_port_policy_start(struct policy *p)
{
	struct sink_port_pe *sink = container_of(p,
					struct sink_port_pe, p);

	pr_debug("SNKPE: %s\n", __func__);
	mutex_lock(&sink->snkpe_state_lock);
	p->state = POLICY_STATE_ONLINE;
	sink->cur_state = PE_SNK_STARTUP;
	mutex_unlock(&sink->snkpe_state_lock);
	return snkpe_start(sink);
}

static int sink_port_policy_stop(struct policy *p)
{
	struct sink_port_pe *sink = container_of(p,
					struct sink_port_pe, p);

	pr_debug("SNKPE: %s\n", __func__);
	/* reset HardResetCounter to zero upon vbus disconnect.
	 */
	mutex_lock(&sink->snkpe_state_lock);
	sink->hard_reset_count = 0;
	p->status = POLICY_STATUS_UNKNOWN;
	p->state = POLICY_STATE_OFFLINE;
	sink->is_sink_cable_connected = false;
	mutex_unlock(&sink->snkpe_state_lock);
	policy_set_pd_state(p, false);

	/* clear any pending completions */
	sink_do_reset(sink);
	sinkpe_reset_timers(sink);
	cancel_work_sync(&sink->timer_work);
	cancel_work_sync(&sink->request_timer);
	sink->resend_cap = 0;
	sink->last_pkt = 0;
	snkpe_reinitialize_completion(sink);
	snkpe_update_state(sink, PE_SNK_STARTUP);
	mutex_lock(&sink->snkpe_state_lock);
	sink->cur_state = PE_SNK_STARTUP;
	mutex_unlock(&sink->snkpe_state_lock);

	return 0;
}

struct policy *sink_port_policy_init(struct policy_engine *pe)
{
	struct sink_port_pe *snkpe;
	struct policy *p;

	snkpe = kzalloc(sizeof(*snkpe), GFP_KERNEL);
	if (!snkpe)
		return ERR_PTR(-ENOMEM);

	INIT_WORK(&snkpe->timer_work, snkpe_task_worker);
	INIT_WORK(&snkpe->request_timer, sink_request_timer_work);
	init_waitqueue_head(&snkpe->wq);
	init_waitqueue_head(&snkpe->wq_req);

	p = &snkpe->p;
	p->type = POLICY_TYPE_SINK;
	p->state = POLICY_STATE_OFFLINE;
	p->status = POLICY_STATUS_UNKNOWN;
	p->pe = pe;
	p->rcv_request = sink_port_policy_rcv_request;
	p->rcv_pkt = sink_port_policy_rcv_pkt;
	p->rcv_cmd = sink_port_policy_rcv_cmd;
	p->start = sink_port_policy_start;
	p->stop = sink_port_policy_stop;
	p->exit = sink_port_policy_exit;
	setup_timer(&snkpe->no_response_timer, sinkpe_no_response_timer,
					(unsigned long) snkpe);
	setup_timer(&snkpe->snk_request_timer, sink_request_timer,
					(unsigned long) snkpe);

	init_completion(&snkpe->wct_complete);
	init_completion(&snkpe->srt_complete);
	init_completion(&snkpe->pstt_complete);
	init_completion(&snkpe->sat_complete);
	init_completion(&snkpe->pssoff_complete);
	mutex_init(&snkpe->snkpe_state_lock);

	return p;

}
EXPORT_SYMBOL_GPL(sink_port_policy_init);

static void sink_port_policy_exit(struct policy *p)
{
	struct sink_port_pe *snkpe;

	if (p) {
		snkpe = container_of(p, struct sink_port_pe, p);
		kfree(snkpe);
	}
}

MODULE_AUTHOR("Albin B <albin.bala.krishnan@intel.com>");
MODULE_DESCRIPTION("PD Sink Port Policy Engine");
MODULE_LICENSE("GPL v2");
