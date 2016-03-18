/* -------------------------------------------------------------------------
 * Copyright (C) 2014-2015, Intel Corporation
 *
 * Derived from:
 *  gslX68X.c
 *  Copyright (C) 2010-2015, Shanghai Sileadinc Co.Ltd
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * ------------------------------------------------------------------------- */
#ifndef __SILEAD_HEADER__H__
#define __SILEAD_HEADER__H__

#define	POINT_MAX		10
struct gsl_touch_info {
	int x[10];
	int y[10];
	int id[10];
	int finger_num;
};

typedef struct {
	unsigned int i;
	unsigned int j;
	unsigned int min; /* distance min */
	unsigned int d[POINT_MAX][POINT_MAX];
} gsl_DISTANCE_TYPE;

typedef union {
	struct {
		unsigned y:12;
		unsigned key:1;
		unsigned fill:1;
		unsigned able:1;
		unsigned predict:1;
		unsigned x:16;
	};
	struct {
		unsigned y:13;
		unsigned rev_2:3;
		unsigned x:16;
	} dis;
	unsigned int all;
} gsl_POINT_TYPE;

typedef union {
	struct {
		unsigned delay:8;
		unsigned report:8;
		unsigned dele:8;
		unsigned rev_1:4;
		unsigned pres:1;
		unsigned mask:1;
		unsigned able:1;
		unsigned init:1;
	};
	unsigned int all;
} gsl_DELAY_TYPE;

typedef union {
	struct {
		unsigned rev_0:8;
		unsigned rev_1:8;

		unsigned rev_2:4;
		unsigned active_prev:1;
		unsigned menu:1;
		unsigned cc_128:1;
		unsigned ex:1;

		unsigned interpolation:4;
		unsigned active:1;
		unsigned only:1;
		unsigned mask:1;
		unsigned reset:1;
	};
	unsigned int all;
} gsl_STATE_TYPE;

typedef struct {
	unsigned int rate;
	unsigned int dis;
	gsl_POINT_TYPE coor;
} gsl_EDGE_TYPE;

typedef union {
	struct {
		short y;
		short x;
	};
	unsigned int all;
} gsl_DECIMAL_TYPE;

typedef union {
	struct {
			unsigned over_report_mask:1;
			unsigned opposite_x:1;
			unsigned opposite_y:1;
			unsigned opposite_xy:1;
			unsigned line:1;
			unsigned line_neg:1;
			unsigned line_half:1;
			unsigned middle_drv:1;

			unsigned key_only_one:1;
			unsigned key_line:1;
			unsigned refe_rt:1;
			unsigned refe_var:1;
			unsigned base_median:1;
			unsigned key_rt:1;
			unsigned refe_reset:1;
			unsigned sub_cross:1;

			unsigned row_neg:1;
			unsigned sub_line_coe:1;
			unsigned sub_row_coe:1;
			unsigned c2f_able:1;
			unsigned thumb:1;
			unsigned graph_h:1;
			unsigned init_repeat:1;
			unsigned near_reset_able:1;

			unsigned emb_dead:1;
			unsigned emb_point_mask:1;
			unsigned interpolation:1;
			unsigned sum2_able:1;
			unsigned reduce_pin:1;
			unsigned drv_order_ex:1;
			unsigned id_over:1;
			unsigned rev_1:1;
	};
	unsigned int all;
} gsl_FLAG_TYPE;
typedef union {
	struct {
			unsigned reso_y:1;
			unsigned reso_x:1;
			unsigned screen_core:1;
			unsigned screen_real:1;
			unsigned ignore_pri:1;
			unsigned id_prec_able:1;
			unsigned first_avg:1;
			unsigned round:1;

			unsigned stretch_off:1;
			unsigned rev_7:7;

			unsigned rev_x:16;
	};
	unsigned int all;
} gsl_ID_FLAG_TYPE;

static union {
	struct {
		unsigned char id;
		unsigned char num;
		unsigned char rev_1;
		unsigned char rev_2;
	};
	unsigned int all;
} prec_id;

unsigned int gsl_mask_tiaoping(void);
unsigned int gsl_version_id(void);
void gsl_alg_id_main(struct gsl_touch_info *cinfo);
void gsl_DataInit(unsigned int *conf);

#endif
