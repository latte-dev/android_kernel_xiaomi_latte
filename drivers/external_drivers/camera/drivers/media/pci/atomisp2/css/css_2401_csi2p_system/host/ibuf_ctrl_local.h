/*
* INTEL CONFIDENTIAL
*
* Copyright (C) 2014 - 2015 Intel Corporation.
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


#ifndef __IBUF_CTRL_LOCAL_H_INCLUDED__
#define __IBUF_CTRL_LOCAL_H_INCLUDED__

#include "ibuf_ctrl_global.h"

typedef struct ibuf_ctrl_proc_state_s	ibuf_ctrl_proc_state_t;
typedef struct ibuf_ctrl_state_s		ibuf_ctrl_state_t;

struct ibuf_ctrl_proc_state_s {
	hrt_data num_items;
	hrt_data num_stores;
	hrt_data dma_channel;
	hrt_data dma_command;
	hrt_data ibuf_st_addr;
	hrt_data ibuf_stride;
	hrt_data ibuf_end_addr;
	hrt_data dest_st_addr;
	hrt_data dest_stride;
	hrt_data dest_end_addr;
	hrt_data sync_frame;
	hrt_data sync_command;
	hrt_data store_command;
	hrt_data shift_returned_items;
	hrt_data elems_ibuf;
	hrt_data elems_dest;
	hrt_data cur_stores;
	hrt_data cur_acks;
	hrt_data cur_s2m_ibuf_addr;
	hrt_data cur_dma_ibuf_addr;
	hrt_data cur_dma_dest_addr;
	hrt_data cur_isp_dest_addr;
	hrt_data dma_cmds_send;
	hrt_data main_cntrl_state;
	hrt_data dma_sync_state;
	hrt_data isp_sync_state;
};

struct ibuf_ctrl_state_s {
	hrt_data	recalc_words;
	hrt_data	arbiters;
	ibuf_ctrl_proc_state_t	proc_state[N_STREAM2MMIO_SID_ID];
};

#endif /* __IBUF_CTRL_LOCAL_H_INCLUDED__ */
