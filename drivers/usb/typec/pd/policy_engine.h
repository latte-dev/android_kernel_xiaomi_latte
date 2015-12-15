/*
 * policy_engine.h : Intel USB Power Delivery Policy Engine Header
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

#ifndef __POLICY_ENGINE_H__
#define __POLICY_ENGINE_H__

#include "protocol.h"
#include "pd_policy.h"

/* Policy engine time values in mSec*/
#define PE_TIME_ATTENTION_AVERAGE	10000
#define PE_TIME_ATTENTION_BURST_SPACING	100
#define PE_TIME_ATTENTION_SPACING	250
#define PE_TIME_BIST_MODE		300
#define PE_TIME_BIST_CONT_MODE		60
#define PE_TIME_BIST_RECEIVE		1
#define PE_TIME_BIST_RESPONSE		15
#define PE_TIME_DISCOVER_IDENTITY	50
#define PE_TIME_DR_SWAP_HARD_RESET	15
#define PE_TIME_FIRST_SOURCE_CAP	250
#define PE_TIME_HARD_RESET		5
#define PE_TIME_HARD_RESET_COMPLETE	5
#define PE_TIME_NO_RESPONSE		5500
#define PE_TIME_PS_HARD_RESET_MAX	35
#define PE_TIME_PS_HARD_RESET_MIN	25
#define PE_TIME_PS_SOURCE_OFF		920
#define PE_TIME_PS_SOURCE_ON		480
#define PE_TIME_PS_TRANSITION		550
/* tReceive is time to receive gcrc and it is 1.1mS as per spec */
/* Due to software delay, here it is defines as 10mS */
#define PE_TIME_RECEIVE			20
#define PE_TIME_RECEIVER_RESPONSE	15
#define PE_TIME_SENDER_RESPONSE		30
#define PE_TIME_SEND_SOURCE_CAP		2000
#define PE_TIME_SINK_ACTIVITY		150
#define PE_TIME_SINK_REQUEST		100
#define PE_TIME_SINK_WAIT_CAP		2500
#define PE_TIME_SOFT_RESET		15
#define PE_TIME_SOURCE_ACTIVITY		50
#define PE_TIME_SWAP_SINK_READY		15
#define PE_TIME_SWAP_SOURCE_START	20
#define PE_TIME_TYPEC_SEND_SOURCE_CAP	200
#define PE_TIME_TYPEC_SINK_WAIT_CAP	620
#define PE_TIME_VCONN_SOURCE_OFF	25
#define PE_TIME_VCONN_SOURCE_ON		100
#define PE_TIME_VDM_BUSSY		50
#define PE_TIME_VDM_ENTER_MODE		25
#define PE_TIME_VDM_EXIT_MODE		25
#define PE_TIME_VDM_RECEIVER_RESPONSE	15
#define PE_TIME_VDM_SENDER_RESPONSE	30
#define PE_TIME_VDM_WAIT_MODE_ENTRY	50
#define PE_TIME_VDM_WAIT_MODE_EXIT	50

/* Policy engine time values in uSec*/
#define PE_TIME_CABLE_MESSAGE		750
#define PE_TIME_RETRY			75
#define PE_TIME_TRANSMIT		195

/* PE misc time values */
#define VBUS_POLL_TIME			10 /* 10 mSec */

/* Counter values */
#define PE_N_ATTENTION_COUNT		10
#define PE_N_BUSY_COUNT			5
#define PE_N_CAPS_COUNT			50
#define PE_N_DISCOVER_IDENTITY_COUNT	20
#define PE_N_HARD_RESET_COUNT		2
#define PE_N_MESSAGE_ID_COUNT		7
#define PE_N_RETRY_COUNT		3
#define PE_N_VBUS_CHECK_COUNT		20

#define SNK_FSPDO_VOLT_SHIFT		10
#define SNK_FSPDO_FIXED_SUPPLY		(3 << 30)
#define SNK_FSPDO_DUAL_ROLE_PWR		(1 << 29)
#define SNK_FSPDO_HIGHTER_CAPABILITY	(1 << 28)
#define SNK_FSPDO_EXT_POWERED		(1 << 27)
#define SNK_FSPDO_USB_COMM_CAPABLE	(1 << 26)
#define SNK_FSPDO_DATA_ROLE_SWAP	(1 << 25)
#define SNK_FSPDO_RESERVED		(3 << 20)
#define SNK_FSPDO_VOLTAGE		(0x3FF << SNK_FSPDO_VOLT_SHIFT)
#define SNK_FSPDO_MAX_CURRENT		(0x3FF << 0)

/*Electrical Requirements, Time in mSec */
#define T_SAFE_0V_MAX			650
#define T_SAFE_5V_MAX			275
#define T_SRC_RECOVER_MIN		660
#define T_SRC_RECOVER_MAX		1000
#define T_SNK_WAIT_FOR_VBUS_OFF		(PE_TIME_PS_HARD_RESET_MAX + \
					T_SAFE_0V_MAX)
#define T_SNK_WAIT_FOR_VBUS_ON		(T_SRC_RECOVER_MAX + \
					T_SAFE_5V_MAX)
#define T_SRC_TURN_ON			275
#define T_SRC_TRANSITION		35

/* returns in mV */
#define DATA_OBJ_TO_VOLT(x)	(((x & SNK_FSPDO_VOLTAGE) >>	\
					SNK_FSPDO_VOLT_SHIFT) * 50)
/* returns in mA */
#define DATA_OBJ_TO_CURRENT(x)	((x & SNK_FSPDO_MAX_CURRENT) * 10)

#define VOLT_TO_DATA_OBJ(x)	(((x / 50) << SNK_FSPDO_VOLT_SHIFT) &	\
					SNK_FSPDO_VOLTAGE)
#define CURRENT_TO_DATA_OBJ(x)	((x / 10) & SNK_FSPDO_MAX_CURRENT)
#define VOLT_TO_CAP_DATA_OBJ(x)		(x / 50)
#define CURRENT_TO_CAP_DATA_OBJ(x)	(x / 10)

#define FEATURE_SUPPORTED	1
#define FEATURE_NOT_SUPPORTED	0

#define LOG_TAG "PE"
#define log_info(format, ...) \
	pr_info(LOG_TAG":%s:"format"\n", __func__, ##__VA_ARGS__)
#define log_dbg(format, ...) \
	pr_debug(LOG_TAG":%s:"format"\n", __func__, ##__VA_ARGS__)
#define log_err(format, ...) \
	pr_err(LOG_TAG":%s:"format"\n", __func__, ##__VA_ARGS__)
#define log_warn(format, ...) \
	pr_warn(LOG_TAG":%s:"format"\n", __func__, ##__VA_ARGS__)

#define PE_MAX_RETRY			20
#define PE_AUTO_TRIGGERING_DELAY	100 /* 100 mSec */

enum pe_states {

	PE_STATE_NONE,

	/* Source Port (1 - 14 ) */
	PE_SRC_STARTUP,
	PE_SRC_DISCOVERY,
	PE_SRC_SEND_CAPABILITIES,
	PE_SRC_NEGOTIATE_CAPABILITY,
	PE_SRC_TRANSITION_SUPPLY,
	PE_SRC_READY,
	PE_SRC_DISABLED,
	PE_SRC_CAPABILITY_RESPONSE,
	PE_SRC_HARD_RESET,
	PE_SRC_HARD_RESET_RECEIVED,
	PE_SRC_TRANSITION_TO_DEFAULT,
	PE_SRC_GIVE_SOURCE_CAP,
	PE_SRC_GET_SINK_CAP,
	PE_SRC_WAIT_NEW_CAPABILITIES,
	/* Extra state added to check VBUS before sending SrcCap */
	PE_SRC_WAIT_FOR_VBUS,
	/* Reserved for src port state enhancements */
	PE_SRC_STATES_RESERVED = 22,

	/* Sink Port (23 - 34*/
	PE_SNK_STARTUP,
	PE_SNK_DISCOVERY,
	PE_SNK_WAIT_FOR_CAPABILITIES,
	PE_SNK_EVALUATE_CAPABILITY,
	PE_SNK_SELECT_CAPABILITY,
	PE_SNK_TRANSITION_SINK,
	PE_SNK_READY,
	PE_SNK_HARD_RESET,
	PE_SNK_TRANSITION_TO_DEFAULT,
	PE_SNK_GIVE_SINK_CAP,
	PE_SNK_GET_SOURCE_CAP,
	PE_SNK_HARD_RESET_RECEIVED,
	/* When PE come to this state from hard reset, then pe
	 * should wait for source to off VBUS on reset*/
	PE_SNK_WAIT_FOR_HARD_RESET_VBUS_OFF,
	/* Reserved for sink state enhancements */
	PE_SNK_STATES_RESERVED = 40,

	/* Source Port Soft Reset (41, 42) */
	PE_SRC_SEND_SOFT_RESET,
	PE_SRC_SOFT_RESET,

	/* Sink Port Soft Reset (43, 44) */
	PE_SNK_SEND_SOFT_RESET,
	PE_SNK_SOFT_RESET,

	/* Source Port Ping (45)*/
	PE_SRC_PING,

	/* Type-A/B Dual-Role (initially Source Port) Ping (46) */
	PE_PRS_SRC_SNK_PING,

	/* Type-A/B Dual-Role (initially Sink Port) Ping (47) */
	PE_PRS_SNK_SRC_PING,

	/* Type-A/B Hard Reset of P/C in Sink Role (48, 49) */
	PE_PC_SNK_HARD_RESET,
	PE_PC_SNK_SWAP_RECOVERY,

	/* Type-A/B Hard Reset of C/P in Source Role (50, 51) */
	PE_CP_SRC_HARD_RESET,
	PE_CP_SRC_TRANSITION_TO_OFF,

	/* Type-A/B C/P Dead Battery/Power Loss (52 - 58) */
	PE_DB_CP_CHECK_FOR_VBUS,
	PE_DB_CP_POWER_VBUS_DB,
	PE_DB_CP_WAIT_FOR_BIT_STREAM,
	PE_DB_CP_POWER_VBUS_5V,
	PE_DB_CP_WAIT_BIT_STREAM_STOP,
	PE_DB_CP_UNPOWER_VBUS,
	PE_DB_CP_PS_DISCHARGE,

	/* Type-A/B P/C Dead Battery/Power Loss (59 - 63) */
	PE_DB_PC_UNPOWERED,
	PE_DB_PC_CHECK_POWER,
	PE_DB_PC_SEND_BIT_STREAM,
	PE_DB_PC_WAIT_TO_DETECT,
	PE_DB_PC_WAIT_TO_START,

	/* Reserved State for DB (63 - 70) */
	PE_DB_PC_RESERVED = 70,

	/* Type-C DFP to UFP Data Role Swap (71 - 75) */
	PE_DRS_DFP_UFP_EVALUATE_DR_SWAP,
	PE_DRS_DFP_UFP_ACCEPT_DR_SWAP,
	PE_DRS_DFP_UFP_CHANGE_TO_UFP,
	PE_DRS_DFP_UFP_SEND_DR_SWAP,
	PE_DRS_DFP_UFP_REJECT_DR_SWAP,

	/* Type-C UFP to DFP Data Role Swap (76 - 80) */
	PE_DRS_UFP_DFP_EVALUATE_DR_SWAP,
	PE_DRS_UFP_DFP_ACCEPT_DR_SWAP,
	PE_DRS_UFP_DFP_CHANGE_TO_DFP,
	PE_DRS_UFP_DFP_SEND_DR_SWAP,
	PE_DRS_UFP_DFP_REJECT_DR_SWAP,

	/* Reserved State for DR_SWAP (81 - 85) */
	PE_DSR_RESERVED = 85,

	/* Source to Sink Power Role Swap (86 - 92) */
	PE_PRS_SRC_SNK_EVALUATE_PR_SWAP,
	PE_PRS_SRC_SNK_ACCEPT_PR_SWAP,
	PE_PRS_SRC_SNK_TRANSITION_TO_OFF,
	PE_PRS_SRC_SNK_ASSERT_RD,
	PE_PRS_SRC_SNK_WAIT_SOURCE_ON,
	PE_PRS_SRC_SNK_SEND_PR_SWAP,
	PE_PRS_SRC_SNK_REJECT_PR_SWAP,

	/* Sink to Source Power Role Swap (93 - 99) */
	PE_PRS_SNK_SRC_EVALUATE_PR_SWAP,
	PE_PRS_SNK_SRC_ACCEPT_PR_SWAP,
	PE_PRS_SNK_SRC_TRANSITION_TO_OFF,
	PE_PRS_SNK_SRC_ASSERT_RP,
	PE_PRS_SNK_SRC_SOURCE_ON,
	PE_PRS_SNK_SRC_SEND_PR_SWAP,
	PE_PRS_SNK_SRC_REJECT_PR_SWAP,

	/* Reserved State for PR_SWAP (100 - 105) */
	PE_PSR_RESERVED = 105,

	/* Dual-Role Source Port Get Source Capabilities (106) */
	PE_DR_SRC_GET_SOURCE_CAP,

	/* Dual-Role Source Port Give Sink Capabilities (107) */
	PE_DR_SRC_GIVE_SINK_CAP,

	/* Dual-Role Sink Port Get Sink Capabilities (108) */
	PE_DR_SNK_GET_SINK_CAP,

	/* Dual-Role Sink Port Give Source Capabilities (109) */
	PE_DR_SNK_GIVE_SOURCE_CAP,

	/* Type-C VCONN Swap (110 - 117) */
	/*VCONN Swap states as per PD V1.1 */
	PE_VCS_SEND_SWAP,
	PE_VCS_EVALUATE_SWAP,
	PE_VCS_ACCEPT_SWAP,
	PE_VCS_REJECT_SWAP,
	PE_VCS_WAIT_FOR_VCONN,
	PE_VCS_TURN_OFF_VCONN,
	PE_VCS_TURN_ON_VCONN,
	PE_VCS_SEND_PS_RDY,

	/* Reserved State 1 (118 - 125) */
	PE_RESERVED_1 = 125,

	/* UFP VDM (126 - 140) */
	PE_UFP_VDM_GET_IDENTITY,
	PE_UFP_VDM_SEND_IDENTITY,
	PE_UFP_VDM_GET_IDENTITY_NAK,
	PE_UFP_VDM_GET_SVIDS,
	PE_UFP_VDM_SEND_SVIDS,
	PE_UFP_VDM_GET_SVIDS_NAK,
	PE_UFP_VDM_GET_MODES,
	PE_UFP_VDM_SEND_MODES,
	PE_UFP_VDM_GET_MODES_NAK,
	PE_UFP_VDM_EVALUATE_MODE_ENTRY,
	PE_UFP_VDM_MODE_ENTRY_ACK,
	PE_UFP_VDM_MODE_ENTRY_NAK,
	PE_UFP_VDM_MODE_EXIT,
	PE_UFP_VDM_MODE_EXIT_ACK,
	PE_UFP_VDM_MODE_EXIT_NAK,

	/* UFP VDM Attention (141) */
	PE_UFP_VDM_ATTENTION_REQUEST,

	/* UFP VDM Reserved States  (142 - 150) */
	PE_UFP_VDM_RESERVED = 150,

	/* DFP -UFP VDM Discover Identity (151 - 153) */
	PE_DFP_UFP_VDM_IDENTITY_REQUEST,
	PE_DFP_UFP_VDM_IDENTITY_ACKED,
	PE_DFP_UFP_VDM_IDENTITY_NAKED,

	/* DFP - Cable VDM Discover Identity (154 - 156) */
	PE_DFP_CBL_VDM_IDENTITY_REQUEST,
	PE_DFP_CBL_VDM_IDENTITY_ACKED,
	PE_DFP_CNL_VDM_IDENTITY_NAKED,

	/* DFP VDM Discover SVIDs (157 - 159) */
	PE_DFP_VDM_SVIDS_REQUEST,
	PE_DFP_VDM_SVIDS_ACKED,
	PE_DFP_VDM_SVIDS_NAKED,

	/* DFP VDM Discover Modes (160 - 162) */
	PE_DFP_VDM_MODES_REQUEST,
	PE_DFP_VDM_MODES_ACKED,
	PE_DFP_VDM_MODES_NAKED,

	/* DFP VDM Mode Entry (163 - 165) */
	PE_DFP_VDM_MODES_ENTRY_REQUEST,
	PE_DFP_VDM_MODES_ENTRY_ACKED,
	PE_DFP_VDM_MODES_ENTRY_NAKED,

	/* DFP VDM Mode Exit (166, 167) */
	PE_DFP_VDM_MODE_EXIT_REQUEST,
	PE_DFP_VDM_MODE_EXIT_ACKED,

	/* DFP VDM Status (168 - 170) */
	PE_DFP_VDM_STATUS_REQUEST,
	PE_DFP_VDM_STATUS_ACKED,
	PE_DFP_VDM_STATUS_NAKED,

	/* DFP VDM Configure (171 - 173) */
	PE_DFP_VDM_CONF_REQUEST,
	PE_DFP_VDM_CONF_ACKED,
	PE_DFP_VDM_CONF_NAKED,

	/* DFP VDM Attention (174) */
	PE_DFP_VDM_ATTENTION_REQUEST,

	/* DFP VDM Reserved States  (175 - 195 */
	PE_DFP_VDM_RESERVED = 195,

	/* USB to USB Cable (196 - 207) */
	PE_CBL_READY,
	PE_CBL_GET_IDENTITY,
	PE_CBL_SEND_IDENTITY,
	PE_CBL_GEG_SVIDS,
	PE_CBL_SEND_SVIDS,
	PE_CBL_GEG_MODES,
	PE_CBL_SEND_MODES,
	PE_CBL_EVALUATE_MODE_ENTRY,
	PE_CBL_MODE_ENTRY_ACK,
	PE_CBL_MODE_ENTRY_NAK,
	PE_CBL_MODE_EXUIT,
	PE_CBL_MODE_EXIT_ACK,

	/* Cable Soft Reset (208) */
	PE_CBL_SOFT_RESET,

	/* Cable Hard Reset (209) */
	PE_CBL_HARD_RESET,

	/* CBL Reserved States  (210 - 215 */
	PE_CBL_RESERVED = 215,

	/* BIST Receive Mode (216, 217) */
	PE_BIST_RECEIVE_MODE,
	PE_BIST_FRAME_RECEIVED,

	/* BIST Transmit Mode (218, 219) */
	PE_BIST_TRANSMIT_MODE,
	PE_BIST_SEND_FRAME,

	/* BIST Carrier Mode and Eye Pattern (220 - 224) */
	PE_BIST_EYE_PATTERN_MODE,
	PE_BIST_CARRIER_MODE_0,
	PE_BIST_CARRIER_MODE_1,
	PE_BIST_CARRIER_MODE_2,
	PE_BIST_CARRIER_MODE_3,

	/* Type-C referenced states (500) */
	ERROR_RECOVERY = 500,

	/* This is PE internal error recovery state, in which
	 * a disconnect will be issued and toggling will be
	 * started to start the detection process.
	 * hence move to PE_STATE_NONE.
	 */
	PE_ERROR_RECOVERY = 501,
};


struct pe_port_partner_caps {
	unsigned pp_is_dual_drole:1;
	unsigned pp_is_dual_prole:1;
	unsigned pp_is_ext_pwrd:1;
};

enum pe_timers {
	/* 0 - 4 */
	BIST_CONT_MODE_TIMER,
	BIST_RECEIVE_ERROR_TIMER,
	BIST_START_TIMER,
	CRC_RECEIVE_TIMER,
	DISCOVER_IDENTITY_TIMER,
	/*5 - 9 */
	HARD_RESET_COMPLETE_TIMER,
	NO_RESPONSE_TIMER,
	PS_HARD_RESET_TIMER,
	PS_SOURCE_OFF_TIMER,
	PS_SOURCE_ON_TIMER,
	/* 10 - 14 */
	PS_TRANSITION_TIMER,
	SENDER_RESPONSE_TIMER,
	SINK_ACTIVITY_TIMER,
	SINK_REQUEST_TIMER,
	SINK_WAIT_CAP_TIMER,

	/* 15 - 19 */
	SOURCE_ACTIVITY_TIMER,
	SOURCE_CAPABILITY_TIMER,
	SWAP_RECOVERY_TIMER,
	SWAP_SOURCE_START_TIMER,
	VCONN_ON_TIMER,
	/* 20 - 22 */
	VDM_MODE_ENTRY_TIMER,
	VDM_MODE_EXIT_TIMER,
	VMD_RESPONSE_TIMER,
	/*23, Misc timer */
	VBUS_CHECK_TIMER,
	SRC_RESET_RECOVER_TIMER,
	SRC_TRANSITION_TIMER,

	/* timer count */
	PE_TIMER_CNT,
};

struct pe_pp_alt_caps {
	unsigned short dmode_2x_index;
	unsigned short dmode_4x_index;
	unsigned short dmode_cur_index;
	unsigned short dp_mode;
	u8 pin_assign;

	unsigned usb_dev_support:1;
	unsigned usb_host_support:1;
	unsigned pp_hpd_state:1;
	unsigned hpd_state:1;
};

enum pe_alt_mode_state {
	PE_ALT_STATE_NONE,
	PE_ALT_STATE_DI_SENT,
	PE_ALT_STATE_DI_ACKED,
	PE_ALT_STATE_SVID_SENT,
	PE_ALT_STATE_SVID_ACKED,
	PE_ALT_STATE_DMODE_SENT,
	PE_ALT_STATE_DMODE_ACKED,
	PE_ALT_STATE_EMODE_SENT,
	PE_ALT_STATE_EMODE_ACKED,
	PE_ALT_STATE_STATUS_SENT,
	PE_ALT_STATE_STATUS_ACKED,
	PE_ALT_STATE_CONF_SENT,
	PE_ALT_STATE_ALT_MODE_SUCCESS,
	PE_ALT_STATE_ALT_MODE_FAIL,
};


struct pe_timer {
	enum pe_timers timer_type;
	unsigned timer_val; /* mSec unit */
	struct timer_list timer;
	struct work_struct work;
	void *data;
};

struct pe_port_pdos {
	int num_pdos;
	u32 pdo[MAX_NUM_DATA_OBJ];
};

struct pe_req_cap {
	u8 obj_pos;
	u32 op_ma;
	u32 max_ma;
	u32 mv;
	bool cap_mismatch;
};

struct policy_engine {
	struct policy p;

	struct mutex pe_lock;
	struct mutex dpm_evt_lock;

	struct work_struct policy_init_work;
	struct work_struct policy_state_work;

	struct pe_port_pdos pp_snk_pdos;
	struct pe_port_pdos pp_src_pdos;
	struct pe_port_pdos self_snk_pdos;
	struct pe_port_pdos self_src_pdos;

	struct pe_port_partner_caps pp_caps;
	struct pe_req_cap self_sink_req_cap;
	/* port partner's sink request caps*/
	struct pd_fixed_var_rdo pp_sink_req_caps;

	/* Timer structs for pe_timers */
	struct pe_timer timers[PE_TIMER_CNT];

	enum pe_states cur_state;
	enum pe_states prev_state;
	enum data_role	cur_drole;
	enum pwr_role cur_prole;
	enum pe_event last_rcv_evt;
	enum pe_event last_sent_evt;

	/* WA as DPM doesnt have VBUS notification*/
	struct delayed_work vbus_poll_work;
	bool vbus_status;

	struct delayed_work post_ready_work;
	enum pe_alt_mode_state	alt_state;
	struct pe_pp_alt_caps pp_alt_caps;

	/* PE counters */
	unsigned src_caps_couner;
	unsigned discover_identity_couner;
	unsigned hard_reset_counter;
	unsigned vdm_busy_couner;
	unsigned retry_counter;

	/* bool variables */
	unsigned is_typec_port:1;
	unsigned is_pp_pd_capable:1;
	unsigned is_no_response_timer_expired:1;
	unsigned is_gcrc_received:1;
	unsigned pd_explicit_contract:1;
	unsigned is_modal_operation:1;
	unsigned is_pr_swap_rejected:1;
	unsigned is_pd_enabled:1;
};


extern int dpm_register_pe(struct policy *p, int port);
extern void dpm_unregister_pe(struct policy *p);
extern int protocol_bind_pe(struct policy *p);
extern void protocol_unbind_pe(struct policy *p);

void pe_change_state_to_snk_or_src_ready(struct policy_engine *pe);
int pe_send_packet(struct policy_engine *pe, void *data, int len,
				u8 msg_type, enum pe_event evt);
#endif /*  __POLICY_ENGINE_H__ */
