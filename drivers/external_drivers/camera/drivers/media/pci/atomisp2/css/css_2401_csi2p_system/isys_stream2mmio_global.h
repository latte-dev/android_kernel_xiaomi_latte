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


#ifndef __ISYS_STREAM2MMIO_GLOBAL_H_INCLUDED__
#define __ISYS_STREAM2MMIO_GLOBAL_H_INCLUDED__

#include <type_support.h>

typedef struct stream2mmio_cfg_s stream2mmio_cfg_t;
struct stream2mmio_cfg_s {
	uint32_t				bits_per_pixel;
	uint32_t				enable_blocking;
};

/* Stream2MMIO limits  per ID*/
/*
 * Stream2MMIO 0 has 8 SIDs that are indexed by
 * [STREAM2MMIO_SID0_ID...STREAM2MMIO_SID7_ID].
 *
 * Stream2MMIO 1 has 4 SIDs that are indexed by
 * [STREAM2MMIO_SID0_ID...TREAM2MMIO_SID3_ID].
 *
 * Stream2MMIO 2 has 4 SIDs that are indexed by
 * [STREAM2MMIO_SID0_ID...STREAM2MMIO_SID3_ID].
 */
extern const stream2mmio_sid_ID_t N_STREAM2MMIO_SID_PROCS[N_STREAM2MMIO_ID];

#endif /* __ISYS_STREAM2MMIO_GLOBAL_H_INCLUDED__ */
