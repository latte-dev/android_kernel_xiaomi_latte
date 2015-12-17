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

#ifndef __PROGRAM_LOAD_H_INCLUDED__
#define __PROGRAM_LOAD_H_INCLUDED__

#include <stdbool.h>

extern bool program_load(
	const cell_id_t			ID,
	const firmware_h		firmware);

#endif /* __PROGRAM_LOAD_H_INCLUDED__ */
