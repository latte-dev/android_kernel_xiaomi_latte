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

/****************************************************************
 *
 * Time   : 2012-09-06, 11:16.
 * Author : zhengjie.lu@intel.com
 * Comment:
 * - Initial version.
 *
 ****************************************************************/

#ifndef __SW_EVENT_PUBLIC_H_INCLUDED__
#define __SW_EVENT_PUBLIC_H_INCLUDED__

#include <stdbool.h>
#include "system_types.h"

/**
 * @brief Encode the information into the software-event.
 * Encode a certain amount of information into a signel software-event.
 *
 * @param[in]	in	The inputs of the encoder.
 * @param[in]	nr	The number of inputs.
 * @param[out]	out	The output of the encoder.
 *
 * @return true if it is successfull.
 */
STORAGE_CLASS_SW_EVENT_H bool encode_sw_event(
	uint32_t	*in,
	uint32_t	nr,
	uint32_t	*out);
#endif /* __SW_EVENT_PUBLIC_H_INCLUDED__ */

