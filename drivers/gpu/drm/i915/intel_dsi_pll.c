/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Shobhit Kumar <shobhit.kumar@intel.com>
 *	Yogesh Mohan Marimuthu <yogesh.mohan.marimuthu@intel.com>
 */

#include <linux/kernel.h>
#include "intel_drv.h"
#include "i915_drv.h"
#include "intel_dsi.h"

#define DSI_HSS_PACKET_SIZE		4
#define DSI_HSE_PACKET_SIZE		4
#define DSI_HSA_PACKET_EXTRA_SIZE	6
#define DSI_HBP_PACKET_EXTRA_SIZE	6
#define DSI_HACTIVE_PACKET_EXTRA_SIZE	6
#define DSI_HFP_PACKET_EXTRA_SIZE	6
#define DSI_EOTP_PACKET_SIZE		4

#define DSI_DRRS_PLL_CONFIG_TIMEOUT_MS	100

static const u32 lfsr_converts[] = {
	426, 469, 234, 373, 442, 221, 110, 311, 411,		/* 62 - 70 */
	461, 486, 243, 377, 188, 350, 175, 343, 427, 213,	/* 71 - 80 */
	106, 53, 282, 397, 454, 227, 113, 56, 284, 142,		/* 81 - 90 */
	71, 35, 273, 136, 324, 418, 465, 488, 500, 506		/* 91 - 100 */
};

#ifdef DSI_CLK_FROM_RR

static u32 dsi_rr_formula(const struct drm_display_mode *mode,
			  int pixel_format, int video_mode_format,
			  int lane_count, bool eotp)
{
	u32 bpp;
	u32 hactive, vactive, hfp, hsync, hbp, vfp, vsync, vbp;
	u32 hsync_bytes, hbp_bytes, hactive_bytes, hfp_bytes;
	u32 bytes_per_line, bytes_per_frame;
	u32 num_frames;
	u32 bytes_per_x_frames, bytes_per_x_frames_x_lanes;
	u32 dsi_bit_clock_hz;
	u32 dsi_clk;

	switch (pixel_format) {
	default:
	case VID_MODE_FORMAT_RGB888:
	case VID_MODE_FORMAT_RGB666_LOOSE:
		bpp = 24;
		break;
	case VID_MODE_FORMAT_RGB666:
		bpp = 18;
		break;
	case VID_MODE_FORMAT_RGB565:
		bpp = 16;
		break;
	}

	hactive = mode->hdisplay;
	vactive = mode->vdisplay;
	hfp = mode->hsync_start - mode->hdisplay;
	hsync = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;

	vfp = mode->vsync_start - mode->vdisplay;
	vsync = mode->vsync_end - mode->vsync_start;
	vbp = mode->vtotal - mode->vsync_end;

	hsync_bytes = DIV_ROUND_UP(hsync * bpp, 8);
	hbp_bytes = DIV_ROUND_UP(hbp * bpp, 8);
	hactive_bytes = DIV_ROUND_UP(hactive * bpp, 8);
	hfp_bytes = DIV_ROUND_UP(hfp * bpp, 8);

	bytes_per_line = DSI_HSS_PACKET_SIZE + hsync_bytes +
		DSI_HSA_PACKET_EXTRA_SIZE + DSI_HSE_PACKET_SIZE +
		hbp_bytes + DSI_HBP_PACKET_EXTRA_SIZE +
		hactive_bytes + DSI_HACTIVE_PACKET_EXTRA_SIZE +
		hfp_bytes + DSI_HFP_PACKET_EXTRA_SIZE;

	/*
	 * XXX: Need to accurately calculate LP to HS transition timeout and add
	 * it to bytes_per_line/bytes_per_frame.
	 */

	if (eotp && video_mode_format == VIDEO_MODE_BURST)
		bytes_per_line += DSI_EOTP_PACKET_SIZE;

	bytes_per_frame = vsync * bytes_per_line + vbp * bytes_per_line +
		vactive * bytes_per_line + vfp * bytes_per_line;

	if (eotp &&
	    (video_mode_format == VIDEO_MODE_NON_BURST_WITH_SYNC_PULSE ||
	     video_mode_format == VIDEO_MODE_NON_BURST_WITH_SYNC_EVENTS))
		bytes_per_frame += DSI_EOTP_PACKET_SIZE;

	num_frames = drm_mode_vrefresh(mode);
	bytes_per_x_frames = num_frames * bytes_per_frame;

	bytes_per_x_frames_x_lanes = bytes_per_x_frames / lane_count;

	/* the dsi clock is divided by 2 in the hardware to get dsi ddr clock */
	dsi_bit_clock_hz = bytes_per_x_frames_x_lanes * 8;
	dsi_clk = dsi_bit_clock_hz / 1000;

	if (eotp && video_mode_format == VIDEO_MODE_BURST)
		dsi_clk *= 2;

	return dsi_clk;
}

#else

static u32 intel_get_bits_per_pixel(struct intel_dsi *intel_dsi)
{
	u32 bpp;

	switch (intel_dsi->pixel_format) {
	default:
	case VID_MODE_FORMAT_RGB888:
	case VID_MODE_FORMAT_RGB666_LOOSE:
		bpp = 24;
		break;
	case VID_MODE_FORMAT_RGB666:
		bpp = 18;
		break;
	case VID_MODE_FORMAT_RGB565:
		bpp = 16;
		break;
	}

	return bpp;
}

void adjust_pclk_for_dual_link(struct intel_dsi *intel_dsi,
				struct drm_display_mode *mode, u32 *pclk)
{
	/* In dual link mode each port needs half of pixel clock */
	*pclk = *pclk / 2;

	/*
	 * If pixel_overlap needed by panel, we need to	increase the pixel
	 * clock for extra pixels.
	 */
	if (intel_dsi->dual_link & MIPI_DUAL_LINK_FRONT_BACK)
		*pclk += DIV_ROUND_UP(mode->vtotal * intel_dsi->pixel_overlap *
							mode->vrefresh, 1000);
}

void adjust_pclk_for_burst_mode(u32 *pclk, u16 burst_mode_ratio)
{
	*pclk = DIV_ROUND_UP(*pclk * burst_mode_ratio, 100);
}


/* To recalculate the pclk considering dual link and Burst mode */
static u32 intel_drrs_calc_pclk(struct intel_dsi *intel_dsi,
					struct drm_display_mode *mode)
{
	u32 pclk;
	int pkt_pixel_size;		/* in bits */

	pclk = mode->clock;

	pkt_pixel_size = intel_get_bits_per_pixel(intel_dsi);

	/* In dual link mode each port needs half of pixel clock */
	if (intel_dsi->dual_link)
		adjust_pclk_for_dual_link(intel_dsi, mode, &pclk);

	/* Retaining the same Burst mode ratio for DRRS. Need to be tested */
	if (intel_dsi->burst_mode_ratio > 100)
		adjust_pclk_for_burst_mode(&pclk, intel_dsi->burst_mode_ratio);

	DRM_DEBUG_KMS("mode->clock : %d, pclk : %d\n", mode->clock, pclk);
	return pclk;
}

/* Get DSI clock from pixel clock */
static u32 dsi_clk_from_pclk(struct intel_dsi *intel_dsi,
					struct drm_display_mode *mode)
{
	u32 dsi_clk_khz;
	u32 bpp;
	u32 pclk;

	bpp = intel_get_bits_per_pixel(intel_dsi);

	pclk = intel_drrs_calc_pclk(intel_dsi, mode);

	/*
	 * DSI data rate = pixel clock * bits per pixel / lane count
	 * pixel clock is converted from KHz to Hz
	 */
	dsi_clk_khz = DIV_ROUND_CLOSEST(pclk * bpp, intel_dsi->lane_count);

	return dsi_clk_khz;
}

#endif

static int dsi_calc_mnp(struct drm_i915_private *dev_priv,
				u32 dsi_clk, struct dsi_mnp *dsi_mnp)
{
	u32 m, n, p;
	u32 ref_clk;
	u32 error;
	u32 tmp_error;
	int target_dsi_clk;
	int calc_dsi_clk;
	u32 calc_m;
	u32 calc_p;
	u32 m_seed;
	u32 m_start, m_limit;
	u32 n_limit;
	u32 p_limit;


	/* dsi_clk is expected in KHZ */
	if (dsi_clk < 300000 || dsi_clk > 1150000) {
		DRM_ERROR("DSI CLK Out of Range\n");
		return -ECHRNG;
	}

	if (IS_CHERRYVIEW(dev_priv->dev)) {
		ref_clk = 100000;
		m_start = 70;
		m_limit = 96;
		n_limit = 4;
		p_limit = 6;
	} else if (IS_VALLEYVIEW(dev_priv->dev)) {
		ref_clk = 25000;
		m_start = 62;
		m_limit = 92;
		n_limit = 1;
		p_limit = 6;
	} else {
		DRM_ERROR("Unsupported device\n");
		return -ENODEV;
	}

	target_dsi_clk = dsi_clk;
	error = 0xFFFFFFFF;
	tmp_error = 0xFFFFFFFF;
	calc_m = 0;
	calc_p = 0;

	for (m = m_start; m <= m_limit; m++) {
		for (p = 2; p <= p_limit; p++) {
			/* Find the optimal m and p divisors
			with minimal error +/- the required clock */
			calc_dsi_clk = (m * ref_clk) / (p * n_limit);
			if (calc_dsi_clk == target_dsi_clk) {
				calc_m = m;
				calc_p = p;
				error = 0;
				break;
			} else
				tmp_error = abs(target_dsi_clk - calc_dsi_clk);

			if (tmp_error < error) {
				error = tmp_error;
				calc_m = m;
				calc_p = p;
			}
		}

		if (error == 0)
			break;
	}

	m_seed = lfsr_converts[calc_m - 62];
	n = n_limit;
	dsi_mnp->dsi_pll_ctrl = 1 << (DSI_PLL_P1_POST_DIV_SHIFT + calc_p - 2);

	if (IS_CHERRYVIEW(dev_priv->dev))
		dsi_mnp->dsi_pll_div = (n/2) << DSI_PLL_N1_DIV_SHIFT |
			m_seed << DSI_PLL_M1_DIV_SHIFT;
	else
		dsi_mnp->dsi_pll_div = (n - 1) << DSI_PLL_N1_DIV_SHIFT |
			m_seed << DSI_PLL_M1_DIV_SHIFT;

	DRM_DEBUG_KMS("calc_m %u, calc_p %u, m_seed %u\n", (unsigned int)calc_m,
				(unsigned int)calc_p, (unsigned int)m_seed);

	return 0;
}

struct dsi_mnp dsi_mnp;

/*
 * vlv_dsi_pll_reg_configure:
 *	Function to configure the CCK registers for PLL control and dividers
 *
 * pll		: Pll that is getting configure
 * dsi_mnp	: Struct with divider values
 * pll_enable	: Flag to indicate whether it is a fresh pll enable call or
 *		  call on DRRS purpose
 */
static void vlv_dsi_pll_reg_configure(struct intel_encoder *encoder,
				struct dsi_mnp *dsi_mnp, bool pll_enable)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum pipe pipe;

	if (!intel_crtc)
		return;

	pipe = intel_crtc->pipe;

	if (pll_enable) {
		vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, 0);

		dsi_mnp->dsi_pll_ctrl |= DSI_PLL_CLK_GATE_DSI0_DSIPLL;

		/* Enable DSI1 pll for DSI Port C & DSI Dual link*/
		if ((pipe == PIPE_B) || intel_dsi->dual_link)
			dsi_mnp->dsi_pll_ctrl |= DSI_PLL_CLK_GATE_DSI1_DSIPLL;
	} else {

		/*
		 * Updating the M1, N1, P1 div values alone on the
		 * CCK registers. these new values are abstracted from
		 * the dsi_mnp struction
		 */
		dsi_mnp->dsi_pll_ctrl =
			(dsi_mnp->dsi_pll_ctrl & DSI_PLL_P1_POST_DIV_MASK) |
			(vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL) &
			~DSI_PLL_P1_POST_DIV_MASK);
		dsi_mnp->dsi_pll_div = (dsi_mnp->dsi_pll_div &
			(DSI_PLL_M1_DIV_MASK | DSI_PLL_N1_DIV_MASK)) |
			(vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_DIVIDER)
			& ~(DSI_PLL_M1_DIV_MASK | DSI_PLL_N1_DIV_MASK));
	}

	DRM_DEBUG("dsi_pll: div %08x, ctrl %08x\n",
				dsi_mnp->dsi_pll_div, dsi_mnp->dsi_pll_ctrl);

	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_DIVIDER, dsi_mnp->dsi_pll_div);
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, dsi_mnp->dsi_pll_ctrl);

	return;
}

/*
 * vlv_drrs_configure_dsi_pll:
 *	Function to configure the PLL dividers and bring the new values
 * into effect by power cycling the VCO. This power cycle is supposed
 * to be completed within the vblank period. This is software implementation
 * and depends on the CCK register access. Needs to be tested thoroughly.
 *
 * encoder	: target encoder
 * dsi_mnp	: struct with pll divider values
 */
int vlv_drrs_configure_dsi_pll(struct intel_encoder *encoder,
						struct dsi_mnp *dsi_mnp)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	struct dsi_drrs *dsi_drrs = &intel_dsi->dsi_drrs;
	struct intel_mipi_drrs_work *work = dsi_drrs->mipi_drrs_work;
	enum pipe pipe;
	u32 dsl_offset, dsl, dsl_end;
	u32 vactive, vtotal, vblank, vblank_30_percent, vblank_70_percent;
	unsigned long timeout;

	if (!intel_crtc)
		return -EPERM;

	pipe = intel_crtc->pipe;
	dsl_offset = PIPEDSL(pipe);

	vlv_dsi_pll_reg_configure(encoder, dsi_mnp, false);

	DRM_DEBUG("dsi_mnp:: ctrl: 0x%X, div: 0x%X\n", dsi_mnp->dsi_pll_ctrl,
							dsi_mnp->dsi_pll_div);

	dsi_mnp->dsi_pll_ctrl &= (~DSI_PLL_VCO_EN);

	vtotal = I915_READ(VTOTAL(pipe));
	vactive = (vtotal & VERTICAL_ACTIVE_DISPLAY_MASK);
	vtotal = (vtotal & VERTICAL_TOTAL_DISPLAY_MASK) >>
					VERTICAL_TOTAL_DISPLAY_OFFSET;
	vblank = vtotal - vactive;
	vblank_30_percent = vactive + DIV_ROUND_UP((vblank * 3), 10);
	vblank_70_percent = vactive + DIV_ROUND_UP((vblank * 7), 10);

	timeout = jiffies + msecs_to_jiffies(DSI_DRRS_PLL_CONFIG_TIMEOUT_MS);

tap_vblank_start:
	do {
		if (atomic_read(&work->abort_wait_loop) == 1) {
			DRM_DEBUG_KMS("Aborting the pll update\n");
			return -EPERM;
		}

		if (time_after(jiffies, timeout)) {
			DRM_DEBUG("Timeout at waiting for Vblank\n");
			return -ETIMEDOUT;
		}

		dsl = (I915_READ(dsl_offset) & DSL_LINEMASK_GEN3);

	} while (dsl <= vactive || dsl > vblank_30_percent);

	mutex_lock(&dev_priv->dpio_lock);

	dsl_end = I915_READ(dsl_offset) & DSL_LINEMASK_GEN3;

	/*
	 * Did we cross Vblank due to delay in mutex acquirement?
	 * Keeping two scanlines in vblank as buffer for ops.
	 */
	if (dsl_end < vactive || dsl_end > vblank_70_percent) {
		mutex_unlock(&dev_priv->dpio_lock);
		goto tap_vblank_start;
	}

	/* Toggle the VCO_EN to bring in the new dividers values */
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, dsi_mnp->dsi_pll_ctrl);
	dsi_mnp->dsi_pll_ctrl |= DSI_PLL_VCO_EN;
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, dsi_mnp->dsi_pll_ctrl);

	dsl_end = I915_READ(dsl_offset) & DSL_LINEMASK_GEN3;

	mutex_unlock(&dev_priv->dpio_lock);

	if (wait_for(I915_READ(PIPECONF(pipe)) &
					PIPECONF_DSI_PLL_LOCKED, 20)) {
		DRM_ERROR("DSI PLL lock failed\n");
		return -1;
	}

	DRM_DEBUG("PLL Changed between DSL: %u, %u\n", dsl, dsl_end);
	DRM_DEBUG("DSI PLL locked\n");
	return 0;
}

/*
 * vlv_dsi_mnp_calculate_for_mode:
 *	calculates the dsi_mnp values for a given mode
 *
 * encoder	: Target encoder
 * dsi_mnp	: output struct to store divider values
 * mode		: Input mode for which mnp is calculated
 */
int vlv_dsi_mnp_calculate_for_mode(struct intel_encoder *encoder,
				struct dsi_mnp *dsi_mnp,
				struct drm_display_mode *mode)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	u32 dsi_clk, ret;

	dsi_clk = dsi_clk_from_pclk(intel_dsi, mode);

	DRM_DEBUG("Mode->clk: %u, dsi_clk: %u\n", mode->clock, dsi_clk);

	ret = dsi_calc_mnp(dev_priv, dsi_clk, dsi_mnp);
	if (ret)
		DRM_DEBUG("dsi_calc_mnp failed\n");
	else
		DRM_DEBUG("dsi_mnp: ctrl : 0x%X, div : 0x%X\n",
						dsi_mnp->dsi_pll_ctrl,
							dsi_mnp->dsi_pll_div);
	return ret;
}

/*
 * XXX: The muxing and gating is hard coded for now. Need to add support for
 * sharing PLLs with two DSI outputs.
 */
static void vlv_configure_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	struct dsi_drrs *drrs = &intel_dsi->dsi_drrs;
	struct intel_connector *intel_connector = intel_dsi->attached_connector;
	enum pipe pipe = intel_crtc->pipe;
	int ret;

	ret = vlv_dsi_mnp_calculate_for_mode(encoder, &dsi_mnp,
					intel_connector->panel.fixed_mode);
	if (ret < 0) {
		DRM_ERROR("dsi_mnp calculations failed\n");
		return;
	}
	drrs->mnp[DRRS_HIGH_RR] = dsi_mnp;

	if (dev_priv->drrs[pipe] && dev_priv->drrs[pipe]->has_drrs &&
				intel_connector->panel.downclock_mode) {
		ret = vlv_dsi_mnp_calculate_for_mode(encoder,
					&drrs->mnp[DRRS_LOW_RR],
					intel_connector->panel.downclock_mode);
		if (ret < 0) {
			DRM_ERROR("dsi_mnp calculations failed\n");
			return;
		}
	}

	vlv_dsi_pll_reg_configure(encoder, &dsi_mnp, true);
}

void vlv_enable_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	u32 tmp;

	DRM_DEBUG_KMS("\n");

	mutex_lock(&dev_priv->dpio_lock);

	vlv_configure_dsi_pll(encoder);

	/* wait at least 0.5 us after ungating before enabling VCO */
	usleep_range(1, 10);

	I915_WRITE(_DPLL_A, I915_READ(_DPLL_A) | DPLL_REFA_CLK_ENABLE_VLV);
	/*
	 * Clock settle time. DSI PLL will be used
	 * for DSI. But the palette registers
	 * need REF clock of DPLLA or B to be
	 * ON for functioning.
	 * This settle time is required as DPLLA will be
	 * unused earlier. Without this delay, system
	 * goes to an unstable condition and throws
	 * crash warnings.
	 */
	udelay(1000);


	if (IS_CHERRYVIEW(dev_priv->dev) && STEP_BELOW(STEP_B1))
		tmp = dsi_mnp.dsi_pll_ctrl;
	else
		tmp = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);

	tmp |= DSI_PLL_VCO_EN;
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, tmp);

	if (wait_for(vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL) &
							0x1, 20)) {
		mutex_unlock(&dev_priv->dpio_lock);
		DRM_ERROR("DSI PLL lock failed\n");
		return;
	}
	mutex_unlock(&dev_priv->dpio_lock);

	DRM_DEBUG_KMS("DSI PLL Locked\n");
}

void vlv_disable_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	u32 tmp;

	DRM_DEBUG_KMS("\n");

	mutex_lock(&dev_priv->dpio_lock);

	if (IS_CHERRYVIEW(dev_priv->dev) && STEP_BELOW(STEP_B1))
		tmp = dsi_mnp.dsi_pll_ctrl;
	else
		tmp = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);

	tmp &= ~DSI_PLL_VCO_EN;
	tmp |= DSI_PLL_LDO_GATE;
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, tmp);

	mutex_unlock(&dev_priv->dpio_lock);
}

static void assert_bpp_mismatch(int pixel_format, int pipe_bpp)
{
	int bpp;

	switch (pixel_format) {
	default:
	case VID_MODE_FORMAT_RGB888:
	case VID_MODE_FORMAT_RGB666_LOOSE:
		bpp = 24;
	break;
	case VID_MODE_FORMAT_RGB666:
		bpp = 18;
	break;
	case VID_MODE_FORMAT_RGB565:
		bpp = 16;
		break;
	}
	WARN(bpp != pipe_bpp,
		"bpp match assertion failure (expected %d, current %d)\n",
		bpp, pipe_bpp);
}

u32 vlv_get_dsi_pclk(struct intel_encoder *encoder, int pipe_bpp)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	u32 dsi_clock, pclk;
	u32 pll_ctl, pll_div;
	u32 m = 0, p = 0;
	int refclk = 25000;
	int i;

	DRM_DEBUG_KMS("\n");

	mutex_lock(&dev_priv->dpio_lock);
	pll_ctl = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);
	pll_div = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_DIVIDER);
	mutex_unlock(&dev_priv->dpio_lock);

	/* mask out other bits and extract the P1 divisor */
	pll_ctl &= DSI_PLL_P1_POST_DIV_MASK;
	pll_ctl = pll_ctl >> (DSI_PLL_P1_POST_DIV_SHIFT - 2);

	/* mask out the other bits and extract the M1 divisor */
	pll_div &= DSI_PLL_M1_DIV_MASK;
	pll_div = pll_div >> DSI_PLL_M1_DIV_SHIFT;

	while (pll_ctl) {
		pll_ctl = pll_ctl >> 1;
		p++;
	}
	p--;

	if (!p) {
		DRM_ERROR("wrong P1 divisor\n");
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(lfsr_converts); i++) {
		if (lfsr_converts[i] == pll_div)
			break;
	}

	if (i == ARRAY_SIZE(lfsr_converts)) {
		DRM_ERROR("wrong m_seed programmed\n");
		return 0;
	}

	m = i + 62;

	dsi_clock = (m * refclk) / p;

	/* pixel_format and pipe_bpp should agree */
	assert_bpp_mismatch(intel_dsi->pixel_format, pipe_bpp);

	pclk = DIV_ROUND_CLOSEST(dsi_clock * intel_dsi->lane_count, pipe_bpp);

	return pclk;
}
