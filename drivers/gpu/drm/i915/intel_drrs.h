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

#ifndef INTEL_DRRS_H__
#define INTEL_DRRS_H__

#define DRRS_IDLENESS_INTERVAL_MS	1000

struct intel_encoder;

int get_drrs_struct_index_for_crtc(struct drm_i915_private *dev_priv,
						struct intel_crtc *intel_crtc);
int get_drrs_struct_index_for_connector(struct drm_i915_private *dev_priv,
				struct intel_connector *intel_connector);
int get_free_drrs_struct_index(struct drm_i915_private *dev_priv);

void intel_disable_idleness_drrs(struct intel_crtc *crtc);
void intel_restart_idleness_drrs(struct intel_crtc *crtc);
int intel_drrs_init(struct drm_device *dev,
			struct intel_connector *intel_connector,
				struct drm_display_mode *fixed_mode);

#endif /* INTEL_DRRS_H__ */
