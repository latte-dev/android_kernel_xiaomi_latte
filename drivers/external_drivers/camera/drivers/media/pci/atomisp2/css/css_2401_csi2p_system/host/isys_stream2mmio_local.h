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


#ifndef __ISYS_STREAM2MMIO_LOCAL_H_INCLUDED__
#define __ISYS_STREAM2MMIO_LOCAL_H_INCLUDED__

#include "isys_stream2mmio_global.h"

typedef struct stream2mmio_state_s		stream2mmio_state_t;
typedef struct stream2mmio_sid_state_s	stream2mmio_sid_state_t;

struct stream2mmio_sid_state_s {
	hrt_data rcv_ack;
	hrt_data pix_width_id;
	hrt_data start_addr;
	hrt_data end_addr;
	hrt_data strides;
	hrt_data num_items;
	hrt_data block_when_no_cmd;
};

struct stream2mmio_state_s {
	stream2mmio_sid_state_t 	sid_state[N_STREAM2MMIO_SID_ID];
};
#endif /* __ISYS_STREAM2MMIO_LOCAL_H_INCLUDED__ */
