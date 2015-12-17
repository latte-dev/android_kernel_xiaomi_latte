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

#ifndef __VAMEM_GLOBAL_H_INCLUDED__
#define __VAMEM_GLOBAL_H_INCLUDED__

#include <stdint.h>

#define IS_VAMEM_VERSION_1

/* (log) stepsize of linear interpolation */
#define VAMEM_INTERP_STEP_LOG2	0
#define VAMEM_INTERP_STEP		(1<<VAMEM_INTERP_STEP_LOG2)
/* (physical) size of the tables */
#define VAMEM_TABLE_UNIT_SIZE	ISP_VAMEM_DEPTH
/* (logical) size of the tables */
#define VAMEM_TABLE_UNIT_STEP	VAMEM_TABLE_UNIT_SIZE
/* Number of tables */
#define VAMEM_TABLE_UNIT_COUNT	(ISP_VAMEM_DEPTH/VAMEM_TABLE_UNIT_STEP)

typedef uint16_t				vamem_data_t;

#endif /* __VAMEM_GLOBAL_H_INCLUDED__ */
