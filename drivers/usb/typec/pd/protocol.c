/*
 * protocol.c: PD Protocol framework for handling PD messages
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
 * Author: Kannappan, R <r.kannappan@intel.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/extcon.h>
#include <linux/usb_typec_phy.h>
#include <linux/errno.h>
#include "message.h"
#include "protocol.h"
#include "pd_policy.h"

struct prot_msg {
	struct list_head node;
	struct pd_packet pkt;
};


static int pd_ctrl_msg(struct pd_prot *pd, u8 msg_type, u8 msg_id)
{
	struct pd_packet *buf = &pd->tx_buf;
	struct pd_pkt_header *header = &buf->header;

	header->msg_type = msg_type & PD_MSG_HEAD_MSG_TYPE;
	header->data_role = pd->data_role & PD_MSG_HEADER_ROLE_BITS_MASK;
	header->rev_id = PD_REV_ID_2 & PD_MSG_HEADER_REVID_BITS_MASK;
	if (pd->pwr_role == PD_POWER_ROLE_PROVIDER)
		header->pwr_role = 1;
	else
		header->pwr_role = 0;
	header->msg_id = msg_id & PD_MSG_HEADER_MSGID_BITS_MASK;
	header->num_data_obj = 0;

	return 0;
}

static int pd_data_msg(struct pd_prot *pd, int len, u8 msg_type)
{
	struct pd_packet *buf = &pd->tx_buf;
	struct pd_pkt_header *header = &buf->header;

	header->msg_type = msg_type & PD_MSG_HEAD_MSG_TYPE;
	header->data_role = pd->data_role & PD_MSG_HEADER_ROLE_BITS_MASK;
	header->rev_id = PD_REV_ID_2 & PD_MSG_HEADER_REVID_BITS_MASK;
	if (pd->pwr_role == PD_POWER_ROLE_PROVIDER)
		header->pwr_role = 1;
	else
		header->pwr_role = 0;
	header->msg_id = pd->tx_msg_id & PD_MSG_HEADER_MSGID_BITS_MASK;
	header->num_data_obj = len & PD_MSG_HEADER_N_DOBJ_BITS_MASK;

	return 0;
}

static void pd_policy_update_data_role(struct pd_prot *prot,
					enum data_role drole)
{
	if (drole == DATA_ROLE_NONE || drole == DATA_ROLE_UFP)
		prot->data_role = PD_DATA_ROLE_UFP;
	else if (drole == DATA_ROLE_DFP)
		prot->data_role = PD_DATA_ROLE_DFP;

	schedule_work(&prot->role_chng_work);
}

static void pd_policy_update_power_role(struct pd_prot *prot,
					enum pwr_role prole)
{
	/* Reset the mag id on power role change*/
	if (prot->pwr_role != prole)
		prot->rx_msg_id = -1;
	if (prole == POWER_ROLE_NONE || prole == POWER_ROLE_SINK)
		prot->pwr_role = PD_POWER_ROLE_CONSUMER;
	else if (prole == POWER_ROLE_SOURCE)
		prot->pwr_role = PD_POWER_ROLE_PROVIDER;

	schedule_work(&prot->role_chng_work);
}

static struct prot_msg *prot_alloc_msg(void)
{
	struct prot_msg *msg;
	msg = kzalloc(sizeof(struct prot_msg),
					GFP_KERNEL);
	if (msg)
		INIT_LIST_HEAD(&msg->node);
	return msg;
}

static inline void prot_free_msg(struct prot_msg *msg)
{
	kfree(msg);
}

static void prot_clear_rx_msg_list(struct pd_prot *pd)
{
	struct prot_msg *msg, *tmp;
	struct list_head tmp_list;

	if (list_empty(&pd->rx_list))
		return;

	mutex_lock(&pd->rx_list_lock);
	list_replace_init(&pd->rx_list, &tmp_list);
	mutex_unlock(&pd->rx_list_lock);

	list_for_each_entry_safe(msg, tmp, &tmp_list, node)
		prot_free_msg(msg);
}

static void pd_reset_counters(struct pd_prot *pd)
{
	mutex_lock(&pd->tx_lock);
	pd->event = PROT_EVENT_NONE;
	pd->tx_msg_id = 0;
	pd->rx_msg_id = -1;
	pd->retry_counter = 0;
	mutex_unlock(&pd->tx_lock);
}

static int do_tx_msg_flush(struct pd_prot *pd)
{
	pd_prot_flush_fifo(pd, FIFO_TYPE_TX);
	pd->retry_counter = 0;
	return 0;
}

static int pd_prot_handle_reset(struct pd_prot *pd, enum typec_phy_evts evt)
{
	pd_prot_flush_fifo(pd, FIFO_TYPE_TX);
	pd_prot_flush_fifo(pd, FIFO_TYPE_RX);
	pd->event = PROT_PHY_EVENT_RESET;
	complete(&pd->tx_complete);
	pd_reset_counters(pd);
	prot_clear_rx_msg_list(pd);
	/*TODO: check if the the work is completed */
	pd->event = PROT_PHY_EVENT_NONE;

	if (evt == PROT_PHY_EVENT_HARD_RST)
		/* notify policy */
		pe_process_cmd(pd->p, PE_EVT_RCVD_HARD_RESET);

	return 0;
}

static int pd_tx_fsm_state(struct pd_prot *pd, int tx_state)
{
	switch (tx_state) {
	case PROT_TX_PHY_LAYER_RESET:
		do_tx_msg_flush(pd);
		/* pd_prot_reset_phy(pd); */
		break;
	default:
		return -EINVAL;
	}
	pd->cur_tx_state = tx_state;
	return 0;
}

static void pd_prot_tx_work(struct pd_prot *prot)
{
	int len;

	len = PD_MSG_LEN(&prot->tx_buf.header) + PD_MSG_HEADER_SIZE;
	pd_prot_send_phy_packet(prot, &prot->tx_buf, len);

}

static inline int prot_rx_reset(struct pd_prot *pd)
{
	pd_reset_counters(pd);
	/* Reset the phy */
	return pd_prot_reset_phy(pd);
}

static int pd_prot_rcv_pkt_from_policy(struct pd_prot *prot, u8 msg_type,
						void *buf, int len)
{
	struct pd_packet *pkt;

	if (msg_type == PD_CMD_HARD_RESET) {
		pd_prot_flush_fifo(prot, FIFO_TYPE_RX);
		prot_clear_rx_msg_list(prot);
		return prot_rx_reset(prot);
	} else if (msg_type == PD_CMD_PROTOCOL_RESET) {
		pd_prot_flush_fifo(prot, FIFO_TYPE_RX);
		pd_reset_counters(prot);
		prot_clear_rx_msg_list(prot);
		return pd_tx_fsm_state(prot, PROT_TX_PHY_LAYER_RESET);
	}

	pkt = &prot->tx_buf;
	memset(pkt, 0, sizeof(struct pd_packet));
	pkt->header.msg_type = msg_type;
	pkt->header.data_role = prot->data_role;
	if (prot->pd_version == 2)
		pkt->header.rev_id = 1;
	else
		pkt->header.rev_id = 0;

	if ((prot->pwr_role == PD_POWER_ROLE_PROVIDER)
		|| (prot->pwr_role == PD_POWER_ROLE_CONSUMER_PROVIDER))
		pkt->header.pwr_role = PD_PWR_ROLE_SRC;

	pkt->header.msg_id = prot->tx_msg_id;
	pkt->header.num_data_obj = len / 4;
	memcpy((u8 *)pkt + sizeof(struct pd_pkt_header), buf, len);

	pd_prot_tx_work(prot);

	return 0;
}

int prot_rx_send_goodcrc(struct pd_prot *pd, u8 msg_id)
{
	mutex_lock(&pd->tx_data_lock);
	pd_ctrl_msg(pd, PD_CTRL_MSG_GOODCRC, msg_id);
	pd_prot_send_phy_packet(pd, &pd->tx_buf, PD_MSG_HEADER_SIZE);
	mutex_unlock(&pd->tx_data_lock);
	return 0;
}

static int prot_fwd_ctrlmsg_to_pe(struct pd_prot *pd, struct prot_msg *msg)
{
	enum pe_event event = PE_EVT_RCVD_NONE;

	switch (msg->pkt.header.msg_type) {
	case PD_CTRL_MSG_GOODCRC:
		event = PE_EVT_RCVD_GOODCRC;
		break;
	case PD_CTRL_MSG_GOTOMIN:
		event = PE_EVT_RCVD_GOTOMIN;
		break;
	case PD_CTRL_MSG_ACCEPT:
		event = PE_EVT_RCVD_ACCEPT;
		break;
	case PD_CTRL_MSG_REJECT:
		event = PE_EVT_RCVD_REJECT;
		break;
	case PD_CTRL_MSG_PING:
		event = PE_EVT_RCVD_PING;
		break;
	case PD_CTRL_MSG_PS_RDY:
		event = PE_EVT_RCVD_PS_RDY;
		break;
	case PD_CTRL_MSG_GET_SRC_CAP:
		event = PE_EVT_RCVD_GET_SRC_CAP;
		break;
	case PD_CTRL_MSG_GET_SINK_CAP:
		event = PE_EVT_RCVD_GET_SINK_CAP;
		break;
	case PD_CTRL_MSG_DR_SWAP:
		event = PE_EVT_RCVD_DR_SWAP;
		break;
	case PD_CTRL_MSG_PR_SWAP:
		event = PE_EVT_RCVD_PR_SWAP;
		break;
	case PD_CTRL_MSG_VCONN_SWAP:
		event = PE_EVT_RCVD_VCONN_SWAP;
		break;
	case PD_CTRL_MSG_WAIT:
		event = PE_EVT_RCVD_WAIT;
		break;
	case PD_CTRL_MSG_SOFT_RESET:
		event = PE_EVT_RCVD_SOFT_RESET;
		break;
	default:
		dev_warn(pd->phy->dev, "%s:Unknown msg received, msg_type=%d\n",
				__func__, msg->pkt.header.msg_type);
	}

	if (event != PE_EVT_RCVD_NONE) {
		/* Forward the msg to policy engine. */
		pe_process_ctrl_msg(pd->p, event, &msg->pkt);
		return 0;
	}
	return -EINVAL;
}

static int prot_fwd_datamsg_to_pe(struct pd_prot *pd, struct prot_msg *msg)
{
	enum pe_event event = PE_EVT_RCVD_NONE;

	switch (msg->pkt.header.msg_type) {
	case PD_DATA_MSG_SRC_CAP:
		event = PE_EVT_RCVD_SRC_CAP;
		break;
	case PD_DATA_MSG_REQUEST:
		event = PE_EVT_RCVD_REQUEST;
		break;
	case PD_DATA_MSG_BIST:
		event = PE_EVT_RCVD_BIST;
		break;
	case PD_DATA_MSG_SINK_CAP:
		event = PE_EVT_RCVD_SNK_CAP;
		break;
	case PD_DATA_MSG_VENDOR_DEF:
		event = PE_EVT_RCVD_VDM;
		break;
	default:
		dev_warn(pd->phy->dev, "%s:Unknown msg received, msg_type=%d\n",
				__func__, msg->pkt.header.msg_type);
	}

	if (event != PE_EVT_RCVD_NONE) {
		/* Forward the msg to policy engine */
		pe_process_data_msg(pd->p, event, &msg->pkt);
		return 0;
	}
	return -EINVAL;

}

static void pd_prot_handle_rx_msg(struct pd_prot *pd, struct prot_msg *msg)
{

	/* wait for goodcrc sent */
	if (!pd->phy->support_auto_goodcrc)
		wait_for_completion(&pd->tx_complete);

	if (IS_DATA_MSG(&msg->pkt.header))
		prot_fwd_datamsg_to_pe(pd, msg);
	else if (IS_CTRL_MSG(&msg->pkt.header))
		prot_fwd_ctrlmsg_to_pe(pd, msg);
	else
		dev_warn(pd->phy->dev,
			"%s:Unknown msg type received, msg_type=%d\n",
			__func__, msg->pkt.header.msg_type);
	if (!pd->phy->support_auto_goodcrc)
		reinit_completion(&pd->tx_complete);
}

static void prot_process_rx_work(struct work_struct *work)
{
	struct pd_prot *pd = container_of(work, struct pd_prot, proc_rx_msg);
	struct prot_msg *msg;

	/* Dequeue all the messages in the rx list and process them.
	 * Note: Do not use list_for_each_entry_safe() as HARD_RESET
	 * may delete the list and may cuase error.
	 */
	mutex_lock(&pd->rx_list_lock);
	while (!list_empty(&pd->rx_list)) {
		msg = list_first_entry(&pd->rx_list,
					struct prot_msg, node);
		list_del(&msg->node);
		/* Unlock the mutex while processing the msg*/
		mutex_unlock(&pd->rx_list_lock);

		pd_prot_handle_rx_msg(pd, msg);
		prot_free_msg(msg);

		mutex_lock(&pd->rx_list_lock);
	}
	mutex_unlock(&pd->rx_list_lock);
}

static int pd_prot_add_msg_rx_list(struct pd_prot *pd,
				struct pd_packet *pkt, int len)
{
	struct prot_msg *msg;

	msg = prot_alloc_msg();
	if (!msg) {
		dev_err(pd->phy->dev,
			"failed to allocate mem for rx msg\n");
		return -ENOMEM;
	}
	memcpy(&msg->pkt, pkt, len);
	mutex_lock(&pd->rx_list_lock);

	/* Add the message to the rx list */
	list_add_tail(&msg->node, &pd->rx_list);
	mutex_unlock(&pd->rx_list_lock);
	schedule_work(&pd->proc_rx_msg);
	return 0;
}

static void pd_prot_phy_rcv(struct pd_prot *pd)
{
	struct pd_packet rcv_buf;
	int len, send_good_crc, msg_type, msg_id;

	mutex_lock(&pd->rx_data_lock);

	memset(&rcv_buf, 0, sizeof(struct pd_packet));
	len = pd_prot_recv_phy_packet(pd, &rcv_buf);
	if (len == 0)
		goto phy_rcv_end;

	msg_type = PD_MSG_TYPE(&rcv_buf.header);
	if (!pd->is_pd_enabled) {
		dev_dbg(pd->phy->dev, "%s:PD disabled, Ignore the pkt=%d\n",
					__func__, msg_type);
		goto phy_rcv_end;
	}

	if (msg_type == PD_CTRL_MSG_RESERVED_0)
		goto phy_rcv_end;

	msg_id = PD_MSG_ID(&rcv_buf.header);
	send_good_crc = 1;

	if (IS_CTRL_MSG(&rcv_buf.header)) {
		if (msg_type == PD_CTRL_MSG_SOFT_RESET)
			prot_rx_reset(pd);
		else if (msg_type == PD_CTRL_MSG_GOODCRC) {
			send_good_crc = 0;
			if (msg_id == pd->tx_msg_id) {
				pd->tx_msg_id = (msg_id + 1) & PD_MAX_MSG_ID;
				pd_prot_add_msg_rx_list(pd, &rcv_buf, len);
			} else
				dev_warn(pd->phy->dev, "GCRC msg id not matching\n");
			complete(&pd->tx_complete);
		}
	}

	if (send_good_crc) {
		if (!pd->phy->support_auto_goodcrc) {
			reinit_completion(&pd->tx_complete);
			prot_rx_send_goodcrc(pd, msg_id);
		}
		/* Check of the message is already received by comparing
		 * current msg id with previous msg id. Discard already
		 * reveived messages.
		 */
		if (pd->rx_msg_id != msg_id) {
			pd->rx_msg_id = msg_id;
			pd_prot_add_msg_rx_list(pd, &rcv_buf, len);
		} else {
			dev_warn(pd->phy->dev,
				"%s:This msg is already received\n",
				__func__);
		}
	}
phy_rcv_end:
	mutex_unlock(&pd->rx_data_lock);
}

static void pd_notify_protocol(struct typec_phy *phy, unsigned long event)
{
	struct pd_prot *pd = phy->proto;

	switch (event) {
	case PROT_PHY_EVENT_TX_SENT:
		mutex_lock(&pd->tx_lock);
		pd->event = PROT_EVENT_TX_COMPLETED;
		/*
		 * msg sent and received good crc.
		 * flush the tx fifo
		 */
		pd_prot_flush_fifo(pd, FIFO_TYPE_TX);
		mutex_unlock(&pd->tx_lock);
		complete(&pd->tx_complete);
		break;
	case PROT_PHY_EVENT_COLLISION:
		mutex_lock(&pd->tx_lock);
		pd->event = PROT_EVENT_COLLISION;
		mutex_unlock(&pd->tx_lock);
		complete(&pd->tx_complete);
		break;
	case PROT_PHY_EVENT_MSG_RCV:
		pd_prot_phy_rcv(pd);
		break;
	case PROT_PHY_EVENT_GOODCRC_SENT:
		dev_dbg(phy->dev, "%s: PROT_PHY_EVENT_GOODCRC_SENT\n",
				__func__);
		pd_prot_phy_rcv(pd);
		break;
	case PROT_PHY_EVENT_HARD_RST: /* recv HRD_RST */
	case PROT_PHY_EVENT_SOFT_RST:
		dev_dbg(phy->dev, "%s: PROT_PHY_EVENT_SOFT/HARD_RST\n",
				__func__);
		pd_prot_handle_reset(pd, event);
		/* wait for activity complete */
		break;
	case PROT_PHY_EVENT_TX_FAIL:
		dev_dbg(phy->dev, "%s: PROT_PHY_EVENT_TX_FAIL\n", __func__);
		mutex_lock(&pd->tx_lock);
		pd->event = PROT_EVENT_TX_FAIL;
		mutex_unlock(&pd->tx_lock);
		complete(&pd->tx_complete);
		break;
	case PROT_PHY_EVENT_SOFT_RST_FAIL:
		break;
	case PROT_PHY_EVENT_TX_HARD_RST: /* sent HRD_RST */
		dev_dbg(phy->dev, "%s: PROT_PHY_EVENT_TX_HARD_RST\n",
				__func__);
		/* Hard reset complete signaling */
		pe_process_cmd(pd->p, PE_EVT_RCVD_HARD_RESET_COMPLETE);
		break;
	default:
		break;
	}
}
static void prot_role_chnage_worker(struct work_struct *work)
{
	struct pd_prot *prot =
		container_of(work, struct pd_prot, role_chng_work);

	pd_prot_setup_role(prot, prot->data_role, prot->pwr_role);
}

static void pd_protocol_enable_pd(struct pd_prot *prot, bool en)
{
	mutex_lock(&prot->rx_data_lock);
	prot->is_pd_enabled = en;
	mutex_unlock(&prot->rx_data_lock);
	prot_clear_rx_msg_list(prot);
	typec_enable_autocrc(prot->phy, en);
}

int protocol_bind_pe(struct policy *p)
{
	struct typec_phy *phy;
	struct pd_prot *prot;

	if (!p)
		return -EINVAL;

	if (p->prot)
		return -EEXIST;

	phy = pe_get_phy(p);
	if (!phy || !phy->proto)
		return -ENODEV;

	prot = phy->proto;
	if (!prot)
		return -ENODEV;

	p->prot = prot;
	prot->p = p;
	return 0;
}
EXPORT_SYMBOL_GPL(protocol_bind_pe);

void protocol_unbind_pe(struct policy *p)
{
	if (!p)
		return;

	p->prot->p = NULL;
	p->prot = NULL;
}
EXPORT_SYMBOL_GPL(protocol_unbind_pe);

int protocol_bind_dpm(struct typec_phy *phy)
{
	struct pd_prot *prot;

	if (!phy)
		return -EINVAL;

	if (phy->proto) {
		dev_dbg(phy->dev, "Protocol alreay exist!\n");
		return -EEXIST;
	}

	prot = devm_kzalloc(phy->dev, sizeof(struct pd_prot), GFP_KERNEL);
	if (!prot)
		return -ENOMEM;

	prot->phy = phy;
	prot->pd_version = phy->get_pd_version(phy);

	if (prot->pd_version == 0) {
		kfree(prot);
		return -EINVAL;
	}

	/* Bind the phy to the protocol */
	phy->proto = prot;
	prot->phy->notify_protocol = pd_notify_protocol;
	mutex_init(&prot->tx_lock);
	mutex_init(&prot->tx_data_lock);
	mutex_init(&prot->rx_data_lock);
	init_completion(&prot->tx_complete);

	prot->data_role = PD_DATA_ROLE_UFP;
	prot->pwr_role = PD_POWER_ROLE_CONSUMER;

	prot->rx_msg_id = -1; /* no message is stored */
	pd_reset_counters(prot);
	pd_tx_fsm_state(prot, PROT_TX_PHY_LAYER_RESET);
	INIT_WORK(&prot->role_chng_work, prot_role_chnage_worker);
	prot->policy_fwd_pkt = pd_prot_rcv_pkt_from_policy;
	prot->policy_update_data_role = pd_policy_update_data_role;
	prot->policy_update_power_role = pd_policy_update_power_role;
	prot->protocol_enable_pd = pd_protocol_enable_pd;

	/* Init Rx list and the worker to preocess rx msgs */
	INIT_LIST_HEAD(&prot->rx_list);
	INIT_WORK(&prot->proc_rx_msg, prot_process_rx_work);
	mutex_init(&prot->rx_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(protocol_bind_dpm);

void protocol_unbind_dpm(struct typec_phy *phy)
{
	struct pd_prot *prot;

	if (!phy || !phy->proto)
		return;

	prot = phy->proto;
	/* Clear the rx list and reset phy */
	pd_reset_counters(prot);
	prot_clear_rx_msg_list(prot);
	pd_prot_flush_fifo(prot, FIFO_TYPE_TX);
	pd_prot_flush_fifo(prot, FIFO_TYPE_RX);
	pd_prot_reset_phy(prot);
	kfree(prot);
}
EXPORT_SYMBOL_GPL(protocol_unbind_dpm);

MODULE_AUTHOR("Kannappan, R <r.kannappan@intel.com>");
MODULE_DESCRIPTION("PD Protocol Layer");
MODULE_LICENSE("GPL v2");
