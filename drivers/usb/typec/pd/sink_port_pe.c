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
#include "policy_engine.h"
#include "sink_port_pe.h"

static inline void snkpe_update_state(struct sink_port_pe *sink,
					enum pe_states cur_state)
{
	if (!sink)
		return;

	mutex_lock(&sink->snkpe_state_lock);
	sink->prev_state = sink->cur_state;
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

static int snkpe_timeout_transition_check(struct sink_port_pe *sink)
{
	int ret = 0;
	enum snkpe_timeout tout = sink->timeout;

	pr_debug("SNKPE: %s tout %d\n", __func__, tout);

	/* ((SinkWaitCapTimer timeout | SinkActivityTimer timeout |
	 * PSTransitionTimer timeout | NoResponseTimer timeout)
	 * & (HardResetCounter <= nHardResetCount)) | Hard Reset request
	 * from Device Policy Manager.
	 * (OR) SENDER_RESPONSE_TIMER timeout.
	 */
	if (((tout == SINK_WAIT_CAP_TIMER || tout == SINK_ACTIVITY_TIMER ||
		tout == PS_TRANSITION_TIMER || tout == NO_RESPONSE_TIMER) &&
		(sink->hard_reset_count <= HARD_RESET_COUNT_N)) ||
		tout == SENDER_RESPONSE_TIMER) {

		/* Move to PE_SNK_Hard_Reset state */
		snkpe_update_state(sink, PE_SNK_HARD_RESET);

		/* FIXME: Not generating hard reset signal now, since vbus is
		 * getting diconnect and after that PD charger is not responding
		 * with source capabilty message.
		 */
		sink->hard_reset_count++;
		/* expect Hard Reset to complete */
	} else if (tout == NO_RESPONSE_TIMER &&
		sink->hard_reset_count > HARD_RESET_COUNT_N) {

		/* FIXME : handle ErrorRecovery state */
		snkpe_update_state(sink, ERROR_RECOVERY);
	}

	return ret;
}

static int snkpe_rxmsg_from_fifo(struct sink_port_pe *sink,
					struct pd_packet *pkt)
{
	int len = 0;
	int ret = 0;

	wait_for_completion(&sink->pktwt_complete);
	len = kfifo_len(&sink->pkt_fifo);
	if (len <= 0) {
		pr_err("SNKPE: Error in getting fifo len %d\n", len);
		ret = -ENODATA;
		goto error;
	}
	ret = kfifo_out(&sink->pkt_fifo, (unsigned char *)pkt, len);
	if (ret != len) {
		pr_warn("SNKPE: pkt size < %d", len);
		ret = -ENODATA;
	}

error:
	reinit_completion(&sink->pktwt_complete);
	return ret;
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
	struct power_cap mpcap;
	int ret;

	ret = policy_get_max_snkpwr_cap(&sink->p, &mpcap);
	if (ret) {
		pr_err("SNKPE: Error in getting max sink pwr cap %d\n",
				ret);
		goto error;
	}

	ret = snkpe_get_req_cap(sink, pkt, &mpcap, rcap);
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

static int snkpe_get_msg(struct sink_port_pe *sink, struct pd_packet *pkt,
				int msg, u32 *data)
{
	int ret = 0;

	switch (msg) {
	case PE_EVT_SEND_REQUEST:
		ret = snkpe_create_reqmsg(sink, pkt, data);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static void snkpe_txmsg_to_fifo(struct sink_port_pe *sink,
					struct pd_packet *pkt)
{
	int len = 0;

	len = PD_MSG_LEN(&pkt->header) + PD_MSG_HEADER_SIZE;
	kfifo_in(&sink->pkt_fifo, (const unsigned char *)pkt, len);
	complete(&sink->pktwt_complete);
}

static int snkpe_handle_gotomin_msg(struct sink_port_pe *sink)
{
	return snkpe_handle_transition_sink_state(sink);
}

static int snkpe_send_pr_swap_accept(struct sink_port_pe *sink)
{
	snkpe_update_state(sink, PE_PRS_SNK_SRC_ACCEPT_PR_SWAP);
	sink->pevt = PE_EVT_SEND_ACCEPT;
	return policy_send_packet(&sink->p, NULL, 0,
					PD_CTRL_MSG_ACCEPT, sink->pevt);
}

static int snkpe_send_pr_swap_reject(struct sink_port_pe *sink)
{
	snkpe_update_state(sink, PE_PRS_SNK_SRC_REJECT_PR_SWAP);
	sink->pevt = PE_EVT_SEND_REJECT;
	return policy_send_packet(&sink->p, NULL, 0,
					PD_CTRL_MSG_REJECT, sink->pevt);
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
	reinit_completion(&sink->nrt_complete);
	reinit_completion(&sink->pstt_complete);
	reinit_completion(&sink->sat_complete);
	reinit_completion(&sink->srqt_complete);
	reinit_completion(&sink->pssoff_complete);
	reinit_completion(&sink->pktwt_complete);
}

static int snkpe_start(struct sink_port_pe *sink)
{
	enum cable_state sink_cable_state;
	int ret = 0;

	pr_debug("SNKPE: %s\n", __func__);
	if (sink->cur_state == PE_SNK_TRANSITION_TO_DEFAULT) {
		complete(&sink->nrt_complete);
		return 0;
	}

	/*---------- Start of Sink Port PE --------------*/
	snkpe_reset_params(sink);
	/* get the sink_cable_state, in case of boot with cable */
	sink_cable_state = policy_get_cable_state(&sink->p,
					CABLE_TYPE_CONSUMER);
	if (sink_cable_state < 0) {
		pr_err("SNKPE: Error in getting vbus state!\n");
		return ret;
	}
	if (sink_cable_state == CABLE_ATTACHED)
		sink->is_sink_cable_connected = true;
	else
		sink->is_sink_cable_connected = false;

	if (sink->is_sink_cable_connected) {
		if (sink->cur_state != PE_SNK_STARTUP)
			return 0;
	} else {
		mutex_lock(&sink->snkpe_state_lock);
		sink->cur_state = PE_SNK_STARTUP;
		mutex_unlock(&sink->snkpe_state_lock);
		return snkpe_do_prot_reset(sink);
	}

	/* move the state from PE_SNK_STARTUP to PE_SNK_DISCOVERY */
	snkpe_update_state(sink, PE_SNK_DISCOVERY);

	/* wait for vbus: get notification from device policy manager
	 * to continue the next state.
	 */
	if (sink->is_sink_cable_connected)
		snkpe_vbus_attached(sink);

	return ret;
}

static inline int sink_port_policy_start(struct policy *p)
{
	struct sink_port_pe *sink = container_of(p,
					struct sink_port_pe, p);

	pr_debug("SNKPE: %s\n", __func__);
	mutex_lock(&sink->snkpe_state_lock);
	p->status = POLICY_STATUS_RUNNING;
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

	cancel_work_sync(&sink->timer_work);
	snkpe_reinitialize_completion(sink);
	/* FIXME: handle the stop state */
	snkpe_do_prot_reset(sink);
	mutex_lock(&sink->snkpe_state_lock);
	sink->cur_state = PE_SNK_STARTUP;
	mutex_unlock(&sink->snkpe_state_lock);

	return 0;
}

static int sink_port_policy_rcv_cmd(struct policy *p, enum pe_event evt)
{
	struct sink_port_pe *sink = container_of(p,
					struct sink_port_pe, p);
	int ret = 0;

	switch (evt) {
	case PE_EVT_RCVD_HARD_RESET:
		ret = snkpe_do_prot_reset(sink);
		sink_port_policy_stop(p);
		snkpe_update_state(sink, PE_SNK_STARTUP);
		sink_port_policy_start(p);
		break;
	case PE_EVT_RCVD_HARD_RESET_COMPLETE:
		return snkpe_handle_transition_to_default(sink);
	default:
		pr_err("SNKPE: %s evt - %d\n", __func__, evt);
		ret = -EINVAL;
		break;
	}

	return ret;
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
	/* request the Device Policy Manager to turn off the Sink
	 * by putting the charger into HiZ mode */
	ret = policy_set_charger_mode(&sink->p, CHRGR_SET_HZ);
	if (ret < 0) {
		pr_err("SNKPE: Error in putting into HiZ mode (%d)\n", ret);
		goto trans_to_off_err;
	}

	schedule_work(&sink->timer_work);
	return 0;

trans_to_off_err:
	/* Move to PE_SNK_Hard_Reset state */
	snkpe_update_state(sink, PE_SNK_HARD_RESET);
	return ret;
}

static int snkpe_received_msg_good_crc(struct sink_port_pe *sink)
{
	int ret = 0;

	if (snkpe_is_cur_state(sink, PE_SNK_SELECT_CAPABILITY) ||
		snkpe_is_cur_state(sink, PE_PRS_SNK_SRC_SEND_PR_SWAP)) {
		schedule_work(&sink->timer_work);
		pr_debug("SNKPE: Received ack for PD_DATA_MSG_REQUEST\n");
	} else if (snkpe_is_cur_state(sink, PE_SNK_GIVE_SINK_CAP) &&
		snkpe_is_prev_evt(sink, PE_EVT_SEND_SNK_CAP)) {
		pr_debug("SNKPE: Received ack for PD_DATA_MSG_SINK_CAP\n");
		return snkpe_handle_snk_ready_state(sink, sink->pevt);
	} else if (snkpe_is_cur_state(sink, PE_PRS_SNK_SRC_REJECT_PR_SWAP)) {
		snkpe_update_state(sink, PE_SNK_READY);
	} else if (snkpe_is_cur_state(sink, PE_PRS_SNK_SRC_ACCEPT_PR_SWAP)) {
		ret = snkpe_handle_pss_transition_to_off(sink);
	} else if (snkpe_is_cur_state(sink, PE_PRS_SNK_SRC_SOURCE_ON) &&
		snkpe_is_prev_evt(sink, PE_EVT_SEND_PS_RDY)) {
		pr_debug("SNKPE: PR_SWAP Authentication Success!\n");
		policy_switch_policy(&sink->p, POLICY_TYPE_SOURCE);
	} else {
		pr_warn("SNKPE: Received Good CRC at state %d pevt %d\n",
					 sink->cur_state, sink->pevt);
		ret = -EINVAL;
	}

	return ret;
}

static int snkpe_received_msg_ps_rdy(struct sink_port_pe *sink)
{
	int ret = 0;

	if ((snkpe_is_prev_state(sink, PE_SNK_SELECT_CAPABILITY) ||
		snkpe_is_prev_state(sink, PE_SNK_READY)) &&
		snkpe_is_cur_state(sink, PE_SNK_TRANSITION_SINK)) {
		complete(&sink->pstt_complete);
	} else if (snkpe_is_prev_state(sink, PE_SNK_SELECT_CAPABILITY) &&
		snkpe_is_cur_state(sink, PE_SNK_READY)) {
		complete(&sink->srqt_complete);
	} else if (snkpe_is_cur_state(sink,
			PE_PRS_SNK_SRC_TRANSITION_TO_OFF)) {
		complete(&sink->pssoff_complete);
	} else {
		pr_err("SNKPE: Error in State Machine!\n");
		ret = -EINVAL;
	}

	return ret;
}

static int sink_port_policy_rcv_request(struct policy *p, enum pe_event evt)
{
	struct sink_port_pe *sink = container_of(p,
					struct sink_port_pe, p);

	switch (evt) {
	case PE_EVT_SEND_PR_SWAP:
		if (sink->pp_is_ext_pwrd || (!sink->pp_is_dual_prole)) {
			pr_info("SNKPE:%s: Not processing PR_SWAP Req\n",
					__func__);
			break;
		}
		snkpe_update_state(sink, PE_PRS_SNK_SRC_SEND_PR_SWAP);
		sink->pevt = evt;
		policy_send_packet(&sink->p, NULL, 0,
				PD_CTRL_MSG_PR_SWAP, sink->pevt);
		/* work schedule for rcv good crc for PR_SWAP and
		 * recevice Accept */
		schedule_work(&sink->timer_work);
		break;
	case PE_EVT_SEND_DR_SWAP:
	default:
		break;
	}

	return 0;
}

static int sink_port_policy_rcv_pkt(struct policy *p, struct pd_packet *pkt,
				enum pe_event evt)
{
	struct sink_port_pe *sink = container_of(p,
					struct sink_port_pe, p);
	int ret = 0;

	pr_debug("SNKPE:%s:evt = %d\n", __func__, evt);
	/* good crc is the ack for the actual command/message */
	if (evt != PE_EVT_RCVD_GOODCRC)
		sink->pevt = evt;

	switch (evt) {
	case PE_EVT_RCVD_SRC_CAP:
		/* FIXME: We can check here only the cur state */
		snkpe_txmsg_to_fifo(sink, pkt);
		if (sink->prev_state == PE_SNK_DISCOVERY &&
			sink->cur_state == PE_SNK_WAIT_FOR_CAPABILITIES)
			complete(&sink->wct_complete);
		else if (sink->cur_state == PE_SNK_TRANSITION_TO_DEFAULT)
			complete(&sink->nrt_complete);
		else if (sink->cur_state != ERROR_RECOVERY)
			/* Handle source cap data at all states */
			 return snkpe_handle_evaluate_capability(sink);
		break;
	case PE_EVT_RCVD_GET_SINK_CAP:
		if (sink->cur_state != ERROR_RECOVERY) {
			return snkpe_handle_give_snk_cap_state(sink);
		} else {
			pr_err("SNKPE: Error in State Machine!\n");
			ret = -EINVAL;
		}
		break;

	case PE_EVT_RCVD_ACCEPT:
	case PE_EVT_RCVD_REJECT:
	case PE_EVT_RCVD_WAIT:
		/* Move to PE_SNK_Ready state as per state machine */
		if (((sink->prev_state == PE_SNK_EVALUATE_CAPABILITY ||
			sink->prev_state == PE_SNK_READY) &&
			sink->cur_state == PE_SNK_SELECT_CAPABILITY) ||
			snkpe_is_cur_state(sink, PE_PRS_SNK_SRC_SEND_PR_SWAP)) {
			complete(&sink->srt_complete);
		} else {
			pr_err("SNKPE: Error in State Machine!\n");
			ret = -EINVAL;
		}
		break;
	case PE_EVT_RCVD_PS_RDY:
		ret = snkpe_received_msg_ps_rdy(sink);
		break;
	case PE_EVT_RCVD_PING:
		if (sink->cur_state == PE_SNK_READY) {
			/* FIXME: stay in the current state...
			 */
		}
		break;
	case PE_EVT_RCVD_GOTOMIN:
		if (sink->cur_state == PE_SNK_READY) {
			return snkpe_handle_gotomin_msg(sink);
		} else {
			pr_err("SNKPE: Error in State Machine!\n");
			ret = -EINVAL;
		}
		break;
	case PE_EVT_RCVD_GOODCRC:
		ret = snkpe_received_msg_good_crc(sink);
		break;
	case PE_EVT_RCVD_PR_SWAP:
		if (sink->cur_state != ERROR_RECOVERY) {
			return snkpe_handle_pr_swap(sink);
		} else {
			pr_err("SNKPE: SM is in Error Recovery Mode!\n");
			ret = -EINVAL;
		}
		break;
	default:
		break;
	}

	return ret;
}

static int snkpe_setup_charging(struct sink_port_pe *sink)
{
	int ret = 0;

	/* Update the charger input current limit */
	ret = policy_update_charger_ilim(&sink->p, sink->ilim);
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

static int snkpe_handle_transition_to_default(struct sink_port_pe *sink)
{
	int ret = 0;
	unsigned long timeout;

	snkpe_update_state(sink, PE_SNK_TRANSITION_TO_DEFAULT);

	/* FIXME: Request Device Policy Manager to request power sink transition
	 * to default Reset local HW, handled automatically based on cable event
	 */

	/* Actions on exit: Initialize and run NoResponseTimer Inform Protocol
	 * Layer Hard Reset complete.
	 */
	timeout = msecs_to_jiffies(TYPEC_NO_RESPONSE_TIMER);
	/* unblock this once PS_RDY msg received by checking the
	 * cur_state */
	ret = wait_for_completion_timeout(&sink->nrt_complete,
						timeout);
	if (ret == 0) {
		sink->timeout = NO_RESPONSE_TIMER;
		ret = snkpe_timeout_transition_check(sink);
		goto end;
	}

	sink->cur_state = PE_SNK_STARTUP;
	if (sink->pevt != PE_EVT_RCVD_SRC_CAP)
		ret = snkpe_start(sink);
	else
		ret = snkpe_handle_evaluate_capability(sink);

end:
	reinit_completion(&sink->nrt_complete);
	return ret;
}


static int snkpe_handle_transition_sink_state(struct sink_port_pe *sink)
{
	int ret = 0;
	unsigned long timeout;

	snkpe_update_state(sink, PE_SNK_TRANSITION_SINK);

	/* FIXME: Request Device Policy Manager transitions sink power
	 * supply to new power (if required): handled in next state (READY) */

	/* Put the charger into HiZ mode */
	ret = policy_set_charger_mode(&sink->p, CHRGR_SET_HZ);
	if (ret < 0)
		pr_err("SNKPE: Error in putting into HiZ mode (%d)\n", ret);

	if (sink->pevt != PE_EVT_RCVD_GOTOMIN)
		sink->ilim = sink->rcap.ma;
	else
		sink->ilim = 0;

	/* Initialize and run PSTransitionTimer */
	timeout = msecs_to_jiffies(TYPEC_PS_TRANSITION_TIMER);
	/* unblock this once PS_RDY msg received by checking the
	 * cur_state */
	ret = wait_for_completion_timeout(&sink->pstt_complete,
						timeout);
	if (ret == 0) {
		sink->timeout = PS_TRANSITION_TIMER;
		ret = snkpe_timeout_transition_check(sink);
		goto error;
	}
	ret = snkpe_handle_snk_ready_state(sink, sink->pevt);

error:
	reinit_completion(&sink->pstt_complete);
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
	enum pe_event evt;
	u32 data = 0;

	/* move the next state PE_SNK_Select_Capability */
	snkpe_update_state(sink, PE_SNK_SELECT_CAPABILITY);

	snkpe_read_src_caps(sink, pkt);

	evt = PE_EVT_SEND_REQUEST;
	/* make request message and send to PE -> protocol */
	ret = snkpe_get_msg(sink, pkt, evt, &data);
	if (ret < 0) {
		pr_err("SNKPE: Error in getting message!\n");
		goto error;
	}

	ret = policy_send_packet(&sink->p, &data, 4, PD_DATA_MSG_REQUEST, evt);
	if (ret < 0) {
		pr_err("SNKPE: Error in sending packet!\n");
		goto error;
	}
	sink->pevt = evt;
	pr_debug("SNKPE: PD_DATA_MSG_REQUEST Sent\n");

	/* Keeping backup to use later if required for wait event and
	 * sink request timer timeout */
	memcpy(&sink->prev_pkt, pkt, sizeof(struct pd_packet));

error:
	return ret;
}

static int snkpe_handle_after_request_sent(struct sink_port_pe *sink)
{
	unsigned long timeout;
	int ret;

	/* Initialize and run SenderResponseTimer */
	timeout = msecs_to_jiffies(TYPEC_SENDER_RESPONSE_TIMER);
	/* unblock this once Accept msg received by checking the
	 * cur_state */
	ret = wait_for_completion_timeout(&sink->srt_complete, timeout);
	if (ret == 0) {
		sink->timeout = SENDER_RESPONSE_TIMER;
		ret = snkpe_timeout_transition_check(sink);
		goto error;
	}

	if (sink->pevt == PE_EVT_RCVD_ACCEPT) {
		if (snkpe_is_cur_state(sink, PE_PRS_SNK_SRC_SEND_PR_SWAP))
			ret = snkpe_handle_pss_transition_to_off(sink);
		else
			ret = snkpe_handle_transition_sink_state(sink);
	} else if (sink->pevt == PE_EVT_RCVD_REJECT ||
		sink->pevt == PE_EVT_RCVD_WAIT)
		ret = snkpe_handle_snk_ready_state(sink, sink->pevt);
	else
		pr_err("SNKPE: Unknown event: %d\n", sink->pevt);
error:
	reinit_completion(&sink->srt_complete);
	return ret;
}

static int snkpe_handle_give_snk_cap_state(struct sink_port_pe *sink)
{
	int ret = 0;
	int i;
	enum pe_event evt;
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
		pdo[i].data_role_swap = 0;
		pdo[i].usb_comm = 0;
		pdo[i].ext_powered = 0;
		pdo[i].higher_cap = 0;
		pdo[i].dual_role_pwr = 0;
		pdo[i].supply_type = 0;
	}

	evt = PE_EVT_SEND_SNK_CAP;
	ret = policy_send_packet(&sink->p, pdo, pcaps.n_cap * 4,
					PD_DATA_MSG_SINK_CAP, evt);
	if (ret < 0) {
		pr_err("SNKPE: Error in sending packet!\n");
		goto error;
	}
	sink->pevt = evt;
	pr_debug("SNKPE: PD_DATA_MSG_SINK_CAP sent\n");

error:
	return ret;
}

static int snkpe_handle_psrdy_after_wait_state(struct sink_port_pe *sink)
{
	int ret;

	/* Put the charger into HiZ mode */
	ret = policy_set_charger_mode(&sink->p, CHRGR_SET_HZ);
	if (ret < 0) {
		pr_err("SNKPE: Error in putting into HiZ mode (%d)\n", ret);
		return ret;
	}
	sink->ilim = sink->rcap.ma;

	ret = snkpe_setup_charging(sink);
	if (ret < 0)
		pr_err("SNKPE: Error in setup charging (%d)\n", ret);

	return ret;
}

static int snkpe_handle_snk_ready_state(struct sink_port_pe *sink,
						enum pe_event evt)
{
	int ret = 0;
	unsigned long timeout;

	snkpe_update_state(sink, PE_SNK_READY);

	/* TODO: if Update remote capabilities request from received from
	 * Device Policy Manager move to PE_SNK_Get_Source_Cap state and
	 * send Send Get_Source_Cap message, then move to PE_SNK_Ready state.
	 */

	switch (evt) {
	case PE_EVT_RCVD_PS_RDY:
		ret = snkpe_setup_charging(sink);
		if (ret < 0)
			pr_warn("SNKPE: Error in setup charging (%d)\n", ret);
		break;
	case PE_EVT_RCVD_REJECT:
	case PE_EVT_SEND_SNK_CAP:
	case PE_EVT_SEND_GET_SRC_CAP:
		/* Do nothing and continue in the same state */
		break;
	case PE_EVT_RCVD_WAIT:
		/* Initialize and run SinkRequestTimer (on receiving
		 * Wait) for PS_RDY */
		timeout = msecs_to_jiffies(TYPEC_SINK_REQUEST_TIMER);
		ret = wait_for_completion_timeout(&sink->srqt_complete,
							timeout);
		if (ret == 0) {
			/* New power required | SinkRequestTimer timeout */
			sink->cur_state = PE_SNK_EVALUATE_CAPABILITY;
			ret = snkpe_handle_select_capability_state(
					sink, &sink->prev_pkt);
			goto end;
		}
		reinit_completion(&sink->srqt_complete);

		/* Received PS_RDY event after a WAIT event */
		ret = snkpe_handle_psrdy_after_wait_state(sink);
		break;
	default:
		pr_err("SNKPE: Unknown state to handle ready evt = %d\n", evt);
		ret = -EINVAL;
	}
	return 0;

end:
	reinit_completion(&sink->srqt_complete);
	return ret;
}

static int snkpe_handle_evaluate_capability(struct sink_port_pe *sink)
{
	int ret = 0;
	struct pd_packet pkt = { {0} };

	snkpe_update_state(sink, PE_SNK_EVALUATE_CAPABILITY);

	/* FIXME: Stop NoResponseTimer and reset HardResetCounter to
	 * zero. Ask Device Policy Manager to evaluate option based on
	 * supplied capabilities (from supplied capabilities, reserve or
	 * Capability (Mismatch.).
	 */
	sink->hard_reset_count = 0;
	policy_set_pd_state(&sink->p, true);

	/* Receive the pd_packet for the source cap message */
	ret = snkpe_rxmsg_from_fifo(sink, &pkt);
	if (ret < 0) {
		pr_err("SNKPE: Error in reading data from fio\n");
		goto error;
	}
	return snkpe_handle_select_capability_state(sink, &pkt);

error:
	return ret;
}

static int snkpe_handle_assert_rp_src_on(struct sink_port_pe *sink)
{
	int ret;

	snkpe_update_state(sink, PE_PRS_SNK_SRC_SOURCE_ON);

	/* Pull-up CC (enable Rp) and Vbus 5V enable */
	ret = policy_set_power_role(&sink->p, POWER_ROLE_SOURCE);
	if (ret < 0) {
		pr_err("SNKPE: Error in enabling source %d\n", ret);
		return ret;
	}
	/* SourceActivityTimer (40 - 50mSec) is not used to monitor source
	 * activity. Assuming the source activity can be done within the time
	 */
	sink->pevt = PE_EVT_SEND_PS_RDY;
	return policy_send_packet(&sink->p, NULL, 0,
					PD_CTRL_MSG_PS_RDY, sink->pevt);
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
	ret = snkpe_handle_assert_rp_src_on(sink);
	if (!ret)
		return;

trans_off_err:
	/* Move to PE_SNK_Hard_Reset state */
	snkpe_update_state(sink, PE_SNK_HARD_RESET);
	return;
}

static void snkpe_timer_worker(struct work_struct *work)
{
	struct sink_port_pe *sink = container_of(work,
					struct sink_port_pe,
					timer_work);

	if ((snkpe_is_cur_state(sink, PE_SNK_SELECT_CAPABILITY) &&
		snkpe_is_prev_evt(sink, PE_EVT_SEND_REQUEST)) ||
		(snkpe_is_cur_state(sink, PE_PRS_SNK_SRC_SEND_PR_SWAP) &&
		snkpe_is_prev_evt(sink, PE_EVT_SEND_PR_SWAP)))
		snkpe_handle_after_request_sent(sink);
	else if (snkpe_is_cur_state(sink, PE_PRS_SNK_SRC_TRANSITION_TO_OFF))
		snkpe_handle_pss_transition_off(sink);
}

static int snkpe_vbus_attached(struct sink_port_pe *sink)
{
	int ret = 0;
	unsigned long timeout =
		msecs_to_jiffies(TYPEC_SINK_WAIT_CAP_TIMER);

	if (sink->prev_state == PE_SNK_STARTUP &&
		sink->cur_state == PE_SNK_DISCOVERY) {
		snkpe_update_state(sink, PE_SNK_WAIT_FOR_CAPABILITIES);

		/* Initialize and run SinkWaitCapTimer */
		/* unblock this once source cap rcv by checking the cur_state */
		ret = wait_for_completion_timeout(&sink->wct_complete, timeout);
		if (ret == 0) {
			sink->timeout = SINK_WAIT_CAP_TIMER;
			ret = snkpe_timeout_transition_check(sink);
			goto error;
		}
		ret = snkpe_handle_evaluate_capability(sink);
	}

error:
	reinit_completion(&sink->wct_complete);
	return ret;
}

struct policy *sink_port_policy_init(struct policy_engine *pe)
{
	struct sink_port_pe *snkpe;
	struct policy *p;
	int ret;

	snkpe = kzalloc(sizeof(*snkpe), GFP_KERNEL);
	if (!snkpe)
		return ERR_PTR(-ENOMEM);

	INIT_WORK(&snkpe->timer_work, snkpe_timer_worker);

	/* Allocate memory for PD packet FIFO */
	if (kfifo_alloc(&snkpe->pkt_fifo,
				sizeof(struct pd_packet),
				GFP_KERNEL)) {
		pr_err("SNKPE: kfifo alloc failed for policy\n");
		ret = -ENOMEM;
		goto error;
	}

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

	init_completion(&snkpe->wct_complete);
	init_completion(&snkpe->srt_complete);
	init_completion(&snkpe->nrt_complete);
	init_completion(&snkpe->pstt_complete);
	init_completion(&snkpe->sat_complete);
	init_completion(&snkpe->srqt_complete);
	init_completion(&snkpe->pssoff_complete);
	init_completion(&snkpe->pktwt_complete);
	mutex_init(&snkpe->snkpe_state_lock);

	return p;

error:
	kfree(snkpe);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(sink_port_policy_init);

static void sink_port_policy_exit(struct policy *p)
{
	struct sink_port_pe *snkpe;

	if (p) {
		snkpe = container_of(p, struct sink_port_pe, p);
		kfifo_free(&snkpe->pkt_fifo);
		kfree(snkpe);
	}
}

MODULE_AUTHOR("Albin B <albin.bala.krishnan@intel.com>");
MODULE_DESCRIPTION("PD Sink Port Policy Engine");
MODULE_LICENSE("GPL v2");
