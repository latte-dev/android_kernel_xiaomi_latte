/*
 * src_port_pe.c: Intel USB Power Delivery Source Port Policy Engine
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
 * Author: Venkataramana Kotakonda <venkataramana.kotakonda@intel.com>
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/delay.h>
#include "message.h"
#include "policy_engine.h"

#define LOG_TAG "src_pe"
#define log_info(format, ...) \
	pr_info(LOG_TAG":%s:"format"\n", __func__, ##__VA_ARGS__)
#define log_dbg(format, ...) \
	pr_debug(LOG_TAG":%s:"format"\n", __func__, ##__VA_ARGS__)
#define log_err(format, ...) \
	pr_err(LOG_TAG":%s:"format"\n", __func__, ##__VA_ARGS__)

#define MAX_CMD_RETRY	50
#define TYPEC_SEND_SRC_CAP_TIME	200 /* 200 mSec */
#define TYPEC_SRC_ACTIVITY_TIME	40 /* 40 mSec */

#define VOLT_TO_SRC_CAP_DATA_OBJ(x)	(x / 50)
#define CURRENT_TO_SRC_CAP_DATA_OBJ(x)	(x / 10)

#define TYPEC_SENDER_RESPONSE_TIMER     30 /* min: 24mSec; max: 30mSec */
#define TYPEC_PS_SRC_ON_TIMER		480 /* min: 390mSec; max: 480mSec */
#define TYPEC_PS_SRC_OFF_TIMER		750 /*750mSec*/

struct src_port_pe {
	struct mutex pe_lock;
	struct policy p;
	int state;
	struct completion srt_complete; /* sender response timer */
	struct completion psso_complete; /* power supply source on timer */
	struct power_cap pcap;
	struct delayed_work start_comm;
	struct work_struct msg_work;
	int cmd_retry;
	int vbus_retry_cnt;
	enum pe_event last_rcv_evt;
	unsigned got_snk_caps:1;
	unsigned is_pd_configured:1;
	/* port partner caps */
	unsigned pp_is_dual_drole:1;
	unsigned pp_is_dual_prole:1;
	unsigned pp_is_ext_pwrd:1;
};

/* Source policy engine states */
enum src_pe_state {
	SRC_PE_STATE_UNKNOWN = -1,
	SRC_PE_STATE_NONE,
	SRC_PE_STATE_SRCCAP_SENT,
	SRC_PE_STATE_SRCCAP_GCRC,
	SRC_PE_STATE_ACCEPT_SENT,
	SRC_PE_STATE_PS_RDY_SENT,
	SRC_PE_STATE_PD_CONFIGURED,
	SRC_PE_STATE_PD_FAILED,
};

static inline int src_pe_get_power_cap(struct src_port_pe *src_pe,
				struct power_cap *pcap)
{
	return policy_get_srcpwr_cap(&src_pe->p, pcap);
}

static void src_pe_reset_policy_engine(struct src_port_pe *src_pe)
{
	src_pe->state = SRC_PE_STATE_NONE;
	src_pe->pcap.mv = 0;
	src_pe->pcap.ma = 0;

	/* By default dual data role is enabled*/
	src_pe->pp_is_dual_drole = 1;
	/* By default dual power role is enabled*/
	src_pe->pp_is_dual_prole = 1;
	src_pe->pp_is_ext_pwrd = 0;
}

static void src_pe_do_pe_reset_on_error(struct src_port_pe *src_pe)
{
	src_pe_reset_policy_engine(src_pe);
	policy_send_packet(&src_pe->p, NULL, 0, PD_CMD_HARD_RESET,
						PE_EVT_SEND_HARD_RESET);

	/* Schedule worker to send src_cap*/
	schedule_delayed_work(&src_pe->start_comm, 0);
}

static int src_pe_send_srccap_cmd(struct src_port_pe *src_pe)
{
	int ret;
	struct pd_fixed_supply_pdo pdo;
	struct power_cap pcap;

	log_dbg("Sending SrcCap");
	ret = src_pe_get_power_cap(src_pe, &pcap);
	if (ret) {
		log_err("Error in getting power capabilities\n");
		return ret;
	}
	memset(&pdo, 0, sizeof(struct pd_fixed_supply_pdo));
	pdo.max_cur = CURRENT_TO_SRC_CAP_DATA_OBJ(pcap.ma); /* In 10mA units */
	pdo.volt = VOLT_TO_SRC_CAP_DATA_OBJ(pcap.mv); /* In 50mV units */
	pdo.peak_cur = 0; /* No peek current supported */
	pdo.dual_role_pwr = 1; /* Dual pwr role supported */
	pdo.data_role_swap = 1; /*Dual data role*/
	pdo.usb_comm = 1; /* USB communication supported */

	ret = policy_send_packet(&src_pe->p, &pdo, 4,
				PD_DATA_MSG_SRC_CAP, PE_EVT_SEND_SRC_CAP);
	return ret;
}

static inline int src_pe_send_accept_cmd(struct src_port_pe *src_pe)
{

	return policy_send_packet(&src_pe->p, NULL, 0,
				PD_CTRL_MSG_ACCEPT, PE_EVT_SEND_ACCEPT);
}

static inline int src_pe_send_psrdy_cmd(struct src_port_pe *src_pe)
{

	return policy_send_packet(&src_pe->p, NULL, 0,
				PD_CTRL_MSG_PS_RDY, PE_EVT_SEND_PS_RDY);
}

static inline int src_pe_send_get_snk_cap_cmd(struct src_port_pe *src_pe)
{

	return policy_send_packet(&src_pe->p, NULL, 0,
			PD_CTRL_MSG_GET_SINK_CAP, PE_EVT_SEND_GET_SINK_CAP);
}

static int src_pe_handle_snk_source_off(struct src_port_pe *src_pe)
{
	mutex_lock(&src_pe->pe_lock);
	src_pe->state = PE_PRS_SRC_SNK_SOURCE_OFF;
	mutex_unlock(&src_pe->pe_lock);

	return src_pe_send_psrdy_cmd(src_pe);
}

/* This function will wait for TYPEC_PS_SRC_OFF_TIMER to disbale vbus.
 * return 0 on success and -ETIME on timeout.
 */
static int src_pe_sink_transition_wait_for_vbus_off(struct src_port_pe *src_pe)
{
	int max_cnt = TYPEC_PS_SRC_OFF_TIMER / TYPEC_SRC_ACTIVITY_TIME;
	int cnt;
	int ret = -ETIME;

	if (!policy_get_vbus_state(&src_pe->p))
		return 0;

	for (cnt = 0; cnt < max_cnt; cnt++) {
		log_dbg("Waiting for vbus to off, cnt=%d\n", cnt);
		msleep(TYPEC_SRC_ACTIVITY_TIME);
		if (!policy_get_vbus_state(&src_pe->p)) {
			ret = 0;
			break;
		}
	}

	return ret;
}

static int src_pe_handle_sink_transition_to_off(struct src_port_pe *src_pe)
{
	int ret = 0;

	mutex_lock(&src_pe->pe_lock);
	src_pe->state = PE_PRS_SRC_SNK_TRANSITION_TO_OFF;
	mutex_unlock(&src_pe->pe_lock);

	/* Pull-down CC (enable Rd) and Vbus 5V disable */
	ret = policy_set_power_role(&src_pe->p, POWER_ROLE_SWAP);
	if (ret < 0) {
		log_err("Error in set pwr role swap %d\n", ret);
		goto trans_to_swap_fail;
	}

	ret = src_pe_sink_transition_wait_for_vbus_off(src_pe);
	if (ret < 0) {
		log_err("Failed to disable the VBUS, HARD_RESET\n");
		goto trans_to_off_fail;
	}

	ret = src_pe_handle_snk_source_off(src_pe);
	if (ret < 0) {
		log_err("Failed to send PD_RDY\n");
		goto trans_to_off_fail;
	}

	return 0;

trans_to_off_fail:
	/* Change the role back to source */
	policy_set_power_role(&src_pe->p, POWER_ROLE_SOURCE);

trans_to_swap_fail:
	/* As role swap accepted, reset state & send hard reset */
	mutex_lock(&src_pe->pe_lock);
	src_pe->state = SRC_PE_STATE_NONE;
	mutex_unlock(&src_pe->pe_lock);

	/* Issue hard reset */
	policy_send_packet(&src_pe->p, NULL, 0, PD_CMD_HARD_RESET,
						PE_EVT_SEND_HARD_RESET);

	/* Schedule worker to send src_cap*/
	schedule_delayed_work(&src_pe->start_comm, 0);

	return ret;
}

static void src_pe_handle_dr_swap_transition(struct src_port_pe *src_pe,
			enum data_role to_role)
{
	int ret;

	mutex_lock(&src_pe->pe_lock);
	if (to_role == DATA_ROLE_UFP)
		src_pe->state = PE_DRS_DFP_UFP_CHANGE_TO_UFP;
	else
		src_pe->state = PE_DRS_UFP_DFP_CHANGE_TO_DFP;
	mutex_unlock(&src_pe->pe_lock);

	log_dbg("Changing data role to %d", to_role);
	ret = policy_set_data_role(&src_pe->p, to_role);
	if (ret) {
		log_err("Failed to change the data role");
		/*Reset pe as role swap failed*/
		src_pe_do_pe_reset_on_error(src_pe);
		return;
	}
	log_dbg("Data role changed to %d", to_role);
	mutex_lock(&src_pe->pe_lock);
	src_pe->state = SRC_PE_STATE_PD_CONFIGURED;
	mutex_unlock(&src_pe->pe_lock);
}

static void src_pe_handle_after_dr_swap_sent(struct src_port_pe *src_pe)
{
	unsigned long timeout;
	int ret, state;

	/* Initialize and run SenderResponseTimer */
	timeout = msecs_to_jiffies(TYPEC_SENDER_RESPONSE_TIMER);
	/* unblock this once Accept msg received by checking the
	 * cur_state */
	ret = wait_for_completion_timeout(&src_pe->srt_complete, timeout);
	mutex_lock(&src_pe->pe_lock);
	if (ret == 0) {
		log_err("SRT time expired, move to READY");
		goto dr_sent_error;
	}

	if (src_pe->last_rcv_evt != PE_EVT_RCVD_ACCEPT) {
		log_info("DR swap not accepted!!");
		goto dr_sent_error;
	}
	state = src_pe->state;
	mutex_unlock(&src_pe->pe_lock);
	log_dbg("DR swap accepted by port partner");
	if (state == PE_DRS_DFP_UFP_SEND_DR_SWAP)
		src_pe_handle_dr_swap_transition(src_pe, DATA_ROLE_UFP);
	else if (state == PE_DRS_UFP_DFP_SEND_DR_SWAP)
		src_pe_handle_dr_swap_transition(src_pe, DATA_ROLE_DFP);
	else
		log_err("Unexpected state=%d !!!\n", state);
	goto dr_sent_end;

dr_sent_error:
	src_pe->state = SRC_PE_STATE_PD_CONFIGURED;
	mutex_unlock(&src_pe->pe_lock);
dr_sent_end:
	reinit_completion(&src_pe->srt_complete);
	return;
}

static int src_pe_handle_trigger_dr_swap(struct src_port_pe *src_pe)
{
	enum data_role drole;

	drole = policy_get_data_role(&src_pe->p);

	if ((src_pe->state != SRC_PE_STATE_PD_CONFIGURED)
		|| ((drole != DATA_ROLE_UFP)
		&& (drole != DATA_ROLE_DFP))) {
		log_dbg("Not processing DR_SWAP request in state=%d",
				src_pe->state);
		return -EINVAL;
	}

	mutex_lock(&src_pe->pe_lock);
	if (drole == DATA_ROLE_DFP)
		src_pe->state = PE_DRS_DFP_UFP_SEND_DR_SWAP;
	else
		src_pe->state = PE_DRS_UFP_DFP_SEND_DR_SWAP;
	src_pe->p.status = POLICY_STATUS_RUNNING;
	mutex_unlock(&src_pe->pe_lock);
	schedule_work(&src_pe->msg_work);

	policy_send_packet(&src_pe->p, NULL, 0,
			PD_CTRL_MSG_DR_SWAP, PE_EVT_SEND_DR_SWAP);

	return 0;
}

static void src_pe_handle_after_dr_swap_accept(struct src_port_pe *src_pe)
{
	unsigned long timeout;
	int ret, state;

	/* Initialize and run SenderResponseTimer */
	timeout = msecs_to_jiffies(TYPEC_SENDER_RESPONSE_TIMER);
	/* unblock this once Accept msg received by checking the
	 * cur_state */
	ret = wait_for_completion_timeout(&src_pe->srt_complete, timeout);
	if (ret == 0) {
		log_err("SRT time expired, move to RESET");
		/*Reset pe as role swap failed*/
		src_pe_do_pe_reset_on_error(src_pe);
		goto swap_accept_error;
	}


	mutex_lock(&src_pe->pe_lock);
	state = src_pe->state;
	mutex_unlock(&src_pe->pe_lock);
	log_dbg("GCRC for DR swap accepted");
	if (state == PE_DRS_DFP_UFP_ACCEPT_DR_SWAP)
		src_pe_handle_dr_swap_transition(src_pe, DATA_ROLE_UFP);
	else if (state == PE_DRS_UFP_DFP_ACCEPT_DR_SWAP)
		src_pe_handle_dr_swap_transition(src_pe, DATA_ROLE_DFP);
	else
		log_err("Unexpected state=%d !!!\n", state);

swap_accept_error:
	reinit_completion(&src_pe->srt_complete);
}

static void src_pe_handle_rcv_dr_swap(struct src_port_pe *src_pe)
{
	enum data_role drole;

	drole = policy_get_data_role(&src_pe->p);

	if ((src_pe->state != SRC_PE_STATE_PD_CONFIGURED)
		|| ((drole != DATA_ROLE_UFP)
		&& (drole != DATA_ROLE_DFP))) {
		log_dbg("Not processing DR_SWAP request in state=%d",
				src_pe->state);
		policy_send_packet(&src_pe->p, NULL, 0,
			PD_CTRL_MSG_REJECT, PE_EVT_SEND_REJECT);
		return;
	}

	mutex_lock(&src_pe->pe_lock);
	if (drole == DATA_ROLE_DFP)
		src_pe->state = PE_DRS_DFP_UFP_ACCEPT_DR_SWAP;
	else
		src_pe->state = PE_DRS_UFP_DFP_ACCEPT_DR_SWAP;
	src_pe->p.status = POLICY_STATUS_RUNNING;
	mutex_unlock(&src_pe->pe_lock);
	schedule_work(&src_pe->msg_work);

	policy_send_packet(&src_pe->p, NULL, 0,
			PD_CTRL_MSG_ACCEPT, PE_EVT_SEND_ACCEPT);

}

static int
src_pe_handle_gcrc(struct src_port_pe *src_pe, struct pd_packet *pkt)
{
	int ret = 0;

	switch (src_pe->state) {
	case SRC_PE_STATE_SRCCAP_SENT:
		mutex_lock(&src_pe->pe_lock);
		src_pe->state = SRC_PE_STATE_SRCCAP_GCRC;
		mutex_unlock(&src_pe->pe_lock);
		log_dbg("SRC_PE_STATE_SRCCAP_SENT -> SRC_PE_STATE_SRCCAP_GCRC");
		break;
	case SRC_PE_STATE_ACCEPT_SENT:
		/* TODO: Enable the 5V  and send PS_DRY */
		ret = src_pe_send_psrdy_cmd(src_pe);
		mutex_lock(&src_pe->pe_lock);
		src_pe->state = SRC_PE_STATE_PS_RDY_SENT;
		mutex_unlock(&src_pe->pe_lock);
		log_dbg("SRC_PE_STATE_ACCEPT_SENT -> SRC_PE_STATE_PS_RDY_SENT");
		break;
	case SRC_PE_STATE_PS_RDY_SENT:
		mutex_lock(&src_pe->pe_lock);
		src_pe->state = SRC_PE_STATE_PD_CONFIGURED;
		src_pe->p.status = POLICY_STATUS_SUCCESS;
		src_pe->cmd_retry = 0;
		src_pe->is_pd_configured = 1;
		mutex_unlock(&src_pe->pe_lock);
		cancel_delayed_work_sync(&src_pe->start_comm);
		log_info("SRC_PE_STATE_PS_RDY_SENT -> SRC_PE_STATE_PD_CONFIGURED");

		/* Schedule worker to get sink caps */
		schedule_work(&src_pe->msg_work);
		break;
	case PE_PRS_SRC_SNK_ACCEPT_PR_SWAP:
		log_dbg("SRC_SNK_ACCEPT_PR_SWAP -> SRC_SNK_TRANSITION_TO_OFF");
		schedule_work(&src_pe->msg_work);
		break;
	case PE_PRS_SRC_SNK_SEND_PR_SWAP:
		/* work schedule after rcv good crc for PR_SWAP to
		 * recevice Accept */
		schedule_work(&src_pe->msg_work);
		break;
	case PE_PRS_SRC_SNK_SOURCE_OFF:
		log_dbg("PE_PRS_SRC_SNK_SOURCE_OFF -> PE_SNK_STARTUP");
		schedule_work(&src_pe->msg_work);
		break;
	case PE_DRS_DFP_UFP_ACCEPT_DR_SWAP:
	case PE_DRS_UFP_DFP_ACCEPT_DR_SWAP:
		complete(&src_pe->srt_complete);
		break;
	default:
		ret = -EINVAL;
		log_info("GCRC received in wrong state=%d\n", src_pe->state);
		break;
	}

	return ret;
}

static int src_pe_handle_request_cmd(struct src_port_pe *src_pe)
{
	if ((src_pe->state == SRC_PE_STATE_SRCCAP_SENT)
		|| (src_pe->state == SRC_PE_STATE_SRCCAP_GCRC)) {
		/* Send accept for request */
		src_pe_send_accept_cmd(src_pe);
		mutex_lock(&src_pe->pe_lock);
		src_pe->state = SRC_PE_STATE_ACCEPT_SENT;
		mutex_unlock(&src_pe->pe_lock);
		log_dbg(" STATE -> SRC_PE_STATE_ACCEPT_SENT\n");
		return 0;
	}
	log_err(" REQUEST MSG received in wrong state!!!\n");
	return -EINVAL;
}

static int src_pe_pr_swap_ok(struct src_port_pe *src_pe)
{
	if (src_pe->state != PE_PRS_SRC_SNK_EVALUATE_PR_SWAP)
		return -EINVAL;

	mutex_lock(&src_pe->pe_lock);
	src_pe->state = PE_PRS_SRC_SNK_ACCEPT_PR_SWAP;
	mutex_unlock(&src_pe->pe_lock);
	return src_pe_send_accept_cmd(src_pe);
}

static int src_pe_handle_pr_swap(struct src_port_pe *src_pe)
{
	enum pwr_role prole;
	int ret;

	prole = policy_get_power_role(&src_pe->p);
	if (prole <= 0) {
		log_err("Error in getting power role\n");
		return -EINVAL;
	}

	if (prole == POWER_ROLE_SOURCE) {
		/* As the request is to transition into consumer mode
		 * should be accepted by default.
		 */
		mutex_lock(&src_pe->pe_lock);
		src_pe->state = PE_PRS_SRC_SNK_EVALUATE_PR_SWAP;
		src_pe->p.status = POLICY_STATUS_RUNNING;
		mutex_unlock(&src_pe->pe_lock);
		ret = src_pe_pr_swap_ok(src_pe);
	} else {
		log_info("Current Power Role - %d\n", prole);
		ret = -ENOTSUPP;
	}

	return ret;
}

static int src_pe_rcv_request(struct policy *srcp, enum pe_event evt)
{
	struct src_port_pe *src_pe = container_of(srcp,
					struct src_port_pe, p);
	int ret = 0;

	log_dbg("%s evt %d\n", __func__, evt);
	switch (evt) {
	case PE_EVT_SEND_PR_SWAP:
		if (!src_pe->pp_is_dual_prole) {
			log_info("Port partner doesnt support pr_swap");
			break;
		}
		if (src_pe->state != SRC_PE_STATE_PD_CONFIGURED) {
			log_info("Cannot process PR_SWAP in state=%d\n",
					src_pe->state);
			break;
		}
		mutex_lock(&src_pe->pe_lock);
		src_pe->state = PE_PRS_SRC_SNK_SEND_PR_SWAP;
		src_pe->p.status = POLICY_STATUS_RUNNING;
		mutex_unlock(&src_pe->pe_lock);
		policy_send_packet(&src_pe->p, NULL, 0,
					PD_CTRL_MSG_PR_SWAP, evt);
		break;
	case PE_EVT_SEND_DR_SWAP:
		ret = src_pe_handle_trigger_dr_swap(src_pe);
		break;
	default:
		break;
	}

	return ret;
}

static void src_pe_handle_snk_cap_rcv(struct src_port_pe *src_pe,
				struct pd_packet *pkt)
{
	struct pd_sink_fixed_pdo *snk_cap;

	snk_cap = (struct pd_sink_fixed_pdo *) &pkt->data_obj[0];

	if (snk_cap->supply_type != SUPPLY_TYPE_FIXED) {
		log_dbg("Port partner is not a fixed sypply");
		return;
	}
	/* Save sink port caps */
	mutex_lock(&src_pe->pe_lock);
	src_pe->pp_is_dual_drole = snk_cap->data_role_swap;
	src_pe->pp_is_dual_prole = snk_cap->dual_role_pwr;
	src_pe->pp_is_ext_pwrd = snk_cap->ext_powered;
	mutex_unlock(&src_pe->pe_lock);

	log_dbg("is_dual_prole=%d, is_dual_drole=%d, is_ext_pwrd=%d",
			snk_cap->dual_role_pwr, snk_cap->data_role_swap,
			snk_cap->ext_powered);
}

static int
src_pe_rcv_pkt(struct policy *srcp, struct pd_packet *pkt, enum pe_event evt)
{
	struct src_port_pe *src_pe = container_of(srcp,
					struct src_port_pe, p);
	int ret = 0;

	log_dbg("%s evt %d\n", __func__, evt);
	switch (evt) {
	case PE_EVT_RCVD_GOODCRC:
		ret = src_pe_handle_gcrc(src_pe, pkt);
		break;
	case PE_EVT_RCVD_REQUEST:
		ret = src_pe_handle_request_cmd(src_pe);
		break;
	case PE_EVT_RCVD_PR_SWAP:
		if (src_pe->state != ERROR_RECOVERY) {
			ret = src_pe_handle_pr_swap(src_pe);
		} else {
			log_err("State Machine is in Error Recovery Mode!\n");
			ret = -EINVAL;
		}
		break;
	case PE_EVT_RCVD_ACCEPT:
	case PE_EVT_RCVD_REJECT:
		if ((src_pe->state == PE_PRS_SRC_SNK_SEND_PR_SWAP)
			|| (src_pe->state == PE_DRS_UFP_DFP_SEND_DR_SWAP)
			|| (src_pe->state == PE_DRS_DFP_UFP_SEND_DR_SWAP)) {
			src_pe->last_rcv_evt = evt;
			complete(&src_pe->srt_complete);
		}
		break;

	case PE_EVT_RCVD_PS_RDY:
		if (src_pe->state == PE_PRS_SRC_SNK_SOURCE_OFF)
			complete(&src_pe->psso_complete);
		break;
	case PE_EVT_RCVD_SNK_CAP:
		if (src_pe->state == PE_SRC_GET_SINK_CAP) {
			mutex_lock(&src_pe->pe_lock);
			src_pe->state = SRC_PE_STATE_PD_CONFIGURED;
			src_pe->got_snk_caps = 1;
			mutex_unlock(&src_pe->pe_lock);
			complete(&src_pe->srt_complete);
			src_pe_handle_snk_cap_rcv(src_pe, pkt);
		}
		break;
	case PE_EVT_RCVD_DR_SWAP:
		src_pe_handle_rcv_dr_swap(src_pe);
		break;
	default:
		ret = -EINVAL;
		log_info("Not proccessing the event=%d\n", evt);
	}
	return ret;
}

int src_pe_rcv_cmd(struct policy *srcp, enum pe_event evt)
{
	struct src_port_pe *src_pe = container_of(srcp,
					struct src_port_pe, p);
	int ret = 0;

	switch (evt) {
	case PE_EVT_RCVD_HARD_RESET:
	case PE_EVT_RCVD_HARD_RESET_COMPLETE:
		src_pe_reset_policy_engine(src_pe);
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int src_pe_handle_after_prswap_sent(struct src_port_pe *src_pe)
{
	unsigned long timeout;
	int ret;

	/* Initialize and run SenderResponseTimer */
	timeout = msecs_to_jiffies(TYPEC_SENDER_RESPONSE_TIMER);
	/* unblock this once Accept msg received by checking the
	 * cur_state */
	ret = wait_for_completion_timeout(&src_pe->srt_complete, timeout);
	mutex_lock(&src_pe->pe_lock);
	if (ret == 0) {
		log_err("SRT time expired, move to READY");
		goto error;
	}
	if (src_pe->last_rcv_evt != PE_EVT_RCVD_ACCEPT)
		goto error;

	mutex_unlock(&src_pe->pe_lock);
	ret = src_pe_handle_sink_transition_to_off(src_pe);
	reinit_completion(&src_pe->srt_complete);
	return ret;

error:
	src_pe->state = SRC_PE_STATE_PD_CONFIGURED;
	mutex_unlock(&src_pe->pe_lock);
	reinit_completion(&src_pe->srt_complete);
	return ret;
}
static int src_pe_snk_source_off_waitfor_psrdy(struct src_port_pe *src_pe)
{
	unsigned long timeout;
	int ret;

	/* Initialize and run PSSourceOnTimer */
	timeout = msecs_to_jiffies(TYPEC_PS_SRC_ON_TIMER);
	/* unblock this once PS_Ready msg received by checking the
	 * cur_state */
	ret = wait_for_completion_timeout(&src_pe->psso_complete, timeout);
	if (ret == 0) {
		log_err("PSSO time expired Sending PD_CMD_HARD_RESET");
		mutex_lock(&src_pe->pe_lock);
		src_pe->cmd_retry = 0;
		mutex_unlock(&src_pe->pe_lock);
		schedule_delayed_work(&src_pe->start_comm, 0);
		goto error;
	}

	/* RR Swap success, set role as sink and switch policy */
	policy_set_power_role(&src_pe->p, POWER_ROLE_SINK);
	log_dbg("Calling swith policy\n");
	policy_switch_policy(&src_pe->p, POLICY_TYPE_SINK);
error:
	reinit_completion(&src_pe->psso_complete);
	return ret;
}

/* This function will send get_snk_cap and wait for responce.
 * If time out, then re-schedule the msg_worker to resend the get_snk_cap.
 */
static void src_pe_get_sink_cap(struct src_port_pe *src_pe)
{
	unsigned long timeout;
	int ret;

	/* Get sink caps */
	src_pe_send_get_snk_cap_cmd(src_pe);
	mutex_lock(&src_pe->pe_lock);
	src_pe->cmd_retry++;
	src_pe->state = PE_SRC_GET_SINK_CAP;
	mutex_unlock(&src_pe->pe_lock);

	/* Initialize and run sender responce timer */
	timeout = msecs_to_jiffies(TYPEC_SENDER_RESPONSE_TIMER);
	ret = wait_for_completion_timeout(&src_pe->srt_complete, timeout);
	if (ret == 0 || !src_pe->got_snk_caps) {
		mutex_lock(&src_pe->pe_lock);
		src_pe->state = SRC_PE_STATE_PD_CONFIGURED;
		if (src_pe->cmd_retry < MAX_CMD_RETRY) {
			log_dbg("SnkCap not received, resend get_snk_cap\n");
			mutex_unlock(&src_pe->pe_lock);
			schedule_work(&src_pe->msg_work);
			goto get_snk_cap_timeout;
		} else{
			log_err("SnkCap not received, even after max retry\n");
			src_pe->cmd_retry = 0;
			mutex_unlock(&src_pe->pe_lock);
			goto get_sink_cap_error;
		}
	}
	log_dbg("Successfuly got sink caps\n");
get_sink_cap_error:
	/* Irrespective of get_sink_cap status, update the
	 * policy engine as success as PD negotiation is success.
	 */
	pe_notify_policy_status_changed(&src_pe->p,
				POLICY_TYPE_SOURCE, POLICY_STATUS_SUCCESS);
get_snk_cap_timeout:
	reinit_completion(&src_pe->srt_complete);
}

static void src_pe_msg_worker(struct work_struct *work)
{
	struct src_port_pe *src_pe = container_of(work,
					struct src_port_pe,
					msg_work);

	switch (src_pe->state) {
	case PE_PRS_SRC_SNK_SEND_PR_SWAP:
		src_pe_handle_after_prswap_sent(src_pe);
		break;
	case PE_PRS_SRC_SNK_ACCEPT_PR_SWAP:
		src_pe_handle_sink_transition_to_off(src_pe);
		break;
	case PE_PRS_SRC_SNK_SOURCE_OFF:
		src_pe_snk_source_off_waitfor_psrdy(src_pe);
		break;
	case SRC_PE_STATE_PD_CONFIGURED:
		if (!src_pe->got_snk_caps)
			src_pe_get_sink_cap(src_pe);
		break;
	case PE_DRS_DFP_UFP_SEND_DR_SWAP:
	case PE_DRS_UFP_DFP_SEND_DR_SWAP:
		src_pe_handle_after_dr_swap_sent(src_pe);
		break;
	case PE_DRS_DFP_UFP_ACCEPT_DR_SWAP:
	case PE_DRS_UFP_DFP_ACCEPT_DR_SWAP:
		src_pe_handle_after_dr_swap_accept(src_pe);
		break;
	default:
		log_err("Unknown state %d\n", src_pe->state);
		break;
	}
}

static void src_pe_start_comm(struct work_struct *work)
{
	struct src_port_pe *src_pe = container_of(work,
					struct src_port_pe,
					start_comm.work);

	if ((src_pe->state == SRC_PE_STATE_PD_FAILED)
		|| (src_pe->is_pd_configured)
		|| (src_pe->p.state == POLICY_STATE_OFFLINE)) {
		log_info("Not required to send srccap in this state=%d\n",
				src_pe->state);
		return;
	}

	if (!policy_get_vbus_state(&src_pe->p)) {
		mutex_lock(&src_pe->pe_lock);
		if (src_pe->vbus_retry_cnt < MAX_CMD_RETRY) {
			log_dbg("VBUS not present, delay SrcCap\n");
			schedule_delayed_work(&src_pe->start_comm,
				msecs_to_jiffies(TYPEC_SRC_ACTIVITY_TIME));
			src_pe->vbus_retry_cnt++;
		}
		mutex_unlock(&src_pe->pe_lock);
		return;
	}

	src_pe_send_srccap_cmd(src_pe);
	mutex_lock(&src_pe->pe_lock);
	src_pe->state = SRC_PE_STATE_SRCCAP_SENT;
	src_pe->cmd_retry++;
	mutex_unlock(&src_pe->pe_lock);

	if (src_pe->cmd_retry < MAX_CMD_RETRY) {
		log_dbg("Re-scheduling the start_comm after %lu mSec\n",
				msecs_to_jiffies(TYPEC_SEND_SRC_CAP_TIME));
		schedule_delayed_work(&src_pe->start_comm,
				msecs_to_jiffies(TYPEC_SEND_SRC_CAP_TIME));
	} else {
		mutex_lock(&src_pe->pe_lock);
		src_pe->state = SRC_PE_STATE_PD_FAILED;
		src_pe->p.status = POLICY_STATUS_FAIL;
		mutex_unlock(&src_pe->pe_lock);
		log_dbg("Not sending srccap as max re-try reached\n");
		pe_notify_policy_status_changed(&src_pe->p,
				POLICY_TYPE_SOURCE, src_pe->p.status);
	}
}

static int src_pe_start_policy_engine(struct policy *p)
{
	struct src_port_pe *src_pe = container_of(p,
					struct src_port_pe, p);

	log_info("IN");
	mutex_lock(&src_pe->pe_lock);
	p->state = POLICY_STATE_ONLINE;
	p->status = POLICY_STATUS_RUNNING;
	policy_set_pd_state(p, true);
	src_pe_reset_policy_engine(src_pe);
	schedule_delayed_work(&src_pe->start_comm, 0);
	mutex_unlock(&src_pe->pe_lock);
	return 0;
}

static int src_pe_stop_policy_engine(struct policy *p)
{
	struct src_port_pe *src_pe = container_of(p,
					struct src_port_pe, p);

	log_info("IN");
	mutex_lock(&src_pe->pe_lock);
	p->state = POLICY_STATE_OFFLINE;
	p->status = POLICY_STATUS_UNKNOWN;
	src_pe_reset_policy_engine(src_pe);
	cancel_delayed_work_sync(&src_pe->start_comm);
	reinit_completion(&src_pe->srt_complete);
	reinit_completion(&src_pe->psso_complete);
	policy_set_pd_state(p, false);
	src_pe->cmd_retry = 0;
	src_pe->got_snk_caps = 0;
	src_pe->is_pd_configured = 0;
	src_pe->vbus_retry_cnt = 0;
	src_pe->pp_is_dual_drole = 0;
	src_pe->pp_is_dual_prole = 0;
	src_pe->pp_is_ext_pwrd = 0;
	mutex_unlock(&src_pe->pe_lock);
	return 0;
}

static void src_pe_exit(struct policy *p)
{
	struct src_port_pe *src_pe = container_of(p,
					struct src_port_pe, p);

	kfree(src_pe);
}

static int src_pe_get_port_caps(struct policy *p,
			struct pe_port_partner_caps *pp_caps)
{
	struct src_port_pe *src_pe = container_of(p,
					struct src_port_pe, p);

	mutex_lock(&src_pe->pe_lock);
	pp_caps->pp_is_dual_drole = src_pe->pp_is_dual_drole;
	pp_caps->pp_is_dual_prole = src_pe->pp_is_dual_prole;
	pp_caps->pp_is_ext_pwrd = src_pe->pp_is_ext_pwrd;
	mutex_unlock(&src_pe->pe_lock);

	return 0;
}

/* Init function to initialize the source policy engine */
struct policy *src_pe_init(struct policy_engine *pe)
{
	struct src_port_pe *src_pe;
	struct policy *p;

	src_pe = kzalloc(sizeof(struct src_port_pe),
						GFP_KERNEL);
	if (!src_pe) {
		log_err("mem alloc failed\n");
		return ERR_PTR(-ENOMEM);
	}

	mutex_init(&src_pe->pe_lock);
	INIT_DELAYED_WORK(&src_pe->start_comm, src_pe_start_comm);
	INIT_WORK(&src_pe->msg_work, src_pe_msg_worker);

	p = &src_pe->p;
	p->type = POLICY_TYPE_SOURCE;
	p->state = POLICY_STATE_OFFLINE;
	p->status = POLICY_STATUS_UNKNOWN;

	p->pe = pe;
	p->rcv_pkt = src_pe_rcv_pkt;
	p->rcv_request = src_pe_rcv_request;
	p->rcv_cmd = src_pe_rcv_cmd;
	p->start = src_pe_start_policy_engine;
	p->stop = src_pe_stop_policy_engine;
	p->exit = src_pe_exit;
	p->get_port_caps = src_pe_get_port_caps;
	init_completion(&src_pe->srt_complete);
	init_completion(&src_pe->psso_complete);

	log_info("Source pe initialized successfuly");

	return p;
}
EXPORT_SYMBOL(src_pe_init);
