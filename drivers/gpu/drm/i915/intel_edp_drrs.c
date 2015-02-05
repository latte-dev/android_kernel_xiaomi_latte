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
 * Authors:
 * Ramalingam C <ramalingam.c@intel.com>
 * Durgadoss R <durgadoss.r@intel.com>
 */

#include <linux/delay.h>
#include <drm/i915_drm.h>

#include "i915_drv.h"

void intel_edp_set_drrs_state(struct i915_drrs *drrs)
{
	struct intel_encoder *intel_encoder = drrs->connector->encoder;
	struct intel_dp *intel_dp = enc_to_intel_dp(&intel_encoder->base);

	intel_dp->drrs_ops->set_drrs_state(intel_encoder,
					drrs->drrs_state.target_rr_type);
}

int intel_edp_drrs_init(struct i915_drrs *drrs,
					struct drm_display_mode *fixed_mode)
{
	struct intel_encoder *intel_encoder = drrs->connector->encoder;
	struct intel_dp *intel_dp = enc_to_intel_dp(&intel_encoder->base);
	struct drm_display_mode *downclock_mode;
	int ret = -EINVAL;

	if (!intel_dp->drrs_ops ||
			!intel_dp->drrs_ops->set_drrs_state) {
		DRM_ERROR("Required platform ops are NULL\n");
		return ret;
	}

	if (fixed_mode->vrefresh == 0)
		fixed_mode->vrefresh = drm_mode_vrefresh(fixed_mode);

	downclock_mode = intel_find_panel_downclock(intel_encoder->base.dev,
					fixed_mode, &drrs->connector->base);
	if (!downclock_mode) {
		DRM_DEBUG("No Downclock mode is found\n");
		return ret;
	}

	if (intel_dp->drrs_ops->init) {
		ret = intel_dp->drrs_ops->init(intel_encoder);
		if (ret < 0)
			return ret;
	}

	DRM_DEBUG("eDP DRRS modes:\n");
	drm_mode_debug_printmodeline(fixed_mode);
	drm_mode_debug_printmodeline(downclock_mode);

	/* We are good to go .. */
	intel_panel_init(&drrs->connector->panel, fixed_mode, downclock_mode);
	drrs->connector->panel.target_mode = NULL;

	drrs->drrs_state.type = SEAMLESS_DRRS_SUPPORT;
	return ret;
}

void intel_edp_drrs_exit(struct i915_drrs *drrs)
{
	struct intel_encoder *intel_encoder = drrs->connector->encoder;
	struct intel_dp *intel_dp = enc_to_intel_dp(&intel_encoder->base);

	if (intel_dp->drrs_ops->exit)
		intel_dp->drrs_ops->exit(intel_encoder);

	drrs->drrs_state.type = DRRS_NOT_SUPPORTED;
}

struct drrs_encoder_ops edp_drrs_ops = {
	.init = intel_edp_drrs_init,
	.exit = intel_edp_drrs_exit,
	.set_drrs_state = intel_edp_set_drrs_state,
};

/* Called by intel_drrs_init() to get ->ops for edp panel */
struct drrs_encoder_ops *get_intel_edp_drrs_ops(void)
{
	return &edp_drrs_ops;
}
