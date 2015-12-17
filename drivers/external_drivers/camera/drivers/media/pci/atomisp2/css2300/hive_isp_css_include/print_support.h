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

#ifndef __PRINT_SUPPORT_H_INCLUDED__
#define __PRINT_SUPPORT_H_INCLUDED__

#if defined(_MSC_VER)

#include <stdio.h>

#elif defined(__HIVECC)
/*
 * Use OP___dump()
 */

#elif defined(__KERNEL__)
/* printk() */

#elif defined(__FIST__)

#elif defined(__GNUC__)

#include <stdio.h>

#else /* default is for unknown environments */

/* ? */

#endif

#endif /* __PRINT_SUPPORT_H_INCLUDED__ */
