/*
 * INTEL CONFIDENTIAL
 *
 * Copyright (C) 2015 Intel Corporation.
 * All Rights Reserved.
 *
 * The source code contained or described herein and all documents
 * related to the source code ("Material") are owned by Intel Corporation
 * or licensors. Title to the Material remains with Intel
 * Corporation or its licensors. The Material contains trade
 * secrets and proprietary and confidential information of Intel or its
 * licensors. The Material is protected by worldwide copyright
 * and trade secret laws and treaty provisions. No part of the Material may
 * be used, copied, reproduced, modified, published, uploaded, posted,
 * transmitted, distributed, or disclosed in any way without Intel's prior
 * express written permission.
 *
 * No License under any patent, copyright, trade secret or other intellectual
 * property right is granted to or conferred upon you by disclosure or
 * delivery of the Materials, either expressly, by implication, inducement,
 * estoppel or otherwise. Any license under such intellectual property rights
 * must be express and approved by Intel in writing.
 */

#include "ia_css_system_ctrl.h"
#include "ia_css_err.h"
#include "ia_css_debug.h"
#include "sh_css_internal.h"
#include "sw_event_global.h"
#include "ia_css_stream.h"
#include "ia_css_pipe.h"
#include "ia_css_control.h"
#include "ia_css_spctrl.h"
#include "sh_css_hrt.h"
#include "sh_css_sp.h"
#include "irq.h"
#include "ia_css_mmu_private.h"
#include "gp_device.h"
#if defined(HAS_BL)
#include "support/bootloader/interface/ia_css_blctrl.h"
#endif
#if !defined(HAS_NO_GPIO)
#define __INLINE_GPIO__
#include "gpio.h"
#endif
#if !defined(HAS_NO_INPUT_SYSTEM)
#include "ia_css_isys.h"
#endif


static enum ia_css_err
ia_css_resume_init(void);

static enum ia_css_err
ia_css_resume_stream_start(struct ia_css_stream *stream);

static enum ia_css_err
ia_css_pipe_enqueue_latest_param_buffer(struct ia_css_pipe *pipe, hrt_vaddress paramset_ptr);

static bool my_css_save_initialized;	/* if my_css_save was initialized */
static struct sh_css_save my_css_save;


void ia_css_set_system_mode(enum ia_css_sys_mode mode)
{
	IA_CSS_ENTER_LEAVE_PRIVATE("void");
	my_css_save.mode = mode;
}

enum ia_css_sys_mode
ia_css_get_system_mode(void)
{
	IA_CSS_ENTER_LEAVE_PRIVATE("void");
	return my_css_save.mode;
}

bool ia_css_is_system_mode_suspend_or_resume(void)
{
	if (my_css_save.mode == IA_CSS_SYS_MODE_SUSPEND ||
	    my_css_save.mode == IA_CSS_SYS_MODE_RESUME)
		return true;
	return false;
}

enum ia_css_err
ia_css_save_stream(struct ia_css_stream *stream)
{
	int i;
	enum ia_css_err err = IA_CSS_SUCCESS;

	IA_CSS_ENTER_PRIVATE("stream = %p", stream);
	for (i = 0; i < MAX_ACTIVE_STREAMS; i++) {
		if (my_css_save.streams[i] == NULL) {
			IA_CSS_LOG("entered stream into loc=%d", i);
			my_css_save.streams[i] = stream;
			break;
		}
	}
	if (i == MAX_ACTIVE_STREAMS) {
		IA_CSS_ERROR("no space left for storing stream = %p", stream);
		err = IA_CSS_ERR_INTERNAL_ERROR;
	}
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

enum ia_css_err
ia_css_save_restore_remove_stream(struct ia_css_stream *stream)
{
	int i;
	enum ia_css_err err = IA_CSS_SUCCESS;
	IA_CSS_ENTER_PRIVATE("stream = %p", stream);

	for (i = 0; i < MAX_ACTIVE_STREAMS; i++) {
		if (my_css_save.streams[i] == stream) {
				IA_CSS_LOG("took out stream %d", i);
				my_css_save.streams[i]  = NULL;
				break;
		}
	}
	if (i == MAX_ACTIVE_STREAMS) {
		IA_CSS_ERROR("stream = %p could not be found in save/restore data" , stream);
		err = IA_CSS_ERR_INTERNAL_ERROR;
	}
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

void ia_css_save_restore_data_init(void)
{
	int i;
	IA_CSS_ENTER_PRIVATE("void");
	IA_CSS_LOG("init: %d", my_css_save_initialized);

	if (!my_css_save_initialized) {
		my_css_save_initialized = true;
		ia_css_set_system_mode(IA_CSS_SYS_MODE_INIT);
		for (i = 0; i < MAX_ACTIVE_STREAMS ; i++) {
			my_css_save.streams[i] = NULL;
		}
	}
	IA_CSS_LEAVE_PRIVATE("");
}

void ia_css_save_mmu_base_addr(uint32_t mmu_base_addr)
{
	IA_CSS_ENTER_LEAVE_PRIVATE("void");
	my_css_save.mmu_base = mmu_base_addr;
}

void ia_css_save_irq_type(enum ia_css_irq_type irq_type)
{
	IA_CSS_ENTER_LEAVE_PRIVATE("void");
	my_css_save.irq_type = irq_type;
}

/* ia_css_resume_init is used upon css resume to initialize css,
   corresponds to ia_css_init */
static enum ia_css_err
ia_css_resume_init(void)
{
	enum ia_css_err err = IA_CSS_SUCCESS;
#if !defined(HAS_NO_GPIO)
	hrt_data select, enable;
#endif

	IA_CSS_ENTER_PRIVATE("void");

#if !defined(HAS_NO_GPIO)
	select = gpio_reg_load(GPIO0_ID, _gpio_block_reg_do_select)
	  & (~GPIO_FLASH_PIN_MASK);
	enable = gpio_reg_load(GPIO0_ID, _gpio_block_reg_do_e)
	  | GPIO_FLASH_PIN_MASK;
#endif
	sh_css_mmu_set_page_table_base_index(my_css_save.mmu_base);
	enable_interrupts(my_css_save.irq_type);

#if !defined(HAS_NO_GPIO)
	/* configure GPIO to output mode */
	gpio_reg_store(GPIO0_ID, _gpio_block_reg_do_select, select);
	gpio_reg_store(GPIO0_ID, _gpio_block_reg_do_e, enable);
	gpio_reg_store(GPIO0_ID, _gpio_block_reg_do_0, 0);
#endif

	sh_css_spctrl_reload_fw(SP0_ID);
#if defined(HAS_SEC_SP)
	sh_css_spctrl_reload_fw(SP1_ID);
#endif
#if defined(HAS_BL)
	ia_css_blctrl_reload_fw();
#endif

#if defined(HRT_CSIM)
	/**
	 * In compiled simulator context include debug support by default.
	 * In all other cases (e.g. Android phone), the user (e.g. driver)
	 * must explicitly enable debug support by calling this function.
	 */
	if (!ia_css_debug_mode_init()) {
		IA_CSS_LEAVE_ERR(IA_CSS_ERR_INTERNAL_ERROR);
		return IA_CSS_ERR_INTERNAL_ERROR;
	}
#endif
	if (!sh_css_hrt_system_is_idle()) {
		IA_CSS_LEAVE_ERR(IA_CSS_ERR_SYSTEM_NOT_IDLE);
		return IA_CSS_ERR_SYSTEM_NOT_IDLE;
	}

#if defined(HAS_INPUT_SYSTEM_VERSION_2) && defined(HAS_INPUT_SYSTEM_VERSION_2401)
#if    defined(USE_INPUT_SYSTEM_VERSION_2)
	gp_device_reg_store(GP_DEVICE0_ID, _REG_GP_SWITCH_ISYS2401_ADDR, 0);
#elif defined(USE_INPUT_SYSTEM_VERSION_2401)
	gp_device_reg_store(GP_DEVICE0_ID, _REG_GP_SWITCH_ISYS2401_ADDR, 1);
#endif
#endif

	IA_CSS_LEAVE_ERR(err);
	return err;
}

/* ia_css_resume_stream_start is used upon CSS resume, function enqueues
   latest paramset and sends a "start_stream" event to SP */
static enum ia_css_err
ia_css_resume_stream_start(struct ia_css_stream *stream)
{
	unsigned int thread_id, pipe_num;
	int i;
	hrt_vaddress paramset_ptr;
	struct ia_css_pipe *pipe;
	enum ia_css_err err = IA_CSS_SUCCESS;

	IA_CSS_ENTER_PRIVATE("stream = %p", stream);
	if (stream == NULL) {
		IA_CSS_LEAVE_ERR(IA_CSS_ERR_INVALID_ARGUMENTS);
		return IA_CSS_ERR_INVALID_ARGUMENTS;
	}

	if (sh_css_sp_is_running() == false) {
		IA_CSS_ERROR("sp is not runnning");
		IA_CSS_LEAVE_ERR(IA_CSS_ERR_INTERNAL_ERROR);
		return IA_CSS_ERR_INTERNAL_ERROR;
	}

	for (i = 0; i < stream->num_pipes ; i++) {
		stream->pipes[i]->stop_requested = false;
		pipe = stream->pipes[i];
		pipe_num = ia_css_pipe_get_pipe_num(pipe);
		ia_css_pipeline_get_sp_thread_id(pipe_num,
						 &thread_id);
		paramset_ptr = my_css_save.latest_params_ptr[pipe_num];
		if (paramset_ptr == (hrt_vaddress)0) { /* should never reach here, in case we do return error */
			IA_CSS_ERROR("no params stored for pipe = %p, pipe_num = %d", pipe, pipe_num);
			err = IA_CSS_ERR_INTERNAL_ERROR;
			goto ERR;
		}
		err = ia_css_pipe_enqueue_latest_param_buffer(pipe, paramset_ptr);
		if (err != IA_CSS_SUCCESS)
			goto ERR;
		ia_css_bufq_enqueue_psys_event(IA_CSS_PSYS_SW_EVENT_START_STREAM,
					       (uint8_t)thread_id, 0, 0);
	}
	stream->started = true;
ERR:
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

static enum ia_css_err
ia_css_pipe_enqueue_latest_param_buffer(struct ia_css_pipe *pipe, hrt_vaddress paramset_ptr)
{
	unsigned int thread_id, pipe_num;
	enum sh_css_queue_id queue_id;
	enum ia_css_err err = IA_CSS_SUCCESS;

	IA_CSS_ENTER_PRIVATE("pipe = %p, paramset = %x", pipe, paramset_ptr);

	if (pipe == NULL || paramset_ptr == (hrt_vaddress)0) {
		IA_CSS_ERROR("invalid arguments");
		err = IA_CSS_ERR_INVALID_ARGUMENTS;
		goto ERR;
	}

	pipe_num = ia_css_pipe_get_pipe_num(pipe);
	ia_css_pipeline_get_sp_thread_id(pipe_num, &thread_id);

	ia_css_query_internal_queue_id(IA_CSS_BUFFER_TYPE_PARAMETER_SET,
				       thread_id,
				       &queue_id);

	err = ia_css_bufq_enqueue_buffer(thread_id, queue_id, (uint32_t)paramset_ptr);
	if (err != IA_CSS_SUCCESS) {
		IA_CSS_ERROR("failed to enqueue param set %x to %d, error = %d", paramset_ptr, thread_id, err);
		goto ERR;
	}

	ia_css_bufq_enqueue_psys_event(IA_CSS_PSYS_SW_EVENT_BUFFER_ENQUEUED,
				       (uint8_t)thread_id,
				       (uint8_t)queue_id,
				       0);
	IA_CSS_LOG("enqueued param set %x to %d", paramset_ptr, thread_id);
ERR:
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

enum ia_css_err
ia_css_save_latest_paramset_ptr(struct ia_css_pipe *pipe, hrt_vaddress ptr)
{
	unsigned int pipe_num, thread_id;

	IA_CSS_ENTER_PRIVATE("pipe = %p, ptr = %x", pipe, ptr);
	if (ia_css_is_system_mode_suspend_or_resume() == true) {
		IA_CSS_ERROR("system mode is suspend or resume");
		IA_CSS_LEAVE_ERR_PRIVATE(IA_CSS_ERR_INTERNAL_ERROR);
		return IA_CSS_ERR_INTERNAL_ERROR;
	}

	pipe_num = ia_css_pipe_get_pipe_num(pipe);
	ia_css_pipeline_get_sp_thread_id(pipe_num,
					 &thread_id);

	if (pipe_num >= IA_CSS_PIPELINE_NUM_MAX) {
		IA_CSS_ERROR("pipe_num = %d is grater than %d", pipe_num, IA_CSS_PIPELINE_NUM_MAX);
		IA_CSS_LEAVE_ERR_PRIVATE(IA_CSS_ERR_INTERNAL_ERROR);
		return IA_CSS_ERR_INTERNAL_ERROR;
	} else {
		my_css_save.latest_params_ptr[pipe_num] = ptr;
	}

	IA_CSS_LEAVE_ERR_PRIVATE(IA_CSS_SUCCESS);
	return IA_CSS_SUCCESS;
}

enum ia_css_err
ia_css_suspend(void)
{
	enum ia_css_err err = IA_CSS_SUCCESS;

	IA_CSS_ENTER("void");

	/* customer (i.e driver) might suspend after css init prior to starting css */
	if (ia_css_get_system_mode() == IA_CSS_SYS_MODE_INIT) {
		IA_CSS_LOG("system was initalized but not started, skipping save stage");
		IA_CSS_LEAVE_ERR(err);
		return err;
	}

	ia_css_set_system_mode(IA_CSS_SYS_MODE_SUSPEND);

	ia_css_dequeue_param_buffers();

	err = ia_css_stop_sp();
	if (err != IA_CSS_SUCCESS) {
		goto ERR; /* currently redundant - but added in case new code will be added after this block */
	}

ERR:
	ia_css_set_system_mode(IA_CSS_SYS_MODE_WORKING);
	IA_CSS_LEAVE_ERR(err);
	return err;
}

enum ia_css_err
ia_css_resume(void)
{
	int i;
	enum ia_css_err err = IA_CSS_SUCCESS;
	enum ia_css_sys_mode orig_mode;

	IA_CSS_ENTER("void");

	orig_mode = ia_css_get_system_mode(); /* save current system mode */
	ia_css_set_system_mode(IA_CSS_SYS_MODE_RESUME);

	err = ia_css_resume_init();
	if (err != IA_CSS_SUCCESS) {
		goto ERR;
	}

	if (orig_mode == IA_CSS_SYS_MODE_INIT) {
		IA_CSS_LOG("system was initalized but not started, skipping rest of resume");
		goto ERR;
	}

	err = ia_css_start_sp();
	if (err != IA_CSS_SUCCESS) {
		goto ERR;
	}

	for (i = 0; i < MAX_ACTIVE_STREAMS; i++) {
		if (my_css_save.streams[i] != NULL) {
			IA_CSS_LOG("resuming stream = %p", my_css_save.streams[i]);
			err = ia_css_resume_stream_start(my_css_save.streams[i]);
			if (err != IA_CSS_SUCCESS) {
				goto ERR;
			}
		}
	}
ERR:
	ia_css_set_system_mode(orig_mode);
	IA_CSS_LEAVE_ERR(err);
	return err;
}

void enable_interrupts(enum ia_css_irq_type irq_type)
{
#ifdef USE_INPUT_SYSTEM_VERSION_2
	mipi_port_ID_t port;
#endif
	bool enable_pulse = irq_type != IA_CSS_IRQ_TYPE_EDGE;
	IA_CSS_ENTER_PRIVATE("");
	/* Enable IRQ on the SP which signals that SP goes to idle
	 * (aka ready state) */
	cnd_sp_irq_enable(SP0_ID, true);
	/* Set the IRQ device 0 to either level or pulse */
	irq_enable_pulse(IRQ0_ID, enable_pulse);

#if defined(IS_ISP_2500_SYSTEM)
	cnd_virq_enable_channel(virq_sp0, true);
	cnd_virq_enable_channel(virq_sp1, true);
#else
	cnd_virq_enable_channel(virq_sp, true);
#endif

	/* Enable SW interrupt 0, this is used to signal ISYS events */
	cnd_virq_enable_channel(
			(virq_id_t)(IRQ_SW_CHANNEL0_ID + IRQ_SW_CHANNEL_OFFSET),
			true);
	/* Enable SW interrupt 1, this is used to signal PSYS events */
	cnd_virq_enable_channel(
			(virq_id_t)(IRQ_SW_CHANNEL1_ID + IRQ_SW_CHANNEL_OFFSET),
			true);
#if !defined(HAS_IRQ_MAP_VERSION_2)
	/* IRQ_SW_CHANNEL2_ID does not exist on 240x systems */
	cnd_virq_enable_channel(
			(virq_id_t)(IRQ_SW_CHANNEL2_ID + IRQ_SW_CHANNEL_OFFSET),
			true);
	virq_clear_all();
#endif

#ifdef USE_INPUT_SYSTEM_VERSION_2
	for (port = 0; port < N_MIPI_PORT_ID; port++)
		ia_css_isys_rx_enable_all_interrupts(port);
#endif

#if defined(HRT_CSIM)
	/*
	 * Enable IRQ on the SP which signals that SP goes to idle
	 * to get statistics for each binary
	 */
	cnd_isp_irq_enable(ISP0_ID, true);
	cnd_virq_enable_channel(virq_isp, true);
#endif
	IA_CSS_LEAVE_PRIVATE("");
}

struct ia_css_pipe* sh_css_get_next_saved_pipe(unsigned int *curr_stream_num,
		unsigned int *curr_pipe_num)
{
	int i;
	int j;
	struct ia_css_pipe *curr_pipe = NULL;

	for (i = *curr_stream_num; i < MAX_ACTIVE_STREAMS; i++) {
		if (my_css_save.streams[i] == NULL) {
			continue;
		}
		for (j = *curr_pipe_num; j < my_css_save.streams[i]->num_pipes; j++) {
			curr_pipe = my_css_save.streams[i]->pipes[j];
			if (curr_pipe != NULL) {
				if ((j + 1) < my_css_save.streams[i]->num_pipes) {
					*curr_pipe_num = j + 1;
				} else {
					*curr_stream_num = i + 1;
					*curr_pipe_num = 0;
				}
				return curr_pipe;
			}
		}
		*curr_pipe_num = 0;
	}
	return NULL;
}
