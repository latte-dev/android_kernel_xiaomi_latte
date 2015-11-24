#ifndef __SINK_PORT_PE__H__
#define __SINK_PORT_PE__H__

#include "policy_engine.h"

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

#define IS_DUAL_ROLE_POWER(x)	(x & SNK_FSPDO_DUAL_ROLE_PWR)
#define IS_USB_SUSPEND_SUPP(x)	(x & SNK_FSPDO_HIGHTER_CAPABILITY)
#define IS_EXT_POWERED(x)	(x & SNK_FSPDO_EXT_POWERED)
#define IS_USB_COMM_CAP(x)	(x & SNK_FSPDO_USB_COMM_CAPABLE)
#define IS_DATA_ROLE_SWAP(x)	(x & SNK_FSPDO_DATA_ROLE_SWAP)

/* returns in mV */
#define DATA_OBJ_TO_VOLT(x)	(((x & SNK_FSPDO_VOLTAGE) >>	\
					SNK_FSPDO_VOLT_SHIFT) * 50)
/* returns in mA */
#define DATA_OBJ_TO_CURRENT(x)	((x & SNK_FSPDO_MAX_CURRENT) * 10)

#define VOLT_TO_DATA_OBJ(x)	(((x / 50) << SNK_FSPDO_VOLT_SHIFT) &	\
					SNK_FSPDO_VOLTAGE)
#define CURRENT_TO_DATA_OBJ(x)	((x / 10) & SNK_FSPDO_MAX_CURRENT)

#define REQ_DOBJ_OBJ_POS_SHIFT		28
#define REQ_DOBJ_GB_FLAG_SHIFT		27
#define REQ_DOBJ_CAP_MISMATCH_SHIFT	26
#define REQ_DOBJ_USB_COMM_CAPABLE_SHIFT	25
#define REQ_DOBJ_NO_USB_SUSPEND_SHIFT	24
#define REQ_DOBJ_OPERATING_CUR_SHIFT	10
#define REQ_DOBJ_MAX_OP_CUR_SHIFT	0

#define REQ_DOBJ_OBJ_POSITION		(7 << REQ_DOBJ_OBJ_POS_SHIFT)
#define REQ_DOBJ_GIVEBACK_FLAG		(1 << REQ_DOBJ_GB_FLAG_SHIFT)
#define REQ_DOBJ_CAP_MISMATCH		(1 << REQ_DOBJ_CAP_MISMATCH_SHIFT)
#define REQ_DOBJ_USB_COMM_CAPABLE	(1 << REQ_DOBJ_USB_COMM_CAPABLE_SHIFT)
#define REQ_DOBJ_NO_USB_SUSPEND		(1 << REQ_DOBJ_NO_USB_SUSPEND_SHIFT)
#define REQ_DOBJ_OPERATING_CUR		(0x3FF << REQ_DOBJ_OPERATING_CUR_SHIFT)
#define REQ_DOBJ_MAX_OPERATING_CUR	(0x3FF << REQ_DOBJ_MAX_OP_CUR_SHIFT)

#define TYPEC_SENDER_RESPONSE_TIMER	30 /* min: 24mSec; max: 30mSec */
#define TYPEC_SINK_WAIT_CAP_TIMER	620 /* min 310mSec; max: 620mSec */
#define TYPEC_NO_RESPONSE_TIMER		5500 /* min 4.5Sec; max: 5.5Sec */
#define TYPEC_PS_TRANSITION_TIMER	550 /* min 450mSec; max: 550mSec */
#define TYPEC_SINK_ACTIVITY_TIMER	150 /* min 120mSec; max: 150mSec */
#define TYPEC_SINK_REQUEST_TIMER	100 /* min 100mSec; max: ? */
#define TYPEC_PS_SRC_OFF_TIMER		920 /* min 750mSec; max: 920mSec */
#define TYPEC_HARD_RESET_TIMER		5 /* max: 5mSec */
#define TYPEC_HARD_RESET_COMPLETE_TIMER	5 /* max: 5mSec */
#define HARD_RESET_COUNT_N		2

enum {
	CHRGR_UNKNOWN,
	CHRGR_SET_HZ,
	CHRGR_ENABLE,
};

struct snk_cable_event {
	struct list_head node;
	bool vbus_state;
};

enum snkpe_timeout {
	UNKNOWN_TIMER,
	SINK_WAIT_CAP_TIMER,
	SINK_ACTIVITY_TIMER,
	SINK_REQUEST_TIMER,
	PS_TRANSITION_TIMER,
	NO_RESPONSE_TIMER,
	SENDER_RESPONSE_TIMER,
	PS_SRC_OFF_TIMER,
};

struct req_cap {
	u8 obj_pos;
	u32 op_ma;
	u32 max_ma;
	u32 mv;
	bool cap_mismatch;
};

struct sink_port_pe {
	struct policy p;
	struct pd_packet prev_pkt;
	struct completion wct_complete; /* wait cap timer */
	struct completion srt_complete; /* sender response timer */
	struct completion pstt_complete; /* PS Transition timer */
	struct completion sat_complete; /* Sink Activity timer */
	struct completion pssoff_complete; /* PS Source Off timer */
	struct mutex snkpe_state_lock;
	struct timer_list no_response_timer;
	struct timer_list snk_request_timer;
	struct work_struct timer_work; /* sink pe worker thread */
	struct work_struct request_timer; /* snk request timer on ready state */
	struct req_cap rcap;
	wait_queue_head_t wq;
	wait_queue_head_t wq_req;
	enum pe_event last_pkt;
	enum pe_states cur_state;
	enum snkpe_timeout timeout;
	u8 hard_reset_count;
	unsigned resend_cap:1;
	unsigned hard_reset_complete:1;
	unsigned is_sink_cable_connected:1;
	unsigned request_timer_expired:1;
	unsigned no_response_timer_expired:1;

	/* Port partner caps */
	unsigned pp_is_dual_prole:1;
	unsigned pp_is_dual_drole:1;
	unsigned pp_is_ext_pwrd:1;
};

#endif /* __SINK_PORT_PE__H__ */
