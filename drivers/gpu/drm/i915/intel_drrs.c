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
#include <linux/list.h>

#include "i915_drv.h"
#include "intel_drv.h"
#include "intel_dsi.h"
#include "intel_drrs.h"

int get_drrs_struct_index_for_crtc(struct drm_i915_private *dev_priv,
						struct intel_crtc *intel_crtc)
{
	int i;

	for (i = 0; i < I915_MAX_PIPES; i++) {
		if (dev_priv->drrs[i] &&
			(dev_priv->drrs[i]->connector->encoder->base.crtc ==
							&intel_crtc->base))
			return i;
	}

	/* No drrs struct exist for this crtc */
	return -ENXIO;

}

int get_drrs_struct_index_for_connector(struct drm_i915_private *dev_priv,
					struct intel_connector *intel_connector)
{
	int i;

	for (i = 0; i < I915_MAX_PIPES; i++) {
		if (dev_priv->drrs[i] &&
			(dev_priv->drrs[i]->connector == intel_connector))
			return i;
	}

	/* No drrs struct exist for this connector */
	return -ENXIO;
}

int get_free_drrs_struct_index(struct drm_i915_private *dev_priv)
{
	int i;

	for (i = 0; i < I915_MAX_PIPES; i++) {
		if (!dev_priv->drrs[i])
			return i;
	}

	/* All drrs index are busy */
	return -EBUSY;
}

void intel_set_drrs_state(struct i915_drrs *drrs)
{
	struct drrs_info *drrs_state;
	struct drm_display_mode *target_mode;
	struct intel_crtc *intel_crtc;
	struct intel_panel *panel;
	int refresh_rate;

	if (!drrs || !drrs->has_drrs) {
		DRM_ERROR("DRRS is not supported on this pipe\n");
		return;
	}

	panel = &drrs->connector->panel;
	drrs_state = &drrs->drrs_state;

	intel_crtc = to_intel_crtc(drrs->connector->encoder->base.crtc);

	if (!intel_crtc) {
		DRM_DEBUG_KMS("DRRS: intel_crtc not initialized\n");
		return;
	}

	if (!intel_crtc->active) {
		DRM_INFO("Encoder has been disabled. CRTC not Active\n");
		return;
	}

	target_mode = panel->target_mode;
	if (target_mode == NULL) {
		DRM_ERROR("target_mode cannot be NULL\n");
		return;
	}
	refresh_rate = target_mode->vrefresh;

	if (refresh_rate <= 0) {
		DRM_ERROR("Refresh rate should be positive non-zero.<%d>\n",
								refresh_rate);
		return;
	}

	if (drrs_state->target_rr_type >= DRRS_MAX_RR) {
		DRM_ERROR("Unknown refresh_rate_type\n");
		return;
	}

	if (drrs_state->target_rr_type == drrs_state->current_rr_type) {
		DRM_INFO("Requested for previously set RR. Ignoring\n");
		return;
	}

	drrs->encoder_ops->set_drrs_state(drrs);
	if (drrs_state->type != SEAMLESS_DRRS_SUPPORT_SW) {

		/*
		 * If it is non-DSI encoders.
		 * As only DSI has SEAMLESS_DRRS_SUPPORT_SW.
		 */
		drrs_state->current_rr_type = drrs_state->target_rr_type;
		DRM_INFO("Refresh Rate set to : %dHz\n", refresh_rate);
	}
}

static void intel_idleness_drrs_work_fn(struct work_struct *__work)
{
	struct intel_idleness_drrs_work *work =
		container_of(to_delayed_work(__work),
				struct intel_idleness_drrs_work, work);
	struct intel_panel *panel;
	struct i915_drrs *drrs = work->drrs;

	panel = &drrs->connector->panel;

	/* Double check if the dual-display mode is active. */
	if (drrs->is_clone)
		return;

	mutex_lock(&drrs->drrs_mutex);
	if (panel->target_mode != NULL)
		DRM_ERROR("FIXME: We shouldn't be here\n");

	panel->target_mode = panel->downclock_mode;
	drrs->drrs_state.target_rr_type = DRRS_LOW_RR;

	intel_set_drrs_state(drrs);

	panel->target_mode = NULL;
	mutex_unlock(&drrs->drrs_mutex);
}

static void intel_cancel_idleness_drrs_work(struct i915_drrs *drrs)
{
	if (!drrs || !drrs->has_drrs || !drrs->idleness_drrs_work)
		return;

	cancel_delayed_work_sync(&drrs->idleness_drrs_work->work);
	drrs->connector->panel.target_mode = NULL;
}

static void intel_enable_idleness_drrs(struct i915_drrs *drrs)
{
	bool force_enable_drrs = false;

	if (!drrs || !drrs->has_drrs)
		return;

	intel_cancel_idleness_drrs_work(drrs);
	mutex_lock(&drrs->drrs_mutex);

	/* Capturing the deferred request for disable_drrs */
	if (drrs->drrs_state.type == SEAMLESS_DRRS_SUPPORT_SW &&
				drrs->encoder_ops->is_drrs_hr_state_pending) {
		if (drrs->encoder_ops->is_drrs_hr_state_pending(drrs))
				force_enable_drrs = true;
	}

	if (drrs->drrs_state.current_rr_type != DRRS_LOW_RR ||
							force_enable_drrs) {
		drrs->idleness_drrs_work->drrs = drrs;

		/*
		 * Delay the actual enabling to let pageflipping cease and the
		 * display to settle before starting DRRS
		 */
		schedule_delayed_work(&drrs->idleness_drrs_work->work,
			msecs_to_jiffies(drrs->idleness_drrs_work->interval));
	}
	mutex_unlock(&drrs->drrs_mutex);
}

void intel_disable_idleness_drrs(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_drrs *drrs;
	struct intel_panel *panel;
	int index;

	index = get_drrs_struct_index_for_crtc(dev_priv, crtc);
	if (index < 0)
		return;

	drrs = dev_priv->drrs[index];
	if (!drrs || !drrs->has_drrs)
		return;

	panel = &drrs->connector->panel;

	/* as part of disable DRRS, reset refresh rate to HIGH_RR */
	if (drrs->drrs_state.current_rr_type == DRRS_LOW_RR) {
		intel_cancel_idleness_drrs_work(drrs);
		mutex_lock(&drrs->drrs_mutex);

		if (panel->target_mode != NULL)
			DRM_ERROR("FIXME: We shouldn't be here\n");

		panel->target_mode = panel->fixed_mode;
		drrs->drrs_state.target_rr_type = DRRS_HIGH_RR;
		intel_set_drrs_state(drrs);
		panel->target_mode = NULL;
		mutex_unlock(&drrs->drrs_mutex);
	}
}

/* Stops and Starts the Idlenes detection */
void intel_restart_idleness_drrs(struct intel_crtc *intel_crtc)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_drrs *drrs;
	struct drm_crtc *crtc = NULL, *tmp_crtc;
	int index;

	index = get_drrs_struct_index_for_crtc(dev_priv, intel_crtc);
	if (index < 0)
		return;

	drrs = dev_priv->drrs[index];
	if (!drrs || !drrs->has_drrs)
		return;

	/*
	 * TODO: This is identifying the multiple active crtcs at a time.
	 * Here we assume that this is clone state and disable DRRS.
	 * But need to implement a proper method to find the real cloned mode
	 * state. DRRS need not be disabled incase of multiple crtcs with
	 * different content.
	 */
	list_for_each_entry(tmp_crtc, &dev->mode_config.crtc_list, head) {
		if (intel_crtc_active(tmp_crtc)) {
			if (crtc) {
				DRM_DEBUG_KMS(
				"more than one pipe active, disabling DRRS\n");
				drrs->is_clone = true;
				intel_disable_idleness_drrs(intel_crtc);
				return;
			}
			crtc = tmp_crtc;
		}
	}

	drrs->is_clone = false;
	intel_disable_idleness_drrs(intel_crtc);

	/* re-enable idleness detection */
	intel_enable_idleness_drrs(drrs);
}


/* Idleness detection logic is initialized */
int intel_drrs_idleness_detection_init(struct i915_drrs *drrs)
{
	struct intel_idleness_drrs_work *work;

	work = kzalloc(sizeof(struct intel_idleness_drrs_work), GFP_KERNEL);
	if (!work) {
		DRM_ERROR("Failed to allocate DRRS work structure\n");
		return -ENOMEM;
	}

	drrs->is_clone = false;
	work->interval = DRRS_IDLENESS_INTERVAL_MS;
	work->drrs = drrs;
	INIT_DELAYED_WORK(&work->work, intel_idleness_drrs_work_fn);

	drrs->idleness_drrs_work = work;
	return 0;
}

/*
 * intel_drrs_init : General entry for DRRS Unit. Called for each PIPE.
 */
int intel_drrs_init(struct drm_device *dev,
				struct intel_connector *intel_connector,
					struct drm_display_mode *fixed_mode)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *intel_encoder = intel_connector->encoder;
	struct i915_drrs *drrs;
	int ret = 0, index;

	if (!IS_PLATFORM_HAS_DRRS(dev)) {
		DRM_DEBUG("DRRS is not enabled on this platform\n");
		return -EPERM;
	}

	if (!IS_ENCODER_SUPPORTS_DRRS(intel_encoder->type)) {
		DRM_DEBUG("DRRS: Unsupported Encoder\n");
		return -EINVAL;
	}

	/* First check if Seamless DRRS is enabled from VBT struct */
	if (dev_priv->vbt.drrs_type != SEAMLESS_DRRS_SUPPORT) {
		DRM_DEBUG("Panel doesn't support SEAMLESS DRRS\n");
		return -EPERM;
	}

	if (get_drrs_struct_index_for_connector(dev_priv, intel_connector)
									>= 0) {
		DRM_DEBUG("DRRS is already initialized for this connector\n");
		return -EINVAL;
	}

	index = get_free_drrs_struct_index(dev_priv);
	if (index < 0) {
		DRM_DEBUG("DRRS is initialized for all pipes\n");
		return -EBUSY;
	}

	dev_priv->drrs[index] = kzalloc(sizeof(*drrs), GFP_KERNEL);
	if (!dev_priv->drrs[index]) {
		DRM_ERROR("DRRS memory allocation failed\n");
		return -ENOMEM;
	}

	drrs = dev_priv->drrs[index];
	drrs->connector = intel_connector;

	if (intel_encoder->type == INTEL_OUTPUT_DSI) {
		drrs->encoder_ops = get_intel_dsi_drrs_ops();
	} else if (intel_encoder->type == INTEL_OUTPUT_EDP) {
		drrs->encoder_ops = get_intel_edp_drrs_ops();
	} else {
		DRM_DEBUG("DRRS: Unsupported Encoder\n");
		ret = -EINVAL;
		goto err_out;
	}

	if (!drrs->encoder_ops) {
		DRM_DEBUG("Encoder ops not initialized\n");
		ret = -EINVAL;
		goto err_out;
	}

	if (!drrs->encoder_ops->init || !drrs->encoder_ops->exit ||
					!drrs->encoder_ops->set_drrs_state) {
		DRM_DEBUG("Essential func ptrs are NULL\n");
		ret = -EINVAL;
		goto err_out;
	}

	ret = drrs->encoder_ops->init(drrs, fixed_mode);
	if (ret < 0) {
		DRM_DEBUG("Encoder DRRS init failed\n");
		goto err_out;
	}

	ret = intel_drrs_idleness_detection_init(drrs);
	if (ret < 0) {
		drrs->encoder_ops->exit(drrs);
		goto err_out;
	}

	/* SEAMLESS DRRS is supported and downclock mode also exist */
	drrs->has_drrs = true;
	mutex_init(&drrs->drrs_mutex);
	drrs->drrs_state.current_rr_type = DRRS_HIGH_RR;
	DRM_INFO("SEAMLESS DRRS supported on this panel.\n");

	return 0;

err_out:
	drrs->drrs_state.type = DRRS_NOT_SUPPORTED;
	kfree(dev_priv->drrs[index]);
	dev_priv->drrs[index] = NULL;
	return ret;
}

void intel_drrs_exit(struct drm_device *dev,
				struct intel_connector *intel_connector)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_drrs *drrs;
	int index;

	index = get_drrs_struct_index_for_connector(dev_priv, intel_connector);
	if (index < 0)
		return;

	drrs = dev_priv->drrs[index];
	intel_cancel_idleness_drrs_work(drrs);

	if (drrs->encoder_ops->exit)
		drrs->encoder_ops->exit(drrs);

	drrs->has_drrs = false;
	mutex_destroy(&drrs->drrs_mutex);
	kfree(drrs->idleness_drrs_work);

	kfree(drrs);
	dev_priv->drrs[index] = NULL;
}
