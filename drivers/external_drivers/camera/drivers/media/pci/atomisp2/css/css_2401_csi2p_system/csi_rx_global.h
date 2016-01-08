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


#ifndef __CSI_RX_GLOBAL_H_INCLUDED__
#define __CSI_RX_GLOBAL_H_INCLUDED__

#include <type_support.h>

typedef enum {
	CSI_MIPI_PACKET_TYPE_UNDEFINED = 0,
	CSI_MIPI_PACKET_TYPE_LONG,
	CSI_MIPI_PACKET_TYPE_SHORT,
	CSI_MIPI_PACKET_TYPE_RESERVED,
	N_CSI_MIPI_PACKET_TYPE
} csi_mipi_packet_type_t;

typedef struct csi_rx_backend_lut_entry_s	csi_rx_backend_lut_entry_t;
struct csi_rx_backend_lut_entry_s {
	uint32_t	long_packet_entry;
	uint32_t	short_packet_entry;
};

typedef struct csi_rx_backend_cfg_s csi_rx_backend_cfg_t;
struct csi_rx_backend_cfg_s {
	/* LUT entry for the packet */
	csi_rx_backend_lut_entry_t lut_entry;

	/* can be derived from the Data Type */
	csi_mipi_packet_type_t csi_mipi_packet_type;

	struct {
		bool     comp_enable;
		uint32_t virtual_channel;
		uint32_t data_type;
		uint32_t comp_scheme;
		uint32_t comp_predictor;
		uint32_t comp_bit_idx;
	} csi_mipi_cfg;
};

typedef struct csi_rx_frontend_cfg_s csi_rx_frontend_cfg_t;
struct csi_rx_frontend_cfg_s {
	uint32_t active_lanes;
};

extern const uint32_t N_SHORT_PACKET_LUT_ENTRIES[N_CSI_RX_BACKEND_ID];
extern const uint32_t N_LONG_PACKET_LUT_ENTRIES[N_CSI_RX_BACKEND_ID];
extern const uint32_t N_CSI_RX_FE_CTRL_DLANES[N_CSI_RX_FRONTEND_ID];
/* sid_width for CSI_RX_BACKEND<N>_ID */
extern const uint32_t N_CSI_RX_BE_SID_WIDTH[N_CSI_RX_BACKEND_ID];

#endif /* __CSI_RX_GLOBAL_H_INCLUDED__ */
