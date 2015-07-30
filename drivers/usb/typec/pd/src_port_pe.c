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
#define CMD_NORESPONCE_TIME	1 /* 4 Sec */

#define VOLT_TO_SRC_CAP_DATA_OBJ(x)	(x / 50)
#define CURRENT_TO_SRC_CAP_DATA_OBJ(x)	(x / 10)

#define TYPEC_SENDER_RESPONSE_TIMER     30 /* min: 24mSec; max: 30mSec */
#define TYPEC_PS_SRC_ON_TIMER		480 /* min: 390mSec; max: 480mSec */

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

static int src_pe_send_srccap_cmd(struct src_port_pe *src_pe)
{
	int ret;
	struct pd_fixed_supply_pdo pdo;
	struct power_cap pcap;

	log_dbg("Sending PD_CMD_HARD_RESET");
	policy_send_packet(&src_pe->p, NULL, 0, PD_CMD_HARD_RESET,
				PE_EVT_SEND_HARD_RESET);
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

static int src_pe_handle_sink_transition_to_off(struct src_port_pe *src_pe)
{
	int ret = 0;

	mutex_lock(&src_pe->pe_lock);
	src_pe->state = PE_PRS_SRC_SNK_TRANSITION_TO_OFF;
	mutex_unlock(&src_pe->pe_lock);

	/* Pull-down CC (enable Rd) and Vbus 5V disable */
	ret = policy_set_power_role(&src_pe->p, POWER_ROLE_SWAP);
	if (ret < 0) {
		log_err("Error in enabling sink %d\n", ret);
		return ret;
	}

	return src_pe_handle_snk_source_off(src_pe);
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
		mutex_unlock(&src_pe->pe_lock);
		log_info("SRC_PE_STATE_PS_RDY_SENT -> SRC_PE_STATE_PD_CONFIGURED");
		pe_notify_policy_status_changed(&src_pe->p,
				POLICY_TYPE_SOURCE, src_pe->p.status);
		/* Get sink caps */
		src_pe_send_get_snk_cap_cmd(src_pe);
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

	log_dbg("%s evt %d\n", __func__, evt);
	switch (evt) {
	case PE_EVT_SEND_PR_SWAP:
		if (!src_pe->pp_is_dual_prole) {
			log_info("Port partner doesnt support pr_swap");
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
	default:
		break;
	}

	return 0;
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
	/* Trigger power role swap if extenally powered */
	if (snk_cap->ext_powered)
		src_pe_rcv_request(&src_pe->p, PE_EVT_SEND_PR_SWAP);
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
		if (src_pe->state == PE_PRS_SRC_SNK_SEND_PR_SWAP)
			complete(&src_pe->srt_complete);
		break;
	case PE_EVT_RCVD_PS_RDY:
		if (src_pe->state == PE_PRS_SRC_SNK_SOURCE_OFF)
			complete(&src_pe->psso_complete);
		break;
	case PE_EVT_RCVD_SNK_CAP:
		if (src_pe->state == SRC_PE_STATE_PD_CONFIGURED)
			src_pe_handle_snk_cap_rcv(src_pe, pkt);
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
	if (ret == 0) {
		log_err("SRT time expired Sending PD_CMD_HARD_RESET");
		policy_send_packet(&src_pe->p, NULL, 0, PD_CMD_HARD_RESET,
					PE_EVT_SEND_HARD_RESET);
		goto error;
	}
	ret = src_pe_handle_sink_transition_to_off(src_pe);

error:
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
		|| (src_pe->state == SRC_PE_STATE_PD_CONFIGURED)
		|| (src_pe->p.state == POLICY_STATE_OFFLINE)) {
		log_info("Not required to send srccap in this state=%d\n",
				src_pe->state);
		return;
	}

	src_pe_send_srccap_cmd(src_pe);
	mutex_lock(&src_pe->pe_lock);
	src_pe->state = SRC_PE_STATE_SRCCAP_SENT;
	src_pe->cmd_retry++;
	mutex_unlock(&src_pe->pe_lock);

	if (src_pe->cmd_retry < MAX_CMD_RETRY) {
		log_dbg("Re-scheduling the start_comm after %dSec\n",
				CMD_NORESPONCE_TIME);
		schedule_delayed_work(&src_pe->start_comm,
					HZ * CMD_NORESPONCE_TIME);
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
	cancel_delayed_work(&src_pe->start_comm);
	reinit_completion(&src_pe->srt_complete);
	reinit_completion(&src_pe->psso_complete);
	policy_set_pd_state(p, false);
	src_pe->cmd_retry = 0;
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
	init_completion(&src_pe->srt_complete);
	init_completion(&src_pe->psso_complete);

	log_info("Source pe initialized successfuly");

	return p;
}
EXPORT_SYMBOL(src_pe_init);
