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

#ifndef __STDBOOL_H_INCLUDED__
#define __STDBOOL_H_INCLUDED__

#ifndef __cplusplus

#if defined(_MSC_VER)
typedef unsigned int bool;

#define true	1
#define false	0

#elif defined(__GNUC__)
#ifndef __KERNEL__
/* Linux kernel driver defines stdbool types in types.h */
typedef unsigned int	bool;

#define true	1
#define false	0

/*
 *Alternatively
 * 
typedef enum {
	false = 0,
	true
} bool;
 */
#endif
#endif

#endif /* __cplusplus */

#endif /* __STDBOOL_H_INCLUDED__ */
