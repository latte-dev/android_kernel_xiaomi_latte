/*
 * Copyright (C) 2015, Intel Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author:
 * Ramalingam C <ramalingam.c@intel.com>
 */

#ifndef INTEL_EDP_DRRS_H
#define INTEL_EDP_DRRS_H

struct intel_encoder;
struct edp_drrs_platform_ops {
	int (*init)(struct intel_encoder *encoder);
	void (*exit)(struct intel_encoder *encoder);
	int (*set_drrs_state)(struct intel_encoder *encoder,
				enum drrs_refresh_rate_type target_rr_type);
};

extern inline struct drrs_encoder_ops *get_intel_edp_drrs_ops(void);
#endif /* INTEL_EDP_DRRS_H */
