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

#ifndef _IA_CSS_SYSTEM_CTRL_H
#define _IA_CSS_SYSTEM_CTRL_H

#include "ia_css_pipeline.h"
#include "ia_css_irq.h"
#include "ia_css_stream.h"

enum ia_css_sys_mode {
	IA_CSS_SYS_MODE_NONE = 0,
	IA_CSS_SYS_MODE_INIT,	     /* css initialized but not started */
	IA_CSS_SYS_MODE_WORKING,     /* css initialized, and started */
	IA_CSS_SYS_MODE_SUSPEND,     /* css is suspending the system */
	IA_CSS_SYS_MODE_RESUME	     /* css is resuming the system */
};

#define MAX_ACTIVE_STREAMS	5

/* internal struct for save/restore to hold all the data that should sustain power-down:
   MMU base, IRQ type, pointers to streams and param-sets
*/
struct sh_css_save {
	enum ia_css_sys_mode	      mode;
	uint32_t                      mmu_base;
	enum ia_css_irq_type          irq_type;
	struct ia_css_stream	      *streams[MAX_ACTIVE_STREAMS];
	hrt_vaddress                  latest_params_ptr[IA_CSS_PIPELINE_NUM_MAX];
};

void ia_css_set_system_mode(enum ia_css_sys_mode mode);

enum ia_css_sys_mode
ia_css_get_system_mode(void);

bool ia_css_is_system_mode_suspend_or_resume(void);

void ia_css_save_restore_data_init(void);

void ia_css_save_irq_type(enum ia_css_irq_type irq_type);

void ia_css_save_mmu_base_addr(uint32_t mmu_base_addr);

enum ia_css_err
ia_css_save_stream(struct ia_css_stream *stream);

enum ia_css_err
ia_css_save_restore_remove_stream(struct ia_css_stream *stream);

enum ia_css_err
ia_css_save_latest_paramset_ptr(struct ia_css_pipe *pipe, hrt_vaddress ptr);

void enable_interrupts(enum ia_css_irq_type irq_type);

struct ia_css_pipe* sh_css_get_next_saved_pipe(unsigned int *curr_stream_num,
					       unsigned int *curr_pipe_num);

#endif /* _IA_CSS_SYSTEM_CTRL_H */
