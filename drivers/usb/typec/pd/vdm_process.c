/*
 * vdm_process.c: Intel USB Power Delivery VDM Message Processor
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

#include <linux/delay.h>
#include "policy_engine.h"

#define PD_SID          0xff00
#define VESA_SVID       0xff01
#define INTEL_SVID	0x8086
#define UNSUPPORTED_SVID	0xeeee
#define STRUCTURED_VDM	1

/* Display port pin assignments */
#define DISP_PORT_PIN_ASSIGN_A	(1 << 0)
#define DISP_PORT_PIN_ASSIGN_B	(1 << 1)
#define DISP_PORT_PIN_ASSIGN_C	(1 << 2)
#define DISP_PORT_PIN_ASSIGN_D	(1 << 3)
#define DISP_PORT_PIN_ASSIGN_E	(1 << 4)
#define DISP_PORT_PIN_ASSIGN_F	(1 << 5)

/* Display configuration */
#define DISP_CONFIG_USB			0
#define DISP_CONFIG_UFPU_AS_DFP_D	1
#define DISP_CONFIG_UFPU_AS_UFP_D	2
#define DISP_CONFIG_RESERVED		3

/* Port partner alt-mode capability  */
#define ALT_MODE_CAP_RESERVED		0
#define ALT_MODE_CAP_UFP_D		1
#define ALT_MODE_CAP_DFP_D		2
#define ALT_MODE_CAP_BOTH_UFP_DFP_D	3

/* Display port signaling for transport */
#define DISP_PORT_SIGNAL_UNSPEC		0
#define DISP_PORT_SIGNAL_DP_1P3		1
#define DISP_PORT_SIGNAL_GEN2		2

#define DP_HPD_LONG_PULSE_TIME		5 /* 5 mSec */
#define DP_HPD_SHORT_PULSE_TIME		1   /* 1 mSec */
#define DP_HPD_AUTO_TRIGGER_TIME	200 /* 200 mSec */

static void pe_prepare_vdm_header(struct vdm_header *v_hdr, enum vdm_cmd cmd,
			enum vdm_cmd_type cmd_type, int svid, int obj_pos)
{
	v_hdr->cmd = cmd;
	v_hdr->cmd_type = cmd_type;
	v_hdr->obj_pos = obj_pos;
	v_hdr->str_vdm_version = 0x0; /* 0 = version 1.0 */
	v_hdr->vdm_type = STRUCTURED_VDM; /* Structured VDM */
	v_hdr->svid = svid;

}

static void pe_prepare_initiator_vdm_header(struct vdm_header *v_hdr,
				enum vdm_cmd cmd, int svid, int obj_pos)
{
	pe_prepare_vdm_header(v_hdr, cmd, INITIATOR, svid, obj_pos);
}

int pe_send_discover_identity(struct policy_engine *pe, int type)
{
	struct vdm_header v_hdr = { 0 };
	int ret;

	pe_prepare_initiator_vdm_header(&v_hdr, DISCOVER_IDENTITY,
						PD_SID, 0);
	ret = pe_send_packet_type(pe, &v_hdr, 4, PD_DATA_MSG_VENDOR_DEF,
				PE_EVT_SEND_VDM, type);

	return ret;
}

static void pe_send_discover_identity_responder_nack(struct policy_engine *pe)
{
	struct vdm_header v_hdr = { 0 };

	v_hdr.cmd = DISCOVER_IDENTITY;
	v_hdr.cmd_type = REP_NACK;
	v_hdr.vdm_type = STRUCTURED_VDM; /* Structured VDM */
	v_hdr.svid = PD_SID;

	if (pe_send_packet(pe, &v_hdr, 4,
				PD_DATA_MSG_VENDOR_DEF, PE_EVT_SEND_VDM))
		log_err("Failed to send DI");
	else
		log_dbg("Sent DI NACK");
}

static void pe_send_discover_identity_responder_ack(struct policy_engine *pe)
{
	u32 vdm[MAX_NUM_DATA_OBJ];
	struct vdm_header *v_hdr = (struct vdm_header *)&vdm[0];
	struct id_header_vdo *id_hdr = (struct id_header_vdo *)&vdm[1];
	struct cert_stat_vdo *cert_vdo = (struct cert_stat_vdo *)&vdm[2];
	struct product_vdo *p_vdo = (struct product_vdo *)&vdm[3];
	struct pd_platfrom_config *pconf = pe->plat_conf;

	memset(&vdm, 0, sizeof(vdm));
	pe_prepare_vdm_header(v_hdr, DISCOVER_IDENTITY, REP_ACK, PD_SID, 0);

	id_hdr->vendor_id = pconf->vendor_id;
	id_hdr->modal_op_supported = pconf->ufp_modal_op_supp;
	id_hdr->product_type = pconf->product_type;
	id_hdr->is_usb_dev_capable = pconf->usb_dev_supp;
	id_hdr->is_usb_host_capable = pconf->usb_host_supp;

	cert_vdo->tid = pconf->test_id;

	p_vdo->bcd_dev = pconf->usb_bcd_device_id;
	p_vdo->product_id = pconf->usb_product_id;
	if (pe_send_packet(pe, &vdm, 16,
				PD_DATA_MSG_VENDOR_DEF, PE_EVT_SEND_VDM))
		log_err("Failed to send DI");
	else
		log_dbg("Sent DI ACK");
}

static void pe_send_discover_svid_responder_ack(struct policy_engine *pe)
{
	u32 vdm[MAX_NUM_DATA_OBJ];
	struct vdm_header *v_hdr = (struct vdm_header *)&vdm[0];
	struct dp_vdo *svid_vdo = (struct dp_vdo *)&vdm[1];

	memset(&vdm, 0, sizeof(vdm));
	pe_prepare_vdm_header(v_hdr, DISCOVER_SVID, REP_ACK, PD_SID, 0);
	svid_vdo->svid0 = PD_SID;

	if (pe_send_packet(pe, &vdm, 8,
				PD_DATA_MSG_VENDOR_DEF, PE_EVT_SEND_VDM))
		log_err("Failed to send SVID ack");
	else
		log_dbg("Sent SVID ACK");
}

static void pe_send_discover_svid_responder_nack(struct policy_engine *pe)
{
	struct vdm_header v_hdr = { 0 };

	pe_prepare_vdm_header(&v_hdr, DISCOVER_SVID, REP_NACK, PD_SID, 0);
	log_dbg("Sending SVID NACK");
	if (pe_send_packet(pe, &v_hdr, 4,
				PD_DATA_MSG_VENDOR_DEF, PE_EVT_SEND_VDM))
		log_err("Failed to send SVID nack");
	else
		log_dbg("Sent SVID NACK");
}

static void pe_send_discover_mode_responder_nack(struct policy_engine *pe)
{
	struct vdm_header v_hdr = { 0 };

	pe_prepare_vdm_header(&v_hdr, DISCOVER_MODE,
				REP_NACK, UNSUPPORTED_SVID, 0);
	if (pe_send_packet(pe, &v_hdr, 4,
				PD_DATA_MSG_VENDOR_DEF, PE_EVT_SEND_VDM))
		log_err("Failed to send discover mode nack");
	else
		log_dbg("Sent DiscoverMode NACK");
}

static void
pe_send_enter_mode_responder_nack(struct policy_engine *pe,
						int svid, int index)
{
	struct vdm_header v_hdr = { 0 };

	pe_prepare_vdm_header(&v_hdr, ENTER_MODE, REP_NACK,
						svid, index);
	if (pe_send_packet(pe, &v_hdr, 4,
				PD_DATA_MSG_VENDOR_DEF, PE_EVT_SEND_VDM))
		log_err("Failed to send enter mode nack");
	else
		log_dbg("Sent EnterMode NACK");
}

static void
pe_send_exit_mode_responder_nack(struct policy_engine *pe,
						int svid, int index)
{
	struct vdm_header v_hdr = { 0 };

	pe_prepare_vdm_header(&v_hdr, EXIT_MODE, REP_NACK,
						svid, index);
	if (pe_send_packet(pe, &v_hdr, 4,
				PD_DATA_MSG_VENDOR_DEF, PE_EVT_SEND_VDM))
		log_err("Failed to send exit mode nack");
	else
		log_dbg("Sent ExitMode NACK");
}

int pe_send_discover_svid(struct policy_engine *pe)
{
	struct vdm_header v_hdr = { 0 };
	int ret;

	pe_prepare_initiator_vdm_header(&v_hdr, DISCOVER_SVID,
						PD_SID, 0);
	ret = pe_send_packet(pe, &v_hdr, 4,
				PD_DATA_MSG_VENDOR_DEF, PE_EVT_SEND_VDM);

	return ret;
}

int pe_send_discover_mode(struct policy_engine *pe)
{
	struct vdm_header v_hdr = { 0 };
	int ret;

	pe_prepare_initiator_vdm_header(&v_hdr, DISCOVER_MODE,
						VESA_SVID, 0);
	ret = pe_send_packet(pe, &v_hdr, 4,
				PD_DATA_MSG_VENDOR_DEF, PE_EVT_SEND_VDM);

	return ret;
}

int pe_send_enter_mode(struct policy_engine *pe, int index)
{
	struct vdm_header v_hdr = { 0 };
	int ret;

	pe_prepare_initiator_vdm_header(&v_hdr, ENTER_MODE,
						VESA_SVID, index);
	ret = pe_send_packet(pe, &v_hdr, 4,
				PD_DATA_MSG_VENDOR_DEF, PE_EVT_SEND_VDM);

	return ret;
}

int pe_send_display_status(struct policy_engine *pe)
{
	struct pd_packet pkt;
	struct dis_port_status *stat;
	struct vdm_header *v_hdr;
	int ret, index;

	if (pe->pp_alt_caps.dp_mode == TYPEC_DP_TYPE_2X)
		index = pe->pp_alt_caps.dmode_2x_index;

	else if (pe->pp_alt_caps.dp_mode == TYPEC_DP_TYPE_4X)
		index = pe->pp_alt_caps.dmode_4x_index;
	else {
		log_err("Invalid mode index=%d!!!\n", pe->pp_alt_caps.dp_mode);
		return -EINVAL;
	}

	memset(&pkt, 0, sizeof(pkt));
	v_hdr = (struct vdm_header *) &pkt.data_obj[0];
	pe_prepare_initiator_vdm_header(v_hdr, DP_STATUS_UPDATE,
						VESA_SVID, index);

	stat = (struct dis_port_status *) &pkt.data_obj[1];
	stat->dev_connected = 1;

	ret = pe_send_packet(pe, v_hdr, 8,
				PD_DATA_MSG_VENDOR_DEF, PE_EVT_SEND_VDM);

	return ret;
}

int pe_send_display_configure(struct policy_engine *pe)
{
	struct pd_packet pkt;
	struct disp_config dconf = { 0 };
	int ret, index;


	dconf.conf_sel = DISP_CONFIG_UFPU_AS_UFP_D;
	dconf.trans_sig = DISP_PORT_SIGNAL_DP_1P3;

	if (pe->pp_alt_caps.dp_mode == TYPEC_DP_TYPE_2X) {
		dconf.dfp_pin = DISP_PORT_PIN_ASSIGN_D;
		index = pe->pp_alt_caps.dmode_2x_index;
		log_dbg("DP 2X with pin assign D");

	} else if (pe->pp_alt_caps.dp_mode == TYPEC_DP_TYPE_4X) {
		if (pe->pp_alt_caps.pin_assign
				& DISP_PORT_PIN_ASSIGN_E) {
			dconf.dfp_pin = DISP_PORT_PIN_ASSIGN_E;
			log_dbg("DP 4X with pin assign E");

		} else if (pe->pp_alt_caps.pin_assign
				& DISP_PORT_PIN_ASSIGN_C) {
			log_dbg("DP 4X with pin assign C");
			dconf.dfp_pin = DISP_PORT_PIN_ASSIGN_C;
		} else {
			log_err("Unknown 4X pin assign=%x\n",
					pe->pp_alt_caps.pin_assign);
			ret = -EINVAL;
			goto config_error;
		}
		index = pe->pp_alt_caps.dmode_4x_index;

	} else {
		log_err("Invalid dp_mode=%d\n", pe->pp_alt_caps.dp_mode);
		ret = -EINVAL;
		goto config_error;
	}

	pkt.data_obj[0] = 0;
	pe_prepare_initiator_vdm_header((struct vdm_header *)&pkt.data_obj[0],
					DP_CONFIGURE, VESA_SVID, index);
	memcpy(&pkt.data_obj[1], &dconf, sizeof(dconf));

	ret = pe_send_packet(pe, &pkt.data_obj[0], 8,
				PD_DATA_MSG_VENDOR_DEF, PE_EVT_SEND_VDM);

config_error:
	return ret;
}

static int pe_handle_discover_identity(struct policy_engine *pe,
							struct pd_packet *pkt)
{
	struct vdm_header *vdm_hdr = (struct vdm_header *)&pkt->data_obj[0];
	unsigned short cmd_type = vdm_hdr->cmd_type;

	if (vdm_hdr->svid != PD_SID) {
		log_warn("Invalid SID, don't respond");
		return -EINVAL;
	}

	if (cmd_type == INITIATOR) {
		if (pe->plat_conf) {
			pe_send_discover_identity_responder_ack(pe);
		} else {
			log_warn("No platform configuration, Sending NAK");
			pe_send_discover_identity_responder_nack(pe);
		}
		return 0;
	}
	if (pe->cur_state != PE_DFP_UFP_VDM_IDENTITY_REQUEST &&
		pe->cur_state != PE_SRC_VDM_IDENTITY_REQUEST) {
		log_warn("DI RACK received in wrong state,state=%d\n",
				pe->cur_state);
		return -EINVAL;
	}
	/*
	 * consider response initiated from cable when in
	 * SRC_VDM_IDENTITY_REQUEST state
	 */
	switch (cmd_type) {
	case REP_ACK:
		/* TODO: Process the port partner's DI */
		log_dbg(" DI Acked ");
		if (pe->cur_state == PE_SRC_VDM_IDENTITY_REQUEST) {
			memcpy(&pe->cable_pkt, &pkt->data_obj[0],
				pkt->header.num_data_obj * 4);
			pe_change_state(pe, PE_SRC_WAIT_FOR_VBUS);
			return 0;
		}
		pe->alt_state = PE_ALT_STATE_DI_ACKED;
		break;
	case REP_NACK:
		log_dbg(" DI Nacked!!! ");
		log_err("Responder doesn't support alternate mode\n");
		if (pe->cur_state == PE_SRC_VDM_IDENTITY_REQUEST) {
			pe_change_state(pe, PE_SRC_WAIT_FOR_VBUS);
			return 0;
		}
		pe->alt_state = PE_ALT_STATE_ALT_MODE_FAIL;
		break;
	case REP_BUSY:
		log_info("Responder BUSY!!. Retry Discover Identity\n");
		if (pe->cur_state == PE_SRC_VDM_IDENTITY_REQUEST) {
			pe_change_state(pe, PE_SRC_WAIT_FOR_VBUS);
			return 0;
		}
		pe->alt_state = PE_ALT_STATE_NONE;
		break;
	}
	pe_change_state_to_snk_or_src_ready(pe);
	return 0;
}


static int pe_handle_discover_svid(struct policy_engine *pe,
						struct pd_packet *pkt)
{
	struct dis_svid_response_pkt *svid_pkt;
	unsigned short cmd_type;
	int num_modes = 0;
	int i, mode = 0;

	svid_pkt = (struct dis_svid_response_pkt *)pkt;
	cmd_type = svid_pkt->vdm_hdr.cmd_type;

	if (svid_pkt->vdm_hdr.svid != PD_SID) {
		log_warn("Invalid SID, don't respond");
		return -EINVAL;
	}

	if (cmd_type == INITIATOR) {
		if (pe->plat_conf->ufp_modal_op_supp)
			pe_send_discover_svid_responder_ack(pe);
		else
			pe_send_discover_svid_responder_nack(pe);
		return 0;
	}
	if (pe->cur_state != PE_DFP_VDM_SVIDS_REQUEST) {
		log_warn("SVID RACK received in wrong state,state=%d\n",
				pe->cur_state);
		return -EINVAL;
	}
	switch (cmd_type) {
	case REP_ACK:
		/* 2 modes per VDO*/
		num_modes = (svid_pkt->msg_hdr.num_data_obj - 1) * 2;
		log_dbg("SVID_ACK-> This Display supports %d modes\n",
				num_modes);
		for (i = 0; i < num_modes; i++) {
			log_dbg("vdo[%d].svid0=0x%x, svid1=0x%x\n",
				i, svid_pkt->vdo[i].svid0,
				svid_pkt->vdo[i].svid1);
			if ((svid_pkt->vdo[i].svid0 == VESA_SVID)
				|| (svid_pkt->vdo[i].svid1 == VESA_SVID)) {
				mode = VESA_SVID;
				break;
			}
		}
		/* Currently we support only VESA */
		if (mode == VESA_SVID) {
			log_dbg("This Display supports VESA\n");
			pe->alt_state = PE_ALT_STATE_SVID_ACKED;
			break;
		} else
			log_err("This Display doesn't supports VESA\n");
		/* Stop the display detection process */
	case REP_NACK:
		log_warn("Responder doesn't support alternate mode\n");
		pe->alt_state = PE_ALT_STATE_ALT_MODE_FAIL;
		break;
	case REP_BUSY:
		log_info("Responder BUSY!!. Retry Discover SVID\n");
		pe->alt_state = PE_ALT_STATE_DI_ACKED;
		break;
	}

	pe_change_state_to_snk_or_src_ready(pe);
	return 0;
}

static void pe_process_dp_modes(struct policy_engine *pe,
				struct dis_mode_response_pkt *dmode_pkt)
{
	int i;
	int index_2x = 0;
	int index_4x = 0;

	for (i = 0; i < dmode_pkt->msg_hdr.num_data_obj - 1; i++) {
		if (dmode_pkt->mode[i].port_cap != ALT_MODE_CAP_UFP_D &&
		dmode_pkt->mode[i].port_cap != ALT_MODE_CAP_BOTH_UFP_DFP_D) {
			log_dbg("Mode[%d] doesn't support UFP_D", i);
			continue;
		}
		if (!index_4x) {
			if (dmode_pkt->mode[i].ufp_pin
					& DISP_PORT_PIN_ASSIGN_E
				|| dmode_pkt->mode[i].dfp_pin
					& DISP_PORT_PIN_ASSIGN_E) {
				/* Mode intex starts from 1 */
				index_4x = i + 1;
				pe->pp_alt_caps.pin_assign |=
					DISP_PORT_PIN_ASSIGN_E;
				log_dbg("Port supports Pin Assign E\n");
			}
			if (dmode_pkt->mode[i].ufp_pin
					& DISP_PORT_PIN_ASSIGN_C
				|| dmode_pkt->mode[i].dfp_pin
					& DISP_PORT_PIN_ASSIGN_C) {
				/* Mode intex starts from 1 */
				index_4x = i + 1;
				pe->pp_alt_caps.pin_assign |=
					DISP_PORT_PIN_ASSIGN_C;
				log_dbg("Port supports Pin Assign C\n");
			}
		}
		if (!index_2x) {
			if (dmode_pkt->mode[i].ufp_pin
					& DISP_PORT_PIN_ASSIGN_D
				|| dmode_pkt->mode[i].dfp_pin
					& DISP_PORT_PIN_ASSIGN_D) {
				/* Mode intex starts from 1 */
				index_2x = i + 1;
				pe->pp_alt_caps.pin_assign |=
					DISP_PORT_PIN_ASSIGN_D;
				log_dbg("Port supports Pin Assign D\n");
			}
		}
		if (index_2x && index_4x)
			break;
	}
	pe->pp_alt_caps.dmode_4x_index = index_4x;
	pe->pp_alt_caps.dmode_2x_index = index_2x;
	log_dbg("4x_index=%d, 2x_index=%d\n", index_4x, index_2x);
}

static int pe_handle_discover_mode(struct policy_engine *pe,
						struct pd_packet *pkt)
{
	struct dis_mode_response_pkt *dmode_pkt;
	unsigned short cmd_type;

	dmode_pkt = (struct dis_mode_response_pkt *)pkt;
	cmd_type = dmode_pkt->vdm_hdr.cmd_type;

	if (cmd_type == INITIATOR) {
		pe_send_discover_mode_responder_nack(pe);
		/* TODO: Support discover mode in UFP */
		return 0;
	}
	if (pe->cur_state != PE_DFP_VDM_MODES_REQUEST) {
		log_warn("DiscMode RACK received in wrong state=%d\n",
					pe->cur_state);
		return -EINVAL;
	}

	switch (cmd_type) {

	case REP_ACK:
		log_dbg("Discover Mode Acked\n");
		pe_process_dp_modes(pe, dmode_pkt);
		/* First check for 2X, Mode D */
		if (pe->pp_alt_caps.dmode_2x_index
			|| pe->pp_alt_caps.dmode_4x_index) {
			pe->alt_state = PE_ALT_STATE_DMODE_ACKED;
			break;
		}
		log_warn("This Display doesn't supports neither 2X nor 4X\n");
		/* Stop the display detection process */

	case REP_NACK:
		log_warn("Responder doesn't support alternate mode\n");
		pe->alt_state = PE_ALT_STATE_ALT_MODE_FAIL;
		break;
	case REP_BUSY:
		log_warn("Responder BUSY!!. Retry Discover Mode\n");
		pe->alt_state = PE_ALT_STATE_SVID_ACKED;
		break;
	}

	pe_change_state_to_snk_or_src_ready(pe);
	return 0;
}


static int pe_handle_enter_mode(struct policy_engine *pe,
						struct pd_packet *pkt)
{
	struct vdm_header *vdm_hdr = (struct vdm_header *)&pkt->data_obj[0];
	unsigned short cmd_type = vdm_hdr->cmd_type;

	if (cmd_type == INITIATOR) {
		pe_send_enter_mode_responder_nack(pe,
				vdm_hdr->svid, vdm_hdr->obj_pos);
		/* TODO: Support enter mode in UFP */
		return 0;
	}
	if (pe->cur_state != PE_DFP_VDM_MODES_ENTRY_REQUEST) {
		log_warn("Entermode RACK received in wrong state,state=%d\n",
				pe->cur_state);
		return -EINVAL;
	}
	switch (cmd_type) {

	case REP_ACK:
		log_info("EnterMode Success, dp_mode=%d\n",
				pe->pp_alt_caps.dp_mode);
		pe->is_modal_operation = true;
		pe->alt_state = PE_ALT_STATE_EMODE_ACKED;
		break;

	case REP_NACK:
		log_warn("Display falied to enter dp mode %d\n",
				pe->pp_alt_caps.dp_mode);
		pe->alt_state = PE_ALT_STATE_ALT_MODE_FAIL;
		break;

	case REP_BUSY:
		log_warn("Responder BUSY!!. Retry Enter Mode\n");
		pe->alt_state = PE_ALT_STATE_DMODE_ACKED;
		break;
	}

	pe_change_state_to_snk_or_src_ready(pe);
	return 0;
}

static int pe_handle_exit_mode(struct policy_engine *pe,
						struct pd_packet *pkt)
{
	struct vdm_header *vdm_hdr = (struct vdm_header *)&pkt->data_obj[0];
	unsigned short cmd_type = vdm_hdr->cmd_type;

	if (cmd_type == INITIATOR)
		pe_send_exit_mode_responder_nack(pe,
				vdm_hdr->svid, vdm_hdr->obj_pos);
	/* TODO: handle exit mode responce */
	return 0;
}

static int pe_handle_display_status(struct policy_engine *pe,
							struct pd_packet *pkt)
{
	struct vdm_header *vdm_hdr = (struct vdm_header *)&pkt->data_obj[0];
	struct dis_port_status *disp_stat;
	unsigned short cmd_type = vdm_hdr->cmd_type;

	if (cmd_type == INITIATOR) {
		log_warn("UFP alternate mode not supported, Sending NAK\n");
		return 0;
	}
	if (pe->cur_state != PE_DFP_VDM_STATUS_REQUEST) {
		log_warn("DI Status received in wrong state,state=%d\n",
				pe->cur_state);
		return -EINVAL;
	}

	switch (cmd_type) {
	case REP_ACK:
		log_dbg(" Status Acked ");
		pe->alt_state = PE_ALT_STATE_STATUS_ACKED;
		disp_stat = (struct dis_port_status *)&pkt->data_obj[1];
		pe->pp_alt_caps.pp_hpd_state = disp_stat->hpd_state;
		break;

	case REP_NACK:
		log_err(" Status Nacked!!! ");
		pe->alt_state = PE_ALT_STATE_ALT_MODE_FAIL;
		break;

	case REP_BUSY:
		log_warn("Responder BUSY!!. Retry Status\n");
		pe->alt_state = PE_ALT_STATE_EMODE_ACKED;
		break;
	}

	pe_change_state_to_snk_or_src_ready(pe);
	return 0;
}

static int pe_handle_display_configure(struct policy_engine *pe,
						struct pd_packet *pkt)
{
	struct vdm_header *vdm_hdr = (struct vdm_header *)&pkt->data_obj[0];
	unsigned short cmd_type = vdm_hdr->cmd_type;

	if (cmd_type == INITIATOR) {
		log_warn("UFP alternate mode not supported, Sending NAK\n");
		return 0;
	}
	if (pe->cur_state != PE_DFP_VDM_CONF_REQUEST) {
		log_warn("DP CONFIG ACK received in wrong state,state=%d\n",
				pe->cur_state);
		return -EINVAL;
	}
	switch (cmd_type) {

	case REP_ACK:
		log_info("DP Config success!!, dp_mode=%d\n",
					pe->pp_alt_caps.dp_mode);

		pe->alt_state = PE_ALT_STATE_ALT_MODE_SUCCESS;
		pe->pp_alt_caps.hpd_state = true;
		devpolicy_set_dp_state(pe->p.dpm, CABLE_ATTACHED,
						pe->pp_alt_caps.dp_mode);
		break;
	case REP_NACK:
		log_warn("NAK for display config cmd %d\n",
					pe->pp_alt_caps.dp_mode);
		pe->alt_state = PE_ALT_STATE_ALT_MODE_FAIL;
		break;

	case REP_BUSY:
		log_warn("Responder BUSY!!. Retry CONFIG\n");
		pe->alt_state = PE_ALT_STATE_STATUS_ACKED;
		break;
	}

	pe_change_state_to_snk_or_src_ready(pe);
	return 0;
}

static void pe_generate_hpd_pulse(struct policy_engine *pe, int pulse_time)
{
	log_dbg("Pulling HPD low\n");
	devpolicy_set_hpd_state(pe->p.dpm, 0);
	udelay(pulse_time * 1000);
	log_dbg("Raising HPD to high\n");
	devpolicy_set_hpd_state(pe->p.dpm, 1);
}

static void pe_handle_hpd_state_change(struct policy_engine *pe, bool hpd)
{
	if (pe->pp_alt_caps.hpd_state != hpd) {
		pe->pp_alt_caps.hpd_state = hpd;
		if (hpd)
			devpolicy_set_dp_state(pe->p.dpm, CABLE_ATTACHED,
						pe->pp_alt_caps.dp_mode);
		else
			devpolicy_set_dp_state(pe->p.dpm, CABLE_DETACHED,
						TYPEC_DP_TYPE_NONE);
	} else {
		/*
		 * Some dp cable which doesnt support status update cmd
		 * which is expected after EnterMode. Due to this the hpd
		 * status cannot be known after EnterMode. To fix this by
		 * default hpd will be triggered after dp configure.
		 * So, if previous hpd is true then retrigger with
		 * short hpd pulse.
		 */
		if (pe->pp_alt_caps.hpd_state) {
			/* Generate hpd long pulse */
			log_dbg("Generating short pulse for display re-detect");
			pe_generate_hpd_pulse(pe,
					DP_HPD_SHORT_PULSE_TIME);
		}
	}
}

static int pe_handle_display_attention(struct policy_engine *pe,
						struct pd_packet *pkt)
{
	struct vdm_header *vdm_hdr = (struct vdm_header *)&pkt->data_obj[0];
	struct dis_port_status *dstat =
				(struct dis_port_status *)&pkt->data_obj[1];

	switch (vdm_hdr->cmd_type) {
	case INITIATOR:
		log_warn("Attention received\n");
		log_info("pp_hpd_status=%d\n", dstat->hpd_state);
		if (pe->pp_alt_caps.pp_hpd_state != dstat->hpd_state) {
			log_dbg("Change in port partner's HPD status\n");
			pe->pp_alt_caps.pp_hpd_state = dstat->hpd_state;
			pe_handle_hpd_state_change(pe, dstat->hpd_state);
		}

		if (dstat->irq_hpd) {
			log_dbg("Got IRQ HPD\n");
			if (pe->pp_alt_caps.hpd_state) {
				/* Generate hpd short pulse */
				pe_generate_hpd_pulse(pe,
						DP_HPD_SHORT_PULSE_TIME);
			}
		}

		break;
	case REP_ACK:
		log_warn("ACK received for attention!!!!!\n");
		break;
	case REP_NACK:
		log_warn("NACK received for attention!!!!!\n");
	case REP_BUSY:
		log_warn("BUSY received for attention!!!!!\n");
	}
	return 0;
}

int pe_handle_vendor_msg(struct policy_engine *pe,
						struct pd_packet *pkt)
{
	struct vdm_header *vdm_hdr = (struct vdm_header *)&pkt->data_obj[0];
	int ret = 0;

	if (vdm_hdr->vdm_type != STRUCTURED_VDM) {
		log_warn("Unstructure vdm not supported");
		return -EINVAL;
	}

	switch (vdm_hdr->cmd) {
	case DISCOVER_IDENTITY:
		ret = pe_handle_discover_identity(pe, pkt);
		break;
	case DISCOVER_SVID:
		ret = pe_handle_discover_svid(pe, pkt);
		break;
	case DISCOVER_MODE:
		ret = pe_handle_discover_mode(pe, pkt);
		break;
	case ENTER_MODE:
		ret = pe_handle_enter_mode(pe, pkt);
		break;
	case EXIT_MODE:
		log_info("EXIT DP mode request received\n");
		ret = pe_handle_exit_mode(pe, pkt);
		break;
	case DP_STATUS_UPDATE:
		log_dbg("Received display status from port partner=%x\n",
					pkt->data_obj[1]);
		ret = pe_handle_display_status(pe, pkt);
		break;
	case DP_CONFIGURE:
		ret = pe_handle_display_configure(pe, pkt);
		break;
	case ATTENTION:
		log_dbg("Received display attention from port partner=%x\n",
					pkt->data_obj[1]);
		ret = pe_handle_display_attention(pe, pkt);
		break;
	default:
		ret = -EINVAL;
		log_err("Not a valid vendor msg to handle\n");
	}
	return ret;
}
