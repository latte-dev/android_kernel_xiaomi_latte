#ifndef __PD_POLICY_H__
#define __PD_POLICY_H__

#include "devpolicy_mgr.h"
#include "message.h"

enum pe_event {

	/* Control Messages (0 - 13) */
	PE_EVT_SEND_NONE,
	PE_EVT_SEND_GOODCRC,
	PE_EVT_SEND_GOTOMIN,
	PE_EVT_SEND_ACCEPT,
	PE_EVT_SEND_REJECT,
	PE_EVT_SEND_PING,
	PE_EVT_SEND_PS_RDY,
	PE_EVT_SEND_GET_SRC_CAP,
	PE_EVT_SEND_GET_SINK_CAP,
	PE_EVT_SEND_DR_SWAP,
	PE_EVT_SEND_PR_SWAP,
	PE_EVT_SEND_VCONN_SWAP,
	PE_EVT_SEND_WAIT,
	PE_EVT_SEND_SOFT_RESET,

	/* Data Messages (14 - 18) */
	PE_EVT_SEND_SRC_CAP,
	PE_EVT_SEND_REQUEST,
	PE_EVT_SEND_BIST,
	PE_EVT_SEND_SNK_CAP,
	PE_EVT_SEND_VDM,

	/* Control Messages (19 - 32) */
	PE_EVT_RCVD_NONE,
	PE_EVT_RCVD_GOODCRC,
	PE_EVT_RCVD_GOTOMIN,
	PE_EVT_RCVD_ACCEPT,
	PE_EVT_RCVD_REJECT,
	PE_EVT_RCVD_PING,
	PE_EVT_RCVD_PS_RDY,
	PE_EVT_RCVD_GET_SRC_CAP,
	PE_EVT_RCVD_GET_SINK_CAP,
	PE_EVT_RCVD_DR_SWAP,
	PE_EVT_RCVD_PR_SWAP,
	PE_EVT_RCVD_VCONN_SWAP,
	PE_EVT_RCVD_WAIT,
	PE_EVT_RCVD_SOFT_RESET,

	/* Data Messages (33 - 37) */
	PE_EVT_RCVD_SRC_CAP,
	PE_EVT_RCVD_REQUEST,
	PE_EVT_RCVD_BIST,
	PE_EVT_RCVD_SNK_CAP,
	PE_EVT_RCVD_VDM,

	/* Other Messages (38 - 41) */
	PE_EVT_SEND_HARD_RESET,
	PE_EVT_SEND_PROTOCOL_RESET,
	PE_EVT_RCVD_HARD_RESET,
	PE_EVT_RCVD_HARD_RESET_COMPLETE,

};

struct pe_operations {
	/* Callback fn to get the current data role of PE */
	enum data_role (*get_data_role)(struct policy *p);
	/* Callback fn to get the current power role of PE */
	enum pwr_role (*get_power_role)(struct policy *p);

	/* Callback functions to receive msgs and cmds from protocol */
	int (*process_data_msg)(struct policy *p, enum pe_event evt,
				struct pd_packet *data);
	int (*process_ctrl_msg)(struct policy *p, enum pe_event evt,
				struct pd_packet *data);
	int (*process_cmd)(struct policy *p, enum pe_event cmd);

	/* Callback fn to receive DPM event */
	int (*notify_dpm_evt)(struct policy *p,
					enum devpolicy_mgr_events evt);
};

struct policy {
	struct pd_prot *prot;
	struct devpolicy_mgr *dpm;
	struct pe_operations *ops;
};

#define pe_get_phy(x)	((x) ?  x->dpm->phy : NULL)

static inline int pe_process_cmd(struct policy *p, enum pe_event cmd)
{
	if (p && p->ops && p->ops->process_cmd)
		return p->ops->process_cmd(p, cmd);

	return -ENOTSUPP;
}

static inline int pe_process_data_msg(struct policy *p,
					enum pe_event evt,
					struct pd_packet *pkt)
{
	if (p && p->ops && p->ops->process_data_msg)
		return p->ops->process_data_msg(p, evt, pkt);

	return -ENOTSUPP;
}

static inline int pe_process_ctrl_msg(struct policy *p,
					enum pe_event evt,
					struct pd_packet *pkt)
{
	if (p && p->ops && p->ops->process_ctrl_msg)
		return p->ops->process_ctrl_msg(p, evt, pkt);

	return -ENOTSUPP;
}

static inline int pe_notify_dpm_evt(struct policy *p,
					enum devpolicy_mgr_events evt)
{
	if (p && p->ops && p->ops->notify_dpm_evt)
		return p->ops->notify_dpm_evt(p, evt);

	return -ENOTSUPP;
}

static inline int policy_engine_bind_dpm(struct devpolicy_mgr *dpm)
{
	return 0;
}
static inline void policy_engine_unbind_dpm(struct devpolicy_mgr *dpm)
{ }
#endif /* __PD_POLICY_H__ */
