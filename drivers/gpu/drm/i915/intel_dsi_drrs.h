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

#ifndef INTEL_DSI_DRRS_H
#define INTEL_DSI_DRRS_H

struct dsi_mnp {
	u32 dsi_pll_ctrl;
	u32 dsi_pll_div;
};

struct drrs_dsi_platform_ops {
	int (*configure_dsi_pll)(struct intel_encoder *encoder,
						struct dsi_mnp *dsi_mnp);
	int (*mnp_calculate_for_mode)(struct intel_encoder *encoder,
				struct dsi_mnp *dsi_mnp,
				struct drm_display_mode *mode);
};

/**
 * MIPI PLL register dont have a option to perform a seamless
 * PLL divider change. To simulate that operation in SW we are using
 * this deferred work
 */
struct intel_mipi_drrs_work {
	struct delayed_work work;
	struct i915_drrs *drrs;

	/* Target Refresh rate type and the target mode */
	enum drrs_refresh_rate_type target_rr_type;
	struct drm_display_mode *target_mode;

	/* Atomic variable to terminate any executing deferred work */
	atomic_t abort_wait_loop;

	/* To indicate the scheduled work completion */
	bool work_completed;
};

struct dsi_drrs {
	struct intel_mipi_drrs_work *mipi_drrs_work;
	struct dsi_mnp mnp[DRRS_MAX_RR];
	int min_vrefresh;
	struct drrs_dsi_platform_ops *ops;
};

extern inline struct drrs_encoder_ops *get_intel_dsi_drrs_ops(void);
int vlv_dsi_mnp_calculate_for_mode(struct intel_encoder *encoder,
				struct dsi_mnp *dsi_mnp,
				struct drm_display_mode *mode);
int vlv_drrs_configure_dsi_pll(struct intel_encoder *encoder,
						struct dsi_mnp *dsi_mnp);
#endif /* INTEL_DSI_DRRS_H */
