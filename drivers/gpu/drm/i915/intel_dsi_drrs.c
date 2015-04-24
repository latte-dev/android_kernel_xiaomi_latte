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

#include <drm/i915_drm.h>
#include <linux/delay.h>

#include "intel_drv.h"
#include "intel_dsi.h"

/* Work function for DSI deferred work */
static void intel_mipi_drrs_work_fn(struct work_struct *__work)
{
	struct intel_mipi_drrs_work *work =
				container_of(to_delayed_work(__work),
					struct intel_mipi_drrs_work, work);
	struct i915_drrs *drrs = work->drrs;
	struct intel_encoder *intel_encoder = drrs->connector->encoder;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&intel_encoder->base);
	struct dsi_drrs *dsi_drrs = &intel_dsi->dsi_drrs;
	struct dsi_mnp *dsi_mnp;
	struct drm_display_mode *prev_mode = NULL;
	bool fallback_attempt = false;
	int ret, retry_cnt = 3;

init:
	dsi_mnp = &dsi_drrs->mnp[work->target_rr_type];
	DRM_DEBUG("Refresh rate Type: %d-->%d\n",
					drrs->drrs_state.current_rr_type,
						work->target_rr_type);
	DRM_DEBUG("Target RR: %d\n", work->target_mode->vrefresh);

retry:
	ret = dsi_drrs->ops->configure_dsi_pll(intel_encoder, dsi_mnp);
	if (ret == 0) {

		/* PLL Programming is successfull */
		mutex_lock(&drrs->drrs_mutex);
		drrs->drrs_state.current_rr_type = work->target_rr_type;
		mutex_unlock(&drrs->drrs_mutex);
		DRM_INFO("Refresh Rate set to : %dHz\n",
						work->target_mode->vrefresh);

		/* TODO: Update crtc mode and drain ladency with watermark */

	} else if (ret == -ETIMEDOUT && retry_cnt) {

		/* Timed out. But still attempts are allowed */
		retry_cnt--;
		DRM_DEBUG("Retry left ... <%d>\n", retry_cnt);
		goto retry;
	} else if (ret == -EACCES && !fallback_attempt) {

		/*
		 * PLL Didn't lock for the programmed value
		 * fall back to prev mode
		 */
		DRM_ERROR("Falling back to the previous DRRS state. %d->%d\n",
					work->target_rr_type,
					drrs->drrs_state.current_rr_type);

		mutex_lock(&drrs->drrs_mutex);
		drrs->drrs_state.target_rr_type =
					drrs->drrs_state.current_rr_type;
		mutex_unlock(&drrs->drrs_mutex);

		work->target_rr_type = drrs->drrs_state.target_rr_type;
		drm_mode_destroy(intel_encoder->base.dev, work->target_mode);

		if (work->target_rr_type == DRRS_HIGH_RR)
			prev_mode = drrs->connector->panel.fixed_mode;
		else if (work->target_rr_type == DRRS_LOW_RR)
			prev_mode = drrs->connector->panel.downclock_mode;

		work->target_mode = drm_mode_duplicate(intel_encoder->base.dev,
								prev_mode);
		fallback_attempt = true;
		goto init;
	} else {

		/*
		 * All attempts are failed or Fall back
		 * mode all didn't go through.
		 */
		if (fallback_attempt)
			DRM_ERROR("DRRS State Fallback attempt failed\n");
		if (ret == -ETIMEDOUT)
			DRM_ERROR("TIMEDOUT in all retry attempt\n");
	}

	drm_mode_destroy(intel_encoder->base.dev, work->target_mode);
	work->work_completed = true;
}

/* Whether DRRS_HR_STATE is pending in the dsi deferred work */
bool intel_dsi_is_drrs_hr_state_pending(struct i915_drrs *drrs)
{
	struct intel_dsi *intel_dsi =
			enc_to_intel_dsi(&drrs->connector->encoder->base);
	struct dsi_drrs *dsi_drrs = &intel_dsi->dsi_drrs;
	struct intel_mipi_drrs_work *work = dsi_drrs->mipi_drrs_work;

	if (work_busy(&work->work.work) && work->target_rr_type == DRRS_HIGH_RR)
		return true;
	return false;
}

void intel_dsi_set_drrs_state(struct i915_drrs *drrs)
{
	struct intel_encoder *intel_encoder = drrs->connector->encoder;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&intel_encoder->base);
	struct dsi_drrs *dsi_drrs = &intel_dsi->dsi_drrs;
	struct drm_display_mode *target_mode =
				drrs->connector->panel.target_mode;
	struct intel_mipi_drrs_work *work = dsi_drrs->mipi_drrs_work;
	unsigned int ret;

	ret = work_busy(&work->work.work);
	if (ret) {
		if (work->target_mode)
			if (work->target_mode->vrefresh ==
						target_mode->vrefresh) {
				DRM_INFO("Repeated request for %dHz\n",
							target_mode->vrefresh);
				return;
			}
		DRM_DEBUG("Cancelling an queued/executing work\n");
		atomic_set(&work->abort_wait_loop, 1);
		cancel_delayed_work_sync(&work->work);
		atomic_set(&work->abort_wait_loop, 0);
		if ((ret & WORK_BUSY_PENDING) && !work->work_completed)
			drm_mode_destroy(intel_encoder->base.dev,
							work->target_mode);
	}

	work->work_completed = false;
	work->drrs = drrs;
	work->target_rr_type = drrs->drrs_state.target_rr_type;
	work->target_mode = drm_mode_duplicate(intel_encoder->base.dev,
								target_mode);
	schedule_delayed_work(&work->work, 0);
}

/* DSI deferred function init*/
int intel_dsi_drrs_deferred_work_init(struct i915_drrs *drrs)
{
	struct intel_encoder *intel_encoder = drrs->connector->encoder;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&intel_encoder->base);
	struct dsi_drrs *dsi_drrs = &intel_dsi->dsi_drrs;
	struct intel_mipi_drrs_work *work;

	work = kzalloc(sizeof(struct intel_mipi_drrs_work), GFP_KERNEL);
	if (!work) {
		DRM_ERROR("Failed to allocate mipi DRRS work structure\n");
		return -ENOMEM;
	}

	atomic_set(&work->abort_wait_loop, 0);
	INIT_DELAYED_WORK(&work->work, intel_mipi_drrs_work_fn);
	work->target_mode = NULL;
	work->work_completed = true;

	dsi_drrs->mipi_drrs_work = work;
	return 0;
}

/* Based on the VBT's min supported vrefresh, downclock mode will be created */
struct drm_display_mode *
intel_dsi_calc_panel_downclock(struct i915_drrs *drrs,
			struct drm_display_mode *fixed_mode)
{
	struct intel_encoder *intel_encoder = drrs->connector->encoder;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&intel_encoder->base);
	struct dsi_drrs *dsi_drrs = &intel_dsi->dsi_drrs;
	struct drm_display_mode *downclock_mode = NULL;

	if (dsi_drrs->min_vrefresh == 0 ||
			dsi_drrs->min_vrefresh >= fixed_mode->vrefresh) {
		DRM_ERROR("Invalid min Vrefresh. %d\n", dsi_drrs->min_vrefresh);
		return NULL;
	}

	/* Allocate */
	downclock_mode =
			drm_mode_duplicate(intel_encoder->base.dev, fixed_mode);
	if (!downclock_mode) {
		DRM_ERROR("Downclock mode allocation failed. No memory\n");
		return NULL;
	}

	downclock_mode->vrefresh = dsi_drrs->min_vrefresh;
	DRM_DEBUG("drrs_min_vrefresh = %u\n", downclock_mode->vrefresh);
	downclock_mode->clock = downclock_mode->vrefresh *
		downclock_mode->vtotal * downclock_mode->htotal / 1000;

	return downclock_mode;
}

/* Main init Enrty for DSI DRRS module */
int intel_dsi_drrs_init(struct i915_drrs *drrs,
					struct drm_display_mode *fixed_mode)
{
	struct intel_encoder *intel_encoder = drrs->connector->encoder;
	struct drm_i915_private *dev_priv =
					intel_encoder->base.dev->dev_private;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&intel_encoder->base);
	struct dsi_drrs *dsi_drrs = &intel_dsi->dsi_drrs;
	struct intel_panel *panel = &drrs->connector->panel;
	struct drm_display_mode *downclock_mode;
	int ret = 0;

	dsi_drrs->min_vrefresh = dev_priv->vbt.drrs_min_vrefresh;

	if (fixed_mode->vrefresh == 0)
		fixed_mode->vrefresh = drm_mode_vrefresh(fixed_mode);

	/* Modes Initialization */
	downclock_mode = intel_dsi_calc_panel_downclock(drrs, fixed_mode);
	if (!downclock_mode) {
		DRM_ERROR("Downclock mode not Found\n");
		ret = -ENOMEM;
		goto out_err_1;
	}

	DRM_DEBUG("downclock_mode :\n");
	drm_mode_debug_printmodeline(downclock_mode);

	panel->target_mode = NULL;

	if (!dsi_drrs->ops || !dsi_drrs->ops->mnp_calculate_for_mode ||
					!dsi_drrs->ops->configure_dsi_pll) {
		DRM_ERROR("DSI platform ops not initialized\n");
		ret = -EINVAL;
		goto out_err_2;
	}

	/* Calculate mnp for fixed and downclock modes */
	ret = dsi_drrs->ops->mnp_calculate_for_mode(
			intel_encoder, &dsi_drrs->mnp[DRRS_HIGH_RR],
			fixed_mode);
	if (ret < 0)
		goto out_err_2;

	ret = dsi_drrs->ops->mnp_calculate_for_mode(
			intel_encoder, &dsi_drrs->mnp[DRRS_LOW_RR],
			downclock_mode);
	if (ret < 0)
		goto out_err_2;

	ret = intel_dsi_drrs_deferred_work_init(drrs);
	if (ret < 0)
		goto out_err_2;

	/* In DSI SEAMLESS DRRS is a SW driven feature */
	drrs->drrs_state.type = SEAMLESS_DRRS_SUPPORT_SW;
	intel_panel_init(panel, fixed_mode, downclock_mode);

	return ret;

out_err_2:
	drm_mode_destroy(intel_encoder->base.dev, downclock_mode);
out_err_1:
	return ret;
}

void intel_dsi_drrs_exit(struct i915_drrs *drrs)
{
	struct intel_encoder *intel_encoder = drrs->connector->encoder;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&intel_encoder->base);
	struct dsi_drrs *dsi_drrs = &intel_dsi->dsi_drrs;
	struct intel_mipi_drrs_work *work = dsi_drrs->mipi_drrs_work;
	unsigned int ret;

	ret = work_busy(&work->work.work);
	if (ret) {
		DRM_DEBUG("Cancelling an queued/executing work\n");
		atomic_set(&work->abort_wait_loop, 1);
		cancel_delayed_work_sync(&work->work);
		atomic_set(&work->abort_wait_loop, 0);
		if ((ret & WORK_BUSY_PENDING) && !work->work_completed)
			drm_mode_destroy(intel_encoder->base.dev,
							work->target_mode);
	}

	drm_mode_destroy(intel_encoder->base.dev,
				drrs->connector->panel.downclock_mode);
	drrs->connector->panel.downclock_mode = NULL;

	kfree(dsi_drrs->mipi_drrs_work);
	drrs->drrs_state.type = DRRS_NOT_SUPPORTED;
}

struct drrs_encoder_ops drrs_dsi_ops = {
	.init = intel_dsi_drrs_init,
	.exit = intel_dsi_drrs_exit,
	.set_drrs_state = intel_dsi_set_drrs_state,
	.is_drrs_hr_state_pending = intel_dsi_is_drrs_hr_state_pending,
};

/* Call back Function for Intel_drrs module to get the dsi func ptr */
inline struct drrs_encoder_ops *get_intel_dsi_drrs_ops(void)
{
	return &drrs_dsi_ops;
}
