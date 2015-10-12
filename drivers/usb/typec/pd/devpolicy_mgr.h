#ifndef __PD_DEVMGR_POLICY_H__
#define __PD_DEVMGR_POLICY_H__

#include <linux/extcon.h>
#include <linux/usb_typec_phy.h>

#define CABLE_CONSUMER	"USB_TYPEC_SNK"
#define CABLE_PROVIDER	"USB_TYPEC_SRC"

enum cable_state {
	CABLE_DETACHED,
	CABLE_ATTACHED,
};

enum cable_type {
	CABLE_TYPE_UNKNOWN,
	CABLE_TYPE_CONSUMER,
	CABLE_TYPE_PROVIDER,
	CABLE_TYPE_DP_SOURCE,
	CABLE_TYPE_DP_SINK,
};

enum charger_mode {
	CHARGER_MODE_UNKNOWN,
	CHARGER_MODE_SET_HZ,
	CHARGER_MODE_ENABLE,
};

enum psy_type {
	PSY_TYPE_UNKNOWN,
	PSY_TYPE_BATTERY,
	PSY_TYPE_CHARGER,
};

enum batt_soc_status {
	BATT_SOC_UNKNOWN = -1,
	BATT_SOC_DEAD,	/* soc = 0		*/
	BATT_SOC_LOW,	/* soc > 0 && < 25	*/
	BATT_SOC_MID1,	/* soc >= 25 && < 50	*/
	BATT_SOC_MID2,	/* soc >= 50 && < 80	*/
	BATT_SOC_GOOD,	/* soc >= 80 && < 100	*/
	BATT_SOC_FULL,	/* soc = 100		*/
};

#define BATT_CAP_FULL		100
#define BATT_CAP_GOOD		80
#define BATT_CAP_MID		50
#define BATT_CAP_LOW		25
#define BATT_CAP_DEAD		0

#define IS_BATT_SOC_FULL(x)	((x) == BATT_CAP_FULL)
#define IS_BATT_SOC_GOOD(x)	((x) >= BATT_CAP_GOOD && (x) < BATT_CAP_FULL)
#define IS_BATT_SOC_MID2(x)	((x) >= BATT_CAP_MID && (x) < BATT_CAP_GOOD)
#define IS_BATT_SOC_MID1(x)	((x) >= BATT_CAP_LOW && (x) < BATT_CAP_MID)
#define IS_BATT_SOC_LOW(x)	((x) > BATT_CAP_DEAD && (x) < BATT_CAP_LOW)
#define IS_BATT_SOC_DEAD(x)	((x) == BATT_CAP_DEAD)

#define IS_BATTERY(psy) (psy->type == POWER_SUPPLY_TYPE_BATTERY)
#define IS_CHARGER(psy) (psy->type == POWER_SUPPLY_TYPE_USB ||\
			psy->type == POWER_SUPPLY_TYPE_USB_CDP || \
			psy->type == POWER_SUPPLY_TYPE_USB_DCP || \
			psy->type == POWER_SUPPLY_TYPE_USB_ACA || \
			psy->type == POWER_SUPPLY_TYPE_USB_TYPEC)

/* host mode: max of 5V, 1A */
#define VBUS_5V		5000
#define IBUS_1A		1000
#define IBUS_0P9A	900
#define IBUS_0P5A	500

/* device mode: max of 12, 3A */
#define VIN_12V		12000
#define VIN_9V		9000
#define VIN_5V		5000

#define ICHRG_3A	3000
#define ICHRG_2A	2000
#define ICHRG_1P5A	1500
#define ICHRG_1A	1000
#define ICHRG_P5A	500

#define DPM_PSY_TYPE_FIXED	0
#define DPM_PSY_TYPE_VARIABLE	2
#define DPM_PSY_TYPE_BATTERY	1

enum devpolicy_mgr_events {
	DEVMGR_EVENT_NONE,
	DEVMGR_EVENT_DFP_CONNECTED,
	DEVMGR_EVENT_DFP_DISCONNECTED,
	DEVMGR_EVENT_UFP_CONNECTED,
	DEVMGR_EVENT_UFP_DISCONNECTED,
	DEVMGR_EVENT_PR_SWAP,
	DEVMGR_EVENT_DR_SWAP,
};

enum policy_type {
	POLICY_TYPE_UNDEFINED,
	POLICY_TYPE_SOURCE,
	POLICY_TYPE_SINK,
	POLICY_TYPE_DISPLAY,
};

enum role_type {
	ROLE_TYPE_DATA,
	ROLE_TYPE_POWER,
};

enum pwr_role {
	POWER_ROLE_NONE,
	POWER_ROLE_SINK,
	POWER_ROLE_SOURCE,
	/* Power role swap in-progress */
	POWER_ROLE_SWAP,
};

enum data_role {
	DATA_ROLE_NONE,
	DATA_ROLE_UFP,
	DATA_ROLE_DFP,
	/* Data role swap in-progress */
	DATA_ROLE_SWAP,
};

struct power_cap {
	int mv;
	int ma;
	int psy_type;
};

struct power_caps {
	struct power_cap *pcap;
	int n_cap;
};

struct cable_event {
	struct list_head node;
	enum cable_type cbl_type;
	enum cable_state cbl_state;
};

struct pd_policy {
	enum policy_type *policies;
	size_t num_policies;
};

struct devpolicy_mgr {
	struct pd_policy *policy;
	struct extcon_specific_cable_nb provider_cable_nb;
	struct extcon_specific_cable_nb consumer_cable_nb;
	struct typec_phy *phy;
	struct notifier_block provider_nb;
	struct notifier_block consumer_nb;
	struct list_head cable_event_queue;
	struct work_struct cable_event_work;
	struct mutex role_lock;
	struct mutex charger_lock;
	struct dpm_interface *interface;
	spinlock_t cable_event_queue_lock;
	enum cable_state consumer_state;    /* cosumer cable state */
	enum cable_state provider_state;    /* provider cable state */
	enum cable_state dp_state;    /* display cable state */
	enum pwr_role cur_prole;
	enum pwr_role prev_prole;
	enum data_role cur_drole;
	enum data_role prev_drole;
	struct policy_engine *pe;
	/* power delivery class device*/
	struct device *pd_dev;
	struct work_struct cable_notify_work;
	struct mutex cable_notify_lock;
	struct list_head cable_notify_list;
	struct power_supply *charger_psy;
	struct power_supply *battery_psy;
};

struct dpm_interface {
	int (*get_max_srcpwr_cap)(struct devpolicy_mgr *dpm,
					struct power_cap *cap);
	int (*get_max_snkpwr_cap)(struct devpolicy_mgr *dpm,
					struct power_cap *cap);
	int (*get_source_power_cap)(struct devpolicy_mgr *dpm,
					struct power_cap *cap);
	int (*get_sink_power_cap)(struct devpolicy_mgr *dpm,
					struct power_cap *cap);
	int (*get_sink_power_caps)(struct devpolicy_mgr *dpm,
					struct power_caps *caps);

	/* methods to get/set the sink/source port states */
	enum cable_state (*get_cable_state)(struct devpolicy_mgr *dpm,
						enum cable_type type);
	/* Policy engine to update the current data and pwr roles*/
	void (*update_data_role)(struct devpolicy_mgr *dpm,
					enum data_role drole);
	void (*update_power_role)(struct devpolicy_mgr *dpm,
					enum pwr_role prole);
	int (*set_charger_mode)(struct devpolicy_mgr *dpm,
					enum charger_mode mode);
	int (*update_charger)(struct devpolicy_mgr *dpm,
					int ilim, int query);
	int (*get_min_current)(struct devpolicy_mgr *dpm,
					int *ma);
	int (*is_pr_swapped)(struct devpolicy_mgr *dpm,
					enum pwr_role prole);
	int (*set_display_port_state)(struct devpolicy_mgr *dpm,
					enum cable_state state,
					enum typec_dp_cable_type type);
	bool (*get_vbus_state)(struct devpolicy_mgr *dpm);
};

static inline int devpolicy_get_max_srcpwr_cap(struct devpolicy_mgr *dpm,
					struct power_cap *caps)
{
	if (dpm && dpm->interface && dpm->interface->get_max_srcpwr_cap)
		return dpm->interface->get_max_srcpwr_cap(dpm, caps);

	return -ENODEV;
}

static inline int devpolicy_get_max_snkpwr_cap(struct devpolicy_mgr *dpm,
					struct power_cap *caps)
{
	if (dpm && dpm->interface && dpm->interface->get_max_snkpwr_cap)
		return dpm->interface->get_max_snkpwr_cap(dpm, caps);

	return -ENODEV;
}

static inline int devpolicy_get_srcpwr_cap(struct devpolicy_mgr *dpm,
					struct power_cap *cap)
{
	if (dpm && dpm->interface && dpm->interface->get_source_power_cap)
		return dpm->interface->get_source_power_cap(dpm, cap);
	else
		return -ENODEV;
}

static inline int devpolicy_get_snkpwr_cap(struct devpolicy_mgr *dpm,
					struct power_cap *cap)
{
	if (dpm && dpm->interface && dpm->interface->get_sink_power_cap)
		return dpm->interface->get_sink_power_cap(dpm, cap);

	return -ENODEV;
}

static inline int devpolicy_get_snkpwr_caps(struct devpolicy_mgr *dpm,
					struct power_caps *caps)
{
	if (dpm && dpm->interface && dpm->interface->get_sink_power_caps)
		return dpm->interface->get_sink_power_caps(dpm, caps);

	return -ENODEV;
}

static inline int devpolicy_set_charger_mode(struct devpolicy_mgr *dpm,
					enum charger_mode mode)
{
	if (dpm && dpm->interface && dpm->interface->set_charger_mode)
		return dpm->interface->set_charger_mode(dpm, mode);

	return -ENODEV;
}

static inline int devpolicy_update_charger(struct devpolicy_mgr *dpm,
							int ilim, int query)
{
	if (dpm && dpm->interface && dpm->interface->update_charger)
		return dpm->interface->update_charger(dpm, ilim, query);

	return -ENODEV;
}

static inline int devpolicy_get_min_snk_current(struct devpolicy_mgr *dpm,
							int *ma)
{
	if (dpm && dpm->interface && dpm->interface->get_min_current)
		return dpm->interface->get_min_current(dpm, ma);

	return -ENODEV;
}

static inline int devpolicy_is_pr_swap_support(struct devpolicy_mgr *dpm,
							enum pwr_role prole)
{
	if (dpm && dpm->interface && dpm->interface->is_pr_swapped)
		return dpm->interface->is_pr_swapped(dpm, prole);

	return -ENODEV;
}

static inline enum cable_state devpolicy_get_cable_state(
					struct devpolicy_mgr *dpm,
					enum cable_type type)
{
	if (dpm && dpm->interface && dpm->interface->get_cable_state)
		return dpm->interface->get_cable_state(dpm, type);

	return -ENODEV;
}

void typec_notify_cable_state(struct typec_phy *phy, char *type, bool state);
void typec_set_pu_pd(struct typec_phy *phy, bool pu_pd);

/* methods to register/unregister device manager policy notifier */
extern int devpolicy_mgr_reg_notifier(struct notifier_block *nb);
extern void devpolicy_mgr_unreg_notifier(struct notifier_block *nb);

/* power_delivery class reference */
extern struct class *power_delivery_class;

#if defined(CONFIG_USBC_PD) && defined(CONFIG_USBC_PD_POLICY)
extern struct devpolicy_mgr *dpm_register_syspolicy(struct typec_phy *phy,
				struct pd_policy *policy);
extern void dpm_unregister_syspolicy(struct devpolicy_mgr *dpm);
#else /* CONFIG_USBC_PD && CONFIG_USBC_PD_POLICY */
static inline
struct devpolicy_mgr *dpm_register_syspolicy(struct typec_phy *phy,
				struct pd_policy *policy)
{
	return ERR_PTR(-ENOTSUPP);
}
static inline void dpm_unregister_syspolicy(struct devpolicy_mgr *dpm)
{ }
#endif /* CONFIG_USBC_PD && CONFIG_USBC_PD_POLICY */

#endif /* __PD_DEVMGR_POLICY_H__ */
