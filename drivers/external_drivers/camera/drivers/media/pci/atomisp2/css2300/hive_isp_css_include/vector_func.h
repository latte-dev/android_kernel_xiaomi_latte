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

#ifndef __VECTOR_FUNC_H_INCLUDED__
#define __VECTOR_FUNC_H_INCLUDED__

#include "storage_class.h"

#include "vector_func_local.h"

#ifndef __INLINE_VECTOR_FUNC__
#define STORAGE_CLASS_VECTOR_FUNC_H STORAGE_CLASS_EXTERN
#define STORAGE_CLASS_VECTOR_FUNC_C 
#include "vector_func_public.h"
#else  /* __INLINE_VECTOR_FUNC__ */
#define STORAGE_CLASS_VECTOR_FUNC_H STORAGE_CLASS_INLINE
#define STORAGE_CLASS_VECTOR_FUNC_C STORAGE_CLASS_INLINE
#include "vector_func_private.h"
#endif /* __INLINE_VECTOR_FUNC__ */

#endif /* __VECTOR_FUNC_H_INCLUDED__ */
