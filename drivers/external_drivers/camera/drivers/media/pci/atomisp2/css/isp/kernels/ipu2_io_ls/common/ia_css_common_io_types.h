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

#ifndef __IA_CSS_COMMON_IPU2_IO_TYPES_H
#define __IA_CSS_COMMON_IPU2_IO_TYPES_H

/** @file
 * CSS-API header for common ipu2_io_ls parameters
 */

#define MAX_IO_DMA_CHANNELS 3	/**< # of dma channels per configuration */

/** common IPU2_IO_LS configuration
 *
 * ISP 2.7: IPU2_IO_LS is used
 */
struct ia_css_common_io_config {
	unsigned base_address;		/**< ddr base address */
	unsigned width;			/**< frame buffer width */
	unsigned height;		/**< frame buffer height */
	unsigned stride;		/**< frame buffer stride */
	unsigned ddr_elems_per_word;	/**< elems per word */
	unsigned dma_channel[MAX_IO_DMA_CHANNELS]; /**< dma channels */
};

#endif /* __IA_CSS_COMMON_IPU2_IO_TYPES_H */
