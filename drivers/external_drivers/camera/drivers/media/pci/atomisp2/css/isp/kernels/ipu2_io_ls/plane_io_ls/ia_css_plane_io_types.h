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

#ifndef __IA_CSS_PLANE_IO_TYPES_H
#define __IA_CSS_PLANE_IO_TYPES_H

/** @file
 * CSS-API header for ipu2_io_ls/planie_io parameters
 */
#include "../common/ia_css_common_io_types.h"

#define PLANE_IO_LS_NUM_PLANES       3	/**< # of planes per io config */

/** IPU2_IO_LS Plane IO configuration
 *
 * ISP 2.7: plane_io is used
 */
struct ia_css_plane_io_config {
	struct ia_css_common_io_config get_plane_io_config[PLANE_IO_LS_NUM_PLANES]; /**< get config */
	struct ia_css_common_io_config put_plane_io_config[PLANE_IO_LS_NUM_PLANES]; /**< put config */
};

#endif /* __IA_CSS_PLANE_IO_TYPES_H */

