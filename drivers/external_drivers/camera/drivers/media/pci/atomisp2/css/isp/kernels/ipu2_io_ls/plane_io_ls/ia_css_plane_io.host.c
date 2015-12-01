/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/

#include "ia_css_plane_io.host.h"
#include "dma.h"
#include "math_support.h"
#ifndef IA_CSS_NO_DEBUG
#include "ia_css_debug.h"
#endif
#include "ia_css_isp_params.h"
#include "ia_css_frame.h"

static void
plane_io_config(struct ia_css_plane_io_config *io_config, unsigned id,
	const struct ia_css_frame_info *in_frame_info,
	const struct ia_css_frame *out_frame)
{
	unsigned uv_subsampling = (id > 0);
	struct ia_css_common_io_config *get = &io_config->get_plane_io_config[id];
	struct ia_css_common_io_config *put = &io_config->put_plane_io_config[id];
	struct dma_port_config config;
	unsigned ddr_bits_per_element = 0;
	unsigned ddr_elems_per_word = 0;

	/* get */
	ia_css_dma_configure_from_info(&config, in_frame_info);
	/* the base address of the input frame will be set in the ISP */
	get->width = in_frame_info->res.width >> uv_subsampling;
	get->height = in_frame_info->res.height >> uv_subsampling;
	get->stride = config.stride >> uv_subsampling;
	ddr_bits_per_element = ia_css_elems_bytes_from_info(in_frame_info) * 8;
	ddr_elems_per_word = ceil_div(HIVE_ISP_DDR_WORD_BITS, ddr_bits_per_element);
	get->ddr_elems_per_word = ddr_elems_per_word;

	/* put */
	ia_css_dma_configure_from_info(&config, &out_frame->info);
	put->base_address = out_frame->data;
	put->width = out_frame->info.res.width >> uv_subsampling;
	put->height = out_frame->info.res.height >> uv_subsampling;
	put->stride = config.stride >> uv_subsampling;
	ddr_bits_per_element = ia_css_elems_bytes_from_info(&out_frame->info) * 8;
	ddr_elems_per_word = ceil_div(HIVE_ISP_DDR_WORD_BITS, ddr_bits_per_element);
	put->ddr_elems_per_word = ddr_elems_per_word;
}

void
ia_css_plane_io_config(
	const struct ia_css_binary *binary,
	const struct sh_css_binary_args *args)
{
	unsigned id;
	const struct ia_css_frame *in_frame = args->in_frame;
	const struct ia_css_frame **out_frame = (const struct ia_css_frame **)&args->out_frame;
	const struct ia_css_frame_info *in_frame_info = (in_frame) ? &in_frame->info : &binary->in_frame_info;

	unsigned size_io_config = 0;
	unsigned offset = 0;

	if (binary->info->mem_offsets.offsets.param) {
		size_io_config = binary->info->mem_offsets.offsets.param->dmem.plane_io_config.size;
		offset = binary->info->mem_offsets.offsets.param->dmem.plane_io_config.offset;
	}

	if (size_io_config) {
		struct ia_css_plane_io_config *io_config =
			(struct ia_css_plane_io_config *)&binary->mem_params.params[IA_CSS_PARAM_CLASS_PARAM][IA_CSS_ISP_DMEM].address[offset];
		for (id = 0; id < PLANE_IO_LS_NUM_PLANES; id++)
			plane_io_config(io_config, id, in_frame_info, out_frame[0]);
	}
}


