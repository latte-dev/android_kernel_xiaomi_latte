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
 * Author: Kotakonda, Venkataramana <venkataramana.kotakonda@intel.com>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/timer.h>
#include <linux/usb_typec_phy.h>
#include "policy_engine.h"
#include "vdm_process_helper.h"


/* Local function prototypes */
static void pe_dump_header(struct pd_pkt_header *header);
static void pe_dump_data_msg(struct pd_packet *pkt);
static void pe_send_self_sink_caps(struct policy_engine *pe);
static void pe_start_timer(struct policy_engine *pe,
				enum pe_timers timer_type, unsigned time);
static void pe_cancel_timer(struct policy_engine *pe,
				enum pe_timers timer_type);
static bool pe_is_timer_pending(struct policy_engine *pe,
				enum pe_timers timer_type);
static void
pe_process_state_pe_src_transition_to_default_exit(struct policy_engine *pe);
static void pe_set_data_role(struct policy_engine *pe, enum data_role role);
static void pe_set_power_role(struct policy_engine *pe, enum pwr_role role);
static void pe_change_state_to_snk_or_src_reset(struct policy_engine *pe);
static void pe_store_port_partner_caps(struct policy_engine *pe,
					struct pe_port_pdos *pdos);

static void pe_change_state(struct policy_engine *pe, enum pe_states state)
{
	pe->prev_state = pe->cur_state;
	pe->cur_state = state;
	pe_schedule_work_pd_wq(&pe->p, &pe->policy_state_work);
}

static void pe_deactivate_all_timers(struct policy_engine *pe)
{
	int i;

	for (i = 0; i < PE_TIMER_CNT; i++)
		del_timer(&pe->timers[i].timer);

}

static void pe_enable_pd(struct policy_engine *pe, bool en)
{
	if (pe->is_pd_enabled == en)
		return;
	if (pe->p.prot) {
		pd_prot_enable_pd(pe->p.prot, en);
		pe->is_pd_enabled = en;
	}
}

static void pe_do_self_reset(struct policy_engine *pe)
{
	pe_deactivate_all_timers(pe);
	/*
	 * Cancel pending state works. Donot use sync as this
	 * reset itself might be runnig in the worker.
	 */
	pe->alt_state = PE_ALT_STATE_NONE;
	pe->is_modal_operation = false;
	if (pe->pp_alt_caps.hpd_state)
		devpolicy_set_dp_state(pe->p.dpm, CABLE_DETACHED,
						TYPEC_DP_TYPE_NONE);

	/* Reset all counter exept reset counter */
	pe->retry_counter = 0;
	pe->src_caps_couner = 0;
	pe->discover_identity_couner = 0;
	pe->vdm_busy_couner = 0;

	/* reset required local variable */
	pe->is_gcrc_received = false;
	pe->last_rcv_evt = PE_EVT_RCVD_NONE;
	pe->pd_explicit_contract = false;
	pe->is_no_response_timer_expired = false;
	pe->is_pr_swap_rejected = false;

	/* reset saved port partner's data */
	memset(&pe->pp_snk_pdos, 0, sizeof(struct pe_port_pdos));
	memset(&pe->pp_src_pdos, 0, sizeof(struct pe_port_pdos));
	memset(&pe->pp_caps, 0, sizeof(struct pe_port_partner_caps));

	/* Disable PD, auto crc */
	pe_enable_pd(pe, false);
}

static void pe_do_dpm_reset_entry(struct policy_engine *pe)
{
	/* VBUS Off*/
	if (pe->cur_prole == POWER_ROLE_SOURCE) {
		devpolicy_set_vbus_state(pe->p.dpm, false);
		if (pe->cur_drole != DATA_ROLE_DFP)
			/* Reset data role to NONE */
			pe_set_data_role(pe, DATA_ROLE_NONE);
	} else if (pe->cur_prole == POWER_ROLE_SINK) {
		if (pe->cur_drole != DATA_ROLE_UFP)
			/* Reset data role to NONE */
			pe_set_data_role(pe, DATA_ROLE_NONE);
	} else
		log_err("Unexpected pwr role =%d", pe->cur_prole);
	/*VCONN Off*/
	devpolicy_set_vconn_state(pe->p.dpm, VCONN_NONE);
}

static void pe_do_dpm_reset_complete(struct policy_engine *pe)
{
	if (pe->cur_prole == POWER_ROLE_SOURCE) {
		/* VBUS On if source*/
		devpolicy_set_vbus_state(pe->p.dpm, true);
		/*VCONN on if source*/
		devpolicy_set_vconn_state(pe->p.dpm, VCONN_SOURCE);
		/* Reset data role to DFP*/
		pe_set_data_role(pe, DATA_ROLE_DFP);
	} else if (pe->cur_prole == POWER_ROLE_SINK) {
		/* Reset data role to UFP*/
		pe_set_data_role(pe, DATA_ROLE_UFP);
	} else
		log_err("Unexpected pwr role =%d", pe->cur_prole);
}

static void pe_do_complete_reset(struct policy_engine *pe)
{
	pe_do_self_reset(pe);

	/* Reset things that should not be cleared on normal reset*/
	pe->hard_reset_counter = 0;
	pe->is_pp_pd_capable = 0;
}

static int policy_engine_process_data_msg(struct policy *p,
				enum pe_event evt, struct pd_packet *pkt)
{
	struct policy_engine *pe = container_of(p, struct policy_engine, p);
	int data_len = PD_MSG_NUM_DATA_OBJS(&pkt->header);

	log_dbg("Data msg received evt - %d\n", evt);
	if (data_len > MAX_NUM_DATA_OBJ)
		data_len = MAX_NUM_DATA_OBJ;

	mutex_lock(&pe->pe_lock);
	pe->is_pp_pd_capable = true;
	switch (evt) {
	case PE_EVT_RCVD_SRC_CAP:
		if (pe->cur_state == PE_SNK_WAIT_FOR_CAPABILITIES) {
			pe_cancel_timer(pe, SINK_WAIT_CAP_TIMER);
			if (pe_is_timer_pending(pe, NO_RESPONSE_TIMER))
				pe_cancel_timer(pe, NO_RESPONSE_TIMER);
			pe->hard_reset_counter = 0;

			memcpy(pe->pp_src_pdos.pdo,
					pkt->data_obj, data_len * 4);
			pe->pp_src_pdos.num_pdos = data_len;
			pe_change_state(pe, PE_SNK_EVALUATE_CAPABILITY);

		} else if (pe->cur_state == PE_SNK_READY) {
			pe_cancel_timer(pe, SINK_REQUEST_TIMER);
			memcpy(pe->pp_src_pdos.pdo,
					pkt->data_obj, data_len * 4);
			pe->pp_src_pdos.num_pdos = data_len;
			pe_change_state(pe, PE_SNK_EVALUATE_CAPABILITY);

		} else if (pe->cur_state == PE_DR_SRC_GET_SOURCE_CAP) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			memcpy(pe->pp_src_pdos.pdo,
					pkt->data_obj, data_len * 4);
			pe->pp_src_pdos.num_pdos = data_len;
			pe_store_port_partner_caps(pe, &pe->pp_src_pdos);
			pe_change_state(pe, PE_SRC_READY);

		} else {
			log_warn("SrcCaps received in wrong state=%d\n",
					pe->cur_state);
		}
		break;
	case PE_EVT_RCVD_REQUEST:
		if (pe->cur_state == PE_SRC_SEND_CAPABILITIES) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			memcpy(&pe->pp_sink_req_caps,
				&pkt->data_obj[0],
				sizeof(struct pd_fixed_var_rdo));
			pe_change_state(pe, PE_SRC_NEGOTIATE_CAPABILITY);
		} else {
			log_warn("Request msg received in wrong state=%d",
					pe->cur_state);
		}
		break;

	case PE_EVT_RCVD_VDM:
		if (pe_is_timer_pending(pe, VMD_RESPONSE_TIMER))
			pe_cancel_timer(pe, VMD_RESPONSE_TIMER);
		pe_handle_vendor_msg(pe, pkt);
		break;

	case PE_EVT_RCVD_SNK_CAP:
		if (pe->cur_state == PE_SRC_GET_SINK_CAP) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			/* TODO: Process sink caps */
			pe_change_state(pe, PE_SRC_READY);
		} else if (pe->cur_state == PE_DR_SNK_GET_SINK_CAP) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			/* TODO: Process sink caps */
			pe_change_state(pe, PE_SNK_READY);
		}
		memcpy(pe->pp_snk_pdos.pdo,
					pkt->data_obj, data_len * 4);
		pe->pp_snk_pdos.num_pdos = data_len;
		pe_store_port_partner_caps(pe, &pe->pp_snk_pdos);
		break;

	case PE_EVT_RCVD_BIST:
	default:
		log_warn("Invalid data msg, event=%d\n", evt);
		pe_dump_data_msg(pkt);
	}

	mutex_unlock(&pe->pe_lock);
	return 0;
}

static void pe_handle_gcrc_received(struct policy_engine *pe)
{
	switch (pe->cur_state) {

	case PE_SNK_SELECT_CAPABILITY:
	case PE_SRC_GET_SINK_CAP:
	case PE_DR_SNK_GET_SINK_CAP:
	case PE_DR_SRC_GET_SOURCE_CAP:
	case PE_DRS_DFP_UFP_SEND_DR_SWAP:
	case PE_DRS_UFP_DFP_SEND_DR_SWAP:
	case PE_PRS_SRC_SNK_SEND_PR_SWAP:
	case PE_PRS_SNK_SRC_SEND_PR_SWAP:
	case PE_SNK_SEND_SOFT_RESET:
	case PE_SRC_SEND_SOFT_RESET:
		/* Start sender response timer */
		pe_start_timer(pe, SENDER_RESPONSE_TIMER,
					PE_TIME_SENDER_RESPONSE);
		break;

	case PE_SNK_GIVE_SINK_CAP:
	case PE_SNK_GET_SOURCE_CAP:
	case PE_DR_SNK_GIVE_SOURCE_CAP:
		pe_change_state(pe, PE_SNK_READY);
		break;

	case PE_SRC_GIVE_SOURCE_CAP:
	case PE_DR_SRC_GIVE_SINK_CAP:
		pe_change_state(pe, PE_SRC_READY);
		break;

	case PE_SRC_SEND_CAPABILITIES:
		if (pe_is_timer_pending(pe, NO_RESPONSE_TIMER))
			pe_cancel_timer(pe, NO_RESPONSE_TIMER);
		pe->hard_reset_counter = 0;
		pe->src_caps_couner = 0;
		/* Start sender response timer */
		pe_start_timer(pe, SENDER_RESPONSE_TIMER,
					PE_TIME_SENDER_RESPONSE);
		break;

	case PE_SRC_NEGOTIATE_CAPABILITY:
		if (pe->last_sent_evt == PE_EVT_SEND_REJECT)
			pe_change_state(pe, PE_SRC_CAPABILITY_RESPONSE);
		else if (pe->last_sent_evt == PE_EVT_SEND_ACCEPT)
			pe_change_state(pe, PE_SRC_TRANSITION_SUPPLY);
		else
			log_warn("Error PE_SRC_NEGOTIATE_CAPABILITY");
		break;

	case PE_SRC_TRANSITION_SUPPLY:
		log_info("PD Configured in Source Mode !!!!!!");
		pe->pd_explicit_contract = true;
		/* Move to ready state */
		pe_change_state(pe, PE_SRC_READY);
		break;

	case PE_DRS_DFP_UFP_ACCEPT_DR_SWAP:
		/* GCRC received for accept, switch to ufp*/
		pe_change_state(pe, PE_DRS_DFP_UFP_CHANGE_TO_UFP);
		break;

	case PE_DRS_UFP_DFP_ACCEPT_DR_SWAP:
		/* GCRC received for accept, switch to dfp*/
		pe_change_state(pe, PE_DRS_UFP_DFP_CHANGE_TO_DFP);
		break;

	case PE_PRS_SRC_SNK_REJECT_PR_SWAP:
		pe_change_state(pe, PE_SRC_READY);
		break;

	case PE_PRS_SNK_SRC_REJECT_PR_SWAP:
		pe_change_state(pe, PE_SNK_READY);
		break;

	case PE_PRS_SRC_SNK_ACCEPT_PR_SWAP:
		pe_change_state(pe, PE_PRS_SRC_SNK_TRANSITION_TO_OFF);
		break;

	case PE_PRS_SNK_SRC_ACCEPT_PR_SWAP:
		pe_change_state(pe, PE_PRS_SNK_SRC_TRANSITION_TO_OFF);
		break;

	case PE_PRS_SRC_SNK_WAIT_SOURCE_ON:
		pe_start_timer(pe, PS_SOURCE_ON_TIMER,
						PE_TIME_PS_SOURCE_ON);
		break;

	case PE_PRS_SNK_SRC_SOURCE_ON:
		log_info("PR_SWAP SNK -> SRC success!!");
		/*
		 * Disable PD if enabled, till PE is
		 * ready to receive src_caps
		 */
		pe_enable_pd(pe, false);

		pe_change_state(pe, PE_SRC_STARTUP);
		break;
	case PE_VCS_SEND_PS_RDY:
		log_info("VCS Success!! moving to ready state");
	case PE_VCS_REJECT_SWAP:
		pe_change_state_to_snk_or_src_ready(pe);
		break;
	case PE_VCS_ACCEPT_SWAP:
		if (devpolicy_get_vconn_state(pe->p.dpm))
			pe_change_state(pe, PE_VCS_WAIT_FOR_VCONN);
		else
			pe_change_state(pe, PE_VCS_TURN_ON_VCONN);
		break;
	case PE_VCS_SEND_SWAP:
		/* Start sender response timer */
		pe_start_timer(pe, SENDER_RESPONSE_TIMER,
					PE_TIME_SENDER_RESPONSE);
		break;
	case PE_DFP_UFP_VDM_IDENTITY_REQUEST:
	case PE_DFP_VDM_SVIDS_REQUEST:
	case PE_DFP_VDM_MODES_REQUEST:
	case PE_DFP_VDM_MODES_ENTRY_REQUEST:
	case PE_DFP_VDM_STATUS_REQUEST:
	case PE_DFP_VDM_CONF_REQUEST:
		pe_start_timer(pe, VMD_RESPONSE_TIMER,
				PE_TIME_VDM_SENDER_RESPONSE);
		break;
	case PE_SNK_SOFT_RESET:
		pe_change_state(pe, PE_SNK_WAIT_FOR_CAPABILITIES);
		break;
	case PE_SRC_SOFT_RESET:
		pe_change_state(pe, PE_SRC_SEND_CAPABILITIES);
		break;
	default:
		log_warn("GCRC received in wrong state=%d",
					pe->cur_state);
	}
}

static int policy_engine_process_ctrl_msg(struct policy *p,
				enum pe_event evt, struct pd_packet *pkt)
{
	int ret = 0;
	struct policy_engine *pe = container_of(p, struct policy_engine, p);

	log_dbg("Ctrl msg received evt - %d\n", evt);

	mutex_lock(&pe->pe_lock);
	pe->is_pp_pd_capable = true;
	switch (evt) {
	case PE_EVT_RCVD_GOODCRC:
		pe_cancel_timer(pe, CRC_RECEIVE_TIMER);
		pe->is_gcrc_received = true;
		pe_handle_gcrc_received(pe);
		break;

	case PE_EVT_RCVD_ACCEPT:
		if (pe->cur_state == PE_SNK_SELECT_CAPABILITY) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe_change_state(pe, PE_SNK_TRANSITION_SINK);

		} else if (pe->cur_state == PE_DRS_DFP_UFP_SEND_DR_SWAP) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe_change_state(pe, PE_DRS_DFP_UFP_CHANGE_TO_UFP);

		} else if (pe->cur_state == PE_DRS_UFP_DFP_SEND_DR_SWAP) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe_change_state(pe, PE_DRS_UFP_DFP_CHANGE_TO_DFP);

		} else if (pe->cur_state == PE_PRS_SRC_SNK_SEND_PR_SWAP) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe_change_state(pe, PE_PRS_SRC_SNK_TRANSITION_TO_OFF);

		} else if (pe->cur_state == PE_PRS_SNK_SRC_SEND_PR_SWAP) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe_change_state(pe, PE_PRS_SNK_SRC_TRANSITION_TO_OFF);

		} else if (pe->cur_state == PE_SRC_SEND_SOFT_RESET) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe_change_state(pe, PE_SRC_SEND_CAPABILITIES);

		} else if (pe->cur_state == PE_SNK_SEND_SOFT_RESET) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe_change_state(pe, PE_SNK_WAIT_FOR_CAPABILITIES);
		} else if (pe->cur_state == PE_VCS_SEND_SWAP) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			if (devpolicy_get_vconn_state(pe->p.dpm)) {
				pe_change_state(pe,
						PE_VCS_WAIT_FOR_VCONN);
			} else
				pe_change_state(pe, PE_VCS_TURN_ON_VCONN);
		} else {
			log_warn("Accept received in wrong state=%d",
					pe->cur_state);
		}
		break;
	case PE_EVT_RCVD_REJECT:
		if (pe->cur_state == PE_SNK_SELECT_CAPABILITY) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe->last_rcv_evt = evt;
			if (pe->pd_explicit_contract)
				pe_change_state(pe, PE_SNK_READY);
			else
				pe_change_state(pe,
					PE_SNK_WAIT_FOR_CAPABILITIES);
		} else if (pe->cur_state == PE_DRS_DFP_UFP_SEND_DR_SWAP ||
				pe->cur_state == PE_DRS_UFP_DFP_SEND_DR_SWAP) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			log_info("DR SWAP Rejected");
			pe_change_state_to_snk_or_src_ready(pe);
		} else if (pe->cur_state == PE_PRS_SRC_SNK_SEND_PR_SWAP) {
			log_dbg("PR SWAP Rejected");
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe->is_pr_swap_rejected = true;
			pe_change_state(pe, PE_SRC_READY);
		} else if (pe->cur_state == PE_PRS_SNK_SRC_SEND_PR_SWAP) {
			log_dbg("PR SWAP Rejected");
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe_change_state(pe, PE_SNK_READY);
		} else if (pe->cur_state == PE_VCS_SEND_SWAP) {
			log_info("VCS Request Rejected! moving to ready state");
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe_change_state_to_snk_or_src_ready(pe);
		} else {
			log_warn("Reject received in wrong state=%d",
					pe->cur_state);
		}
		break;
	case PE_EVT_RCVD_WAIT:
		if (pe->cur_state == PE_SNK_SELECT_CAPABILITY) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe->last_rcv_evt = evt;
			if (pe->pd_explicit_contract)
				pe_change_state(pe, PE_SNK_READY);
			else
				pe_change_state(pe,
					PE_SNK_WAIT_FOR_CAPABILITIES);
		} else if (pe->cur_state == PE_DRS_DFP_UFP_SEND_DR_SWAP ||
				pe->cur_state == PE_DRS_UFP_DFP_SEND_DR_SWAP) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe_change_state_to_snk_or_src_ready(pe);
		} else if (pe->cur_state == PE_PRS_SRC_SNK_SEND_PR_SWAP) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe_change_state(pe, PE_SRC_READY);
		} else if (pe->cur_state == PE_PRS_SNK_SRC_SEND_PR_SWAP) {
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe_change_state(pe, PE_SNK_READY);
		} else if (pe->cur_state == PE_VCS_SEND_SWAP) {
			log_info("Wait received for VCS moving to ready state");
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
			pe_change_state_to_snk_or_src_ready(pe);
		} else {
			log_warn("Wait received in wrong state=%d",
					pe->cur_state);
		}
		break;
	case PE_EVT_RCVD_PS_RDY:
		if (pe->cur_state == PE_SNK_TRANSITION_SINK) {
			pe_cancel_timer(pe, PS_TRANSITION_TIMER);
			if (pe->prev_state == PE_SNK_SELECT_CAPABILITY)
				pe->pd_explicit_contract = true;
			pe_change_state(pe, PE_SNK_READY);
			log_info("Sink Negotiation Success !!!!");
		} else if (pe->cur_state == PE_PRS_SRC_SNK_WAIT_SOURCE_ON) {
			log_info("PR_SWAP SRC -> SNK Success!!");
			pe_cancel_timer(pe, PS_SOURCE_ON_TIMER);
			/*
			 * Disable PD if enabled, till PE is
			 * ready to receive src_caps
			 */
			pe_enable_pd(pe, false);
			pe_set_power_role(pe, POWER_ROLE_SINK);
			pe_change_state(pe, PE_SNK_STARTUP);

		} else if (pe->cur_state == PE_PRS_SNK_SRC_TRANSITION_TO_OFF) {
			log_dbg("PS_RDY received from src during pr_swap");
			pe_cancel_timer(pe, PS_SOURCE_OFF_TIMER);
			pe_change_state(pe, PE_PRS_SNK_SRC_ASSERT_RP);
		} else if (pe->cur_state == PE_VCS_WAIT_FOR_VCONN) {
			pe_cancel_timer(pe, VCONN_ON_TIMER);
			pe_change_state(pe, PE_VCS_TURN_OFF_VCONN);
		} else {
			log_warn("PsRdy received in wrong state=%d",
					pe->cur_state);
		}
		break;
	case PE_EVT_RCVD_GET_SINK_CAP:
		if (pe->cur_state == PE_SNK_READY)
			pe_change_state(pe, PE_SNK_GIVE_SINK_CAP);
		else if (pe->cur_state == PE_SRC_READY)
			pe_change_state(pe, PE_DR_SRC_GIVE_SINK_CAP);
		else
			pe_send_self_sink_caps(pe);
		break;

	case PE_EVT_RCVD_GET_SRC_CAP:
		if (pe->cur_state == PE_SNK_READY)
			pe_change_state(pe, PE_DR_SNK_GIVE_SOURCE_CAP);
		else if (pe->cur_state == PE_SRC_READY)
			pe_change_state(pe, PE_SRC_GIVE_SOURCE_CAP);
		else
			log_info("Cannot provide source caps in state=%d",
							pe->cur_state);
		break;

	case PE_EVT_RCVD_DR_SWAP:
		if (pe->is_modal_operation) {
			log_warn(" DR Swap cannot perform in modal operation");
			pe_change_state_to_snk_or_src_reset(pe);

		} else if (pe->cur_state == PE_SRC_READY
				|| pe->cur_state == PE_SNK_READY) {
			if (pe->cur_drole == DATA_ROLE_DFP)
				pe_change_state(pe,
					PE_DRS_DFP_UFP_EVALUATE_DR_SWAP);
			else if (pe->cur_drole == DATA_ROLE_UFP)
				pe_change_state(pe,
					PE_DRS_UFP_DFP_EVALUATE_DR_SWAP);
			else {
				log_err("Error in current data role %d",
					pe->cur_drole);
				pe_change_state_to_snk_or_src_reset(pe);
			}
		} else {
			log_info("PE not ready to process DR_SWAP, state=%d",
					pe->cur_state);
			pe_send_packet(pe, NULL, 0,
				PD_CTRL_MSG_WAIT, PE_EVT_SEND_WAIT);
		}
		break;

	case PE_EVT_RCVD_PR_SWAP:
		if (pe->cur_state == PE_SRC_READY)
			pe_change_state(pe, PE_PRS_SRC_SNK_EVALUATE_PR_SWAP);
		else if (pe->cur_state == PE_SNK_READY)
			pe_change_state(pe, PE_PRS_SNK_SRC_EVALUATE_PR_SWAP);
		else
			/* Send Wait */
			pe_send_packet(pe, NULL, 0,
					PD_CTRL_MSG_WAIT, PE_EVT_SEND_WAIT);
		break;

	case PE_EVT_RCVD_SOFT_RESET:
		if (pe->cur_prole == POWER_ROLE_SOURCE)
			pe_change_state(pe, PE_SRC_SOFT_RESET);
		else if (pe->cur_prole == POWER_ROLE_SINK)
			pe_change_state(pe, PE_SNK_SOFT_RESET);
		else
			pe_change_state(pe, PE_ERROR_RECOVERY);
		break;
	case PE_EVT_RCVD_VCONN_SWAP:
		if (pe->cur_state == PE_SRC_READY ||
			pe->cur_state == PE_SNK_READY)
			pe_change_state(pe, PE_VCS_EVALUATE_SWAP);
		break;
	case PE_EVT_RCVD_GOTOMIN:
	case PE_EVT_RCVD_PING:
		break;
	default:
		log_warn("Not a valid ctrl msg to process, event=%d\n", evt);
		pe_dump_header(&pkt->header);
	}
	mutex_unlock(&pe->pe_lock);
	return ret;
}

static int policy_engine_process_cmd(struct policy *p,
				enum pe_event evt)
{
	int ret = 0;
	struct policy_engine *pe = container_of(p, struct policy_engine, p);

	log_dbg("cmd %d\n", evt);
	mutex_lock(&pe->pe_lock);
	switch (evt) {
	case PE_EVT_RCVD_HARD_RESET:
		if (pe->cur_prole == POWER_ROLE_SOURCE) {
			log_dbg("HardReset received in Src role");
			pe_change_state(pe, PE_SRC_HARD_RESET_RECEIVED);
		} else if (pe->cur_prole == POWER_ROLE_SINK) {
			log_dbg("HardReset received in Sink role");
			pe_change_state(pe, PE_SNK_HARD_RESET_RECEIVED);
		} else
			log_err("HardReset received in unknown role=%d",
					pe->cur_prole);
		break;
	case PE_EVT_RCVD_HARD_RESET_COMPLETE:
		if (pe->cur_state == PE_SNK_HARD_RESET) {
			pe_cancel_timer(pe, HARD_RESET_COMPLETE_TIMER);
			pe_change_state(pe, PE_SNK_TRANSITION_TO_DEFAULT);
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&pe->pe_lock);
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


int pe_send_packet(struct policy_engine *pe, void *data, int len,
				u8 msg_type, enum pe_event evt)
{
	int ret = 0;
	bool is_crc_timer_req = true;

	switch (evt) {
	case PE_EVT_SEND_GOTOMIN:
	case PE_EVT_SEND_ACCEPT:
	case PE_EVT_SEND_REJECT:
	case PE_EVT_SEND_WAIT:
	case PE_EVT_SEND_PING:
	case PE_EVT_SEND_PS_RDY:
	case PE_EVT_SEND_GET_SRC_CAP:
	case PE_EVT_SEND_GET_SINK_CAP:
	case PE_EVT_SEND_SRC_CAP:
	case PE_EVT_SEND_SNK_CAP:
	case PE_EVT_SEND_DR_SWAP:
	case PE_EVT_SEND_PR_SWAP:
	case PE_EVT_SEND_VCONN_SWAP:
	case PE_EVT_SEND_REQUEST:
	case PE_EVT_SEND_BIST:
	case PE_EVT_SEND_VDM:
	case PE_EVT_SEND_SOFT_RESET:
		break;
	case PE_EVT_SEND_HARD_RESET:
	case PE_EVT_SEND_PROTOCOL_RESET:
		is_crc_timer_req = false;
		break;
	default:
		ret = -EINVAL;
		log_err("Cannot send Unknown evt=%d", evt);
		goto snd_pkt_err;
	}

	/* Send the pd_packet to protocol */
	pe->is_gcrc_received = false;
	log_dbg("Sending pkt, evt=%d", evt);
	if (pe->p.prot && pe->p.prot->policy_fwd_pkt)
		pe->p.prot->policy_fwd_pkt(pe->p.prot, msg_type, data, len);

	pe->last_sent_evt = evt;
	if (is_crc_timer_req)
		pe_start_timer(pe, CRC_RECEIVE_TIMER, PE_TIME_RECEIVE);
snd_pkt_err:
	return ret;
}

void pe_change_state_to_snk_or_src_ready(struct policy_engine *pe)
{
	if (pe->cur_prole == POWER_ROLE_SOURCE)
		pe_change_state(pe, PE_SRC_READY);
	else if (pe->cur_prole == POWER_ROLE_SINK)
		pe_change_state(pe, PE_SNK_READY);
	else {
		log_err("Unexpected power role %d!!", pe->cur_prole);
		pe_change_state(pe, ERROR_RECOVERY);
	}
}

static void pe_change_state_to_snk_or_src_reset(struct policy_engine *pe)
{
	if (pe->cur_prole == POWER_ROLE_SOURCE)
		pe_change_state(pe, PE_SRC_HARD_RESET);
	else if (pe->cur_prole == POWER_ROLE_SINK)
		pe_change_state(pe, PE_SNK_HARD_RESET);
	else {
		log_err("Unexpected power role %d!!", pe->cur_prole);
		pe_change_state(pe, ERROR_RECOVERY);
	}
}

static void pe_change_state_to_soft_reset(struct policy_engine *pe)
{
	if (pe->cur_prole == POWER_ROLE_SOURCE)
		pe_change_state(pe, PE_SRC_SEND_SOFT_RESET);
	else if (pe->cur_prole == POWER_ROLE_SINK)
		pe_change_state(pe, PE_SNK_SEND_SOFT_RESET);
	else {
		log_err("Unexpected power role %d!!", pe->cur_prole);
		pe_change_state(pe, ERROR_RECOVERY);
	}
}

static inline void policy_prot_update_data_role(struct policy_engine *pe,
				enum data_role drole)
{
	if (pe->p.prot->policy_update_data_role)
		pe->p.prot->policy_update_data_role(pe->p.prot, drole);
}

static inline void policy_prot_update_power_role(struct policy_engine *pe,
				enum pwr_role prole)
{
	if (pe->p.prot->policy_update_power_role)
		pe->p.prot->policy_update_power_role(pe->p.prot, prole);
}

static enum data_role pe_get_data_role(struct policy *p)
{
	struct policy_engine *pe = container_of(p, struct policy_engine, p);
	enum data_role drole;

	mutex_lock(&pe->pe_lock);
	drole = pe->cur_drole;
	mutex_unlock(&pe->pe_lock);
	return drole;
}

static enum pwr_role pe_get_power_role(struct policy *p)
{
	struct policy_engine *pe = container_of(p, struct policy_engine, p);
	enum pwr_role prole;

	mutex_lock(&pe->pe_lock);
	prole = pe->cur_prole;
	mutex_unlock(&pe->pe_lock);
	return prole;
}

static void pe_set_data_role(struct policy_engine *pe, enum data_role role)
{
	if (pe->cur_drole == role)
		return;

	pe->cur_drole = role;
	devpolicy_update_data_role(pe->p.dpm, role);
	/* If role swap, no need to update protocol */
	if (role != DATA_ROLE_SWAP) {
		/* Update the protocol */
		policy_prot_update_data_role(pe, role);
	}
}

static void pe_set_power_role(struct policy_engine *pe, enum pwr_role role)
{
	if (pe->cur_prole == role)
		return;

	pe->cur_prole = role;
	devpolicy_update_power_role(pe->p.dpm, role);
	/* If role swap, no need to update protocol */
	if (role != POWER_ROLE_SWAP) {
		/* Update the protocol */
		policy_prot_update_power_role(pe, role);
	}
}

static void pe_handle_dpm_event(struct policy_engine *pe,
					enum devpolicy_mgr_events evt)
{

	log_info("event - %d\n", evt);
	mutex_lock(&pe->pe_lock);
	switch (evt) {
	case DEVMGR_EVENT_UFP_CONNECTED:
		log_dbg(" UFP - Connected ");
		pe_set_data_role(pe, DATA_ROLE_UFP);
		pe_set_power_role(pe, POWER_ROLE_SINK);
		if (pe->cur_state == PE_STATE_NONE) {
			pe_change_state(pe, PE_SNK_STARTUP);
		} else {
			/*
			 * This could occur due to error in pe state.
			 * Transition to sink default.
			 */
			log_err("PE in wrong state=%d on ufp connect",
					pe->cur_state);
			pe_do_complete_reset(pe);
			pe_do_dpm_reset_entry(pe);
			pe_change_state(pe, PE_SNK_TRANSITION_TO_DEFAULT);
		}
		break;
	case DEVMGR_EVENT_DFP_CONNECTED:
		log_dbg(" DFP - Connected ");
		pe_set_data_role(pe, DATA_ROLE_DFP);
		pe_set_power_role(pe, POWER_ROLE_SOURCE);
		if (pe->cur_state == PE_STATE_NONE) {
			pe_change_state(pe, PE_SRC_STARTUP);
		} else {
			/*
			 * This could occur due to error in pe state.
			 * Transition to src default.
			 */
			log_err("PE in wrong state=%d on dfp connect",
					pe->cur_state);
			pe_do_complete_reset(pe);
			pe_do_dpm_reset_entry(pe);
			pe_change_state(pe, PE_SRC_TRANSITION_TO_DEFAULT);
		}
		break;
	case DEVMGR_EVENT_UFP_DISCONNECTED:
	case DEVMGR_EVENT_DFP_DISCONNECTED:
		pe_change_state(pe, PE_STATE_NONE);
		break;
	case DEVMGR_EVENT_VBUS_OFF:
		log_dbg("VBUS turned OFF");
		if (pe->cur_state == PE_SNK_WAIT_FOR_HARD_RESET_VBUS_OFF) {
			/* VBUS off due to reset, move to discovery*/
			pe_cancel_timer(pe, VBUS_CHECK_TIMER);
			pe_change_state(pe, PE_SNK_DISCOVERY);
		} else if (pe->cur_state == PE_PRS_SRC_SNK_TRANSITION_TO_OFF) {
			pe_cancel_timer(pe, VBUS_CHECK_TIMER);
			pe_change_state(pe, PE_PRS_SRC_SNK_ASSERT_RD);
		}
		break;

	case DEVMGR_EVENT_VBUS_ON:
		log_dbg("VBUS turned ON");
		if (pe->cur_state == PE_SNK_DISCOVERY) {
			/* VBUS on, wait for scrcap*/
			pe_change_state(pe, PE_SNK_WAIT_FOR_CAPABILITIES);
		} else if (pe->cur_state == PE_SRC_WAIT_FOR_VBUS) {
			/* VBUS on, send SrcCap*/
			pe_cancel_timer(pe, VBUS_CHECK_TIMER);
			pe_change_state(pe, PE_SRC_SEND_CAPABILITIES);

		} else if (pe->cur_state == PE_PRS_SNK_SRC_SOURCE_ON) {
			/* VBUS on, send PS_RDY*/
			pe_cancel_timer(pe, VBUS_CHECK_TIMER);
			pe_send_packet(pe, NULL, 0,
					PD_CTRL_MSG_PS_RDY, PE_EVT_SEND_PS_RDY);
		}
		break;

	case DEVMGR_EVENT_DR_SWAP:
		if (pe->is_modal_operation) {
			log_warn("Cannot perform DR_SWAP in modal operation");
		} else if (pe->cur_state == PE_SRC_READY
				|| pe->cur_state == PE_SNK_READY) {
			if (pe->cur_drole == DATA_ROLE_DFP)
				pe_change_state(pe,
					PE_DRS_DFP_UFP_SEND_DR_SWAP);
			else if (pe->cur_drole == DATA_ROLE_UFP)
				pe_change_state(pe,
					PE_DRS_UFP_DFP_SEND_DR_SWAP);
			else {
				log_err("Error in current data role %d",
					pe->cur_drole);
				pe_change_state_to_snk_or_src_reset(pe);
			}
		} else
			log_info("PE not ready to process DR_SWAP");
		break;

	case DEVMGR_EVENT_PR_SWAP:
		if (pe->cur_state == PE_SNK_READY)
			pe_change_state(pe, PE_PRS_SNK_SRC_SEND_PR_SWAP);
		else if (pe->cur_state == PE_SRC_READY)
			pe_change_state(pe, PE_PRS_SRC_SNK_SEND_PR_SWAP);
		else
			log_info("Cann't trigger PR_SWAP in state=%d",
					pe->cur_state);
		break;
	case DEVMGR_EVENT_VCONN_SWAP:
		if (pe->cur_state == PE_SNK_READY ||
			pe->cur_state == PE_SRC_READY) {
			log_dbg("Received VCS request from dpm!!");
			pe_change_state(pe, PE_VCS_SEND_SWAP);
		} else {
			log_info("Cann't trigger VCONN_SWAP in state=%d",
					pe->cur_state);
		}
		break;
	default:
		log_err("Unknown dpm event=%d\n", evt);
	}
	mutex_unlock(&pe->pe_lock);
}

static int pe_dpm_notification(struct policy *p,
				enum devpolicy_mgr_events evt)
{
	struct policy_engine *pe = container_of(p, struct policy_engine, p);
	pe_handle_dpm_event(pe, evt);
	return 0;
}

/************************** PE helper functions ******************/

static void pe_fill_default_self_sink_cap(struct policy_engine *pe)
{
	struct pd_sink_fixed_pdo pdo = { 0 };

	/* setting default pdo as vsafe5V with 500mA*/
	pdo.data_role_swap = FEATURE_SUPPORTED;
	pdo.usb_comm = FEATURE_SUPPORTED;
	pdo.ext_powered = FEATURE_NOT_SUPPORTED;
	pdo.higher_cap = FEATURE_NOT_SUPPORTED;
	pdo.dual_role_pwr = FEATURE_SUPPORTED;

	pdo.supply_type = SUPPLY_TYPE_FIXED;
	pdo.max_cur = CURRENT_TO_DATA_OBJ(500);
	pdo.volt = (VOLT_TO_DATA_OBJ(5000) >>
				SNK_FSPDO_VOLT_SHIFT);

	memcpy(pe->self_snk_pdos.pdo, &pdo, sizeof(pdo));
	pe->self_snk_pdos.num_pdos = 1;
}


static void pe_fetch_self_sink_cap(struct policy_engine *pe)
{
	int ret = 0;
	int i;
	struct power_caps pcaps;
	struct pd_sink_fixed_pdo pdo[MAX_NUM_DATA_OBJ] = { {0} };


	ret = devpolicy_get_snkpwr_caps(pe->p.dpm, &pcaps);
	if (ret < 0 || pcaps.n_cap <= 0) {
		log_info("No PDO's from dpm setting default vasafe5v\n");
		pe_fill_default_self_sink_cap(pe);
		return;
	}

	if (pcaps.n_cap > MAX_NUM_DATA_OBJ)
		pcaps.n_cap = MAX_NUM_DATA_OBJ;

	/*
	 * As per PD v1.1 spec except first pdo, all other Fixed Supply Power
	 * Data Objects shall set bits 29...20 to zero.
	 */
	/*
	 * FIXME: DPM should provide info on USB capable and
	 * higher power support required.
	 */
	pdo[0].data_role_swap = FEATURE_SUPPORTED;
	pdo[0].usb_comm = FEATURE_SUPPORTED;
	pdo[0].ext_powered = FEATURE_NOT_SUPPORTED;
	pdo[0].higher_cap = FEATURE_NOT_SUPPORTED;
	pdo[0].dual_role_pwr = FEATURE_SUPPORTED;

	for (i = 0; i < pcaps.n_cap; i++) {

		pdo[i].max_cur = CURRENT_TO_DATA_OBJ(pcaps.pcap[i].ma);
		pdo[i].volt = (VOLT_TO_DATA_OBJ(pcaps.pcap[i].mv) >>
					SNK_FSPDO_VOLT_SHIFT);
		pdo[i].supply_type = pcaps.pcap[i].psy_type;
	}

	memcpy(pe->self_snk_pdos.pdo, pdo,
			pcaps.n_cap * sizeof(struct pd_sink_fixed_pdo));
	pe->self_snk_pdos.num_pdos = pcaps.n_cap;
}

static void pe_send_self_sink_caps(struct policy_engine *pe)
{
	int size, ret;

	if (pe->self_snk_pdos.num_pdos <= 0)
		pe_fetch_self_sink_cap(pe);

	size = pe->self_snk_pdos.num_pdos * 4;

	ret = pe_send_packet(pe, pe->self_snk_pdos.pdo, size,
			PD_DATA_MSG_SINK_CAP, PE_EVT_SEND_SNK_CAP);
	if (ret < 0)
		log_err("Failed to send sink caps");
	return;
}

/*
 * This function will pick one to received caps fron source
 * based on current sysntem required caps given by DPM.
 */
static int pe_sink_set_request_cap(struct policy_engine *pe,
					struct pe_port_pdos *rcv_pdos,
					struct power_cap *pcap,
					struct pe_req_cap *rcap)
{
	int i;
	int mv = 0;
	int ma = 0;
	bool is_mv_match = false;

	rcap->cap_mismatch = true;

	for (i = 0; i < rcv_pdos->num_pdos; i++) {
		/*
		 * FIXME: should be selected based on the power (V*I) cap.
		 */
		mv = DATA_OBJ_TO_VOLT(rcv_pdos->pdo[i]);
		if (mv == pcap->mv) {
			is_mv_match = true;
			ma = DATA_OBJ_TO_CURRENT(rcv_pdos->pdo[i]);
			if (ma == pcap->ma) {
				rcap->cap_mismatch = false;
				break;
			} else if (ma > pcap->ma) {
				/*
				 * If the ma in the pdo is greater than the
				 * required ma, exit from the loop as the pdo
				 * capabilites are in ascending order.
				 */
				break;
			}
		} else if (mv > pcap->mv) {
			/*
			 * If the mv value in the pdo is greater than the
			 * required mv, exit from the loop as the pdo
			 * capabilites are in ascending order.
			 */
			break;
		}
	}

	if (!is_mv_match)
		i = 0; /* to select 1st pdo, Vsafe5V */

	if (!rcap->cap_mismatch) {
		rcap->obj_pos = i + 1; /* obj pos always starts from 1 */
		rcap->max_ma = pcap->ma;
		rcap->op_ma = pcap->ma;
	} else  {
		/* if cur is not match, select the previous pdo */
		rcap->obj_pos = i ? i : 1;
		rcap->op_ma =
			DATA_OBJ_TO_CURRENT(rcv_pdos->pdo[rcap->obj_pos - 1]);

		if (pcap->ma < rcap->op_ma) {
			rcap->cap_mismatch = false;
			rcap->max_ma = rcap->op_ma;
			rcap->op_ma = pcap->ma;
		} else {
			rcap->max_ma = pcap->ma;
		}
	}

	rcap->mv = DATA_OBJ_TO_VOLT(rcv_pdos->pdo[rcap->obj_pos - 1]);

	return 0;
}

/*
 * This function will read the port partner capabilities and
 * save it for further use.
 */
static void pe_store_port_partner_caps(struct policy_engine *pe,
					struct pe_port_pdos *pdos)
{
	struct pd_fixed_supply_pdo *pdo;
	int num;

	if (!pdos) {
		log_err("No pdos");
		return;
	}
	for (num = 0; num < pdos->num_pdos; num++) {
		pdo = (struct pd_fixed_supply_pdo *) &pdos->pdo[num];

		if (pdo->fixed_supply == SUPPLY_TYPE_FIXED) {
			pe->pp_caps.pp_is_dual_prole = pdo->dual_role_pwr;
			pe->pp_caps.pp_is_dual_drole = pdo->data_role_swap;
			pe->pp_caps.pp_is_ext_pwrd = pdo->ext_powered;

			log_info("dual_prole=%d, dual_drole=%d, ext_pwrd=%d",
					pe->pp_caps.pp_is_dual_prole,
					pe->pp_caps.pp_is_dual_drole,
					pe->pp_caps.pp_is_ext_pwrd);
			return;
		}
	}
	log_warn("port partner is not fixed supply, num_pdos=%d",
					pdos->num_pdos);
}

static void pe_sink_evaluate_src_caps(struct policy_engine *pe,
					struct pe_port_pdos *src_pdos)
{
	struct pe_req_cap *rcap = &pe->self_sink_req_cap;
	struct power_cap dpm_suggested_cap;
	int ret;

	/* Get current system required caps from DPM*/
	ret = devpolicy_get_snkpwr_cap(pe->p.dpm, &dpm_suggested_cap);
	if (ret) {
		log_err("Error in getting max sink pwr cap from DPM %d",
				ret);
		goto error;
	}

	ret = pe_sink_set_request_cap(pe, src_pdos, &dpm_suggested_cap, rcap);
	if (ret < 0) {
		log_err("Failed to get request caps");
		goto error;
	}

	log_dbg("Request index=%d, volt=%u, op-cur=%u, max-cur=%u",
			rcap->obj_pos, rcap->mv, rcap->op_ma, rcap->max_ma);
	return;

error:
	memset(rcap, 0, sizeof(struct pe_req_cap));
	return;
}

static void pe_sink_create_request_msg_data(struct policy_engine *pe,
						u32 *data)
{
	struct pd_fixed_var_rdo *rdo = (struct pd_fixed_var_rdo *)data;
	struct pe_req_cap *rcap = &pe->self_sink_req_cap;

	rdo->obj_pos = rcap->obj_pos;
	rdo->cap_mismatch = rcap->cap_mismatch;
	rdo->op_cur = CURRENT_TO_DATA_OBJ(rcap->op_ma);
	rdo->max_cur = CURRENT_TO_DATA_OBJ(rcap->max_ma);

}

static int pe_send_srccap_cmd(struct policy_engine *pe)
{
	int ret;
	struct pd_fixed_supply_pdo *pdo;
	struct power_cap pcap;

	log_dbg("Sending SrcCap");
	/* TODO: Support multiple SrcCap PODs */
	ret = devpolicy_get_srcpwr_cap(pe->p.dpm, &pcap);
	if (ret) {
		log_err("Error in getting power capabilities from DPM");
		return ret;
	}
	pdo = (struct pd_fixed_supply_pdo *) &pe->self_src_pdos.pdo[0];
	memset(pdo, 0, sizeof(struct pd_fixed_supply_pdo));
	pdo->max_cur = CURRENT_TO_CAP_DATA_OBJ(pcap.ma); /* In 10mA units */
	pdo->volt = VOLT_TO_CAP_DATA_OBJ(pcap.mv); /* In 50mV units */
	pdo->peak_cur = 0; /* No peak current supported */
	pdo->dual_role_pwr = 1; /* Dual pwr role supported */
	pdo->data_role_swap = 1; /*Dual data role*/
	pdo->usb_comm = 1; /* USB communication supported */

	ret = pe_send_packet(pe, pdo, 4,
				PD_DATA_MSG_SRC_CAP, PE_EVT_SEND_SRC_CAP);
	pe->self_src_pdos.num_pdos = 1;
	return ret;
}

/************************ TImer related functions *****************/
static char *timer_to_str(enum pe_timers timer_type)
{
	switch (timer_type) {
	case BIST_CONT_MODE_TIMER:
		return "BIST_CONT_MODE_TIMER";
	case BIST_RECEIVE_ERROR_TIMER:
		return "BIST_RECEIVE_ERROR_TIMER";
	case BIST_START_TIMER:
		return "BIST_START_TIMER";
	case CRC_RECEIVE_TIMER:
		return "CRC_RECEIVE_TIMER";
	case DISCOVER_IDENTITY_TIMER:
		return "DISCOVER_IDENTITY_TIMER";
	case HARD_RESET_COMPLETE_TIMER:
		return "HARD_RESET_COMPLETE_TIMER";
	case NO_RESPONSE_TIMER:
		return "NO_RESPONSE_TIMER";
	case PS_HARD_RESET_TIMER:
		return "PS_HARD_RESET_TIMER";
	case PS_SOURCE_OFF_TIMER:
		return "PS_SOURCE_OFF_TIMER";
	case PS_SOURCE_ON_TIMER:
		return "PS_SOURCE_ON_TIMER";
	case PS_TRANSITION_TIMER:
		return "PS_TRANSITION_TIMER";
	case SENDER_RESPONSE_TIMER:
		return "SENDER_RESPONSE_TIMER";
	case SINK_ACTIVITY_TIMER:
		return "SINK_ACTIVITY_TIMER";
	case SINK_REQUEST_TIMER:
		return "SINK_REQUEST_TIMER";
	case SINK_WAIT_CAP_TIMER:
		return "SINK_WAIT_CAP_TIMER";
	case SOURCE_ACTIVITY_TIMER:
		return "SOURCE_ACTIVITY_TIMER";
	case SOURCE_CAPABILITY_TIMER:
		return "SOURCE_CAPABILITY_TIMER";
	case SWAP_RECOVERY_TIMER:
		return "SWAP_RECOVERY_TIMER";
	case SWAP_SOURCE_START_TIMER:
		return "SWAP_SOURCE_START_TIMER";
	case VCONN_ON_TIMER:
		return "VCONN_ON_TIMER";
	case VDM_MODE_ENTRY_TIMER:
		return "VDM_MODE_ENTRY_TIMER";
	case VDM_MODE_EXIT_TIMER:
		return "VDM_MODE_EXIT_TIMER";
	case VMD_RESPONSE_TIMER:
		return "VMD_RESPONSE_TIMER";
	case VBUS_CHECK_TIMER:
		return "VBUS_CHECK_TIMER";
	case SRC_RESET_RECOVER_TIMER:
		return "SRC_RESET_RECOVER_TIMER";
	case SRC_TRANSITION_TIMER:
		return "SRC_TRANSITION_TIMER";
	default:
		return "Unknown";
	}
}

static struct pe_timer *pe_get_timer(struct policy_engine *pe,
					enum pe_timers timer_type)
{
	struct pe_timer *cur_timer = &pe->timers[timer_type];

	if (cur_timer->timer_type == timer_type)
		return cur_timer;

	log_err("Timer %d not found", timer_type);
	return NULL;
}

static bool pe_is_timer_pending(struct policy_engine *pe,
				enum pe_timers timer_type)
{
	struct pe_timer *cur_timer;

	cur_timer = pe_get_timer(pe, timer_type);
	if (cur_timer)
		if (timer_pending(&cur_timer->timer))
			return true;

	return false;
}

static void pe_start_timer(struct policy_engine *pe,
				enum pe_timers timer_type, unsigned time)
{
	struct pe_timer *cur_timer;
	int ret;

	cur_timer = pe_get_timer(pe, timer_type);
	if (!cur_timer)
		return;

	if (timer_pending(&cur_timer->timer))
		log_warn("Timer %d already pending!!", timer_type);
	ret = mod_timer(&cur_timer->timer, jiffies + msecs_to_jiffies(time));
	log_dbg("%s, time=%u , mod_timer ret=%d",
			timer_to_str(timer_type), time, ret);

}

static void pe_cancel_timer(struct policy_engine *pe,
				enum pe_timers timer_type)
{
	struct pe_timer *cur_timer;

	cur_timer = pe_get_timer(pe, timer_type);
	if (!cur_timer)
		return;
	if (!del_timer(&cur_timer->timer))
		log_warn("Timer %s not active!!", timer_to_str(timer_type));
}

static void pe_timer_expire_worker(struct work_struct *work)
{
	struct pe_timer *cur_timer = container_of(work,
					struct pe_timer, work);
	struct policy_engine *pe = (struct policy_engine *) cur_timer->data;
	enum pe_timers type = cur_timer->timer_type;

	mutex_lock(&pe->pe_lock);
	log_dbg("%s expiration handling!!!",
			timer_to_str(type));
	switch (type) {

	case SENDER_RESPONSE_TIMER:
		if (pe->cur_state == PE_SRC_SEND_CAPABILITIES) {
			if (pe->is_gcrc_received)
				pe_change_state(pe, PE_SRC_HARD_RESET);
			else
				pe_change_state(pe, PE_SRC_DISCOVERY);
			break;
		} else if (pe->cur_state == PE_DRS_UFP_DFP_SEND_DR_SWAP ||
				pe->cur_state == PE_DRS_DFP_UFP_SEND_DR_SWAP) {
			log_info("Didn't get response for dr_swap");
			pe_change_state_to_snk_or_src_ready(pe);
			break;
		} else if (pe->cur_state == PE_PRS_SRC_SNK_SEND_PR_SWAP ||
				pe->cur_state == PE_PRS_SNK_SRC_SEND_PR_SWAP) {
			log_info("Didn't get response for pr_swap");
			pe_change_state_to_snk_or_src_ready(pe);
			break;
		} else if (pe->cur_state == PE_SRC_GET_SINK_CAP
				|| pe->cur_state == PE_DR_SRC_GET_SOURCE_CAP) {
			pe_change_state(pe, PE_SRC_READY);
			break;
		} else if (pe->cur_state == PE_DR_SNK_GET_SINK_CAP) {
			pe_change_state(pe, PE_SNK_READY);
			break;
		} else if (pe->cur_state == PE_VCS_SEND_SWAP) {
			log_info("Accept not received for VCS Request!");
			pe_change_state_to_snk_or_src_ready(pe);
			break;
		} else if (pe->cur_state == PE_SRC_SEND_SOFT_RESET
				|| pe->cur_state == PE_SNK_SEND_SOFT_RESET)
			log_info("Soft_Reset failed, Issue hard reset");

		log_warn("%s expired move to hard reset",
				timer_to_str(type));
		pe_change_state_to_snk_or_src_reset(pe);
		break;

	case SINK_WAIT_CAP_TIMER:
		if (pe->cur_state == PE_SNK_WAIT_FOR_CAPABILITIES) {
			log_dbg("%s expired,HardResetCount=%d",
					timer_to_str(type),
					pe->hard_reset_counter);
			if (pe->hard_reset_counter <= PE_N_HARD_RESET_COUNT)
				pe_change_state(pe, PE_SNK_HARD_RESET);
			else
				pe_change_state(pe, ERROR_RECOVERY);
		} else
			log_warn("%s expired in wrong state=%d",
				timer_to_str(type), pe->cur_state);
		break;

	case SINK_REQUEST_TIMER:
			if (pe->cur_state == PE_SNK_READY) {
				/* As source didnt send SrcCaps,
				 * resend the request.
				 */
				pe_change_state(pe, PE_SNK_SELECT_CAPABILITY);
			} else
				log_warn("%s expired in wrong state=%d",
					timer_to_str(type), pe->cur_state);
		break;

	case NO_RESPONSE_TIMER:
		log_info("%s Expired!!, state=%d",
				timer_to_str(type), pe->cur_state);
		pe->is_no_response_timer_expired = true;

		if (pe->hard_reset_counter <= PE_N_HARD_RESET_COUNT)
			pe_change_state_to_snk_or_src_reset(pe);
		else
				pe_change_state(pe, ERROR_RECOVERY);

		break;

	case PS_TRANSITION_TIMER:
			if (pe->cur_state == PE_SNK_TRANSITION_SINK)
				pe_change_state(pe, PE_SNK_HARD_RESET);
			else
				log_warn("%s expired in wrong state=%d",
					timer_to_str(type), pe->cur_state);
		break;

	case PS_HARD_RESET_TIMER:
		if (pe->cur_state == PE_SRC_HARD_RESET
			|| pe->cur_state == PE_SRC_HARD_RESET_RECEIVED)
			pe_change_state(pe, PE_SRC_TRANSITION_TO_DEFAULT);
		else
			log_warn("%s expired in wrong state=%d",
				timer_to_str(type), pe->cur_state);
		break;

	case HARD_RESET_COMPLETE_TIMER:
		if (pe->cur_state == PE_SNK_HARD_RESET)
			pe_change_state(pe, PE_SNK_TRANSITION_TO_DEFAULT);
		else
			log_warn("%s expired in wrong state=%d",
				timer_to_str(type), pe->cur_state);
		break;

	case BIST_CONT_MODE_TIMER:
		break;

	case BIST_RECEIVE_ERROR_TIMER:
		break;

	case BIST_START_TIMER:
		break;

	case CRC_RECEIVE_TIMER:
		if (pe->cur_state == PE_SRC_SEND_CAPABILITIES) {
			pe_change_state(pe, PE_SRC_DISCOVERY);
			break;
		} else if (pe->cur_state == PE_PRS_SRC_SNK_WAIT_SOURCE_ON) {
			log_err("PS_RDY Sent fail during pr_swap");
			pe_change_state(pe, PE_ERROR_RECOVERY);
			break;
		} else if (pe->cur_state == PE_PRS_SNK_SRC_SOURCE_ON) {
			log_err("PS_RDY Sent fail during pr_swap");
			pe_change_state(pe, PE_ERROR_RECOVERY);
			break;
		} else if (pe->cur_state == PE_SRC_SEND_SOFT_RESET
				|| pe->cur_state == PE_SNK_SEND_SOFT_RESET
				|| pe->cur_state == PE_SNK_SOFT_RESET
				||  pe->cur_state == PE_SRC_SOFT_RESET) {
			log_info("SOFT_RESET failed!!");
			/* Issue Hard Reset */
			pe_change_state_to_snk_or_src_reset(pe);
			break;
		}
		if (pe_is_timer_pending(pe, SENDER_RESPONSE_TIMER))
			pe_cancel_timer(pe, SENDER_RESPONSE_TIMER);
		log_warn("%s expired in state=%d",
				timer_to_str(type), pe->cur_state);
		pe_change_state_to_soft_reset(pe);
		break;

	case DISCOVER_IDENTITY_TIMER:
		break;

	case PS_SOURCE_OFF_TIMER:
		if (pe->cur_state == PE_PRS_SNK_SRC_TRANSITION_TO_OFF) {
			log_err("No PS_RDY from src during pr swap!!");
			pe_change_state(pe, PE_ERROR_RECOVERY);
		}
		break;

	case PS_SOURCE_ON_TIMER:
		if (pe->cur_state == PE_PRS_SRC_SNK_WAIT_SOURCE_ON) {
			log_err("PR_SWAP: PS_RDY not received from sink");
			pe_change_state(pe, PE_ERROR_RECOVERY);
		}
		break;

	case SINK_ACTIVITY_TIMER:
		break;

	case SOURCE_ACTIVITY_TIMER:
		break;

	case SOURCE_CAPABILITY_TIMER:
		if (pe->cur_state == PE_SRC_DISCOVERY) {
			if (pe->src_caps_couner <= PE_N_CAPS_COUNT)
				pe_change_state(pe, PE_SRC_SEND_CAPABILITIES);
			else
				pe_change_state(pe, PE_SRC_DISABLED);
		} else
			log_warn("%s expired in wrong state=%d",
				timer_to_str(type), pe->cur_state);
		break;

	case SWAP_SOURCE_START_TIMER:
		if (pe->cur_state == PE_SRC_STARTUP) {
			/*
			 * Move to PE_SRC_WAIT_FOR_VBUS to check
			 * VBUS before sending SrcCap.
			 */
			pe_change_state(pe, PE_SRC_WAIT_FOR_VBUS);
		} else
			log_warn("%s expired in wrong state=%d",
				timer_to_str(type), pe->cur_state);
		break;
	case VCONN_ON_TIMER:
		if (pe->cur_state == PE_VCS_WAIT_FOR_VCONN) {
			pe_change_state_to_snk_or_src_reset(pe);
		} else {
			log_warn("%s expired in wrong state=%d",
					timer_to_str(type), pe->cur_state);
		}
		break;
	case SWAP_RECOVERY_TIMER:
	case VDM_MODE_ENTRY_TIMER:
	case VDM_MODE_EXIT_TIMER:
		break;
	case VMD_RESPONSE_TIMER:
		if (pe->cur_state == PE_DFP_UFP_VDM_IDENTITY_REQUEST
			|| pe->cur_state == PE_DFP_VDM_SVIDS_REQUEST
			|| pe->cur_state == PE_DFP_VDM_MODES_REQUEST
			|| pe->cur_state == PE_DFP_VDM_MODES_ENTRY_REQUEST
			|| pe->cur_state == PE_DFP_VDM_STATUS_REQUEST
			|| pe->cur_state == PE_DFP_VDM_CONF_REQUEST) {
			log_warn("no response for VDM");
			pe_change_state_to_snk_or_src_ready(pe);
		} else
			log_warn("%s expired in wrong state=%d",
				timer_to_str(type), pe->cur_state);
		break;

	case VBUS_CHECK_TIMER:
		if (pe->cur_state == PE_SRC_WAIT_FOR_VBUS) {
			log_warn("System did not enable vbus in source mode");
			if (pe->hard_reset_counter > PE_N_HARD_RESET_COUNT)
				pe_change_state(pe, ERROR_RECOVERY);
			else
				pe_change_state(pe, PE_SRC_HARD_RESET);

		} else if (pe->cur_state == PE_PRS_SRC_SNK_TRANSITION_TO_OFF) {
			log_warn("VBUS not off!!!");
			pe_change_state(pe, ERROR_RECOVERY);

		} else if (pe->cur_state == PE_PRS_SNK_SRC_SOURCE_ON) {
			log_warn("VBUS not on!!!");
			pe_change_state(pe, PE_ERROR_RECOVERY);

		} else if (pe->cur_state ==
				PE_SNK_WAIT_FOR_HARD_RESET_VBUS_OFF) {
			log_warn("Vbus not off for %dmsec, moving to state=%d",
					T_SRC_RECOVER_MIN, PE_SNK_DISCOVERY);
			pe_change_state(pe, PE_SNK_DISCOVERY);

		} else
			log_warn("%s expired in wrong state=%d",
				timer_to_str(type), pe->cur_state);
		break;
	case SRC_RESET_RECOVER_TIMER:
		if (pe->cur_state == PE_SRC_TRANSITION_TO_DEFAULT) {
			pe_process_state_pe_src_transition_to_default_exit(pe);
		} else
			log_warn("%s expired in wrong state=%d",
				timer_to_str(type), pe->cur_state);
		break;

	case SRC_TRANSITION_TIMER:
		if (pe->cur_state == PE_SRC_TRANSITION_SUPPLY) {
			log_info("Sending PS_RDY");
			pe_send_packet(pe, NULL, 0,
				PD_CTRL_MSG_PS_RDY, PE_EVT_SEND_PS_RDY);

		} else if (pe->cur_state == PE_PRS_SRC_SNK_TRANSITION_TO_OFF) {
			/* Turn off the VBUS */
			devpolicy_set_vbus_state(pe->p.dpm, false);

			pe_start_timer(pe, VBUS_CHECK_TIMER, T_SAFE_0V_MAX);
			pe_set_power_role(pe, POWER_ROLE_SWAP);
		} else
			log_warn("%s expired in wrong state=%d",
				timer_to_str(type), pe->cur_state);
		break;

	default:
		log_warn("Unknown timer=%d expired!!!",
				cur_timer->timer_type);
	}
	mutex_unlock(&pe->pe_lock);
}

static void pe_timer_expire_callback(unsigned long data)
{
	struct pe_timer *cur_timer = (struct pe_timer *) data;

	log_dbg("%s expired!!!", timer_to_str(cur_timer->timer_type));
	schedule_work(&cur_timer->work);
}

/************************ Sink State Handlers ********************/

static void pe_process_state_pe_snk_startup(struct policy_engine *pe)
{
	/* Disable PD if enabled, till PE is ready to receive src_caps */
	pe_enable_pd(pe, false);

	/* Reset protocol layer */
	pe_send_packet(pe, NULL, 0, PD_CMD_PROTOCOL_RESET,
				PE_EVT_SEND_PROTOCOL_RESET);
	/* TODO: Check condition for PE_DB_CP_CHECK_FOR_VBUS */

	/*
	 * When PE come to this state from hard reset, then pe
	 * should wait for source to off VBUS on reset.
	 */
	if (pe->prev_state == PE_SNK_TRANSITION_TO_DEFAULT) {
		/* This is due to hard reaset, wait for vbus off*/
		pe_change_state(pe, PE_SNK_WAIT_FOR_HARD_RESET_VBUS_OFF);
	} else {
		/*This is due to connect */
		pe_change_state(pe, PE_SNK_DISCOVERY);
	}
}

static void
pe_process_state_pe_snk_wait_for_hard_reset_vbus_off(struct policy_engine *pe)
{
	if (devpolicy_get_vbus_state(pe->p.dpm)) {
		log_info("Vbus present, wait for VBUS off due to reset");
		pe_start_timer(pe, VBUS_CHECK_TIMER, T_SRC_RECOVER_MIN);
		return;
	}
	log_dbg("VBUS off, Move to PE_SNK_DISCOVERY");
	pe_change_state(pe, PE_SNK_DISCOVERY);
}

static void pe_process_state_pe_snk_discovery(struct policy_engine *pe)
{
	if (!devpolicy_get_vbus_state(pe->p.dpm)) {
		log_info("Vbus not present, wait for VBUS On");
		return;
	}
	log_dbg("VBUS present, Move to PE_SNK_WAIT_FOR_CAPABILITIES");
	pe_change_state(pe, PE_SNK_WAIT_FOR_CAPABILITIES);
}

static void
pe_process_state_pe_snk_wait_for_capabilities(struct policy_engine *pe)
{
	unsigned time_out;

	/* Check condition for ERROR_RECOVERY */
	if (pe->is_typec_port
		&& pe->is_no_response_timer_expired
		&& pe->hard_reset_counter > PE_N_HARD_RESET_COUNT) {

		log_info("ERROR_RECOVERY condition!!!");
		pe_change_state(pe, ERROR_RECOVERY);
		return;
	}
	pe_enable_pd(pe, true);

	/* Start SinkWaitCapTimer */
	if (pe->is_typec_port)
		time_out = PE_TIME_TYPEC_SINK_WAIT_CAP;
	else
		time_out = PE_TIME_SINK_WAIT_CAP;
	pe_start_timer(pe, SINK_WAIT_CAP_TIMER, time_out);
}

static void
pe_process_state_pe_snk_evaluate_capabilities(struct policy_engine *pe)
{
	pe_sink_evaluate_src_caps(pe, &pe->pp_src_pdos);

	/* As caps got evaluated, mode to pe_snk_select_capability*/
	pe_change_state(pe, PE_SNK_SELECT_CAPABILITY);
}

static void
pe_process_state_pe_snk_select_capability(struct policy_engine *pe)
{
	int ret;
	u32 data = 0;

	/* make request message and send to PE -> protocol */
	pe_sink_create_request_msg_data(pe, &data);

	ret = pe_send_packet(pe, &data, sizeof(data),
				PD_DATA_MSG_REQUEST, PE_EVT_SEND_REQUEST);
	if (ret < 0) {
		log_err("Error in sending request packet!\n");
		return;
	}
	log_dbg("PD_DATA_MSG_REQUEST Sent, %x\n", data);
}

static void
pe_process_state_pe_snk_transition_sink(struct policy_engine *pe)
{
	/* Start PS_TRANSITION_TIMER and wait for PS_RDY */
	pe_start_timer(pe, PS_TRANSITION_TIMER, PE_TIME_PS_TRANSITION);
	devpolicy_update_charger(pe->p.dpm, 0, true);

	/* TODO: Intimate DPM about new power supply params */
	return;
}

static void
pe_process_state_pe_snk_ready(struct policy_engine *pe)
{
	struct pe_req_cap *rcap = &pe->self_sink_req_cap;

	log_info("In SNK_READY");

	/*
	 * If Wait received for request msg then start
	 * sink_request_timer to resend the request.
	 */
	if (pe->prev_state == PE_SNK_SELECT_CAPABILITY
		&& pe->last_rcv_evt == PE_EVT_RCVD_WAIT) {
		pe_start_timer(pe, SINK_REQUEST_TIMER,
				PE_TIME_SINK_REQUEST);
	} else if (pe->prev_state == PE_SNK_TRANSITION_SINK) {
		/* Set new inlimit */
		devpolicy_update_charger(pe->p.dpm, rcap->op_ma, 0);
		pe_store_port_partner_caps(pe, &pe->pp_src_pdos);
		schedule_delayed_work(&pe->post_ready_work,
				msecs_to_jiffies(PE_AUTO_TRIGGERING_DELAY));
	} else
		schedule_delayed_work(&pe->post_ready_work, 0);
}

static void
pe_process_state_pe_snk_hard_reset(struct policy_engine *pe)
{
	pe_do_self_reset(pe);
	pe->hard_reset_counter++;
	pe_send_packet(pe, NULL, 0, PD_CMD_HARD_RESET,
			PE_EVT_SEND_HARD_RESET);
	pe_start_timer(pe, HARD_RESET_COMPLETE_TIMER,
		PE_TIME_HARD_RESET + PE_TIME_HARD_RESET_COMPLETE);
}

static void
pe_process_state_pe_snk_transition_to_default(struct policy_engine *pe)
{
	/* TODO: Notify DPM about reset to default roles and power*/
	/*TODO: set data role to UFP, VCONN off*/
	pe_do_dpm_reset_entry(pe);
	pe_do_dpm_reset_complete(pe);
	pe_start_timer(pe, NO_RESPONSE_TIMER, PE_TIME_NO_RESPONSE);
	pe_change_state(pe, PE_SNK_STARTUP);
}

static void
pe_process_state_pe_snk_give_sink_cap(struct policy_engine *pe)
{

	pe_send_self_sink_caps(pe);
}

static void
pe_process_state_pe_snk_get_source_cap(struct policy_engine *pe)
{

	pe_send_packet(pe, NULL, 0, PD_CTRL_MSG_GET_SINK_CAP,
					PE_EVT_SEND_GET_SINK_CAP);
}

static void
pe_process_state_pe_snk_hard_reset_received(struct policy_engine *pe)
{
	pe_do_self_reset(pe);
	pe_change_state(pe, PE_SNK_TRANSITION_TO_DEFAULT);
}

/********** Source Port State Handlers **********************/
static void pe_process_state_pe_src_wait_for_vbus(struct policy_engine *pe)
{
	if (!devpolicy_get_vbus_state(pe->p.dpm)) {
		log_dbg("VBUS not present, Start %s",
				timer_to_str(VBUS_CHECK_TIMER));
		pe_start_timer(pe, VBUS_CHECK_TIMER, T_SAFE_5V_MAX);
	} else {
		log_dbg("VBUS present, move to PE_SRC_SEND_CAPABILITIES");
		pe_change_state(pe, PE_SRC_SEND_CAPABILITIES);
	}
}

static void pe_process_state_pe_src_startup(struct policy_engine *pe)
{

	/* Disable PD if enabled, till PE is ready to send src_caps */
	pe_enable_pd(pe, false);

	/* Reset caps counter */
	pe->src_caps_couner = 0;
	/* Reset protocol layer */
	pe_send_packet(pe, NULL, 0, PD_CMD_PROTOCOL_RESET,
				PE_EVT_SEND_PROTOCOL_RESET);

	/* Start swap src start timer ( only after pr swap) */
	if (pe->prev_state == PE_PRS_SNK_SRC_SOURCE_ON) {
		pe_start_timer(pe, SWAP_SOURCE_START_TIMER,
					PE_TIME_SWAP_SOURCE_START);
	} else {
		/*
		 * Move to PE_SRC_WAIT_FOR_VBUS to check
		 * VBUS before sending SrcCap.
		 */
		pe_change_state(pe, PE_SRC_WAIT_FOR_VBUS);
	}
}

static void pe_process_state_pe_src_discovery(struct policy_engine *pe)
{
	unsigned time_out;

	if ((!pe->is_typec_port
		|| (pe->is_typec_port && !pe->is_pp_pd_capable))
		&& pe->is_no_response_timer_expired
		&& (pe->hard_reset_counter > PE_N_HARD_RESET_COUNT)) {
		/* Treat port partner as non PD */
		pe_change_state(pe, PE_SRC_DISABLED);
		return;
	}
	if (pe->is_typec_port)
		time_out = PE_TIME_TYPEC_SEND_SOURCE_CAP - PE_TIME_RECEIVE;
	else
		time_out = PE_TIME_SEND_SOURCE_CAP - PE_TIME_RECEIVE;
	pe_start_timer(pe, SOURCE_CAPABILITY_TIMER, time_out);

}

static void
pe_process_state_pe_src_send_capabilities(struct policy_engine *pe)
{
	if ((!pe->is_typec_port
		|| (pe->is_typec_port && !pe->is_pp_pd_capable))
			&& pe->is_no_response_timer_expired
			&& (pe->hard_reset_counter > PE_N_HARD_RESET_COUNT)) {
		/* Treat port partner as non PD */
		pe_change_state(pe, PE_SRC_DISABLED);
		return;
	}
	if (pe->is_typec_port
		&& pe->is_pp_pd_capable
		&& pe->is_no_response_timer_expired
		&& (pe->hard_reset_counter > PE_N_HARD_RESET_COUNT)) {
		/* Port partner as PD capable but in error state*/
		pe_change_state(pe, ERROR_RECOVERY);
		return;
	}
	pe_enable_pd(pe, true);
	pe->src_caps_couner++;

	if (pe_send_srccap_cmd(pe)) {
		/* failed to send Srccap */
		log_dbg("Failed to send SrcCap");
		pe_change_state(pe, PE_SRC_DISCOVERY);
		return;
	}
}

static void
pe_process_state_pe_src_negotiate_capability(struct policy_engine *pe)
{
	struct pd_fixed_var_rdo *snk_rdo = &pe->pp_sink_req_caps;
	struct pd_fixed_supply_pdo *src_pdo =
		(struct pd_fixed_supply_pdo *) &pe->self_src_pdos.pdo[0];

	/* TODO: Support multiple request PDOs*/
	if (snk_rdo->cap_mismatch)
		log_warn("Capability mismatch!!\n");

	if (snk_rdo->op_cur <= src_pdo->max_cur) {
		log_dbg("Requested current is less than source cap");
		pe_send_packet(pe, NULL, 0,
			PD_CTRL_MSG_ACCEPT, PE_EVT_SEND_ACCEPT);
		/*Inform DMP to update the new supply*/
		/*Currently system support only one supply*/
	} else {
		log_err("Requested current is more than source cap");
		pe_send_packet(pe, NULL, 0,
				PD_CTRL_MSG_REJECT, PE_EVT_SEND_REJECT);
	}
}


static void
pe_process_state_pe_src_transition_supply(struct policy_engine *pe)
{
	/* Wait for tSrcTransition time to sink to settle*/
	pe_start_timer(pe, SRC_TRANSITION_TIMER, T_SRC_TRANSITION);
}

static void
pe_process_state_pe_src_ready(struct policy_engine *pe)
{
	unsigned delay = 0;

	log_dbg("In PE_SRC_READY");

	if (pe->prev_state == PE_SRC_TRANSITION_SUPPLY)
		delay = PE_AUTO_TRIGGERING_DELAY;
	schedule_delayed_work(&pe->post_ready_work,
					msecs_to_jiffies(delay));
}

static void
pe_process_state_pe_src_capability_response(struct policy_engine *pe)
{
	/*
	 * As system currently supports one PDO and sink is not accepting it,
	 * mode to disable state
	 */
	 pe_change_state(pe, PE_SRC_DISABLED);
}
static void pe_process_state_pe_src_disabled(struct policy_engine *pe)
{
	/* Disable PD, auto crc */
	pe_enable_pd(pe, false);
	log_err("Source port in disable mode!!");
}

static void pe_process_state_pe_src_hard_reset(struct policy_engine *pe)
{
	if (pe->hard_reset_counter > PE_N_HARD_RESET_COUNT) {
		log_info("Max Hard Reset Count reached!!");
		if (pe->is_pp_pd_capable)
			pe_change_state(pe, ERROR_RECOVERY);
		else
			pe_change_state(pe, PE_SRC_DISABLED);
	}
	pe_send_packet(pe, NULL, 0,
		PD_CMD_HARD_RESET, PE_EVT_SEND_HARD_RESET);
	pe->hard_reset_counter++;
	pe_do_self_reset(pe);
	pe_start_timer(pe, PS_HARD_RESET_TIMER, PE_TIME_PS_HARD_RESET_MIN);
}

static void
pe_process_state_pe_src_hard_reset_received(struct policy_engine *pe)
{

	pe_do_self_reset(pe);
	pe_start_timer(pe, PS_HARD_RESET_TIMER, PE_TIME_PS_HARD_RESET_MIN);

}

static void
pe_process_state_pe_src_transition_to_default(struct policy_engine *pe)
{
	pe_do_dpm_reset_entry(pe);
	pe_start_timer(pe, SRC_RESET_RECOVER_TIMER,
			T_SRC_RECOVER_MIN);
}

static void
pe_process_state_pe_src_transition_to_default_exit(struct policy_engine *pe)
{
	pe_do_dpm_reset_complete(pe);
	pe_start_timer(pe, NO_RESPONSE_TIMER, PE_TIME_NO_RESPONSE);
	pe_change_state(pe, PE_SRC_STARTUP);
}

static void
pe_process_state_pe_src_give_source_cap(struct policy_engine *pe)
{
	pe_send_srccap_cmd(pe);
}

static void
pe_process_state_pe_src_get_sink_cap(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0, PD_CTRL_MSG_GET_SINK_CAP,
				PE_EVT_SEND_GET_SINK_CAP);
}

/******************* DR_SWAP State handlers ***********************/

static void
pe_process_state_pe_drs_dfp_ufp_evaluate_dr_swap(struct policy_engine *pe)
{
	if (pe->cur_drole == DATA_ROLE_DFP) {
		/* Always accept dr_swap */
		pe_change_state(pe, PE_DRS_DFP_UFP_ACCEPT_DR_SWAP);
		return;
	}
	log_err("Error data role=%d", pe->cur_drole);
	pe_change_state_to_snk_or_src_reset(pe);
}

static void
pe_process_state_pe_drs_dfp_ufp_accept_dr_swap(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0,
			PD_CTRL_MSG_ACCEPT, PE_EVT_SEND_ACCEPT);
}

static void
pe_process_state_pe_drs_dfp_ufp_change_to_ufp(struct policy_engine *pe)
{
	/* Change data tole to UFP */
	pe_set_data_role(pe, DATA_ROLE_UFP);
	log_err(" DR_SWAP Success: DFP -> UFP");
	pe_change_state_to_snk_or_src_ready(pe);
}

static void
pe_process_state_pe_drs_dfp_ufp_send_dr_swap(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0,
			PD_CTRL_MSG_DR_SWAP, PE_EVT_SEND_DR_SWAP);
}

static void
pe_process_state_pe_drs_ufp_dfp_evaluate_dr_swap(struct policy_engine *pe)
{
	if (pe->cur_drole == DATA_ROLE_UFP) {
		/* Always accept dr_swap */
		pe_change_state(pe, PE_DRS_UFP_DFP_ACCEPT_DR_SWAP);
		return;
	}
	log_err("Error data role=%d", pe->cur_drole);
	pe_change_state_to_snk_or_src_reset(pe);

}

static void
pe_process_state_pe_drs_ufp_dfp_accept_dr_swap(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0,
			PD_CTRL_MSG_ACCEPT, PE_EVT_SEND_ACCEPT);
}

static void
pe_process_state_pe_drs_ufp_dfp_change_to_dfp(struct policy_engine *pe)
{
	/* Change data tole to DFP */
	pe_set_data_role(pe, DATA_ROLE_DFP);
	log_err(" DR_SWAP Success: UFP -> DFP");
	pe_change_state_to_snk_or_src_ready(pe);
}

static void
pe_process_state_pe_drs_ufp_dfp_send_dr_swap(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0,
			PD_CTRL_MSG_DR_SWAP, PE_EVT_SEND_DR_SWAP);
}

static void
pe_process_state_pe_prs_src_snk_send_pr_swap(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0,
				PD_CTRL_MSG_PR_SWAP, PE_EVT_SEND_PR_SWAP);
}

static void
pe_process_state_pe_prs_src_snk_evaluate_pr_swap(struct policy_engine *pe)
{
	int ret;

	ret = devpolicy_is_pr_swap_support(pe->p.dpm, pe->cur_prole);
	if (ret > 0) {
		/* Accept PR_SWAP*/
		pe_change_state(pe, PE_PRS_SRC_SNK_ACCEPT_PR_SWAP);

	} else {
		/* Wait PR_SWAP*/
		pe_change_state(pe, PE_PRS_SRC_SNK_REJECT_PR_SWAP);
	}
}

static void
pe_process_state_pe_prs_src_snk_accept_pr_swap(struct policy_engine *pe)
{
	log_dbg("Accepting PR_SWAP");
	pe_send_packet(pe, NULL, 0,
			PD_CTRL_MSG_ACCEPT, PE_EVT_SEND_ACCEPT);

}

static void
pe_process_state_pe_prs_src_snk_reject_pr_swap(struct policy_engine *pe)
{
	log_dbg("Sending Wait PR_SWAP");
	pe_send_packet(pe, NULL, 0,
			PD_CTRL_MSG_WAIT, PE_EVT_SEND_WAIT);
}

static void
pe_process_state_pe_prs_src_snk_transition_to_off(struct policy_engine *pe)
{
	/* Wait for tSrcTransition time to sink to settle*/
	pe_start_timer(pe, SRC_TRANSITION_TIMER, T_SRC_TRANSITION);

}

static void
pe_process_state_pe_prs_src_snk_assert_rd(struct policy_engine *pe)
{
	/* Pull-Down acc line */
	devpolicy_set_cc_pu_pd(pe->p.dpm, TYPEC_CC_PULL_DOWN);
	pe_change_state(pe, PE_PRS_SRC_SNK_WAIT_SOURCE_ON);
}

static void
pe_process_state_pe_prs_src_snk_wait_source_on(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0,
			PD_CTRL_MSG_PS_RDY, PE_EVT_SEND_PS_RDY);
}


static void
pe_process_state_pe_prs_snk_src_send_pr_swap(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0,
			PD_CTRL_MSG_PR_SWAP, PE_EVT_SEND_PR_SWAP);
}

static void
pe_process_state_pe_prs_snk_src_evaluate_swap(struct policy_engine *pe)
{
	int ret;

	ret = devpolicy_is_pr_swap_support(pe->p.dpm, pe->cur_prole);
	if (ret > 0) {
		/* Accept PR_SWAP*/
		pe_change_state(pe, PE_PRS_SNK_SRC_ACCEPT_PR_SWAP);

	} else {
		/* Wait PR_SWAP*/
		pe_change_state(pe, PE_PRS_SNK_SRC_REJECT_PR_SWAP);
	}
}

static void
pe_process_state_pe_prs_snk_src_reject_swap(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0,
			PD_CTRL_MSG_REJECT, PE_EVT_SEND_REJECT);

}

static void
pe_process_state_pe_prs_snk_src_accept_pr_swap(struct policy_engine *pe)
{
	log_dbg("Accepting PR_SWAP");
	pe_send_packet(pe, NULL, 0,
			PD_CTRL_MSG_ACCEPT, PE_EVT_SEND_ACCEPT);

}

static void
pe_process_state_pe_prs_snk_src_transition_to_off(struct policy_engine *pe)
{
	/* Wait for PS_RDY from source*/
	pe_start_timer(pe, PS_SOURCE_OFF_TIMER, PE_TIME_PS_SOURCE_OFF);

	/* power sink off */
	pe_set_power_role(pe, POWER_ROLE_SWAP);
}

static void
pe_process_state_pe_prs_snk_src_assert_rp(struct policy_engine *pe)
{
	/* Pull-Up  CC line */
	devpolicy_set_cc_pu_pd(pe->p.dpm, TYPEC_CC_PULL_UP);
	pe_change_state(pe, PE_PRS_SNK_SRC_SOURCE_ON);
}

static void
pe_process_state_pe_prs_snk_src_source_on(struct policy_engine *pe)
{
	/* Turn on the VBUS */
	devpolicy_set_vbus_state(pe->p.dpm, true);
	pe_set_power_role(pe, POWER_ROLE_SOURCE);

	pe_start_timer(pe, VBUS_CHECK_TIMER, T_SAFE_5V_MAX);
}

/******** VCONN SWAP State **********/
static void
pe_process_state_pe_vcs_reject_vconn_swap(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0, PD_CTRL_MSG_REJECT, PE_EVT_SEND_REJECT);
}

static void pe_process_state_pe_vcs_accept_swap(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0, PD_CTRL_MSG_ACCEPT, PE_EVT_SEND_ACCEPT);
}

static void pe_process_state_pe_vcs_send_ps_rdy(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0, PD_CTRL_MSG_PS_RDY, PE_EVT_SEND_PS_RDY);
}

static void pe_process_state_pe_vcs_turn_on_vconn(struct policy_engine *pe)
{
	int ret;

	ret = devpolicy_set_vconn_state(pe->p.dpm, VCONN_SOURCE);
	if (ret < 0) {
		log_err("Erorr(%d) in turn of vconn, moving to error recovery.",
				ret);
		pe_change_state(pe, PE_ERROR_RECOVERY);
		return;
	}
	log_dbg("Vconn is enabled.");
	pe_change_state(pe, PE_VCS_SEND_PS_RDY);
}

static void pe_process_state_pe_vcs_turn_off_vconn(struct policy_engine *pe)
{
	int ret;

	ret = devpolicy_set_vconn_state(pe->p.dpm, VCONN_SINK);
	if (ret < 0) {
		log_err("Erorr (%d)in turn of vconn, moving to error recovery",
				ret);
		pe_change_state(pe, PE_ERROR_RECOVERY);
		return;
	}
	log_info("VCS Success!! moving to ready state");
	pe_change_state_to_snk_or_src_ready(pe);
}

static void
pe_process_state_pe_vcs_wait_for_vconn(struct policy_engine *pe)
{
	/* wait tVCONNSourceOn time to to receive PS Ready from source */
	pe_start_timer(pe, VCONN_ON_TIMER, PE_TIME_VCONN_SOURCE_ON);
}

static void pe_process_state_pe_vcs_evaluate_swap(struct policy_engine *pe)
{
	int ret;

	ret = devpolicy_is_vconn_swap_supported(pe->p.dpm);
	if (ret <= 0) {
		log_info("VCS is not suppored by DPM!");
		pe_change_state(pe, PE_VCS_REJECT_SWAP);
	} else {
		pe_change_state(pe, PE_VCS_ACCEPT_SWAP);
		log_dbg("VCS is accepted by DPM!");
	}
}

static void pe_process_state_pe_vcs_send_swap(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0, PD_CTRL_MSG_VCONN_SWAP,
				PE_EVT_SEND_VCONN_SWAP);
}

/******* Dual Role state ************/

static void
pe_process_state_pe_dr_src_get_source_cap(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0, PD_CTRL_MSG_GET_SRC_CAP,
			PE_EVT_SEND_GET_SRC_CAP);
}

static void
pe_process_state_pe_dr_src_give_sink_cap(struct policy_engine *pe)
{
	pe_send_self_sink_caps(pe);
}

static void
pe_process_state_pe_dr_snk_get_sink_cap(struct policy_engine *pe)
{
	pe_send_packet(pe, NULL, 0, PD_CTRL_MSG_GET_SINK_CAP,
			PE_EVT_SEND_GET_SINK_CAP);
}

static void
pe_process_state_pe_dr_snk_give_source_cap(struct policy_engine *pe)
{
	pe_send_srccap_cmd(pe);
}


/* Alternate Mode State handlers*/
static void
pe_process_state_pe_dfp_ufp_vdm_identity_request(struct policy_engine *pe)
{
	pe_send_discover_identity(pe);
}

static void
pe_process_state_pe_dfp_vdm_svids_request(struct policy_engine *pe)
{
	pe_send_discover_svid(pe);
}

static void
pe_process_state_pe_dfp_vdm_status_request(struct policy_engine *pe)
{
	/* pin assign and index will picked from pp_alt_caps*/
	pe_send_display_status(pe);
}

static void
pe_process_state_pe_dfp_vdm_conf_request(struct policy_engine *pe)
{
	/* pin assign and index will picked from pp_alt_caps*/
	pe_send_display_configure(pe);
}

static void
pe_process_state_pe_dfp_vdm_modes_entry_request(struct policy_engine *pe)
{
	int index;

	if (pe->pp_alt_caps.dmode_2x_index) {
		log_info("Selecting 2X DP");
		index = pe->pp_alt_caps.dmode_2x_index;
		pe->pp_alt_caps.dp_mode = TYPEC_DP_TYPE_2X;

	} else if (pe->pp_alt_caps.dmode_4x_index) {
		log_info("Selecting 4X DP");
		index = pe->pp_alt_caps.dmode_4x_index;
		pe->pp_alt_caps.dp_mode = TYPEC_DP_TYPE_4X;
	} else {
		log_warn("Port neither support 2X nor 4X!!");
		pe->alt_state = PE_ALT_STATE_ALT_MODE_FAIL;
		pe_change_state_to_snk_or_src_ready(pe);
		return;
	}

	pe->pp_alt_caps.dmode_cur_index = index;
	pe_send_enter_mode(pe, index);

}

static void
pe_process_state_pe_dfp_vdm_modes_request(struct policy_engine *pe)
{
	/* In DFP, only VESA_SVID supported */
	pe_send_discover_mode(pe);
}

static void pe_process_state_pe_send_soft_reset(struct policy_engine *pe)
{
	/* Send Soft Reset */
	pe_send_packet(pe, NULL, 0, PD_CTRL_MSG_SOFT_RESET,
					PE_EVT_SEND_SOFT_RESET);
}

static void pe_process_state_pe_accept_soft_reset(struct policy_engine *pe)
{
	/* Send Accept for Soft Reset */
	pe_send_packet(pe, NULL, 0, PD_CTRL_MSG_ACCEPT,
					PE_EVT_SEND_ACCEPT);
}

/* Error Recovery state handlers */

static void
pe_process_state_error_recovery(struct policy_engine *pe)
{
	log_warn("PE moved to ERROR RECOVERY!!");
	pe_do_self_reset(pe);
}


static void
pe_process_state_pe_error_recovery(struct policy_engine *pe)
{

	log_info("Issue disconnect and connect");
	pe_change_state(pe, PE_STATE_NONE);
}

static void
pe_process_state_pe_state_none(struct policy_engine *pe)
{
	pe_do_complete_reset(pe);
	/* VBUS Off */
	devpolicy_set_vbus_state(pe->p.dpm, false);
	/*VCONN off */
	devpolicy_set_vconn_state(pe->p.dpm, VCONN_NONE);
	pe_set_data_role(pe, DATA_ROLE_NONE);
	pe_set_power_role(pe, POWER_ROLE_NONE);
	if (pe->prev_state == PE_ERROR_RECOVERY)
		devpolicy_set_cc_pu_pd(pe->p.dpm, TYPEC_CC_PULL_NONE);
}

static void pe_state_change_worker(struct work_struct *work)
{
	struct policy_engine *pe = container_of(work,
					struct policy_engine,
					policy_state_work);
	unsigned state;

	mutex_lock(&pe->pe_lock);
	state = pe->cur_state;
	log_dbg("Processing state %d", state);
	switch (state) {
	case ERROR_RECOVERY:
		pe_process_state_error_recovery(pe);
		break;
	case PE_ERROR_RECOVERY:
		pe_process_state_pe_error_recovery(pe);
	case PE_STATE_NONE:
		pe_process_state_pe_state_none(pe);
		break;
	/* Sink Port State */
	case PE_SNK_STARTUP:
		pe_process_state_pe_snk_startup(pe);
		break;
	case PE_SNK_WAIT_FOR_HARD_RESET_VBUS_OFF:
		pe_process_state_pe_snk_wait_for_hard_reset_vbus_off(pe);
		break;
	case PE_SNK_DISCOVERY:
		pe_process_state_pe_snk_discovery(pe);
		break;
	case PE_SNK_WAIT_FOR_CAPABILITIES:
		pe_process_state_pe_snk_wait_for_capabilities(pe);
		break;
	case PE_SNK_EVALUATE_CAPABILITY:
		pe_process_state_pe_snk_evaluate_capabilities(pe);
		break;
	case PE_SNK_SELECT_CAPABILITY:
		pe_process_state_pe_snk_select_capability(pe);
		break;
	case PE_SNK_TRANSITION_SINK:
		pe_process_state_pe_snk_transition_sink(pe);
		break;
	case PE_SNK_READY:
		pe_process_state_pe_snk_ready(pe);
		break;
	case PE_SNK_HARD_RESET:
		pe_process_state_pe_snk_hard_reset(pe);
		break;
	case PE_SNK_TRANSITION_TO_DEFAULT:
		pe_process_state_pe_snk_transition_to_default(pe);
		break;

	case PE_SNK_GIVE_SINK_CAP:
		pe_process_state_pe_snk_give_sink_cap(pe);
		break;

	case PE_SNK_GET_SOURCE_CAP:
		pe_process_state_pe_snk_get_source_cap(pe);
		break;

	case PE_SNK_HARD_RESET_RECEIVED:
		pe_process_state_pe_snk_hard_reset_received(pe);
		break;

	/* Soirce Port States */
	case PE_SRC_STARTUP:
		pe_process_state_pe_src_startup(pe);
		break;
	case PE_SRC_WAIT_FOR_VBUS:
		pe_process_state_pe_src_wait_for_vbus(pe);
		break;
	case PE_SRC_DISCOVERY:
		pe_process_state_pe_src_discovery(pe);
		break;
	case PE_SRC_SEND_CAPABILITIES:
		pe_process_state_pe_src_send_capabilities(pe);
		break;
	case PE_SRC_NEGOTIATE_CAPABILITY:
		pe_process_state_pe_src_negotiate_capability(pe);
		break;
	case PE_SRC_TRANSITION_SUPPLY:
		pe_process_state_pe_src_transition_supply(pe);
		break;
	case PE_SRC_READY:
		pe_process_state_pe_src_ready(pe);
		break;
	case PE_SRC_DISABLED:
		pe_process_state_pe_src_disabled(pe);
		break;
	case PE_SRC_CAPABILITY_RESPONSE:
		pe_process_state_pe_src_capability_response(pe);
		break;
	case PE_SRC_HARD_RESET:
		pe_process_state_pe_src_hard_reset(pe);
		break;
	case PE_SRC_HARD_RESET_RECEIVED:
		pe_process_state_pe_src_hard_reset_received(pe);
		break;
	case PE_SRC_TRANSITION_TO_DEFAULT:
		pe_process_state_pe_src_transition_to_default(pe);
		break;
	case PE_SRC_GIVE_SOURCE_CAP:
		pe_process_state_pe_src_give_source_cap(pe);
		break;
	case PE_SRC_GET_SINK_CAP:
		pe_process_state_pe_src_get_sink_cap(pe);
		break;

	/* DR_SWAP state */
	case PE_DRS_DFP_UFP_EVALUATE_DR_SWAP:
		pe_process_state_pe_drs_dfp_ufp_evaluate_dr_swap(pe);
		break;
	case PE_DRS_DFP_UFP_ACCEPT_DR_SWAP:
		pe_process_state_pe_drs_dfp_ufp_accept_dr_swap(pe);
		break;
	case PE_DRS_DFP_UFP_CHANGE_TO_UFP:
		pe_process_state_pe_drs_dfp_ufp_change_to_ufp(pe);
		break;
	case PE_DRS_DFP_UFP_SEND_DR_SWAP:
		pe_process_state_pe_drs_dfp_ufp_send_dr_swap(pe);
		break;
	case PE_DRS_DFP_UFP_REJECT_DR_SWAP:
		log_warn("Not Processing PE_DRS_DFP_UFP_REJECT_DR_SWAP");
		break;
	case PE_DRS_UFP_DFP_EVALUATE_DR_SWAP:
		pe_process_state_pe_drs_ufp_dfp_evaluate_dr_swap(pe);
		break;
	case PE_DRS_UFP_DFP_ACCEPT_DR_SWAP:
		pe_process_state_pe_drs_ufp_dfp_accept_dr_swap(pe);
		break;
	case PE_DRS_UFP_DFP_CHANGE_TO_DFP:
		pe_process_state_pe_drs_ufp_dfp_change_to_dfp(pe);
		break;
	case PE_DRS_UFP_DFP_SEND_DR_SWAP:
		pe_process_state_pe_drs_ufp_dfp_send_dr_swap(pe);
		break;
	case PE_DRS_UFP_DFP_REJECT_DR_SWAP:
		log_warn("Not Processing PE_DRS_UFP_DFP_REJECT_DR_SWAP");
		break;
	/* Source to Sink Power Role Swap States */
	case PE_PRS_SRC_SNK_EVALUATE_PR_SWAP:
		pe_process_state_pe_prs_src_snk_evaluate_pr_swap(pe);
		break;
	case PE_PRS_SRC_SNK_ACCEPT_PR_SWAP:
		pe_process_state_pe_prs_src_snk_accept_pr_swap(pe);
		break;
	case PE_PRS_SRC_SNK_TRANSITION_TO_OFF:
		pe_process_state_pe_prs_src_snk_transition_to_off(pe);
		break;
	case PE_PRS_SRC_SNK_ASSERT_RD:
		pe_process_state_pe_prs_src_snk_assert_rd(pe);
		break;
	case PE_PRS_SRC_SNK_WAIT_SOURCE_ON:
		pe_process_state_pe_prs_src_snk_wait_source_on(pe);
		break;
	case PE_PRS_SRC_SNK_SEND_PR_SWAP:
		pe_process_state_pe_prs_src_snk_send_pr_swap(pe);
		break;
	case PE_PRS_SRC_SNK_REJECT_PR_SWAP:
		pe_process_state_pe_prs_src_snk_reject_pr_swap(pe);
		break;

	/* Sink to Source Power Role Swap States */
	case PE_PRS_SNK_SRC_EVALUATE_PR_SWAP:
		pe_process_state_pe_prs_snk_src_evaluate_swap(pe);
		break;
	case PE_PRS_SNK_SRC_ACCEPT_PR_SWAP:
		pe_process_state_pe_prs_snk_src_accept_pr_swap(pe);
		break;
	case PE_PRS_SNK_SRC_TRANSITION_TO_OFF:
		pe_process_state_pe_prs_snk_src_transition_to_off(pe);
		break;
	case PE_PRS_SNK_SRC_ASSERT_RP:
		pe_process_state_pe_prs_snk_src_assert_rp(pe);
		break;
	case PE_PRS_SNK_SRC_SOURCE_ON:
		pe_process_state_pe_prs_snk_src_source_on(pe);
		break;
	case PE_PRS_SNK_SRC_SEND_PR_SWAP:
		pe_process_state_pe_prs_snk_src_send_pr_swap(pe);
		break;
	case PE_PRS_SNK_SRC_REJECT_PR_SWAP:
		pe_process_state_pe_prs_snk_src_reject_swap(pe);
		break;

	/* Alternate Mode States */
	case PE_DFP_UFP_VDM_IDENTITY_REQUEST:
		pe_process_state_pe_dfp_ufp_vdm_identity_request(pe);
		break;
	case PE_DFP_VDM_SVIDS_REQUEST:
		pe_process_state_pe_dfp_vdm_svids_request(pe);
		break;

	case PE_DFP_VDM_MODES_REQUEST:
		pe_process_state_pe_dfp_vdm_modes_request(pe);
		break;

	case PE_DFP_VDM_MODES_ENTRY_REQUEST:
		pe_process_state_pe_dfp_vdm_modes_entry_request(pe);
		break;

	case PE_DFP_VDM_STATUS_REQUEST:
		pe_process_state_pe_dfp_vdm_status_request(pe);
		break;

	case PE_DFP_VDM_CONF_REQUEST:
		pe_process_state_pe_dfp_vdm_conf_request(pe);
		break;

	case PE_DR_SRC_GET_SOURCE_CAP:
		pe_process_state_pe_dr_src_get_source_cap(pe);
		break;
	case PE_DR_SRC_GIVE_SINK_CAP:
		pe_process_state_pe_dr_src_give_sink_cap(pe);
		break;
	case PE_DR_SNK_GET_SINK_CAP:
		pe_process_state_pe_dr_snk_get_sink_cap(pe);
		break;
	case PE_DR_SNK_GIVE_SOURCE_CAP:
		pe_process_state_pe_dr_snk_give_source_cap(pe);
		break;
	case PE_SRC_SEND_SOFT_RESET:
	case PE_SNK_SEND_SOFT_RESET:
		pe_process_state_pe_send_soft_reset(pe);
		break;
	case PE_SRC_SOFT_RESET:
	case PE_SNK_SOFT_RESET:
		pe_process_state_pe_accept_soft_reset(pe);
		break;
	/* vconn swap states */
	case PE_VCS_EVALUATE_SWAP:
		pe_process_state_pe_vcs_evaluate_swap(pe);
		break;
	case PE_VCS_ACCEPT_SWAP:
		pe_process_state_pe_vcs_accept_swap(pe);
		break;
	case PE_VCS_REJECT_SWAP:
		pe_process_state_pe_vcs_reject_vconn_swap(pe);
		break;
	case PE_VCS_WAIT_FOR_VCONN:
		pe_process_state_pe_vcs_wait_for_vconn(pe);
		break;
	case PE_VCS_TURN_ON_VCONN:
		pe_process_state_pe_vcs_turn_on_vconn(pe);
		break;
	case PE_VCS_TURN_OFF_VCONN:
		pe_process_state_pe_vcs_turn_off_vconn(pe);
		break;
	case PE_VCS_SEND_PS_RDY:
		pe_process_state_pe_vcs_send_ps_rdy(pe);
		break;
	case PE_VCS_SEND_SWAP:
		pe_process_state_pe_vcs_send_swap(pe);
		break;
	default:
		log_info("Cannot process unknown state %d", state);
	}
	mutex_unlock(&pe->pe_lock);
	log_dbg("Processing state %d complete\n", state);
}

static void
pe_alt_mode_initiator(struct policy_engine *pe)
{

	switch (pe->alt_state) {
	case PE_ALT_STATE_NONE:
		pe_change_state(pe, PE_DFP_UFP_VDM_IDENTITY_REQUEST);
		break;

	case PE_ALT_STATE_DI_ACKED:
		pe_change_state(pe, PE_DFP_VDM_SVIDS_REQUEST);
		break;

	case PE_ALT_STATE_SVID_ACKED:
		pe_change_state(pe, PE_DFP_VDM_MODES_REQUEST);
		break;

	case PE_ALT_STATE_DMODE_ACKED:
		pe_change_state(pe, PE_DFP_VDM_MODES_ENTRY_REQUEST);
		break;

	case PE_ALT_STATE_EMODE_ACKED:
		pe_change_state(pe, PE_DFP_VDM_STATUS_REQUEST);
		break;

	case PE_ALT_STATE_STATUS_ACKED:
		pe_change_state(pe, PE_DFP_VDM_CONF_REQUEST);
		break;

	default:
		log_warn("Cannot trigger VDM in alt_state=%d",
					pe->alt_state);
	}
}

static void pe_post_ready_worker(struct work_struct *work)
{
	struct policy_engine *pe = container_of(work, struct policy_engine,
							post_ready_work.work);

	mutex_lock(&pe->pe_lock);
	switch (pe->cur_state) {

	case PE_SRC_READY:
		if (pe->pp_snk_pdos.num_pdos == 0
			&& pe->retry_counter < PE_MAX_RETRY) {
			/* Get port partner's sink caps */
			pe->retry_counter++;
			log_dbg("Auto request sink cap");
			pe_change_state(pe, PE_SRC_GET_SINK_CAP);
			goto ready_work_done;
		}
		if ((!pe->is_pr_swap_rejected)
			&& pe->pp_caps.pp_is_ext_pwrd) {
			log_info("Auto triggering PR_SWAP");
			pe_change_state(pe, PE_PRS_SRC_SNK_SEND_PR_SWAP);
			goto ready_work_done;
		}
		break;

	case PE_SNK_READY:
		break;

	default:
		log_info("PE not in SNK/SRC READY state, quit!!");
		goto ready_work_done;
	}

	if (pe->cur_drole != DATA_ROLE_DFP
		|| pe->alt_state == PE_ALT_STATE_ALT_MODE_FAIL
		|| pe->alt_state == PE_ALT_STATE_ALT_MODE_SUCCESS)
		goto ready_work_done;

	log_dbg("Start alternate mode CMDs");
	pe_alt_mode_initiator(pe);

ready_work_done:
	mutex_unlock(&pe->pe_lock);
}

static struct pe_operations ops = {
	.get_power_role = pe_get_power_role,
	.get_data_role = pe_get_data_role,
	.process_data_msg = policy_engine_process_data_msg,
	.process_ctrl_msg = policy_engine_process_ctrl_msg,
	.process_cmd = policy_engine_process_cmd,
	.notify_dpm_evt = pe_dpm_notification,
};

static void pe_init_timers(struct policy_engine *pe)
{
	int i;
	struct pe_timer *cur_timer;

	for (i = 0; i < PE_TIMER_CNT; i++) {
		cur_timer = &pe->timers[i];
		cur_timer->timer_type = i;
		cur_timer->data = pe;
		INIT_WORK(&cur_timer->work, pe_timer_expire_worker);
		setup_timer(&cur_timer->timer,
			pe_timer_expire_callback,
			(unsigned long)cur_timer);
	}
}

static void pe_init_policy(struct work_struct *work)
{
	struct policy_engine *pe;

	pe = container_of(work, struct policy_engine, policy_init_work);
	mutex_init(&pe->pe_lock);
	pe->cur_drole = DATA_ROLE_NONE;
	pe->cur_prole = POWER_ROLE_NONE;
	pe->is_typec_port = true;

	/* Initialize pe timers */
	pe_init_timers(pe);
	INIT_WORK(&pe->policy_state_work, pe_state_change_worker);
	INIT_DELAYED_WORK(&pe->post_ready_work, pe_post_ready_worker);
	pe->cur_state = PE_STATE_NONE;
	pe->alt_state = PE_ALT_STATE_NONE;

	pe->p.ops = &ops;
	return;
}


int policy_engine_bind_dpm(struct devpolicy_mgr *dpm)
{
	int ret;
	struct policy_engine *pe;

	if (!dpm)
		return -EINVAL;

	if (dpm->p)
		return -EEXIST;

	pe = devm_kzalloc(dpm->phy->dev, sizeof(struct policy_engine),
				GFP_KERNEL);
	if (!pe)
		return -ENOMEM;


	/*
	 * As PD negotiation is time bound, create work queue with
	 * WQ_HIGHPRI  to schedule the work as soon as possible.
	 */
	pe->p.pd_wq = alloc_workqueue("pd_wq", WQ_HIGHPRI, 0);
	if (!pe->p.pd_wq) {
		log_err("Failed to allocate pd work queue");
		return -ENOMEM;
	}

	pe->p.dpm = dpm;
	ret = protocol_bind_pe(&pe->p);
	if (ret) {
		log_err("Failed to bind pe to protocol\n");
		ret = -EINVAL;
		goto bind_error;
	}

	dpm->p = &pe->p;

	INIT_WORK(&pe->policy_init_work, pe_init_policy);
	schedule_work(&pe->policy_init_work);
	log_info("Policy engine bind success\n");
	return 0;

bind_error:
	destroy_workqueue(pe->p.pd_wq);
	kfree(pe);
	return ret;
}
EXPORT_SYMBOL_GPL(policy_engine_bind_dpm);

void policy_engine_unbind_dpm(struct devpolicy_mgr *dpm)
{
	struct policy_engine *pe;
	struct policy *p;

	if (!dpm || !dpm->p)
		return;
	p = dpm->p;
	pe = container_of(p, struct policy_engine, p);
	mutex_lock(&pe->pe_lock);
	/*
	 * Remove the pe ops to avoid further external
	 * notifications and callbacks.
	 */
	p->ops = NULL;

	pe_do_complete_reset(pe);

	/* Unbind from protocol layer */
	protocol_unbind_pe(&pe->p);
	if (pe->p.pd_wq)
		destroy_workqueue(pe->p.pd_wq);
	mutex_unlock(&pe->pe_lock);

	kfree(pe);
}
EXPORT_SYMBOL_GPL(policy_engine_unbind_dpm);

MODULE_AUTHOR("Kotakonda, Venkataramana <venkataramana.kotakonda@intel.com>");
MODULE_DESCRIPTION("PD Policy Engine");
MODULE_LICENSE("GPL v2");
