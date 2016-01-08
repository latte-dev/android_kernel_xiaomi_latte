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


#include "isys_dma.h"
#include "assert_support.h"

#ifndef __INLINE_ISYS2401_DMA__
/*
 * Include definitions for isys dma register access functions. isys_dma.h
 * includes declarations of these functions by including isys_dma_public.h.
 */
#include "isys_dma_private.h"
#endif

const isys2401_dma_channel N_ISYS2401_DMA_CHANNEL_PROCS[N_ISYS2401_DMA_ID] = {
	N_ISYS2401_DMA_CHANNEL
};

void isys2401_dma_set_max_burst_size(
	const isys2401_dma_ID_t	dma_id,
	uint32_t		max_burst_size)
{
	assert(dma_id < N_ISYS2401_DMA_ID);
	assert((max_burst_size > 0x00) && (max_burst_size <= 0xFF));

	isys2401_dma_reg_store(dma_id,
		DMA_DEV_INFO_REG_IDX(_DMA_V2_DEV_INTERF_MAX_BURST_IDX, HIVE_DMA_BUS_DDR_CONN),
		(max_burst_size - 1));
}
