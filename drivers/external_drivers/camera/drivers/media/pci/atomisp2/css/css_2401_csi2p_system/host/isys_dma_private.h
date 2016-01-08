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


#ifndef __ISYS_DMA_PRIVATE_H_INCLUDED__
#define __ISYS_DMA_PRIVATE_H_INCLUDED__

#include "isys_dma_public.h"
#include "device_access.h"
#include "assert_support.h"
#include "dma.h"
#include "dma_v2_defs.h"
#include "print_support.h"


STORAGE_CLASS_ISYS2401_DMA_C void isys2401_dma_reg_store(
	const isys2401_dma_ID_t	dma_id,
	const unsigned int	reg,
	const hrt_data		value)
{
	unsigned int reg_loc;

	assert(dma_id < N_ISYS2401_DMA_ID);
	assert(ISYS2401_DMA_BASE[dma_id] != (hrt_address)-1);

	reg_loc = ISYS2401_DMA_BASE[dma_id] + (reg * sizeof(hrt_data));

	ia_css_print("isys dma store at addr(0x%x) val(%u)\n", reg_loc, (unsigned int)value);
	ia_css_device_store_uint32(reg_loc, value);
}

STORAGE_CLASS_ISYS2401_DMA_C hrt_data isys2401_dma_reg_load(
	const isys2401_dma_ID_t	dma_id,
	const unsigned int	reg)
{
	unsigned int reg_loc;
	hrt_data value;

	assert(dma_id < N_ISYS2401_DMA_ID);
	assert(ISYS2401_DMA_BASE[dma_id] != (hrt_address)-1);

	reg_loc = ISYS2401_DMA_BASE[dma_id] + (reg * sizeof(hrt_data));

	value = ia_css_device_load_uint32(reg_loc);
	ia_css_print("isys dma load from addr(0x%x) val(%u)\n", reg_loc, (unsigned int)value);

	return value;
}

#endif /* __ISYS_DMA_PRIVATE_H_INCLUDED__ */
