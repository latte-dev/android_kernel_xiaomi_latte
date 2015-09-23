/*
 * Intel Shared Transport Driver
 * Copyright (C) 2013 -2014, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/lbf_ldisc.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/irq.h>
#include <linux/poll.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/jiffies.h>
#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/netlink.h>
#include <net/sock.h>
#ifdef CONFIG_SERIAL_HSU
#include <linux/serial_hsu.h>
#endif

#define CREATE_TRACE_POINTS
#include "lnp_ldisc_trace.h"

#define WAKEUP_HOST "wakeup-host"
#ifdef CONFIG_SERIAL_XGOLD
#define PROP_LNP_BT_GPIO_ENB "intel,bt-fmr-gpio-reset"
#endif
#ifdef CONFIG_ACPI
struct intel_bt_gpio_data {
	struct gpio_desc	*reg_on_gpiod;
	int	host_wake_irq;
};

struct intel_bt_gpio_desc {
	int			reg_on_idx;
	int			host_wake_idx;
};
static struct intel_bt_gpio_desc acpi_default_bluetooth = {
	.reg_on_idx = 1,
	.host_wake_idx = 0,
};

static struct acpi_device_id intel_id_table[] = {
	/* ACPI IDs here */
	{ "INT33E1", (kernel_ulong_t)&acpi_default_bluetooth },
	{ },
};

MODULE_DEVICE_TABLE(acpi, intel_id_table);
#endif

static DECLARE_WAIT_QUEUE_HEAD(waitFordxexit);
static DECLARE_WAIT_QUEUE_HEAD(waitFord0exit);
static DECLARE_WAIT_QUEUE_HEAD(waitFord2exit);

struct intel_bt_lpm {
	bool interrupt;
	unsigned int int_host_wake;
	unsigned int bt_module_state;
	unsigned int fm_module_state;
	unsigned int bt_fmr_state;
	unsigned int module_refcount;
	unsigned char hostwake;
	unsigned int platform_state;
	int dx_packet_pending;
	int d0_packet_pending;
#ifdef CONFIG_ACPI
	struct gpio_desc *gpiod_enable_bt;
#endif
	int gpio_host_wake;
#ifdef CONFIG_SERIAL_XGOLD
	int gpio_enable_bt;
#endif
	int host_wake;
	int lpm_enable;
	struct mutex lpmtxlock;
	struct mutex idleupdatelock;
	struct mutex lpmenable;
	struct platform_device *pdev;
	struct device *tty_dev;
	spinlock_t rxref_lock;
	spinlock_t interrupt_lock;
	spinlock_t txref_lock;
	spinlock_t devicest_lock;
	spinlock_t ackrsp_lock;
	spinlock_t wakeuprsp_lock;
	spinlock_t host_wake_lock;
	spinlock_t lpm_modulestate;
	struct work_struct hostwake_update_work;
	struct work_struct host_enable_work;
	int tx_refcount;
	int device_state;
	int rx_refcount;
	int port;
};
static struct intel_bt_lpm intel_lbf_lpm;


struct lbf_uart {

	unsigned long rx_state;
	unsigned long rx_count;
	char *read_buf;
	int read_head;
	int read_cnt;
	int read_tail;
	wait_queue_t wait;
	wait_queue_head_t read_wait;
	struct mutex atomic_read_lock;
	spinlock_t tx_lock;
	spinlock_t tx_update_lock;
	spinlock_t hw_reset_lock;
	struct tty_struct *tty;
	spinlock_t rx_lock;
	struct sk_buff *rx_skb;
	unsigned short rx_chnl;
	unsigned short type;
	unsigned short bytes_pending;
	unsigned short len;
	struct sock *nl_socket;
	struct hrtimer hci_reset_timer;
	ktime_t hci_reset_delay;
	unsigned long hci_reset;
	unsigned long hci_cmd_st;

};

struct lbf_q_tx {
	struct sk_buff_head txq, tx_waitq;
	struct sk_buff *tx_skb;
	spinlock_t lock;
	struct mutex writelock;
	struct mutex fwdownloadlock;
	struct work_struct tx_wakeup_work;
	struct mutex tx_wakeup;
	int tbusy;
	int woke_up;
	struct tty_struct *tty;
};

struct st_proto_s {
	unsigned char chnl_id;
	unsigned char hdr_len;
	unsigned char offset_len_in_hdr;
	unsigned char len_size;
	unsigned char reserve;
};

enum {
	MODULE_INVALID,
	MODULE_FM,
	MODULE_BT,
};

enum ioctl_status {
	FW_BLANK,
	DO_FW_DL,
	DO_STACK_INIT,
	FW_FAILED,
	FW_SUCCESS
};

enum bt_hci_nl_messages {
	/*FMR PID Notification, From FMR stack -> STD */
	BT_DRIVER_REGISTER,
	/* HCI_Reset Notification, From STD -> FMR stack */
	 BT_HCI_RESET_REQUEST,
	/* HCI_Reset Acknowledgment, From FMR stack -> STD */
	BT_HCI_RESET_RESPONSE,
	/* HCI Reset up complete event, From STD -> FMR stack */
	BT_HCI_RESET_COMPLETE_NOTIFY,
	/* Send Hardware err in case of hardware error & BT command timeout*/
	BT_HCI_HW_ERR_REQUEST,
	/* FOCRE_INIT notification, From FMR stack -> STD*/
	BT_DEV_HARD_RESET_HW_ERR_RSP,
	BT_DEV_HARD_RESET_CMD_TO,
	/* FOCRE_INIT notification, From FMR stack -> STD
				for FMR hci command timeout*/
};



#define DX_HCI_CMD_SIZE		4
#define D0_HCI_CMD_SIZE		1
#define HCI_RESET_CMD_SIZE	4
#define WAKEUP_RSP_SIZE		3
#define RECEIVE_ROOM		4096
#define IGNORE			-3
#define FAILED			4

#define HOSTWAKE_HIGH		BIT(0)  /* 2^0, bit 0 */
#define HOSTWAKE_LOW		BIT(1)  /* 2^1, bit 1 */
#define HOSTWAKE_D0		BIT(2)  /* 2^2, bit 2 */
#define HOSTWAKE_D0_TO_D2	BIT(3)  /* 2^3, bit 3 */
#define HOSTWAKE_D2_TO_D0	BIT(4) /* 2^4, bit 4 */
#define HOSTWAKE_D2		BIT(5) /* and so on...*/



/*------ Forward Declaration-----*/
struct sk_buff *lbf_dequeue(void);
static int lbf_tty_write(struct tty_struct *tty, const unsigned char *data,
				int count);
static void lbf_enqueue(struct sk_buff *skb);
static void bt_rfkill_set_power(unsigned long gpio_st);
static void lbf_update_set_room(struct tty_struct *tty, signed int cnt);
static void lbf_ldisc_fw_download_complete(unsigned long);
static void lbf_ldisc_receive(struct tty_struct *tty, const u8 *cp, char *fp,
		int count);
static int activate_irq_handler(void);
static int lbf_ldisc_fw_download_init(void);
static int lpm_tx_update(int module);
static int enqueue_dx_packet(uint8_t *hci_cmd, int size);
#ifdef CONFIG_ACPI
static int intel_bt_lpm_init(struct platform_device *pdev);
#endif
static void lbf_tx_wakeup(struct lbf_uart *lbf_uart);
static int wait_dx_exit(void);
static void lbf_ldisc_release_serial(void);
static int send_d0_dx_packet(void);

static struct st_proto_s lbf_st_proto[6] = {
		{ .chnl_id = PM_PKT, /* PM_PACKET */
		.hdr_len = HCI_EVENT_HDR_SIZE, .offset_len_in_hdr = 2,
				.len_size = 1, .reserve = 8, },
		{ .chnl_id = HCI_COMMAND_PKT, /* HCI COMMANDS */
		.hdr_len = HCI_COMMAND_HDR_SIZE, .offset_len_in_hdr = 3,
				.len_size = 1, .reserve = 8, },
		{ .chnl_id = HCI_ACLDATA_PKT, /* ACL */
		.hdr_len = HCI_ACL_HDR_SIZE, .offset_len_in_hdr = 3,
				.len_size = 2, .reserve = 8, },
		{ .chnl_id = HCI_SCODATA_PKT, /* SCO */
		.hdr_len = HCI_SCO_HDR_SIZE, .offset_len_in_hdr = 3,
				.len_size = 1, .reserve = 8, },
		{ .chnl_id = HCI_EVENT_PKT, /* HCI Events */
		.hdr_len = HCI_EVENT_HDR_SIZE, .offset_len_in_hdr = 2,
				.len_size = 1, .reserve = 8, },
		{ .chnl_id = HCI_EVENT_PKT, /* HCI Events */
		.hdr_len = HCI_EVENT_HDR_SIZE, .offset_len_in_hdr = 2,
				.len_size = 1, .reserve = 8, }, };

/* Static variable */

static int fw_download_state;
static unsigned int lbf_ldisc_running = INVALID;
static int bt_enable_state = DISABLE;
static struct lbf_q_tx *lbf_tx;
static struct fm_ld_drv_register *fm;
static unsigned stack_pid;

/*Dx  Entry Request  form host*/

/*Wake Enabled*/
static uint8_t hci_dx_request[] = {0xF1, 0x01, 0x01, 0x01};
/*Wake Enabled response*/
static uint8_t hci_controlerwake_req_rsp[] = {0xF1, 0x03, 0x00};
/*Wakeup BT packet - HIGH*/
static uint8_t hci_dx_wake[] = {0xF0};
/*BT HCI Reset*/
static uint8_t hci_reset[] = {0x01, 0x03, 0x0c, 0x00};
static uint8_t hci_hw_err[] = {0x04, 0x10, 0x01, 0x01};
static unsigned int ignore_ack;
static unsigned int ignore_wakeuprsp;
static unsigned int host_wake_intr_count;
static unsigned int bt_self_test_running;


static enum hrtimer_restart lbf_hci_reset_expire(struct hrtimer *timer)
{
	struct lbf_uart *lbf_uart;

	trace_lbf_ldisc_reset(__func__, "LBF_LDISC Timer expired");
	lbf_uart = (struct lbf_uart *) lbf_tx->tty->disc_data;
	/* Dont send HCI Reset cmd if already sent
		out on receipt of BT_HCI_RESET_RESPONSE */
	if (!test_and_set_bit(HCI_RESET_SEND, &lbf_uart->hci_reset) &&
		!test_bit(HCI_RESET_COMPL_RCV, &lbf_uart->hci_reset))
		lbf_tty_write((void *) lbf_tx->tty,
			hci_reset, HCI_RESET_CMD_SIZE);
	return HRTIMER_NORESTART;
}

/* lbf_set_ackign_st
 *
 * set the ACK RSP processing state.
 *
 * Arguments:
 * state to set
 *
 * Return Value:
 * void
 */
static inline void lbf_set_ackign_st(unsigned int state)
{
	spin_lock(&intel_lbf_lpm.ackrsp_lock);
	ignore_ack = state;
	spin_unlock(&intel_lbf_lpm.ackrsp_lock);
}

/* lbf_get_ackigne_st
 *
 * get the ACK RSP processing state.
 *
 * Arguments:
 * void
 *
 * Return Value:
 * state of ackrsp processing
 */
static inline unsigned int lbf_get_ackigne_st(void)
{
	unsigned int state;

	spin_lock(&intel_lbf_lpm.ackrsp_lock);
	state = ignore_ack;
	spin_unlock(&intel_lbf_lpm.ackrsp_lock);
	return state;
}

/* lbf_set_wakeupign_st
 *
 * set the WAKEUP RSP processing state.
 *
 * Arguments:
 * state to set
 *
 * Return Value:
 * void
 */
static inline void lbf_set_wakeupign_st(unsigned int state)
{
	spin_lock(&intel_lbf_lpm.wakeuprsp_lock);
	ignore_wakeuprsp = state;
	spin_unlock(&intel_lbf_lpm.wakeuprsp_lock);
}

/* lbf_get_wakeupign_st
 *
 * get the wake_up RSP processing state.
 *
 * Arguments:
 * void
 *
 * Return Value:
 * state of wakeup rsp
 */
static inline unsigned int lbf_get_wakeupign_st(void)
{
	unsigned int state;

	spin_lock(&intel_lbf_lpm.wakeuprsp_lock);
	state = ignore_wakeuprsp;
	spin_unlock(&intel_lbf_lpm.wakeuprsp_lock);
	return state;
}

/* lbf_set_device_state
 *
 * set the device state.
 *
 * Arguments:
 * state to set
 *
 * Return Value:
 * void
 */

static inline void lbf_set_device_state(unsigned int state)
{
	spin_lock(&intel_lbf_lpm.devicest_lock);
	intel_lbf_lpm.device_state = state;
	spin_unlock(&intel_lbf_lpm.devicest_lock);
	trace_lbf_device_st(__func__, intel_lbf_lpm.device_state);
}

/* lbf_get_device_state
 *
 * get the device state.
 *
 * Arguments:
 * void
 *
 * Return Value:
 *	device state
 *
 */

static inline unsigned int lbf_get_device_state(void)
{
	unsigned int device_st;

	spin_lock(&intel_lbf_lpm.devicest_lock);
	device_st = intel_lbf_lpm.device_state;
	spin_unlock(&intel_lbf_lpm.devicest_lock);
	trace_lbf_device_st(__func__, device_st);
	return device_st;
}

/* lbf_set_bt_fmr_state
 *
 * set the bt fmr state.
 *
 * Arguments:
 * void
 *
 * Return Value:
 * void
 */

static inline void lbf_set_bt_fmr_state(void)
{
	spin_lock(&intel_lbf_lpm.lpm_modulestate);
	if (!intel_lbf_lpm.bt_module_state && !intel_lbf_lpm.fm_module_state)
		intel_lbf_lpm.bt_fmr_state = IDLE;
	else
		intel_lbf_lpm.bt_fmr_state = ACTIVE;
	spin_unlock(&intel_lbf_lpm.lpm_modulestate);
	trace_lbf_bt_fmr_st(__func__, intel_lbf_lpm.bt_fmr_state);
}

/* lbf_get_bt_fmr_state
 *
 * get the bt and fmr  state.
 *
 * Arguments:
 * void
 *
 * Return Value:
 * module state
 */

static inline int lbf_get_bt_fmr_state(void)
{
	int bt_fmr_state;

	spin_lock(&intel_lbf_lpm.lpm_modulestate);
	bt_fmr_state = intel_lbf_lpm.bt_fmr_state;
	spin_unlock(&intel_lbf_lpm.lpm_modulestate);
	trace_lbf_bt_fmr_st(__func__, bt_fmr_state);
	return bt_fmr_state;
}

/* lbf_serial_get
 *
 * Acquire the serial driver handle to allow Tx & Rx
 *
 * Arguments:
 * tty_dev
 *
 * Return Value:
 * void
 */
static inline void lbf_serial_get(void)
{
	trace_lbf_func_start(__func__);
	if (intel_lbf_lpm.tty_dev)
		pm_runtime_get_sync(intel_lbf_lpm.tty_dev);
	else
		pr_err("%s: no tty device\n", __func__);
}

static void lbf_host_enable_work(struct work_struct *work)
{
	trace_lbf_func_start(__func__);
	lbf_serial_get();
}

/* lbf_serial_put
 *
 * release the serial driver handle to allow to go into Low power state
 *
 * Arguments:
 * tty_dev
 *
 * Return Value:
 * void
 */

static inline void lbf_serial_put(void)
{
	trace_lbf_func_start(__func__);
	if (intel_lbf_lpm.tty_dev) {
#ifdef CONFIG_SERIAL_XGOLD
		pm_runtime_mark_last_busy(intel_lbf_lpm.tty_dev);
		pm_runtime_put_autosuspend(intel_lbf_lpm.tty_dev);
#endif
#if defined(CONFIG_SERIAL_HSU) || defined(CONFIG_SERIAL_8250)
		pm_runtime_put(intel_lbf_lpm.tty_dev);
#endif
	}
}

/* lbf_set_host_wake_state
 *
 * set the host wake state.
 *
 * Arguments:
 * host_wake state
 *
 * Return Value:
 * void
 */

static inline void lbf_set_host_wake_state(int host_wake)
{
	spin_lock(&intel_lbf_lpm.host_wake_lock);
	intel_lbf_lpm.host_wake = host_wake;
	spin_unlock(&intel_lbf_lpm.host_wake_lock);
	trace_lbf_hostwake(__func__, intel_lbf_lpm.host_wake);
}

/* lbf_get_tx_ref_count
 *
 * get the tx reference count.
 *
 * Arguments:
 * void
 *
 * Return Value:
 * tx reference count
 */

static inline int lbf_get_tx_ref_count(void)
{
	int ref_count;

	spin_lock(&intel_lbf_lpm.txref_lock);
	ref_count = intel_lbf_lpm.tx_refcount;
	spin_unlock(&intel_lbf_lpm.txref_lock);
	return ref_count;
}

/* lbf_get_rx_ref_count
 *
 * get the rx reference count.
 *
 * Arguments:
 * void
 *
 * Return Value:
 * rx reference count
 */
static inline int lbf_get_rx_ref_count(void)
{
	int ref_count;

	spin_lock(&intel_lbf_lpm.rxref_lock);
	ref_count = intel_lbf_lpm.rx_refcount;
	spin_unlock(&intel_lbf_lpm.rxref_lock);
	return ref_count;
}

/* lbf_set_tx_ref_count
 *
 * set the rx reference count.
 *
 * Arguments:
 * count
 *
 * Return Value:
 * void
 */

static inline void lbf_set_tx_ref_count(signed int count)
{
	spin_lock(&intel_lbf_lpm.txref_lock);
	intel_lbf_lpm.tx_refcount = intel_lbf_lpm.tx_refcount + count;
	spin_unlock(&intel_lbf_lpm.txref_lock);
}

/* lbf_set_rx_ref_count
 *
 * set the tx reference count.
 *
 * Arguments:
 * count
 *
 * Return Value:
 * void
 */

static inline void lbf_set_rx_ref_count(signed int count)
{
	spin_lock(&intel_lbf_lpm.rxref_lock);
	intel_lbf_lpm.rx_refcount = intel_lbf_lpm.rx_refcount + count;
	spin_unlock(&intel_lbf_lpm.rxref_lock);
}

/* lbf_get_host_wake_state
 *
 * get the host wake state.
 *
 * Arguments:
 * void
 *
 * Return Value:
 * host_wake state
 */

static inline int lbf_get_host_wake_state(void)
{
	int host_wake;

	spin_lock(&intel_lbf_lpm.host_wake_lock);
	host_wake = intel_lbf_lpm.host_wake;
	spin_unlock(&intel_lbf_lpm.host_wake_lock);
	trace_lbf_hostwake(__func__, host_wake);

	return host_wake;
}



static void hostwake_low_processing(void)
{
	trace_lbf_func_start(__func__);

	if (lbf_get_device_state() == D2_TO_D0)
		wait_dx_exit();
	if (lbf_get_device_state() == D0)
		lbf_tx_wakeup(NULL);

	trace_lbf_func_end(__func__);
}


static void hostwake_high_processing(void)
{
	lbf_tty_write((void *) lbf_tx->tty, hci_controlerwake_req_rsp,
		WAKEUP_RSP_SIZE);

	switch (lbf_get_device_state()) {
	case D0_TO_D2:
		lbf_set_ackign_st(true);
		intel_lbf_lpm.hostwake |= HOSTWAKE_D0_TO_D2;
		break;

	case D2_TO_D0:
		lbf_set_wakeupign_st(true);
		intel_lbf_lpm.hostwake |= HOSTWAKE_D2_TO_D0;
		break;
	default:
		intel_lbf_lpm.hostwake |= HOSTWAKE_D2;
		break;
	}

	lbf_set_device_state(D0);

	if (!lbf_get_tx_ref_count()) {
		lbf_set_tx_ref_count(1);
		pm_runtime_get(intel_lbf_lpm.tty_dev);
	}

	wake_up_interruptible(&waitFord2exit);
	wake_up_interruptible(&waitFord0exit);
	wake_up_interruptible(&waitFordxexit);
	trace_lbf_func_end(__func__);
}

static void lbf_hostwake_update_work(struct work_struct *work)
{
	hostwake_low_processing();
}

/* irqreturn_t lbf_ldisc_hostwake_bh
 *
 * Bottom half peforms awakening of the device,
 * staring of I/O operation
 *
 * Arguments:
 *
 * Return Value:
 * irqreturn_t
 */
static irqreturn_t lbf_ldisc_hostwake_bh(int irq, void *dev)
{
	int ret = 0;

	intel_lbf_lpm.hostwake = 0;

	trace_lbf_func_start(__func__);
    /*If LPM is disabled exit from here*/
	if (intel_lbf_lpm.lpm_enable == DISABLE)
		goto out;
	/*Platform is still in resuming state*/
	if (intel_lbf_lpm.platform_state == S3) {
		intel_lbf_lpm.interrupt =  true;
		goto out;
	}

	if (HOSTWAKE_HIGH == lbf_get_host_wake_state()
			&& lbf_get_device_state() == D0)
		pr_info("<-%s high hs_wk:%d\n", __func__,
			intel_lbf_lpm.hostwake);

	if (!lbf_get_rx_ref_count()) {
		lbf_set_rx_ref_count(1);
		if (intel_lbf_lpm.tty_dev)
			ret = pm_runtime_get_sync(intel_lbf_lpm.tty_dev);
			pr_debug("ret = %d\n", ret);
	}
	intel_lbf_lpm.hostwake |= HOSTWAKE_HIGH;
	hostwake_high_processing();
	intel_lbf_lpm.interrupt = false;

out:
	enable_irq(intel_lbf_lpm.int_host_wake);
	trace_lbf_irq_state(__func__, intel_lbf_lpm.hostwake);
	return IRQ_HANDLED;
}


/* lbf_update_host_wake
 *
 * update the host_wake_locked
 *
 * Arguments:
 * host_wake state
 *
 * Return Value:
 * void
 */

static void lbf_update_host_wake(int host_wake)
{

	if (host_wake == lbf_get_host_wake_state() && host_wake == IDLE)
		return;

	if (host_wake == lbf_get_host_wake_state() &&
			lbf_get_device_state() == D0)
		return;

	lbf_set_host_wake_state(host_wake);
	intel_lbf_lpm.hostwake = 0;

	if (!host_wake) {
		intel_lbf_lpm.hostwake |= HOSTWAKE_LOW;
		switch (lbf_get_device_state()) {
		case D2:
			intel_lbf_lpm.hostwake |= HOSTWAKE_D2;
			break;
		case D0_TO_D2:
			intel_lbf_lpm.hostwake |= HOSTWAKE_D0_TO_D2;
			pr_debug("Ignore tx idle notification\n");
			break;
		case D0:
			intel_lbf_lpm.hostwake |= HOSTWAKE_D0;
		case D2_TO_D0:
			intel_lbf_lpm.hostwake |= HOSTWAKE_D2_TO_D0;
			schedule_work(&intel_lbf_lpm.hostwake_update_work);
			break;
		default:
			break;
		}

	}
	trace_lbf_irq_state(__func__, intel_lbf_lpm.hostwake);
}

/* irqreturn_t lbf_ldisc_hostwake_handler
 *
 * The so-called top half is the routine that actually responds
 * to the interrupt.
 * Here it disables the irq & allows it to served by bottom half
 * Interrupt handler
 *
 * Arguments:
 *
 * Return Value:
 * irqreturn_t
 */

static irqreturn_t lbf_ldisc_hostwake_handler(int irq, void *dev)
{
	int ret = IRQ_WAKE_THREAD;

	intel_lbf_lpm.host_wake = 1;
	if (!bt_self_test_running) {
		if (intel_lbf_lpm.int_host_wake)
			disable_irq_nosync(intel_lbf_lpm.int_host_wake);
		trace_lbf_irq_state(__func__, intel_lbf_lpm.host_wake);
		ret = IRQ_WAKE_THREAD;
	} else {
		host_wake_intr_count++;
		ret = IRQ_HANDLED;
	}

	return ret;
}


/* activate_irq_handler
 *
 * Activaes the irq and registers the irq handler
 *
 * Arguments:
 * void
 * Return Value:
 * void
 */

static int activate_irq_handler(void)
{
	int ret = 0;
#ifdef CONFIG_SERIAL_XGOLD
	trace_lbf_func_start(__func__);
	if (!IS_ERR_VALUE(intel_lbf_lpm.int_host_wake)) {
		if (devm_request_threaded_irq(&intel_lbf_lpm.pdev->dev,
				intel_lbf_lpm.int_host_wake,
				lbf_ldisc_hostwake_handler,
				lbf_ldisc_hostwake_bh,
				IRQF_SHARED | IRQF_TRIGGER_FALLING |
				IRQF_NO_SUSPEND, WAKEUP_HOST,
				&intel_lbf_lpm)) {
			pr_err("bt_hostwake:%s irq failed\n", WAKEUP_HOST);
			return -EINVAL;
			}
	}
#endif
#ifdef CONFIG_ACPI
	int irqf = IRQF_TRIGGER_FALLING | IRQF_NO_SUSPEND | IRQF_ONESHOT;

	pr_info("%s\n", __func__);
	if (intel_lbf_lpm.gpio_host_wake > 0) {
		ret = devm_request_threaded_irq(&intel_lbf_lpm.pdev->dev,
						intel_lbf_lpm.int_host_wake,
						lbf_ldisc_hostwake_handler,
						lbf_ldisc_hostwake_bh,
						irqf, "host_wake",
						NULL);

		if (ret < 0) {
			pr_err("Error lpm request IRQ");
			gpio_free(intel_lbf_lpm.gpio_host_wake);
		}
	} else
		pr_info("%s: no host wake is configured\n", __func__);
#endif
	return ret;
}

#ifdef CONFIG_ACPI
/* intel_bt_lpm_init
 *
 * lpm intialization
 *
 * Arguments:
 * platform device
 * Return Value:
 * status of lpm initialization
 */

static int intel_bt_lpm_init(struct platform_device *pdev)
{
	struct device *tty_dev = NULL;

	pr_debug("%s\n", __func__);
#ifdef CONFIG_SERIAL_HSU
	tty_dev = intel_mid_hsu_set_wake_peer(intel_lbf_lpm.port, NULL);
#endif
	if (!tty_dev) {
		pr_err("Error no tty dev");
		gpio_free(intel_lbf_lpm.gpio_host_wake);
		return -ENODEV;
	}

	intel_lbf_lpm.tty_dev = tty_dev;
	return 0;
}
#endif

#ifdef CONFIG_SERIAL_XGOLD
/* lbf_get_usif_dev
 *
 * Gets the usif3 handle
 *
 * Arguments:
 * void
 * Return Value:
 * void
 */
static int lbf_get_usif_dev(void)
{
	struct device *usif_dev;
	struct platform_device *usif_pdev;
	int ret = 0;

	usif_dev = bus_find_device_by_name(&platform_bus_type,
					NULL,
					"e1900000.usif3-bt-fmr");

	if (usif_dev) {
		pr_debug("%s: found device! 0x%p\n", __func__, usif_dev);
		usif_pdev = to_platform_device(usif_dev);
		intel_lbf_lpm.tty_dev = usif_dev;
		pr_debug("%s: name is %s\n", __func__, usif_pdev->name);
	} else
		ret = -EINVAL;
	trace_lbf_usif_dev(__func__, ret);
	return ret;
}
#endif


/* check_unthrottle
 *
 * Unthrottle the tty buffer
 *
 * Arguments:
 * tty structure
 * Return Value:
 * void
 */

static inline void check_unthrottle(struct tty_struct *tty)
{
	if (tty->count)
		tty_unthrottle(tty);
}

/* send_d0_dx_packet
 *
 * send Dx request packet
 *
 * Arguments:
 * void
 * Return Value:
 * status of Dx Request
 */

static int send_d0_dx_packet(void)
{
	int len;
	int ret = 0;

	trace_lbf_func_start(__func__);

	if (lbf_tx->tx_skb == NULL && !(skb_queue_len(&lbf_tx->txq)) &&
		!lbf_get_bt_fmr_state() && !lbf_get_host_wake_state() &&
			(lbf_get_device_state() == D0)) {

		lbf_set_device_state(D0_TO_D2);
		disable_irq_nosync(intel_lbf_lpm.int_host_wake);
		len = lbf_tty_write((void *) lbf_tx->tty, hci_dx_request,
			DX_HCI_CMD_SIZE);
		trace_lbf_ldisc_kpi(__func__, "BT CORE SEND D2 REQ");
		pr_debug("BT CORE SEND D2 REQ len:%d\n", len);
		if (len != DX_HCI_CMD_SIZE) {
			ret = enqueue_dx_packet(&hci_dx_request[len],
					(DX_HCI_CMD_SIZE - len));
			intel_lbf_lpm.dx_packet_pending =
				(DX_HCI_CMD_SIZE - len);
		}
	}
	trace_lbf_ldisc_sendpm(__func__, ret);
	return ret;
}

/* reset_buffer_flags
 *
 * Resets the st driver's buffer
 *
 * Arguments:
 * void
 * Return Value:
 * void
 */

static void reset_buffer_flags(struct tty_struct *tty)
{
	struct lbf_uart *lbf_ldisc;

	trace_lbf_func_start(__func__);
	lbf_ldisc = tty->disc_data;
	spin_lock(&lbf_ldisc->tx_lock);
	lbf_ldisc->read_head = 0;
	lbf_ldisc->read_cnt = 0;
	lbf_ldisc->read_tail = 0;
	spin_unlock(&lbf_ldisc->tx_lock);
	tty->receive_room = RECEIVE_ROOM;
	lbf_update_set_room(tty, 0);
	check_unthrottle(tty);

}

/* dx_pending_packet
 *
 * Handles if Dx packet couldnot be sent in one go
 *
 * Arguments:
 * len
 * Return Value:
 * void
 */

static void dx_pending_packet(int len)
{
	trace_lbf_func_start(__func__);
	if (intel_lbf_lpm.dx_packet_pending == len) {
		intel_lbf_lpm.dx_packet_pending = 0;
		lbf_set_device_state(D0_TO_D2);
	} else
		intel_lbf_lpm.dx_packet_pending =
			intel_lbf_lpm.dx_packet_pending - len;

}

/* d0_pending_packet
 *
 * Handles if D0 packet couldnot be sent in one go
 *
 * Arguments:
 * len
 * Return Value:
 * void
 */

static void d0_pending_packet(int len)
{
	trace_lbf_func_start(__func__);
	if (intel_lbf_lpm.d0_packet_pending == len) {
		intel_lbf_lpm.d0_packet_pending = 0;
		lbf_set_device_state(D2_TO_D0);
	} else
		intel_lbf_lpm.d0_packet_pending =
			intel_lbf_lpm.d0_packet_pending - len;

}

/* pending_dx_packet
 *
 * checks for Dx pending packet and update the dx pending packet
 *
 * Arguments:
 * len
 * Return Value:
 * void
 */

static inline void pending_dx_packet(int len)
{
	trace_lbf_func_start(__func__);
	if (intel_lbf_lpm.dx_packet_pending)
		dx_pending_packet(len);
	if (intel_lbf_lpm.d0_packet_pending)
		d0_pending_packet(len);
}

/* lbf_tx_wakeup
 *
 * wakes up and does Tx operation
 *
 * Arguments:
 * Driver's internal structure
 * Return Value:
 * void
 */

static void lbf_tx_wakeup(struct lbf_uart *lbf_uart)
{
	struct sk_buff *skb;
	int len = 0;

	trace_lbf_func_start(__func__);

	mutex_lock(&lbf_tx->tx_wakeup);
	if (lbf_tx->tbusy) {
		lbf_tx->woke_up = 1;
		mutex_unlock(&lbf_tx->tx_wakeup);
		return;
	}
	lbf_tx->tbusy = 1; /* tx write is busy.*/

check_again:
	lbf_tx->woke_up = 0;
	mutex_unlock(&lbf_tx->tx_wakeup);


	while ((skb = lbf_dequeue())) {

		if (lbf_tx->tty) {
			set_bit(TTY_DO_WRITE_WAKEUP, &lbf_tx->tty->flags);
			len = lbf_tty_write((void *) lbf_tx->tty, skb->data,
					skb->len);
			if (intel_lbf_lpm.lpm_enable == ENABLE)
				pending_dx_packet(len);
			skb_pull(skb, len);

			if (skb->len) {
				lbf_tx->tx_skb = skb;
				pr_debug("-> %s added to pending buffer:%d\n",
					__func__, skb->len);
				break;
			}
			kfree_skb(skb);
		}
	}

	mutex_lock(&lbf_tx->tx_wakeup);
	if (lbf_tx->woke_up)
		goto check_again;

	if (intel_lbf_lpm.lpm_enable == ENABLE)
		send_d0_dx_packet();
	lbf_tx->tbusy = 0; /* Done with Tx.*/
	mutex_unlock(&lbf_tx->tx_wakeup);

	trace_lbf_func_end(__func__);

}

static void lbf_tx_module_st(unsigned long  module)
{
	spin_lock(&intel_lbf_lpm.lpm_modulestate);

	switch (module) {
	case  MODULE_FM:
		intel_lbf_lpm.fm_module_state = IDLE;
		break;
	case MODULE_BT:
		intel_lbf_lpm.bt_module_state = IDLE;
		break;
	}

	if (!intel_lbf_lpm.bt_module_state && !intel_lbf_lpm.fm_module_state) {
		intel_lbf_lpm.bt_fmr_state = IDLE;
		pr_debug("-> %sBoth BT & FMR Idle\n", __func__);
	} else
		intel_lbf_lpm.bt_fmr_state = ACTIVE;

	spin_unlock(&intel_lbf_lpm.lpm_modulestate);
}

static void lbf_tx_wakeup_work(struct work_struct *work)
{
	lbf_tx_wakeup(NULL);
}

/* This is the internal write function - a wrapper
 * to tty->ops->write
 */
int lbf_tty_write(struct tty_struct *tty, const unsigned char *data, int count)
{
	struct lbf_uart *lbf_uart;

	lbf_uart = tty->disc_data;
	if ((lbf_uart != NULL) && (lbf_uart->tty->ops->write != NULL))
		return lbf_uart->tty->ops->write(tty, data, count);
	else
		return 0;
}

static long lbf_write(struct sk_buff *skb)
{
	long len = 0;
	int ret = 0;
	struct lbf_uart *lbf_uart;

	trace_lbf_func_start(__func__);
	lbf_uart = (struct lbf_uart *) lbf_tx->tty->disc_data;

	print_hex_dump_debug("<FMR out<", DUMP_PREFIX_NONE, 16, 1, skb->data,
			skb->len, 0);
	if (test_bit(HCI_REST_CMD, &lbf_uart->hci_reset) ||
		test_bit(HCI_HW_ERR, &lbf_uart->hci_cmd_st) ||
		test_bit(HCI_CMD_TOUT, &lbf_uart->hci_cmd_st))
		goto not_allowed;

	if (intel_lbf_lpm.lpm_enable == ENABLE) {
		spin_lock(&intel_lbf_lpm.lpm_modulestate);
		intel_lbf_lpm.fm_module_state = ACTIVE;
		intel_lbf_lpm.bt_fmr_state = ACTIVE;
		pr_debug("-> %s BT_FMR st:%d\n", __func__,
			intel_lbf_lpm.bt_fmr_state);
		spin_unlock(&intel_lbf_lpm.lpm_modulestate);
		ret = lpm_tx_update(MODULE_FM);
	}
	/*ST to decide where to enqueue the skb */
	if (!ret) {
		lbf_enqueue(skb);
		/* wake up */
		len = skb->len;
		lbf_tx_wakeup(NULL);
	} else
		lbf_tx_module_st(MODULE_FM);

not_allowed:
	trace_lbf_ldisc_write(__func__, len);
	/* return number of bytes written */
	return len;
}

/* for protocols making use of shared transport */

long unregister_fmdrv_from_ld_driv(struct fm_ld_drv_register *fm_ld_drv_reg)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&lbf_tx->lock, flags);
	stack_pid = 0;
	spin_unlock_irqrestore(&lbf_tx->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(unregister_fmdrv_from_ld_driv);

long register_fmdrv_to_ld_driv(struct fm_ld_drv_register *fm_ld_drv_reg)
{
	unsigned long flags;
	long err = 0;

	fm = NULL;
	trace_lbf_func_start(__func__);

	if (lbf_ldisc_running == 1) {

		if (fm_ld_drv_reg == NULL || fm_ld_drv_reg->fm_cmd_handler
				== NULL
				|| fm_ld_drv_reg->ld_drv_reg_complete_cb
						== NULL) {
			pr_err("fmr ops are NULL");
			return -EINVAL;
		}
		spin_lock_irqsave(&lbf_tx->lock, flags);
		fm_ld_drv_reg->fm_cmd_write = lbf_write;
		fm = fm_ld_drv_reg;
		spin_unlock_irqrestore(&lbf_tx->lock, flags);

		if (fm) {
			if (likely(fm->ld_drv_reg_complete_cb != NULL)) {
				fm->ld_drv_reg_complete_cb(fm->priv_data,
						0);
				pr_debug("Registration called");

			}
		}
	} else
		err = -EINVAL;

	return err;
}
EXPORT_SYMBOL_GPL(register_fmdrv_to_ld_driv);

static void lbf_ldisc_flush_buffer(struct tty_struct *tty)
{
	/* clear everything and unthrottle the driver */

	unsigned long flags;

	trace_lbf_func_start(__func__);
	reset_buffer_flags(tty);

	if (!tty->link)
		return;

	if (tty->link) {
		spin_lock_irqsave(&tty->ctrl_lock, flags);
		if (tty->link->packet) {
			tty->ctrl_status |= TIOCPKT_FLUSHREAD;
			wake_up_interruptible(&tty->link->read_wait);
		}
		spin_unlock_irqrestore(&tty->ctrl_lock, flags);
	}
	trace_lbf_func_end(__func__);
}

/**
 * lbf_dequeue - internal de-Q function.
 * If the previous data set was not written
 * completely, return that skb which has the pending data.
 * In normal cases, return top of txq.
 */
struct sk_buff *lbf_dequeue(void)
{
	struct sk_buff *returning_skb;

	if (lbf_tx->tx_skb != NULL) {
		returning_skb = lbf_tx->tx_skb;
		lbf_tx->tx_skb = NULL;
		return returning_skb;
	}
	return skb_dequeue(&lbf_tx->txq);
}

/**
 * lbf_enqueue - internal Q-ing function.
 * Will either Q the skb to txq or the tx_waitq
 * depending on the ST LL state.
 * txq needs protection since the other contexts
 * may be sending data, waking up chip.
 */
static inline void lbf_enqueue(struct sk_buff *skb)
{
	skb_queue_tail(&lbf_tx->txq, skb);
}

/* lbf_ldisc_free_skbuff()
 * Free all skb_rx buffer
 * protocol stacks
 * Arguments :
 * lbf_uart : Disc data for tty
 * Return Type: void
 */
static void lbf_ldisc_free_skbuff(struct lbf_uart *lbf_uart)
{
	if (lbf_uart->rx_skb) {
			kfree_skb(lbf_uart->rx_skb);
			lbf_uart->rx_skb = NULL;
		}

		lbf_uart->rx_state = LBF_W4_H4_HDR;
		lbf_uart->rx_count = 0;
		lbf_uart->rx_chnl = 0;
		lbf_uart->bytes_pending = 0;
}

static int lbf_send_netlink_message(
	struct tty_struct *tty, enum bt_hci_nl_messages code)
{
	struct lbf_uart *lbf_uart = (void *) tty->disc_data;
	struct sk_buff *skb_out = NULL;
	struct nlmsghdr *nlh = NULL;
	int err = 0;

	size_t msglen = sizeof(enum bt_hci_nl_messages);

	if ((!lbf_uart->nl_socket) || (!stack_pid)) {
		err = INVALID;
		goto netlink_failed;
	}

	skb_out = nlmsg_new(msglen, GFP_ATOMIC);

	if (!skb_out) {
		trace_lbf_ldisc_info(__func__,
			"Could not allocate Netlink message");
		err = INVALID;
		goto netlink_failed;
	}


	nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msglen, 0);
	NETLINK_CB(skb_out).dst_group = 0;

	memcpy(nlmsg_data(nlh), (void *)&code, msglen);

	err = nlmsg_unicast(lbf_uart->nl_socket, skb_out, stack_pid);

	if (err < 0)
		trace_lbf_ldisc_info(__func__,
				"Netlink message failed");

netlink_failed:

	return err;
}

static void lbf_ldisc_hci_reset(void)
{
	int ret = INVALID;
	struct lbf_uart *lbf_uart = (struct lbf_uart *) lbf_tx->tty->disc_data;

	trace_lbf_func_start(__func__);
	ret = hrtimer_try_to_cancel(&lbf_uart->hci_reset_timer);

	/* Dont send HCI Reset cmd if already sent out on expire of the timer
		or if the timer expiation function is currently executing */
	if (!test_and_set_bit(HCI_RESET_SEND, &lbf_uart->hci_reset) &&
		!test_bit(HCI_RESET_COMPL_RCV, &lbf_uart->hci_reset))
		lbf_tty_write((void *) lbf_tx->tty,
			hci_reset, HCI_RESET_CMD_SIZE);
		/*Sends HCI Reset cmd*/

}

static void lbf_netlink_callback(struct sk_buff *skb)
{
	/* Extract the Netlink Message Header */
	struct lbf_uart *lbf_uart;
	struct nlmsghdr *nlh = (struct nlmsghdr *)skb->data;
	int data;

	trace_lbf_func_start(__func__);

	lbf_uart = (struct lbf_uart *) lbf_tx->tty->disc_data;

	memcpy((void *)&data, nlmsg_data(nlh), sizeof(int));
	switch (data) {
	case BT_DRIVER_REGISTER:
		/* Process ID (PID) of sending process */
		trace_lbf_ldisc_info(__func__, "BT_DRIVER_REGISTER");
		stack_pid = nlh->nlmsg_pid;
		break;
	case BT_DEV_HARD_RESET_HW_ERR_RSP:
		trace_lbf_ldisc_info(__func__,
				"Recv BT_DEV_HARD_RESET_HW_ERR_INIT");
		spin_lock(&lbf_uart->hw_reset_lock);
		if (fw_download_state != FW_BLANK) {
			bt_rfkill_set_power(DISABLE);
			fw_download_state = FW_BLANK;
		}

		spin_unlock(&lbf_uart->hw_reset_lock);
		break;
	case BT_HCI_RESET_RESPONSE:
		trace_lbf_ldisc_reset(__func__, "Recv BT_HCI_RESET_RESPONSE");
		lbf_ldisc_hci_reset();
		break;
	case BT_DEV_HARD_RESET_CMD_TO:
		trace_lbf_ldisc_info(__func__,
			"Recv BT_DEV_HARD_RESET_CMD_TO");
		spin_lock(&lbf_uart->hw_reset_lock);
		if (fw_download_state == FW_SUCCESS) {
			bt_rfkill_set_power(DISABLE);
			fw_download_state = FW_BLANK;
			reset_buffer_flags(lbf_tx->tty);
			lbf_ldisc_free_skbuff(lbf_uart);
			if (!test_and_set_bit(HCI_CMD_TOUT,
						&lbf_uart->hci_cmd_st))
				lbf_ldisc_receive(lbf_tx->tty, hci_hw_err,
						NULL, 4);
		}

		spin_unlock(&lbf_uart->hw_reset_lock);
		break;
	default:
		trace_lbf_ldisc_info(__func__, "LNP_LDISC Not a valid msg\n");
	}
}

struct netlink_kernel_cfg netlink_cfg = {
	.groups = 0,
	.input = lbf_netlink_callback,
};

/* lbf_ldisc_open
 *
 * Called when line discipline changed to HCI_UART.
 *
 * Arguments:
 * tty pointer to tty info structure
 * Return Value:
 * 0 if success, otherwise error code
 */
static int lbf_ldisc_open(struct tty_struct *tty)
{

	struct lbf_uart *lbf_uart;
	int ret = 0;
	struct ktermios ktermios;
	tty->driver->ops->tiocmget(tty);

	trace_lbf_func_start(__func__);

	memcpy(&ktermios, &tty->termios, sizeof(ktermios));
	ktermios.c_cflag = B115200;
	ktermios.c_ospeed = ktermios.c_ispeed = 115200;
	tty_set_termios(tty, &ktermios);

	if (tty->disc_data) {
		pr_debug("%s ldiscdata exist\n ", __func__);
		ret = -EEXIST;
		goto fail_alloc_lbf_buff;
	}
	/* Error if the tty has no write op instead of leaving an exploitable
	 hole */
	if (tty->ops->write == NULL) {
		pr_debug("%s write = NULL\n ", __func__);
		ret = -EOPNOTSUPP;
		goto fail_alloc_lbf_buff;
	}

	lbf_uart = kzalloc(sizeof(struct lbf_uart), GFP_KERNEL);
	if (!lbf_uart) {
		/*pr_debug(" out of memory\n ");*/
		ret = -ENOMEM;
		goto fail_alloc_lbf_buff;
	}

	lbf_tx = kzalloc(sizeof(struct lbf_q_tx), GFP_KERNEL);
	if (!lbf_tx) {
		/*pr_debug(" out of memory\n ");*/
		ret = -ENOMEM;
		goto fail_alloc_tx_q;
	}


	tty->disc_data = lbf_uart;
	lbf_uart->tty = tty;
	lbf_tx->tty = tty;
	skb_queue_head_init(&lbf_tx->txq);
	skb_queue_head_init(&lbf_tx->tx_waitq);
	/* don't do an wakeup for now */
	clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);

	if (likely(!lbf_uart->read_buf)) {
		lbf_uart->read_buf
				= kzalloc(N_INTEL_LDISC_BUF_SIZE, GFP_KERNEL);

		if (!lbf_uart->read_buf) {
			return -ENOMEM;
			goto fail_alloc_buff;
		}
	}

	tty->receive_room = RECEIVE_ROOM;
	spin_lock_init(&lbf_uart->rx_lock);
	spin_lock_init(&lbf_tx->lock);
	spin_lock_init(&lbf_uart->tx_lock);
	spin_lock_init(&lbf_uart->tx_update_lock);
	spin_lock_init(&lbf_uart->hw_reset_lock);
	mutex_init(&lbf_tx->writelock);
	mutex_init(&lbf_tx->fwdownloadlock);
	mutex_init(&lbf_tx->tx_wakeup);
	mutex_init(&intel_lbf_lpm.lpmenable);
	spin_lock_init(&intel_lbf_lpm.lpm_modulestate);
	mutex_init(&lbf_uart->atomic_read_lock);

	INIT_WORK(&lbf_tx->tx_wakeup_work, lbf_tx_wakeup_work);
	INIT_WORK(&intel_lbf_lpm.hostwake_update_work,
			lbf_hostwake_update_work);
	INIT_WORK(&intel_lbf_lpm.host_enable_work, lbf_host_enable_work);

	init_waitqueue_head(&lbf_uart->read_wait);

	lbf_ldisc_running = 1;
	intel_lbf_lpm.device_state = D0;
	fw_download_state = FW_BLANK;
	intel_lbf_lpm.interrupt = false;

	/* Flush any pending characters in the driver and line discipline. */
	reset_buffer_flags(tty);

	if (tty->ldisc->ops->flush_buffer)
		tty->ldisc->ops->flush_buffer(tty);
	tty_driver_flush_buffer(tty);
	intel_lbf_lpm.lpm_enable = DISABLE;
	lbf_serial_get();
	intel_lbf_lpm.rx_refcount = 1;
	lbf_serial_get();
	intel_lbf_lpm.tx_refcount = 1;

	lbf_uart->nl_socket = netlink_kernel_create(&init_net, NETLINK_USERSOCK,
			&netlink_cfg);
	if (!lbf_uart->nl_socket) {
		pr_err(
			"Netlink receive handler registration failed\n");
		goto fail_create_nl_sk;
	} else
		pr_debug(
			"Netlink receive handler registration succeded\n");
	stack_pid = 0;

	hrtimer_init(&(lbf_uart->hci_reset_timer), CLOCK_MONOTONIC,
							HRTIMER_MODE_REL);
	lbf_uart->hci_reset_delay = ktime_set(0, 10000000);  /* 1 sec */
	lbf_uart->hci_reset_timer.function = lbf_hci_reset_expire;
	return 0;

fail_create_nl_sk:
	kfree(lbf_uart->read_buf);
fail_alloc_buff:
	kfree(lbf_tx);
fail_alloc_tx_q:
	kfree(lbf_uart);
fail_alloc_lbf_buff:
	tty_unregister_ldisc(N_INTEL_LDISC);

	trace_lbf_func_end(__func__);

	return ret;
}


/* lbf_ldisc_close()
 *
 * Called when the line discipline is changed to something
 * else, the tty is closed, or the tty detects a hangup.
 */

static void lbf_ldisc_close(struct tty_struct *tty)
{
	struct lbf_uart *lbf_uart = (void *) tty->disc_data;

	tty->disc_data = NULL; /* Detach from the tty */
	bt_rfkill_set_power(DISABLE);
	trace_lbf_func_start(__func__);

	if (intel_lbf_lpm.rx_refcount > 0) {
		lbf_serial_put();
		intel_lbf_lpm.rx_refcount = 0;
	}
	if (intel_lbf_lpm.tx_refcount > 0) {
		lbf_serial_put();
		intel_lbf_lpm.tx_refcount = 0;
	}

	if (lbf_uart->nl_socket) {
		netlink_kernel_release(lbf_uart->nl_socket);
		pr_debug("->Netlink receive handler release succeded\n");
	}

	if (intel_lbf_lpm.lpm_enable == ENABLE &&
		intel_lbf_lpm.module_refcount > 0) {
		intel_lbf_lpm.module_refcount = 0;
		lbf_ldisc_release_serial();
		intel_lbf_lpm.lpm_enable = DISABLE;
	}

	flush_work(&lbf_tx->tx_wakeup_work);
	flush_work(&intel_lbf_lpm.hostwake_update_work);
	flush_work(&intel_lbf_lpm.host_enable_work);
	skb_queue_purge(&lbf_tx->txq);
	skb_queue_purge(&lbf_tx->tx_waitq);
	lbf_uart->rx_count = 0;
	lbf_uart->rx_state = LBF_W4_H4_HDR;
	kfree_skb(lbf_uart->rx_skb);
	kfree(lbf_uart->read_buf);
	lbf_uart->rx_skb = NULL;
	lbf_ldisc_running = -1;

	kfree(lbf_tx);
	lbf_tx = NULL;
	kfree(lbf_uart);
	lbf_uart = NULL;
}

/* lbf_ldisc_wakeup()
 *
 * Callback for transmit wakeup. Called when low level
 * device driver can accept more send data.
 *
 * Arguments: tty pointer to associated tty instance data
 * Return Value: None
 */
static void lbf_ldisc_wakeup(struct tty_struct *tty)
{

	struct lbf_uart *lbf_uart = (void *) tty->disc_data;

	trace_lbf_func_start(__func__);

	clear_bit(TTY_DO_WRITE_WAKEUP, &lbf_uart->tty->flags);
	schedule_work(&lbf_tx->tx_wakeup_work);

	trace_lbf_func_end(__func__);

}

/* select_proto()
 * Note the HCI Header type
 * Arguments : type: the 1st byte in the packet
 * Return Type: The Corresponding type in the array defined
 * for all headers
 */

static inline int select_proto(int type)
{
	return (type >= 1 && type < 6) ? type : INVALID;
}

/* lbf_ldisc_copy()
 * push the skb received to relevant
 * protocol stacks
 * Arguments : tty pointer to associated tty instance data
 * lbf_uart : Disc data for tty
 * Return Type: int
 */
static void lbf_ldisc_copy(struct tty_struct *tty, struct lbf_uart *lbf_uart)
{
	unsigned int i;
	unsigned int count;

	trace_lbf_func_start(__func__);

	count = lbf_uart->rx_skb->len;
	spin_lock(&lbf_uart->tx_lock);
	i = min3((N_INTEL_LDISC_BUF_SIZE - lbf_uart->read_cnt),
				N_INTEL_LDISC_BUF_SIZE - lbf_uart->read_head,
				(int)lbf_uart->rx_skb->len);
	spin_unlock(&lbf_uart->tx_lock);
	memcpy(lbf_uart->read_buf + lbf_uart->read_head,
			 &lbf_uart->rx_skb->data[0], i);
	spin_lock(&lbf_uart->tx_lock);
	lbf_uart->read_head =
			(lbf_uart->read_head + i) & (N_INTEL_LDISC_BUF_SIZE-1);
	lbf_uart->read_cnt += i;
	spin_unlock(&lbf_uart->tx_lock);
	count = count - i;

	if (unlikely(count)) {
		pr_debug(" Added in buffer inside count %d readhead %d\n"
			, count, lbf_uart->read_head);
		spin_lock(&lbf_uart->tx_lock);
		i = min3((N_INTEL_LDISC_BUF_SIZE - lbf_uart->read_cnt),
			(N_INTEL_LDISC_BUF_SIZE - lbf_uart->read_head),
			(int)count);
		spin_unlock(&lbf_uart->tx_lock);
		memcpy(lbf_uart->read_buf + lbf_uart->read_head,
		&lbf_uart->rx_skb->data[lbf_uart->rx_skb->len - count], i);
		spin_lock(&lbf_uart->tx_lock);
		lbf_uart->read_head =
			(lbf_uart->read_head + i) & (N_INTEL_LDISC_BUF_SIZE-1);
		lbf_uart->read_cnt += i;
		spin_unlock(&lbf_uart->tx_lock);

	}

}

/* lbf_ldisc_send_hwerr_frame()
 * push the skb received to relevant
 * protocol stacks
 * Arguments : tty pointer to associated tty instance data
 * lbf_uart : Disc data for tty
 * Return Type: int
 */
static int lbf_ldisc_send_hwerr_frame(struct tty_struct *tty,
			struct lbf_uart *lbf_uart)
{
	int len = (int)(sizeof(hci_hw_err) / sizeof(hci_hw_err[0]));

	trace_lbf_func_start(__func__);
	if (lbf_uart->rx_skb) {
		kfree_skb(lbf_uart->rx_skb);
		lbf_uart->rx_skb = NULL;
	}
	lbf_uart->rx_skb = alloc_skb(len, GFP_ATOMIC);

	if (!lbf_uart->rx_skb)
		return -ENOMEM;
	memcpy(skb_put(lbf_uart->rx_skb, len), hci_hw_err, len);
	lbf_ldisc_copy(tty, lbf_uart);
	return 0;
}


/* lbf_ldisc_bootuperr()
 * push the skb received to relevant
 * protocol stacks
 * Arguments : tty pointer to associated tty instance data
 * lbf_uart : Disc data for tty
 * Return Type: int
 */
static int lbf_ldisc_bootuperr(struct tty_struct *tty,
					struct lbf_uart *lbf_uart)
{
	int ret = 0;

	trace_lbf_func_start(__func__);
	set_bit(HCI_HW_ERR, &lbf_uart->hci_cmd_st);
	lbf_update_set_room(tty, lbf_uart->rx_skb->len);
	lbf_send_netlink_message(tty, BT_HCI_HW_ERR_REQUEST);
	ret = lbf_ldisc_send_hwerr_frame(tty, lbf_uart);
	return ret;
}


/* st_send_frame()
 * push the skb received to relevant
 * protocol stacks
 * Arguments : tty pointer to associated tty instance data
 * lbf_uart : Disc data for tty
 * Return Type: int
 */
static int st_send_frame(struct tty_struct *tty, struct lbf_uart *lbf_uart)
{
	int ret = 0;
	int chnl_id = MODULE_BT;
	unsigned char *buff;
	unsigned int opcode = 0;

	if (unlikely(lbf_uart == NULL || lbf_uart->rx_skb == NULL)) {
		pr_err(" No channel registered, no data to send?");
		return ret;
	}
	buff = &lbf_uart->rx_skb->data[0];
	if (intel_lbf_lpm.lpm_enable == ENABLE &&
		(lbf_uart->rx_skb->data[0] == LPM_PKT) &&
		(lbf_uart->rx_skb->data[1] == TX_HOST_NOTIFICATION)) {
		if (lbf_uart->rx_skb->data[3] == IDLE) {
			lbf_update_host_wake(IDLE);
			pr_debug("Tx Idle Notification\n");
		} else {
			lbf_set_host_wake_state(ACTIVE);
			pr_debug("Tx Active Notification\n");
		}
		lbf_update_set_room(tty, DX_HCI_CMD_SIZE);
		goto free_packet;
	}

	STREAM_TO_UINT16(opcode, buff);
	pr_debug("opcode : 0x%x event code: 0x%x registered", opcode,
			 lbf_uart->rx_skb->data[1]);

	switch (lbf_uart->rx_skb->data[0]) {
	case LPM_PKT:
	case HCI_EVENT_PKT:
		trace_lbf_bt_rx_buf(__func__, lbf_uart->rx_skb->data,
					(lbf_uart->rx_skb->len));
		break;
	case HCI_ACLDATA_PKT:
		trace_lbf_bt_rx_buf(__func__, lbf_uart->rx_skb->data,
					(lbf_uart->rx_skb->len)/3);
		break;
	default:
		pr_err("LBF_LDISC:Invalid pkt\n");
		break;
	}

	if (HCI_COMMAND_HARDWARE_ERROR == lbf_uart->rx_skb->data[1] &&
		!test_bit(HCI_CMD_TOUT, &lbf_uart->hci_cmd_st)) {
		lbf_send_netlink_message(tty, BT_HCI_HW_ERR_REQUEST);
		set_bit(HCI_HW_ERR, &lbf_uart->hci_cmd_st);
		trace_lbf_ldisc_info(__func__, "H/W error");
	}

	if (HCI_COMMAND_COMPLETE_EVENT == lbf_uart->rx_skb->data[1]) {
		switch (opcode) {
		case FMR_IRQ_CONFIRM:
		case FMR_SET_POWER:
		case FMR_READ:
		case FMR_WRITE:
		case FMR_SET_AUDIO:
		case FMR_TOP_READ:
		case FMR_TOP_WRITE:
			chnl_id = MODULE_FM;
			trace_lbf_ldisc_info(__func__, "Its FMR event");
			break;
		/* Ignore the CCE for all Secure send command
						during FW Download */
#ifndef CONFIG_SERIAL_8250
		case BT_SECURE_SEND:
			ret = lbf_uart->rx_skb->len;
			goto free_packet;
#endif
		case BT_HCI_RESET:
			set_bit(HCI_RESET_COMPL_RCV, &lbf_uart->hci_reset);
			clear_bit(HCI_RESET_SEND, &lbf_uart->hci_reset);
			clear_bit(HCI_REST_CMD, &lbf_uart->hci_reset);
			pr_debug("chnl_id: %d lbf_uart->hci_reset: %ld\n",
					chnl_id, lbf_uart->hci_reset);
			trace_lbf_ldisc_reset(__func__,
					"LBF_LDISC HCI Complete");
			lbf_send_netlink_message(tty,
					BT_HCI_RESET_COMPLETE_NOTIFY);
			chnl_id = MODULE_BT;
			trace_lbf_ldisc_info(__func__, "Its BT event");
			break;
		default:
			chnl_id = MODULE_BT;
			trace_lbf_ldisc_info(__func__, "Its BT event");
			break;
		}
	}

	pr_debug("chnl_id: %d lbf_uart->hci_reset: %ld\n", chnl_id,
			lbf_uart->hci_reset);
	if (test_bit(HCI_REST_CMD, &lbf_uart->hci_reset))
		goto free_packet;


	if (HCI_INTERRUPT_EVENT == lbf_uart->rx_skb->data[1]) {
		switch (lbf_uart->rx_skb->data[3]) {
		case HCI_INTEL_BOOTUP_EVENT_ID:
			switch (lbf_uart->rx_skb->data[6]) {
			case OPERATIONAL:
				switch (lbf_uart->rx_skb->data[8]) {
				case HCI_INTEL_BOOTUP_REASON_WATCHDOG_EXCEP:
				case HCI_INTEL_BOOTUP_REASON_FATAL_EXCEP:
				case HCI_INTEL_BOOTUP_REASON_SYSTEM_EXCEP:
					trace_lbf_ldisc_info(__func__,
							"Bootup error");
					ret = lbf_ldisc_bootuperr(tty,
								lbf_uart);
					if (ret < 0)
						pr_err("LNP_LDISC malloc fail\n");
					else
						goto free_packet;
				break;
				case HCI_INTEL_BOOTUP_REASON_UNKNW_EXCEP:
				case HCI_INTEL_BOOTUP_REASON_HCI_RESET:
				case HCI_INTEL_BOOTUP_REASON_HW_RESET:
				case HCI_INTEL_BOOTUP_REASON_INTEL_RESET:
					trace_lbf_ldisc_info(__func__,
						"Bootup HCI event");
					chnl_id = MODULE_BT;
				break;
				default:
				break;
				}
			case BOOTLOADER:
				switch (lbf_uart->rx_skb->data[8]) {
				case HCI_INTEL_BOOTUP_REASON_HCI_RESET:
				case HCI_INTEL_BOOTUP_REASON_HW_RESET:
				case HCI_INTEL_BOOTUP_REASON_INTEL_RESET:
				case HCI_INTEL_BOOTUP_REASON_FATAL_EXCEP:
				case HCI_INTEL_BOOTUP_REASON_SYSTEM_EXCEP:
				case HCI_INTEL_BOOTUP_REASON_UNKNW_EXCEP:
					trace_lbf_ldisc_info(__func__,
							"Bootup HCI event");
					chnl_id = MODULE_BT;
				break;
				case HCI_INTEL_BOOTUP_REASON_WATCHDOG_EXCEP:
					trace_lbf_ldisc_info(__func__,
								"Bootup error");
					ret = lbf_ldisc_bootuperr(tty,
								lbf_uart);
					if (ret < 0)
						pr_err("LNP_LDISC mallofail\n");
					else
						goto free_packet;
				break;
				default:
				break;
				}
			default:
				break;
			}
			break;
		case FMR_DEBUG_EVENT_ID:
			chnl_id = MODULE_FM;
			break;
		default:
			chnl_id = MODULE_BT;
			}
		}


	if (chnl_id == MODULE_FM) {
		if (likely(fm)) {
			if (likely(fm->fm_cmd_handler != NULL)) {
				if (unlikely(fm->fm_cmd_handler(fm->
					priv_data, lbf_uart->rx_skb) != 0))
					pr_err("proto stack %d recv failed"
						, chnl_id);
				else
					ret = lbf_uart->rx_skb->len;
			}
		} else
			pr_err("fm ops is NULL\n");
	} else if (chnl_id == MODULE_BT) {
		lbf_ldisc_copy(tty, lbf_uart);
		trace_lbf_ldisc_info(__func__, "BT packet");
	}


free_packet:
	lbf_ldisc_free_skbuff(lbf_uart);
	return ret;
}

/* lbf_ldisc_fw_download_init()
 * lock the Mutex to block the IOCTL on 2nd call
 * Return Type: void
 */
static int lbf_ldisc_fw_download_init(void)
{
	unsigned long ret = -1;
	struct lbf_uart *lbf_uart;

	lbf_uart = (struct lbf_uart *) lbf_tx->tty->disc_data;

	trace_lbf_func_start(__func__);
	if (mutex_lock_interruptible(&lbf_tx->fwdownloadlock))
		return -ERESTARTSYS;
	pr_debug("->%s fw_download_state : %d\n", __func__, fw_download_state);
	if (bt_enable_state == DISABLE) {
		bt_rfkill_set_power(ENABLE);
		fw_download_state = DO_FW_DL;
		clear_bit(HCI_CMD_TOUT, &lbf_uart->hci_cmd_st);
		clear_bit(HCI_HW_ERR, &lbf_uart->hci_cmd_st);
		ret = DO_FW_DL;
	} else {
		if (fw_download_state == FW_FAILED)
			ret = FW_FAILED;
		else if (fw_download_state == FW_SUCCESS)
			ret = DO_STACK_INIT;
	}
	trace_lbf_func_end(__func__);
	return ret;
}

/* wait_d2_exit()
 * wait until Dx to D0 transition is completed
 * Return Type: status
 * Dx wake status
 */

static int wait_d2_exit(void)
{
	int ret = 0;
	DEFINE_WAIT(d2wait);

	trace_lbf_func_start(__func__);

	while (lbf_get_device_state() == D2) {
		prepare_to_wait(&waitFord2exit, &d2wait, TASK_INTERRUPTIBLE);

		if (lbf_get_device_state() == D2)
			ret = schedule_timeout(3*HZ);

		finish_wait(&waitFord2exit, &d2wait);

		if (ret == 0 && lbf_get_device_state() == D2) {
			ret = FAILED;
			pr_err("%s D2 time out\n", __func__);
			break;
		}
		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	trace_lbf_ldisc_sendpm(__func__, ret);
	return ret;
}

/* wait_dx_exit()
 * wait until Dx to D0 transition is completed
 * Return Type: status
 * Dx wake status
 */

static int wait_dx_exit(void)
{
	int ret = 0;
	DEFINE_WAIT(dxwait);

	trace_lbf_func_start(__func__);

	while (lbf_get_device_state() == D2_TO_D0) {
		prepare_to_wait(&waitFordxexit, &dxwait, TASK_INTERRUPTIBLE);

		if (lbf_get_device_state() == D2_TO_D0)
			ret = schedule_timeout(2*HZ);

		finish_wait(&waitFordxexit, &dxwait);

		if (ret == 0 && lbf_get_device_state() == D2_TO_D0) {
			ret = FAILED;
			lbf_set_device_state(D2);
			pr_err("%s D2_TO_D0 time out\n", __func__);
			break;
		}
		if (signal_pending(current))
			return -ERESTARTSYS;


	}

	trace_lbf_ldisc_sendpm(__func__, ret);
	return ret;
}

/* wait_d0_exit()
 * wait until D0 to Dx transition is completed
 * Return Type: status
 * D0 exit status
 */

static int wait_d0_exit(void)
{
	int ret = 0;

	trace_lbf_func_start(__func__);

	while (lbf_get_device_state() == D0_TO_D2) {

		DEFINE_WAIT(d0wait);

		prepare_to_wait(&waitFord0exit, &d0wait, TASK_INTERRUPTIBLE);

		if (lbf_get_device_state() == D0_TO_D2)
			ret = schedule_timeout(2*HZ);

			/* the remaining time in jiffies will be returned,
			 or 0 if the timer expired in time*/

		finish_wait(&waitFord0exit, &d0wait);

		if (ret == 0 && lbf_get_device_state() == D0_TO_D2) {
			ret = FAILED;
			lbf_set_device_state(D0);
			pr_err("%s D0_TO_D2 time out\n", __func__);
			break;
		}
		if (signal_pending(current))
			return -ERESTARTSYS;

	}

	trace_lbf_ldisc_sendpm(__func__, ret);
	return ret;
}

/* enqueue_dx_packet()
 * Enqueue the Dx packet
 * Return Type: status
 * enqueue status
 */

static int enqueue_dx_packet(uint8_t *hci_cmd, int size)
{
	struct sk_buff *skb;

	trace_lbf_func_start(__func__);

	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	memcpy(skb_put(skb, size), hci_cmd, size);
	lbf_enqueue(skb);
	return 0;
}
/* send_d0_packet()
 * Send the D0 packet
 * Return Type: status
 * sending the d0 packet sending status
 */

static int send_d0_packet(void)
{
	int len = 0;
	int dev_st;

	trace_lbf_func_start(__func__);
	dev_st = lbf_get_device_state();
	if (dev_st == D0 || lbf_get_host_wake_state() == HIGH)
		return IGNORE;
	lbf_set_device_state(D2_TO_D0);
	len = lbf_tty_write((void *) lbf_tx->tty, hci_dx_wake,
			D0_HCI_CMD_SIZE);
	trace_lbf_ldisc_kpi(__func__, "BT CORE SEND D0 REQ");
	pr_debug("BT CORE SEND D0 REQ len:%d\n", len);
	if (len != D0_HCI_CMD_SIZE) {
		lbf_set_device_state(dev_st);
		len = enqueue_dx_packet(&hci_dx_wake[len],
				(D0_HCI_CMD_SIZE - len));
		intel_lbf_lpm.d0_packet_pending = (D0_HCI_CMD_SIZE - len);
	} else
		len = 0;

	trace_lbf_ldisc_sendpm(__func__, len);
	return len;
}

/* lpm_tx_update()
 * update the device and module states on tx  being received
 * Return Type: status
 * D0 packet sending status
 */

static int lpm_tx_update(int module)
{
	struct lbf_uart *lbf_ldisc;
	int ret = 0;

	trace_lbf_func_start(__func__);

	lbf_ldisc = (struct lbf_uart *) lbf_tx->tty->disc_data;
	mutex_lock(&intel_lbf_lpm.lpmtxlock);

check_refcount:
	switch (lbf_get_device_state()) {
	case D2:
	case D3:
		if (!lbf_get_tx_ref_count()) {
			lbf_set_tx_ref_count(1);
			lbf_serial_get();
		}
		disable_irq(intel_lbf_lpm.int_host_wake);
		ret = send_d0_packet();
		enable_irq(intel_lbf_lpm.int_host_wake);
		if (ret != IGNORE)
			ret = wait_dx_exit();
		else
			ret = 0;
		break;
	case D0_TO_D2:
		ret = wait_d0_exit();
		if (ret == 0)
			goto check_refcount;
		break;
	case D0:
		break;
	}

	if (ret == IGNORE && lbf_get_device_state() == D2) {
		ret = wait_d2_exit();
		if (ret == 0)
			goto check_refcount;
	}

	mutex_unlock(&intel_lbf_lpm.lpmtxlock);
	trace_lbf_ldisc_pm(__func__, ret);
	return ret;
}

/* lbf_ldisc_release_serial()
 * Reset all the device and module states
 * Return Type: void
 */

static void lbf_ldisc_release_serial(void)
{
	trace_lbf_func_start(__func__);

	if (lbf_get_rx_ref_count()) {
		lbf_serial_put();
		trace_lbf_ldisc_pmprocessing(__func__, "pm_put");
		lbf_set_rx_ref_count(-1);
	}
	if (lbf_get_tx_ref_count()) {
		lbf_serial_put();
		trace_lbf_ldisc_pmprocessing(__func__, "pm_put");
		lbf_set_tx_ref_count(-1);
	}

#if defined(CONFIG_SERIAL_HSU) || defined(CONFIG_SERIAL_8250)
	if (intel_lbf_lpm.gpio_host_wake > 0) {
		free_irq(intel_lbf_lpm.int_host_wake, NULL);
		gpio_free(intel_lbf_lpm.gpio_host_wake);
		pr_debug("<- Free the  %s\n", __func__);
	}
#endif

	pr_debug("<- %s\n", __func__);

}

/* lbf_ldisc_lpm_enable_init()
 * Initializzes all the device and module states
 * Return Type: void
 */

static void lbf_ldisc_lpm_enable_init(void)
{
	trace_lbf_func_start(__func__);

	if (intel_lbf_lpm.rx_refcount == 0) {
		lbf_serial_get();
		intel_lbf_lpm.rx_refcount = 1;
	}
	if (intel_lbf_lpm.tx_refcount == 0) {
		lbf_serial_get();
		intel_lbf_lpm.tx_refcount = 1;
	}
	intel_lbf_lpm.device_state = D0;

	intel_lbf_lpm.host_wake = HIGH;
	intel_lbf_lpm.bt_module_state = ACTIVE;
	intel_lbf_lpm.fm_module_state = ACTIVE;
	intel_lbf_lpm.bt_fmr_state = ACTIVE;

	trace_lbf_func_end(__func__);
}


/* lbf_ldisc_lpm_enable()
 * IOCTL to enable LPM in st driver
 * Return Type: int
 * status of LPM initialization
 */

static int lbf_ldisc_lpm_enable(unsigned long arg)
{
	int ret = -EINVAL;

	trace_lbf_func_start(__func__);

	mutex_lock(&intel_lbf_lpm.lpmenable);

	if (arg == ENABLE) {
		++intel_lbf_lpm.module_refcount;
		pr_debug("-> %s module refcount: %d\n", __func__,
				intel_lbf_lpm.module_refcount);
		if (arg == intel_lbf_lpm.lpm_enable) {
			mutex_unlock(&intel_lbf_lpm.lpmenable);
			return 0;
		}

		spin_lock_init(&intel_lbf_lpm.devicest_lock);
		spin_lock_init(&intel_lbf_lpm.host_wake_lock);
		spin_lock_init(&intel_lbf_lpm.ackrsp_lock);
		spin_lock_init(&intel_lbf_lpm.wakeuprsp_lock);
		mutex_init(&intel_lbf_lpm.lpmtxlock);
		mutex_init(&intel_lbf_lpm.idleupdatelock);
		spin_lock_init(&intel_lbf_lpm.rxref_lock);
		spin_lock_init(&intel_lbf_lpm.interrupt_lock);
		spin_lock_init(&intel_lbf_lpm.txref_lock);

		intel_lbf_lpm.lpm_enable = ENABLE;
#ifdef CONFIG_SERIAL_HSU
		activate_irq_handler();
#endif
		pr_debug("LPM enabled success\n");
	} else {

		--intel_lbf_lpm.module_refcount;
		pr_debug("-> %s module refcount: %d\n", __func__,
						intel_lbf_lpm.module_refcount);
		if (!intel_lbf_lpm.module_refcount) {
			lbf_ldisc_release_serial();
			intel_lbf_lpm.lpm_enable = DISABLE;
		}
	}

	mutex_unlock(&intel_lbf_lpm.lpmenable);
	pr_debug("<- %s\n", __func__);

	return ret;
}

/* lbf_ldisc_lpm_idle()
 * IOCTL to send module IDLE State from userspace
 * Return Type: void
 */

static int lbf_ldisc_lpm_idle(unsigned long arg)
{
	int ret = 0;

	trace_lbf_func_start(__func__);
	mutex_lock(&intel_lbf_lpm.idleupdatelock);
	lbf_tx_module_st(arg);

try_again:
	if (lbf_get_device_state() == D0) {
		lbf_tx_wakeup(NULL);
		trace_lbf_ldisc_pmprocessing(__func__, "Try to send idle");
	} else if (lbf_get_device_state() == D2_TO_D0) {
		trace_lbf_ldisc_pmprocessing(__func__, "wait until D0");
		ret = wait_dx_exit();
		goto try_again;
	}

	mutex_unlock(&intel_lbf_lpm.idleupdatelock);
	return ret;
}

#ifdef TEST_CODE
static int lbf_ldisc_hw_err(unsigned long arg)
{
	int ret = 0;

	pr_info("err code: %ld\n", arg);
	switch (arg) {
	case 1:
	{
	const u8 hci_hw_err1[] = {0x04, 0x10, 0x01, 0x01};

	lbf_ldisc_receive(lbf_tx->tty, hci_hw_err1, NULL, 4);
	}
	break;
	case 2:
	{
	const u8 hci_hw_err1[] = {0x04, 0x10, 0x01, 0x04};

	lbf_ldisc_receive(lbf_tx->tty, hci_hw_err1, NULL, 4);
	}
	break;
	case 3:
	{
	const u8 hci_hw_err1[] = {0x04, 0x10, 0x01, 0x07};

	lbf_ldisc_receive(lbf_tx->tty, hci_hw_err1, NULL, 4);
	}
	break;
	case 4:
	{
	const u8 hci_hw_err[] = {0x04, 0x10, 0x01, 0x08};

	lbf_ldisc_receive(lbf_tx->tty, hci_hw_err, NULL, 4);
	}
	break;
	case 5:
	{
	const u8 hci_hw_err1[] = {0x04, 0x10, 0x01, 0x0C};

	lbf_ldisc_receive(lbf_tx->tty, hci_hw_err1, NULL, 4);
	}
	break;
	case 6:
	{
	const u8 hci_hw_err[] = {0x04, 0x10, 0x01, 0x0D};

	lbf_ldisc_receive(lbf_tx->tty, hci_hw_err, NULL, 4);
	}
	break;
	case 7:
	{
	const u8 hci_hw_err1[] = {0x04, 0x10, 0x01, 0x0E};

	lbf_ldisc_receive(lbf_tx->tty, hci_hw_err1, NULL, 4);
	}
	break;
	default:
	ret = -1;
	pr_info("invalid\n");
	}
	return ret;
}

static int lbf_ldisc_bootup_err(unsigned long arg)
{
	int ret = 0;

	pr_info("err code: %ld\n", arg);
	switch (arg) {
	case 1:
	{
	const u8 hci_bootup_err[] = {0x04, 0xFF, 0x07, 0x02, 0x00, 0x20, 0x01,
							0x00, 0x03, 0x03};

	lbf_ldisc_receive(lbf_tx->tty, hci_bootup_err,
						NULL, 10);
	}
	break;
	case 2:
	{
	const u8 hci_bootup_err[] = {0x04, 0xFF, 0x07, 0x02, 0x00, 0x20, 0x01,
							0x00, 0x04, 0x03};

	lbf_ldisc_receive(lbf_tx->tty, hci_bootup_err, NULL, 10);
	}
	break;
	case 3:
	{
	const u8 hci_bootup_err[] = {0x04, 0xFF, 0x07, 0x02, 0x00, 0x20, 0x01,
							0x00, 0x05, 0x03};

	lbf_ldisc_receive(lbf_tx->tty, hci_bootup_err, NULL, 10);
	}
	break;
	case 4:
	{
	const u8 hci_bootup_err[] = {0x04, 0xFF, 0x07, 0x02, 0x00, 0x20, 0x00,
							0x00, 0x03, 0x03};

	lbf_ldisc_receive(lbf_tx->tty, hci_bootup_err, NULL, 10);
	}
	break;
	case 5:
	{
	const u8 hci_bootup_err[] = {0x04, 0xFF, 0x07, 0x02, 0x00, 0x20, 0x01,
							0x00, 0xFF, 0x03};

	lbf_ldisc_receive(lbf_tx->tty, hci_bootup_err, NULL, 10);
	}
	break;
	case 6:
	{
	const u8 hci_bootup_err[] = {0x04, 0xFF, 0x07, 0x02, 0x00, 0x20, 0x00,
							0x00, 0x04, 0x03};

	lbf_ldisc_receive(lbf_tx->tty, hci_bootup_err, NULL, 10);
	}
	break;
	case 7:
	{
	const u8 hci_bootup_err[] = {0x04, 0xFF, 0x07, 0x02, 0x00, 0x20, 0x00,
							0x00, 0x05, 0x03};

	lbf_ldisc_receive(lbf_tx->tty, hci_bootup_err, NULL, 10);
	}
	break;
	case 8:
	{
	const u8 hci_bootup_err[] = {0x04, 0xFF, 0x07, 0x02, 0x00, 0x20, 0x00,
							0x00, 0xFF, 0x03};

	lbf_ldisc_receive(lbf_tx->tty, hci_bootup_err, NULL, 10);
	}
	break;
	default:
	ret = -1;
	pr_info("invalid\n");
	}
	return ret;
}
#endif /* TEST_CODE */

/* lbf_ldisc_hw_reset()
 * IOCTL to do a hrad reset from independent module
 * Return Type: void
 */
static int lbf_ldisc_hw_reset(unsigned long arg)
{
	struct lbf_uart *lbf_uart;

	lbf_uart = (struct lbf_uart *) lbf_tx->tty->disc_data;
	trace_lbf_func_start(__func__);
	spin_lock(&lbf_uart->hw_reset_lock);
	if (fw_download_state == FW_SUCCESS) {
		bt_rfkill_set_power(DISABLE);
		fw_download_state = FW_BLANK;
		switch (arg) {
		case MODULE_HW_ERR:
			trace_lbf_ldisc_info(__func__,
				"H/W reset for Hardware error");
			break;
		case MODULE_CMD_TIMEOUT:
			if (!test_and_set_bit(HCI_CMD_TOUT,
						&lbf_uart->hci_cmd_st))
				lbf_send_netlink_message(lbf_tx->tty,
						BT_HCI_HW_ERR_REQUEST);
			/*set_bit(HCI_HW_ERR, &lbf_uart->hci_cmd_st);*/
			trace_lbf_ldisc_info(__func__,
					"H/W reset for Bluetooth cmd timeout");
			break;
		}
	}
	spin_unlock(&lbf_uart->hw_reset_lock);

	return 0;
}

/* lbf_ldisc_fw_download_complete()
 * unlock the Mutex to unblock the IOCTL on 2nd call
 * Return Type: void
 */
static void lbf_ldisc_fw_download_complete(unsigned long arg)
{

	struct lbf_uart *lbf_uart;

	lbf_uart = (struct lbf_uart *) lbf_tx->tty->disc_data;

	if (arg == FW_SUCCESS)
		fw_download_state = FW_SUCCESS;
	else if (arg == FW_FAILED) {
		fw_download_state = FW_FAILED;
		bt_rfkill_set_power(DISABLE);
	}

	intel_lbf_lpm.bt_module_state = IDLE;
	intel_lbf_lpm.fm_module_state = IDLE;
	intel_lbf_lpm.bt_fmr_state = IDLE;

	reset_buffer_flags(lbf_tx->tty);
	lbf_ldisc_free_skbuff(lbf_uart);
	mutex_unlock(&lbf_tx->fwdownloadlock);
}

/* st_wakeup_packet_processing()
 * Processes all the LPM packet
 * Return Type: void
 */


static void st_wakeup_packet_processing(struct lbf_uart *lbf_uart)
{
	spin_lock(&intel_lbf_lpm.interrupt_lock);
	if (lbf_uart->rx_skb->data[0] != LPM_PKT) {
		spin_unlock(&intel_lbf_lpm.interrupt_lock);
		return;
	}
	switch (lbf_uart->rx_skb->data[1]) {
	case ACK_RSP:
		trace_lbf_ldisc_kpi(__func__, "BT CORE IN D2 STATE");
		if (lbf_get_ackigne_st() == false &&
				lbf_get_device_state() != D0) {
			lbf_set_host_wake_state(IDLE);
			/*Work around for not receiving Idle
				notification before interrupt*/
			lbf_set_device_state(D2);
			if (lbf_get_tx_ref_count()) {
				lbf_set_tx_ref_count(-1);
				lbf_serial_put();
			}
			if (lbf_get_rx_ref_count()) {
				lbf_set_rx_ref_count(-1);
				lbf_serial_put();
			}
		}
		lbf_set_ackign_st(false);
		enable_irq(intel_lbf_lpm.int_host_wake);
		wake_up_interruptible(&waitFord0exit);
		break;
	case WAKE_UP_RSP:
		if (lbf_get_wakeupign_st() == false &&
				lbf_get_device_state() == D2_TO_D0) {
			trace_lbf_ldisc_kpi(__func__, "BT CORE IN D0 STATE");
			lbf_set_device_state(D0);
			if (!lbf_get_rx_ref_count()) {
				lbf_set_rx_ref_count(1);
				schedule_work(&intel_lbf_lpm.host_enable_work);
			}
			wake_up_interruptible(&waitFordxexit);
		}
		lbf_set_wakeupign_st(false);
		break;
	default:
		break;
	}
	spin_unlock(&intel_lbf_lpm.interrupt_lock);
	lbf_update_set_room(lbf_tx->tty, WAKEUP_RSP_SIZE);
}

/* st_check_data_len()
 * push the skb received to relevant
 * protocol stacks
 * Arguments : tty pointer to associated tty instance data
 *lbf_uart : Disc data for tty
 *len : lenn of data
 * Return Type: void
 */

static inline int st_check_data_len(struct lbf_uart *lbf_uart, int len)
{
	int room = skb_tailroom(lbf_uart->rx_skb);

	if (!len) {
		/* Received packet has only packet header and
		 * has zero length payload. So, ask ST CORE to
		 * forward the packet to protocol driver (BT/FM/GPS)
		 */
		 if (intel_lbf_lpm.lpm_enable == ENABLE) {
			if (lbf_uart->rx_skb->data[0] == HCI_COMMAND_PKT)
				st_send_frame(lbf_uart->tty, lbf_uart);
			else {
				st_wakeup_packet_processing(lbf_uart);
				goto pm_packet;
			}
		} else {
			st_send_frame(lbf_uart->tty, lbf_uart);
			pr_err("Should not be hit\n");
		}

	} else if (len > room) {
		/* Received packet's payload length is larger.
		 * We can't accommodate it in created skb.
		 */
		pr_debug("Data length is too large len %d room %d", len, room);
		kfree_skb(lbf_uart->rx_skb);
	} else {
		/* Packet header has non-zero payload length and
		 * we have enough space in created skb. Lets read
		 * payload data */

		lbf_uart->rx_state = LBF_W4_DATA;
		lbf_uart->rx_count = len;

		return len;
	}

	/* Change LDISC state to continue to process next
	 * packet */
pm_packet:
	lbf_ldisc_free_skbuff(lbf_uart);
	return 0;
}

/**
 * lbf_update_set_room - receive space
 * @tty: terminal
 *
 * Called by the driver to find out how much data it is
 * permitted to feed to the line discipline without any being lost
 * and thus to manage flow control. Not serialized. Answers for the
 * "instant".
 */

static void lbf_update_set_room(struct tty_struct *tty, signed int cnt)
{
	struct lbf_uart *lbf_uart = (struct lbf_uart *) tty->disc_data;
	int left;
	int old_left;

	spin_lock(&lbf_uart->tx_update_lock);
	left = tty->receive_room + cnt;

	if (left < 0)
		left = 0;
	old_left = tty->receive_room;
	tty->receive_room = left;
	spin_unlock(&lbf_uart->tx_update_lock);

	trace_lbf_ldisc_room(__func__, tty->receive_room);

	if (left && !old_left) {
		WARN_RATELIMIT(tty->port->itty == NULL,
				"scheduling with invalid itty\n");
		/* see if ldisc has been killed - if so, this means that
		 * even though the ldisc has been halted and ->buf.work
		 * cancelled, ->buf.work is about to be rescheduled
		 */
		WARN_RATELIMIT(test_bit(TTY_LDISC_HALTED, &tty->flags),
				"scheduling buffer work for halted ldisc\n");
		schedule_work(&tty->port->buf.work);
	}
}

static inline int packet_state(int exp_count, int recv_count)
{
	int status = 0;

	if (exp_count > recv_count)
		status = exp_count - recv_count;

	return status;
}

/* lbf_ldisc_receive()
 *
 * Called by tty low level driver when receive data is
 * available.
 *
 * Arguments: tty pointer to tty isntance data
 * data pointer to received data
 * flags pointer to flags for data
 * count count of received data in bytes
 *
 * Return Value: None
 */
static void lbf_ldisc_receive(struct tty_struct *tty, const u8 *cp, char *fp,
		int count)
{
	unsigned char *ptr;
	int proto = INVALID;
	int pkt_status;
	unsigned short payload_len = 0;
	int len = 0, type = 0;
	unsigned char *plen;
	int st_ret = 0;
	struct lbf_uart *lbf_uart = (struct lbf_uart *) tty->disc_data;

	trace_lbf_func_start(__func__);
	print_hex_dump_debug(">in>", DUMP_PREFIX_NONE, 16, 1, cp, count,
			0);
	ptr = (unsigned char *) cp;

	if (unlikely(ptr == NULL) || unlikely(lbf_uart == NULL)) {
		pr_debug(" received null from TTY ");
		return;
	}

	spin_lock(&lbf_uart->rx_lock);
	lbf_update_set_room(tty, (-1)*count);

	trace_lbf_ldisc_receive(__func__, lbf_uart->rx_state);

	while (count) {
		if (likely(lbf_uart->bytes_pending)) {
			len = min_t(unsigned int, lbf_uart->bytes_pending,
				 count);
			lbf_uart->rx_count = lbf_uart->bytes_pending;
			memcpy(skb_put(lbf_uart->rx_skb, len),
					ptr, len);
		} else if ((lbf_uart->rx_count)) {
			len = min_t(unsigned int, lbf_uart->rx_count, count);
			memcpy(skb_put(lbf_uart->rx_skb, len), ptr, len);
		}

		switch (lbf_uart->rx_state) {
		case LBF_W4_H4_HDR:
			if (*ptr != 0x01 && *ptr != 0x02 && *ptr != 0x03 &&
				*ptr != 0x04 && *ptr != LPM_PKT) {
				pr_debug(" Discard a byte 0x%x\n", *ptr);
				lbf_update_set_room(tty, 1);
				ptr++;
				count = count - 1;
				continue;
			}
			switch (*ptr) {
			default:
				type = *ptr;
				proto = select_proto(type);
				if (type == LPM_PKT)
					proto = 0;
				if (proto != INVALID) {
					lbf_uart->rx_skb = alloc_skb(1700,
						GFP_ATOMIC);
					if (!lbf_uart->rx_skb) {
						goto buff_fail;
					}
					lbf_uart->rx_chnl = proto;
					lbf_uart->rx_state = LBF_W4_PKT_HDR;
					lbf_uart->rx_count =
					lbf_st_proto[lbf_uart->rx_chnl].hdr_len + 1;
				} else {
					pr_err("Invalid header: 0x%x\n", type);
					count = count - 1;
					ptr++;
					lbf_update_set_room(tty, 1);
				}
				continue;
			}
			break;

		case LBF_W4_PKT_HDR:
			pkt_status = packet_state(lbf_uart->rx_count, count);
			trace_lbf_ldisc_receive(__func__, lbf_uart->rx_count);
			count -= len;
			ptr += len;
			lbf_uart->rx_count -= len;
			if (pkt_status) {
				lbf_uart->bytes_pending = pkt_status;
				count = 0;
			} else {
				plen = &lbf_uart->rx_skb->data
			[lbf_st_proto[lbf_uart->rx_chnl].offset_len_in_hdr];
				lbf_uart->bytes_pending = 0;
				if (lbf_st_proto[lbf_uart->rx_chnl].len_size
					== 1)
					payload_len = *(unsigned char *)plen;
				else if (lbf_st_proto[lbf_uart->rx_chnl].
						len_size == 2)
					payload_len =
					__le16_to_cpu(*(unsigned short *)plen);
				else
					pr_debug("invalid len for id %d\n",
							proto);

				st_check_data_len(lbf_uart, payload_len);
				}
			continue;

		case LBF_W4_DATA:
			pkt_status = packet_state(lbf_uart->rx_count, count);
			count -= len;
			ptr += len;
			lbf_uart->rx_count -= len;
			if (!pkt_status) {
				pr_debug("\n--Complete pkt received--\n");
				lbf_uart->rx_state = LBF_W4_H4_HDR;
				st_ret = st_send_frame(tty, lbf_uart);
				lbf_uart->bytes_pending = 0;
				lbf_uart->rx_count = 0;
				lbf_uart->rx_skb = NULL;
			} else {
				lbf_uart->bytes_pending = pkt_status;
				count = 0;
			}

			if (st_ret > 0)
				lbf_update_set_room(tty, st_ret);
			continue;

		} /* end of switch rx_state*/

	} /* end of while*/

buff_fail:
	spin_unlock(&lbf_uart->rx_lock);

	if (waitqueue_active(&lbf_uart->read_wait) && st_ret == 0)
		wake_up_interruptible(&lbf_uart->read_wait);


	if (tty->receive_room < TTY_THRESHOLD_THROTTLE)
		tty_throttle(tty);
}

/* lbf_ldisc_ioctl()
 *
 * Process IOCTL system call for the tty device.
 *
 * Arguments:
 *
 * tty pointer to tty instance data
 * file pointer to open file object for device
 * cmd IOCTL command code
 * arg argument for IOCTL call (cmd dependent)
 *
 * Return Value: Command dependent
 */
static int lbf_ldisc_ioctl(struct tty_struct *tty, struct file *file,
		unsigned int cmd, unsigned long arg)
{

	int err = 0;

	trace_lbf_func_start(__func__);
	switch (cmd) {
	case BT_FW_DOWNLOAD_INIT:
		trace_lbf_ldisc_ioctl(__func__, "BT_FW_DOWNLOAD_INIT");
		err = lbf_ldisc_fw_download_init();
		bt_self_test_running = 0;
		break;
	case BT_FW_DOWNLOAD_COMPLETE:
		trace_lbf_ldisc_ioctl(__func__, "BT_FW_DOWNLOAD_COMPLETE");
		lbf_ldisc_fw_download_complete(arg);
		break;
	case BT_FMR_LPM_ENABLE:
		trace_lbf_ldisc_ioctl(__func__, "BT_FMR_LPM_ENABLE");
		lbf_ldisc_lpm_enable(arg);
		break;
	case BT_FMR_IDLE:
		trace_lbf_ldisc_ioctl(__func__, "BT_FMR_IDLE");
		lbf_ldisc_lpm_idle(arg);
		break;
	case BT_FMR_HW_RESET:
		trace_lbf_ldisc_ioctl(__func__, "BT_FMR_HW_RESET");
		lbf_ldisc_hw_reset(arg);
		break;
#ifdef TEST_CODE
	case BT_FMR_HW_ERR:
		trace_lbf_ldisc_ioctl(__func__, "BT_FMR_HW_ERR");
		lbf_ldisc_hw_err(arg);
		break;
	case BT_FMR_BOOTUP_ERR:
		trace_lbf_ldisc_ioctl(__func__, "BT_FMR_BOOTUP_ERR");
		lbf_ldisc_bootup_err(arg);
		break;
#endif
	case BT_HOST_WAKE_INTR_COUNT:
		trace_lbf_ldisc_ioctl(__func__, "BT_HOST_WAKE_INTR_COUNT");
		bt_self_test_running = 1;
		err = host_wake_intr_count;
		break;
	case BT_HOST_WAKE_INTR_COUNT_RESET:
		trace_lbf_ldisc_ioctl(__func__,
		"BT_HOST_WAKE_INTR_COUNT_RESET");
		bt_self_test_running = 1;
		host_wake_intr_count = 0;
		break;
	default:
		trace_lbf_ldisc_ioctl(__func__, "DEFAULT");
		err = n_tty_ioctl_helper(tty, file, cmd, arg);
	}
	return err;
}

/* lbf_ldisc_compat_ioctl()
 *
 * Process IOCTL system call for the tty device.
 *
 * Arguments:
 *
 * tty pointer to tty instance data
 * file pointer to open file object for device
 * cmd IOCTL command code
 * arg argument for IOCTL call (cmd dependent)
 *
 * Return Value: Command dependent
 */
static long lbf_ldisc_compat_ioctl(struct tty_struct *tty, struct file *file,
		unsigned int cmd, unsigned long arg)
{

	int err = 0;

	trace_lbf_func_start(__func__);
	switch (cmd) {
	case BT_FW_DOWNLOAD_INIT:
		trace_lbf_ldisc_ioctl(__func__, "BT_FW_DOWNLOAD_INIT");
		err = lbf_ldisc_fw_download_init();
		bt_self_test_running = 0;
		break;
	case BT_FW_DOWNLOAD_COMPLETE:
		trace_lbf_ldisc_ioctl(__func__, "BT_FW_DOWNLOAD_COMPLETE");
		lbf_ldisc_fw_download_complete(arg);
		break;
	case BT_FMR_LPM_ENABLE:
		trace_lbf_ldisc_ioctl(__func__, "BT_FMR_LPM_ENABLE");
		lbf_ldisc_lpm_enable(arg);
		break;
	case BT_FMR_IDLE:
		trace_lbf_ldisc_ioctl(__func__, "BT_FMR_IDLE");
		lbf_ldisc_lpm_idle(arg);
		break;
	case BT_FMR_HW_RESET:
		trace_lbf_ldisc_ioctl(__func__, "BT_FMR_HW_RESET");
		lbf_ldisc_hw_reset(arg);
		break;
	case BT_HOST_WAKE_INTR_COUNT:
		trace_lbf_ldisc_ioctl(__func__, "BT_HOST_WAKE_INTR_COUNT");
		bt_self_test_running = 1;
		err = host_wake_intr_count;
		break;
	case BT_HOST_WAKE_INTR_COUNT_RESET:
		trace_lbf_ldisc_ioctl(__func__,
		"BT_HOST_WAKE_INTR_COUNT_RESET");
		bt_self_test_running = 1;
		host_wake_intr_count = 0;
		break;
	default:
		err = n_tty_ioctl_helper(tty, file, cmd, arg);
	}
	return err;
}

/* copy_from_read_buf()
 *
 * Internal function for lbf_ldisc_read().
 *
 * Arguments:
 *
 * tty pointer to tty instance data
 * buf buf to copy to user space
 * nr number of bytes to be read
 *
 * Return Value: number of bytes copy to user spcae successfully
 */
static int copy_from_read_buf(struct tty_struct *tty,
		unsigned char __user **b,
		size_t *nr)
{
	int retval = 0;
	size_t n;
	struct lbf_uart *lbf_ldisc = (struct lbf_uart *)tty->disc_data;

	spin_lock(&lbf_ldisc->tx_lock);
	n = min3(lbf_ldisc->read_cnt,
		 (N_INTEL_LDISC_BUF_SIZE - lbf_ldisc->read_tail), (int)*nr);
	spin_unlock(&lbf_ldisc->tx_lock);
	if (n) {
		retval =
		copy_to_user(*b, &lbf_ldisc->read_buf[lbf_ldisc->read_tail], n);
		n -= retval;
		spin_lock(&lbf_ldisc->tx_lock);
		lbf_ldisc->read_tail =
		(lbf_ldisc->read_tail + n) & (N_INTEL_LDISC_BUF_SIZE-1);
		lbf_ldisc->read_cnt -= n;
		spin_unlock(&lbf_ldisc->tx_lock);
		*b += n;
	}
	return n;
}

/* lbf_ldisc_get_read_cnt()
 *
 * Gets the read count available currently
 *
 * Arguments:
 *
 * void
 *
 * Return Value: number of bytes available to be copied to user space
 *
 */
static unsigned int lbf_ldisc_get_read_cnt(void)
{
	int read_cnt = 0;
	struct lbf_uart *lbf_ldisc = (struct lbf_uart *)lbf_tx->tty->disc_data;

	spin_lock(&lbf_ldisc->tx_lock);
	read_cnt = lbf_ldisc->read_cnt;
	spin_unlock(&lbf_ldisc->tx_lock);
	return read_cnt;
}

/* lbf_ldisc_read()
 *
 * Process read system call for the tty device.
 *
 * Arguments:
 *
 * tty pointer to tty instance datan
 * file pointer to open file object for device
 * buf buf to copy to user space
 * nr number of bytes to be read
 *
 * Return Value: number of bytes read copied to user space
 * successfully
 */
static ssize_t lbf_ldisc_read(struct tty_struct *tty, struct file *file,
		unsigned char __user *buf, size_t nr)
{
	unsigned char __user *b = buf;
	ssize_t size;
	int retval = 0;
	int state = -1;
	struct lbf_uart *lbf_uart = (struct lbf_uart *)tty->disc_data;

	trace_lbf_func_start(__func__);
	init_waitqueue_entry(&lbf_uart->wait, current);

	if (!lbf_ldisc_get_read_cnt()) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (!(file->f_flags & O_NONBLOCK)) {
			if (mutex_lock_interruptible(
				&lbf_uart->atomic_read_lock))
				return -ERESTARTSYS;
			if (!(mutex_lock_interruptible(
				&lbf_uart->atomic_read_lock)))
				state = 0;
			}
	}

	add_wait_queue(&lbf_uart->read_wait, &lbf_uart->wait);
	while (nr) {
		if (lbf_ldisc_get_read_cnt() > 0) {
			int copied;

			__set_current_state(TASK_RUNNING);
			/* The copy function takes the read lock and handles
			 locking internally for this case */
			copied = copy_from_read_buf(tty, &b, &nr);
			copied += copy_from_read_buf(tty, &b, &nr);
			if (copied) {
				retval = 0;
				if (lbf_uart->read_cnt <=
					TTY_THRESHOLD_UNTHROTTLE) {
					check_unthrottle(tty);
				}
			break;
			}
		} else {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			continue;
		}
	}

	if (state == 0)
		mutex_unlock(&lbf_uart->atomic_read_lock);


	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&lbf_uart->read_wait, &lbf_uart->wait);

	size = b - buf;
	if (size)
		retval = size;

	lbf_update_set_room(tty, retval);
	trace_lbf_ldisc_read(__func__, retval);
	return retval;
}

/* lbf_ldisc_write()
 *
 * Process Write system call from user Space.
 *
 * Arguments:
 *
 * tty pointer to tty instance data
 * file pointer to open file object for device
 * buf data to pass down
 * nr number of bytes to be written down
 *
 * Return Value: no of bytes written successfully
 */
static ssize_t lbf_ldisc_write(struct tty_struct *tty, struct file *file,
		const unsigned char *data, size_t count)
{
	struct sk_buff *skb = NULL;
	int len = 0;
	int ret = 0;
	int status = 0;
	unsigned int opcode = 0;
	const unsigned char *buff = data;
	struct lbf_uart *lbf_uart = (struct lbf_uart *)tty->disc_data;

	trace_lbf_func_start(__func__);

	print_hex_dump_debug("<BT<", DUMP_PREFIX_NONE, 16, 1, data,
			count, 0);
	pr_debug("lbf_uart->hci_reset: %ld\n", lbf_uart->hci_reset);
	if (test_bit(HCI_REST_CMD, &lbf_uart->hci_reset) ||
		test_bit(HCI_HW_ERR, &lbf_uart->hci_cmd_st) ||
		test_bit(HCI_CMD_TOUT, &lbf_uart->hci_cmd_st))
		goto write_fail;

	STREAM_TO_UINT16_OPCODE(opcode, buff);
	pr_debug("opcode: 0x%x", opcode);

	if (intel_lbf_lpm.lpm_enable == ENABLE) {
		spin_lock(&intel_lbf_lpm.lpm_modulestate);
		intel_lbf_lpm.bt_module_state = ACTIVE;
		intel_lbf_lpm.bt_fmr_state = ACTIVE;
		spin_unlock(&intel_lbf_lpm.lpm_modulestate);
		ret = lpm_tx_update(MODULE_BT);
	}

	if (ret == 0 && BT_HCI_RESET == opcode) {
		pr_debug("Reset cmd is receive\n");
		status = lbf_send_netlink_message(tty,  BT_HCI_RESET_REQUEST);
		if (status == INVALID)
			goto send_immediately;
		trace_lbf_ldisc_reset(__func__, "LBF_LDISC start timer");
		hrtimer_start(&lbf_uart->hci_reset_timer,
		lbf_uart->hci_reset_delay, HRTIMER_MODE_REL);
		set_bit(HCI_REST_CMD, &lbf_uart->hci_reset);
		clear_bit(HCI_RESET_COMPL_RCV, &lbf_uart->hci_reset);
		clear_bit(HCI_RESET_SEND, &lbf_uart->hci_reset);
		pr_debug("lbf_uart->hci_reset: %ld\n", lbf_uart->hci_reset);
		return count;
	}

send_immediately:
	if (ret == 0) {
		skb = alloc_skb(count + 1, GFP_ATOMIC);
		if (!skb) {
			len = -ENOMEM;
			goto write_fail;
		}
		memcpy(skb_put(skb, count), data, count);
		lbf_enqueue(skb);
		len = count;
		lbf_tx_wakeup(NULL);
	} else
		lbf_tx_module_st(MODULE_BT);

write_fail:
	trace_lbf_ldisc_write(__func__, len);
	return len;
}

/* lbf_ldisc_poll()
 *
 * Process POLL system call for the tty device.
 *
 * Arguments:
 *
 * tty pointer to tty instance data
 * file pointer to open file object for device
 *
 * Return Value: mask of events registered with this poll call
 */
static unsigned int lbf_ldisc_poll(struct tty_struct *tty, struct file *file,
		poll_table *wait)
{
	unsigned int mask = 0;
	struct lbf_uart *lbf_uart = (struct lbf_uart *) tty->disc_data;

	trace_lbf_func_start(__func__);

	if (lbf_uart->read_cnt > 0)
		mask |= POLLIN | POLLRDNORM;
	else {
		poll_wait(file, &lbf_uart->read_wait, wait);
		if (lbf_uart->read_cnt > 0)
			mask |= POLLIN | POLLRDNORM;
		if (tty->packet && tty->link->ctrl_status)
			mask |= POLLPRI | POLLIN | POLLRDNORM;
		if (test_bit(TTY_OTHER_CLOSED, &tty->flags))
			mask |= POLLHUP;
		if (tty_hung_up_p(file))
			mask |= POLLHUP;
		if (tty->ops->write && !tty_is_writelocked(tty)
				&& tty_chars_in_buffer(tty) < 256
				&& tty_write_room(tty) > 0)
			mask |= POLLOUT | POLLWRNORM;
	}
	trace_lbf_ldisc_poll(__func__, mask);
	return mask;
}

/* lbf_ldisc_set_termios()
 *
 * It notifies the line discpline that a change has been made to the
 * termios structure
 *
 * Arguments:
 *
 * tty pointer to tty instance data
 * file pointer to open file object for device
 *
 * Return Value: void
 */
static void lbf_ldisc_set_termios(struct tty_struct *tty, struct ktermios *old)
{
	unsigned int cflag;

	cflag = tty->termios.c_cflag;
	trace_lbf_func_start(__func__);

	if (cflag & CRTSCTS)
		pr_debug(" - RTS/CTS is enabled\n");
	else
		pr_debug(" - RTS/CTS is disabled\n");
}
#ifdef CONFIG_ACPI
static int bt_gpio_init(struct device *dev, struct intel_bt_gpio_desc *desc)
{
	int ret = 0;
	struct gpio_desc *gpiod;

	if (!desc)
		gpiod = devm_gpiod_get_index(dev, "bt_reg_on", 1);
	else if (desc->reg_on_idx >= 0) {
		pr_info("%s: reg_on_idx=%d, host_wake_idx=%d",
				__func__, desc->reg_on_idx,
				desc->host_wake_idx);
		gpiod = devm_gpiod_get_index(dev, "bt_reg_on",
		desc->reg_on_idx);
	} else
		gpiod = NULL;

	if (gpiod && !IS_ERR(gpiod)) {
		intel_lbf_lpm.gpiod_enable_bt = gpiod;
		pr_info("%s: gpio enable_bt:%d\n", __func__,
				desc_to_gpio(intel_lbf_lpm.gpiod_enable_bt));
		ret = gpiod_direction_output(intel_lbf_lpm.gpiod_enable_bt, 0);
		if (ret) {
			pr_err("could not set gpiod_enable_bt as output");
			return ret;
		}
	} else
		pr_err("Failed to get bt_reg_on gpio\n");

	if (desc && (desc->host_wake_idx >= 0)) {
		gpiod = devm_gpiod_get_index(dev, "bt_host_wake",
					    desc->host_wake_idx);
		if (!IS_ERR(gpiod)) {
			intel_lbf_lpm.gpio_host_wake = desc_to_gpio(gpiod);
			pr_info("%s: gpio_host_wake:%d\n",
					__func__, intel_lbf_lpm.gpio_host_wake);
			ret = gpiod_direction_input(gpiod);
			if (ret)
				return ret;

			intel_lbf_lpm.int_host_wake = gpiod_to_irq(gpiod);
			pr_info("%s: int_host_wake:%d\n",
					__func__, intel_lbf_lpm.int_host_wake);

			if (ret)
				return ret;

		}
	} else {
		intel_lbf_lpm.gpio_host_wake = -1;
		pr_err("Failed to get bt_host_wake gpio\n");
	}
	return 0;
}
static int intel_bt_acpi_probe(struct device *dev)
{

	const struct acpi_device_id *id;
	struct intel_bt_gpio_desc *desc;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id) {
		pr_debug("acpi id didn't match");
		return -ENODEV;
	}

	desc = (struct intel_bt_gpio_desc *)id->driver_data;
	if (!desc) {
		pr_debug("acpi desc didn't match");
		return -ENODEV;
	}
	pr_info("%s: found table %s\n", __func__, id->id);

	return bt_gpio_init(dev, desc);
}
#endif
#ifdef CONFIG_SERIAL_XGOLD
static int intel_bluetooth_pdata_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	int ret = 0;

	trace_lbf_func_start(__func__);
	if (!dn) {
		pr_err("cann't find matching node\n");
		ret = -EINVAL;
		goto error;
	}

	intel_lbf_lpm.gpio_enable_bt = of_get_named_gpio_flags(dn,
					PROP_LNP_BT_GPIO_ENB, 0, NULL);
	if (intel_lbf_lpm.gpio_enable_bt <= 0) {
		pr_err("Can't find node %s\n", PROP_LNP_BT_GPIO_ENB);
		goto error;
	} else
		pr_debug("bt_fmr %d\n", intel_lbf_lpm.gpio_enable_bt);
	/* Register wake interrupt */
	intel_lbf_lpm.pdev = pdev;
	intel_lbf_lpm.int_host_wake =
		platform_get_irq_byname(pdev, WAKEUP_HOST);
	if (!IS_ERR_VALUE(intel_lbf_lpm.int_host_wake))
		pr_info("%s irq found success\n", WAKEUP_HOST);
	else
		pr_err("no %s irq found\n", WAKEUP_HOST);

	device_init_wakeup(&pdev->dev, true);

	ret = lbf_get_usif_dev();
	if (!ret)
		ret = activate_irq_handler();

	return ret;

error:
	pr_err("%s:Error in probe\n", __func__);
	return ret;
}
#endif

static void reset_serial_baud(void)
{
	struct tty_struct *tty;
	struct ktermios ktermios;

	trace_lbf_func_start(__func__);
	tty = (struct tty_struct *) lbf_tx->tty;
	tty->driver->ops->tiocmget(tty);
	memcpy(&ktermios, &tty->termios, sizeof(ktermios));
	ktermios.c_cflag |= B115200;
	ktermios.c_ospeed = ktermios.c_ispeed = 115200;
	tty_set_termios(tty, &ktermios);
}


static void bt_rfkill_set_power(unsigned long gpio_st)
{
	if (ENABLE == gpio_st) {
		reset_serial_baud();
#ifdef CONFIG_SERIAL_XGOLD
		gpio_set_value(intel_lbf_lpm.gpio_enable_bt, 1);
#endif
#ifdef CONFIG_ACPI
		gpiod_set_value(intel_lbf_lpm.gpiod_enable_bt, 1);
#endif
		trace_lbf_ldisc_kpi(__func__, "BT CORE IN D0 STATE");
		pr_debug("BT CORE IN D0 STATE\n");
		bt_enable_state = ENABLE;
		if (intel_lbf_lpm.lpm_enable == ENABLE)
			lbf_ldisc_lpm_enable_init();
	} else if (DISABLE == gpio_st) {
#ifdef CONFIG_SERIAL_XGOLD
		gpio_set_value(intel_lbf_lpm.gpio_enable_bt, 0);
#endif
#ifdef CONFIG_ACPI
		gpiod_set_value(intel_lbf_lpm.gpiod_enable_bt, 0);
#endif
		trace_lbf_ldisc_kpi(__func__, "BT CORE IN D3 STATE");
		pr_debug("BT CORE IN D3 STATE\n");
		bt_self_test_running = 0;
		bt_enable_state = DISABLE;
	}
}


static int intel_lpm_bluetooth_probe(struct platform_device *pdev)
{
	int default_state = 0;	/* off */
	int ret = 0;

	trace_lbf_func_start(__func__);
	pr_info("BT DRIVER LOADING STARTED");
#ifdef CONFIG_ACPI
	struct intel_bt_gpio_data *bt_gpio;

	bt_gpio = devm_kzalloc(&pdev->dev, sizeof(*bt_gpio), GFP_KERNEL);
	if (!bt_gpio) {
		ret = -ENOMEM;
		pr_err("%s bt_gpio allocation fails", __func__);
		goto err_data_probe;
	}

	platform_set_drvdata(pdev, bt_gpio);
	intel_lbf_lpm.pdev = pdev;

	if (ACPI_HANDLE(&pdev->dev)) {
		ret = intel_bt_acpi_probe(&pdev->dev);
		if (ret) {
			pr_err("%s intel_bt_acpi_probe returns", __func__);
			goto err_data_probe;
			}

		bt_rfkill_set_power(default_state);

	} else
		ret = -ENODEV;

	ret = intel_bt_lpm_init(intel_lbf_lpm.pdev);
	if (ret)
		return -EINVAL;
#endif
/* If Device tree Structure is supported*/
#ifdef CONFIG_OF
	if (pdev == NULL) {
		ret = -EINVAL;
		goto err_data_probe;
		pr_err("%s pdev is NULL", __func__);
	} else
		intel_lbf_lpm.tty_dev = &pdev->dev;

	ret = intel_bluetooth_pdata_probe(pdev);

	if (ret < 0) {
		pr_err("%s: Cannot register platform data\n", __func__);
		ret = -EINVAL;
		goto err_data_probe;
	}

	ret = gpio_request(intel_lbf_lpm.gpio_enable_bt, "bt-reset");
	if (ret < 0) {
		pr_err("%s: Unable to request gpio %d\n", __func__,
				intel_lbf_lpm.gpio_enable_bt);
		ret = -EINVAL;
		goto err_gpio_enable_req;
	} else
		pr_err("%s: succeeded to request gpio\n", __func__);

	ret = gpio_direction_output(intel_lbf_lpm.gpio_enable_bt, 0);
	if (ret < 0) {
		pr_err("Unable to set direction for gpio %d\n",
				intel_lbf_lpm.gpio_enable_bt);
		ret = -EINVAL;
		goto err_gpio_enable_dir;
	} else
		pr_err("%s: succeded to request direction gpio\n", __func__);

	pr_debug("%s: bt_gpio:%d/n", __func__, intel_lbf_lpm.gpio_enable_bt);

	bt_rfkill_set_power(default_state);
	return ret;

err_gpio_enable_dir:
	gpio_free(intel_lbf_lpm.gpio_enable_bt);
err_gpio_enable_req:
#endif
	pr_info("%s BT DRIVER LOADING ENDS, ret:%d", __func__, ret);
err_data_probe:
	return ret;
}

static int intel_lpm_bluetooth_remove(struct platform_device *pdev)
{
	trace_lbf_func_start(__func__);
#ifdef CONFIG_SERIAL_XGOLD
	device_init_wakeup(&pdev->dev, false);
	gpio_free(intel_lbf_lpm.gpio_enable_bt);
#endif
	return 0;
}


static int intel_lpm_bluetooth_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	trace_lbf_func_start(__func__);

	intel_lbf_lpm.platform_state = S3;
	if (intel_lbf_lpm.int_host_wake && device_may_wakeup(&pdev->dev))
		enable_irq_wake(intel_lbf_lpm.int_host_wake);

	return 0;
}

static int intel_lpm_bluetooth_resume(struct platform_device *pdev)
{
	trace_lbf_func_start(__func__);
	intel_lbf_lpm.platform_state = S0;
	if (intel_lbf_lpm.int_host_wake && device_may_wakeup(&pdev->dev))
		disable_irq_wake(intel_lbf_lpm.int_host_wake);
	if (intel_lbf_lpm.interrupt == true) {
		if (!lbf_get_rx_ref_count()) {
			lbf_set_rx_ref_count(1);
			pm_runtime_get_sync(intel_lbf_lpm.tty_dev);
		}
		hostwake_high_processing();
	}
	intel_lbf_lpm.interrupt = false;

	return 0;
}


/*Line discipline op*/
static struct tty_ldisc_ops lbf_ldisc = {
	.magic = TTY_LDISC_MAGIC,
	.name = "n_ld_intel",
	.open = lbf_ldisc_open,
	.close = lbf_ldisc_close,
	.read = lbf_ldisc_read,
	.write = lbf_ldisc_write,
	.ioctl = lbf_ldisc_ioctl,
	.compat_ioctl = lbf_ldisc_compat_ioctl,
	.poll = lbf_ldisc_poll,
	.receive_buf = lbf_ldisc_receive,
	.write_wakeup = lbf_ldisc_wakeup,
	.flush_buffer = lbf_ldisc_flush_buffer,
	.set_termios = lbf_ldisc_set_termios,
	.owner = THIS_MODULE
};

static const struct of_device_id intel_bluetooth_of_match[] = {
	{.compatible = "intel,bt-fmr",},
	{},
};

static struct platform_driver intel_bluetooth_platform_driver = {
	.probe = intel_lpm_bluetooth_probe,
	.remove = intel_lpm_bluetooth_remove,
	.suspend = intel_lpm_bluetooth_suspend,
	.resume = intel_lpm_bluetooth_resume,
	.driver = {
		.name = "intel_bt_lpm",
		.owner = THIS_MODULE,
		.of_match_table = intel_bluetooth_of_match,
#ifdef CONFIG_ACPI
		.acpi_match_table = ACPI_PTR(intel_id_table),
#endif
		},
};

static int __init lbf_uart_init(void)
{
	int err = 0;

	trace_lbf_func_start(__func__);

	err = platform_driver_register(&intel_bluetooth_platform_driver);
	if (err) {
		pr_err("Registration failed. (%d)", err);
		goto err_platform_register;
	} else
		pr_debug("%s: Plarform registration succeded\n", __func__);

	/* Register the tty discipline */

	err = tty_register_ldisc(N_INTEL_LDISC, &lbf_ldisc);
	if (err) {
		pr_err("Line Discipline Registration failed. (%d)", err);
		goto err_tty_register;
	} else
		pr_debug("%s: N_INTEL_LDISC registration succeded\n", __func__);

	trace_lbf_ldisc_call(__func__, err);

	return err;

	trace_lbf_ldisc_call(__func__, err);
err_tty_register:
	platform_driver_unregister(&intel_bluetooth_platform_driver);
err_platform_register:
	return err;
}

static void __exit lbf_uart_exit(void)
{
	int err = 0;

	trace_lbf_func_start(__func__);

	/* Release tty registration of line discipline */
	err = tty_unregister_ldisc(N_INTEL_LDISC);
	if (err)
		pr_err("Can't unregister N_INTEL_LDISC line discipline (%d)",
			 err);
	platform_driver_unregister(&intel_bluetooth_platform_driver);

	trace_lbf_ldisc_call(__func__, err);
}

module_init(lbf_uart_init);
module_exit(lbf_uart_exit);
MODULE_LICENSE("GPL");
