/**
 * Copyright (c) 2010 - 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
**/

#include "ia_css_i_rmgr.h"

#include <stdbool.h>
#include <assert_support.h>


void ia_css_i_host_rmgr_init(void)
{
	ia_css_i_host_rmgr_init_vbuf(vbuf_ref);
	ia_css_i_host_rmgr_init_vbuf(vbuf_write);
	ia_css_i_host_rmgr_init_vbuf(hmm_buffer_pool);
}

void ia_css_i_host_rmgr_uninit(void)
{
	ia_css_i_host_rmgr_uninit_vbuf(hmm_buffer_pool);
	ia_css_i_host_rmgr_uninit_vbuf(vbuf_write);
	ia_css_i_host_rmgr_uninit_vbuf(vbuf_ref);
}

