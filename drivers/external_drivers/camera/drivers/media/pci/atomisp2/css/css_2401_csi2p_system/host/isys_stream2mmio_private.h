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


#ifndef __ISYS_STREAM2MMIO_PRIVATE_H_INCLUDED__
#define __ISYS_STREAM2MMIO_PRIVATE_H_INCLUDED__

#include "isys_stream2mmio_public.h"
#include "device_access.h"	/* ia_css_device_load_uint32 */
#include "assert_support.h"	/* assert */
#include "print_support.h"	/* print */

#define STREAM2MMIO_COMMAND_REG_ID             0
#define STREAM2MMIO_ACKNOWLEDGE_REG_ID         1
#define STREAM2MMIO_PIX_WIDTH_ID_REG_ID        2
#define STREAM2MMIO_START_ADDR_REG_ID          3      /* master port address,NOT Byte */
#define STREAM2MMIO_END_ADDR_REG_ID            4      /* master port address,NOT Byte */
#define STREAM2MMIO_STRIDE_REG_ID              5      /* stride in master port words, increment is per packet for long sids, stride is not used for short sid's*/
#define STREAM2MMIO_NUM_ITEMS_REG_ID           6      /* number of packets for store packets cmd, number of words for store_words cmd */
#define STREAM2MMIO_BLOCK_WHEN_NO_CMD_REG_ID   7      /* if this register is 1, input will be stalled if there is no pending command for this sid */
#define STREAM2MMIO_REGS_PER_SID               8

/*****************************************************
 *
 * Native command interface (NCI).
 *
 *****************************************************/
/**
 * @brief Get the stream2mmio-controller state.
 * Refer to "stream2mmio_public.h" for details.
 */
STORAGE_CLASS_STREAM2MMIO_C void stream2mmio_get_state(
		const stream2mmio_ID_t ID,
		stream2mmio_state_t *state)
{
	stream2mmio_sid_ID_t i;

	/*
	 * Get the values of the register-set per
	 * stream2mmio-controller sids.
	 */
	for (i = STREAM2MMIO_SID0_ID; i < N_STREAM2MMIO_SID_PROCS[ID]; i++) {
		stream2mmio_get_sid_state(ID, i, &(state->sid_state[i]));
	}
}

/**
 * @brief Get the state of the stream2mmio-controller sidess.
 * Refer to "stream2mmio_public.h" for details.
 */
STORAGE_CLASS_STREAM2MMIO_C void stream2mmio_get_sid_state(
		const stream2mmio_ID_t ID,
		const stream2mmio_sid_ID_t sid_id,
		stream2mmio_sid_state_t	*state)
{

	state->rcv_ack =
		stream2mmio_reg_load(ID, sid_id, STREAM2MMIO_ACKNOWLEDGE_REG_ID);

	state->pix_width_id =
		stream2mmio_reg_load(ID, sid_id, STREAM2MMIO_PIX_WIDTH_ID_REG_ID);

	state->start_addr =
		stream2mmio_reg_load(ID, sid_id, STREAM2MMIO_START_ADDR_REG_ID);

	state->end_addr =
		stream2mmio_reg_load(ID, sid_id, STREAM2MMIO_END_ADDR_REG_ID);

	state->strides =
		stream2mmio_reg_load(ID, sid_id, STREAM2MMIO_STRIDE_REG_ID);

	state->num_items =
		stream2mmio_reg_load(ID, sid_id, STREAM2MMIO_NUM_ITEMS_REG_ID);

	state->block_when_no_cmd =
		stream2mmio_reg_load(ID, sid_id, STREAM2MMIO_BLOCK_WHEN_NO_CMD_REG_ID);

}

/**
 * @brief Dump the state of the stream2mmio-controller sidess.
 * Refer to "stream2mmio_public.h" for details.
 */
STORAGE_CLASS_STREAM2MMIO_C void stream2mmio_print_sid_state(
		stream2mmio_sid_state_t	*state)
{
	ia_css_print("\t \t Receive acks 0x%x\n", state->rcv_ack);
	ia_css_print("\t \t Pixel width 0x%x\n", state->pix_width_id);
	ia_css_print("\t \t Startaddr 0x%x\n", state->start_addr);
	ia_css_print("\t \t Endaddr 0x%x\n", state->end_addr);
	ia_css_print("\t \t Strides 0x%x\n", state->strides);
	ia_css_print("\t \t Num Items 0x%x\n", state->num_items);
	ia_css_print("\t \t block when no cmd 0x%x\n", state->block_when_no_cmd);

}
/**
 * @brief Dump the ibuf-controller state.
 * Refer to "stream2mmio_public.h" for details.
 */
STORAGE_CLASS_STREAM2MMIO_C void stream2mmio_dump_state(
		const stream2mmio_ID_t ID,
		stream2mmio_state_t *state)
{
	stream2mmio_sid_ID_t i;

	/*
	 * Get the values of the register-set per
	 * stream2mmio-controller sids.
	 */
	for (i = STREAM2MMIO_SID0_ID; i < N_STREAM2MMIO_SID_PROCS[ID]; i++) {
		ia_css_print("StREAM2MMIO ID %d SID %d\n", ID, i);
		stream2mmio_print_sid_state(&(state->sid_state[i]));
	}
}
/** end of NCI */

/*****************************************************
 *
 * Device level interface (DLI).
 *
 *****************************************************/
/**
 * @brief Load the register value.
 * Refer to "stream2mmio_public.h" for details.
 */
STORAGE_CLASS_STREAM2MMIO_C hrt_data stream2mmio_reg_load(
		const stream2mmio_ID_t ID,
		const stream2mmio_sid_ID_t sid_id,
		const uint32_t reg_idx)
{
	uint32_t reg_bank_offset;

	assert(ID < N_STREAM2MMIO_ID);

	reg_bank_offset = STREAM2MMIO_REGS_PER_SID * sid_id;
	return ia_css_device_load_uint32(STREAM2MMIO_CTRL_BASE[ID] +
			(reg_bank_offset + reg_idx) * sizeof(hrt_data));
}


/**
 * @brief Store a value to the register.
 * Refer to "stream2mmio_public.h" for details.
 */
STORAGE_CLASS_STREAM2MMIO_C void stream2mmio_reg_store(
		const stream2mmio_ID_t ID,
		const hrt_address reg,
		const hrt_data value)
{
	assert(ID < N_STREAM2MMIO_ID);
	assert(STREAM2MMIO_CTRL_BASE[ID] != (hrt_address)-1);

	ia_css_device_store_uint32(STREAM2MMIO_CTRL_BASE[ID] +
		reg * sizeof(hrt_data), value);
}
/** end of DLI */

#endif /* __ISYS_STREAM2MMIO_PRIVATE_H_INCLUDED__ */
