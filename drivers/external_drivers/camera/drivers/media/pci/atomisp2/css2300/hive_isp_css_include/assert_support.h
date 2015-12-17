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

#ifndef __ASSERT_SUPPORT_H_INCLUDED__
#define __ASSERT_SUPPORT_H_INCLUDED__

#if defined(_MSC_VER)
#include "assert.h"
#define OP___assert(cnd) assert(cnd)
#elif defined(__HIVECC)

/*
 * Enabling assert on cells has too many side effects, it should
 * by default be limited to the unsched CSIM mode, or to only
 * controller type processors. Presently there are not controls
 * in place for that
 */
/* #define OP___assert(cnd) OP___csim_assert(cnd) */
#define OP___assert(cnd) ((void)0)

#elif defined(__KERNEL__) /* a.o. Android builds */

#include "sh_css_debug.h"
#define __symbol2value( x ) #x
#define __symbol2string( x ) __symbol2value( x )
#define assert( expression )                                            \
	do {                                                            \
		if (!(expression))                                      \
			sh_css_dtrace(SH_DBG_ERROR, "%s",               \
				"Assertion failed: " #expression        \
				  ", file " __FILE__                    \
				  ", line " __symbol2string( __LINE__ ) \
				  ".\n" );                              \
	} while (0)

#define OP___assert(cnd) assert(cnd)

#elif defined(__FIST__)

#include "assert.h"
#define OP___assert(cnd) assert(cnd)

#elif defined(__GNUC__)
#include "assert.h"
#define OP___assert(cnd) assert(cnd)
#else /* default is for unknown environments */
#define assert(cnd) ((void)0)
#endif

#endif /* __ASSERT_SUPPORT_H_INCLUDED__ */
