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


#ifndef __PIXELGEN_LOCAL_H_INCLUDED__
#define __PIXELGEN_LOCAL_H_INCLUDED__

#include "pixelgen_global.h"

typedef struct pixelgen_ctrl_state_s	pixelgen_ctrl_state_t;
struct pixelgen_ctrl_state_s {
	hrt_data	com_enable;
	hrt_data	prbs_rstval0;
	hrt_data	prbs_rstval1;
	hrt_data	syng_sid;
	hrt_data	syng_free_run;
	hrt_data	syng_pause;
	hrt_data	syng_nof_frames;
	hrt_data	syng_nof_pixels;
	hrt_data	syng_nof_line;
	hrt_data	syng_hblank_cyc;
	hrt_data	syng_vblank_cyc;
	hrt_data	syng_stat_hcnt;
	hrt_data	syng_stat_vcnt;
	hrt_data	syng_stat_fcnt;
	hrt_data	syng_stat_done;
	hrt_data	tpg_mode;
	hrt_data	tpg_hcnt_mask;
	hrt_data	tpg_vcnt_mask;
	hrt_data	tpg_xycnt_mask;
	hrt_data	tpg_hcnt_delta;
	hrt_data	tpg_vcnt_delta;
	hrt_data	tpg_r1;
	hrt_data	tpg_g1;
	hrt_data	tpg_b1;
	hrt_data	tpg_r2;
	hrt_data	tpg_g2;
	hrt_data	tpg_b2;
};
#endif /* __PIXELGEN_LOCAL_H_INCLUDED__ */
