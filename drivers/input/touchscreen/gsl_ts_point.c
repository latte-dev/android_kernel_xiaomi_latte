/* drivers/input/touchscreen/gsl_ts_point.c
 *
 * 2010 - 2016 SLIEAD Technology.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the SLIEAD's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#include "linux/module.h"
#include "silead.h"

#define	GESTURE_LICH	1

#define GSL_VERSION		0xe0150914

#ifndef NULL
#define	NULL  ((void *)0)
#endif
#ifndef UINT
#define	UINT  unsigned int
#endif

#define	PP_DEEP			10
#define	PS_DEEP			10
#define PR_DEEP			10
#define AVG_DEEP		5
#define POINT_DEEP		(PP_DEEP + PS_DEEP + PR_DEEP)
#define	PRESSURE_DEEP	8
#define INTE_INIT		8
#define	CONFIG_LENGTH	512
#define TRUE			1
#define FALSE			0
#define FLAG_ABLE		(0x4<<12)
#define FLAG_FILL		(0x2<<12)
#define	FLAG_KEY		(0x1<<12)
#define	FLAG_COOR		(0x0fff0fff)
#define	FLAG_COOR_EX	(0xffff0fff)
#define	FLAG_ID			(0xf0000000)

static gsl_POINT_TYPE point_array[POINT_DEEP][POINT_MAX];
static gsl_POINT_TYPE *point_pointer[PP_DEEP];
static gsl_POINT_TYPE *point_stretch[PS_DEEP];
static gsl_POINT_TYPE *point_report[PR_DEEP];
static gsl_POINT_TYPE point_now[POINT_MAX];
static gsl_DELAY_TYPE point_delay[POINT_MAX];

#define	pp							point_pointer
#define	ps							point_stretch
#define	pr							point_report
#define	point_predict				pp[0]

static	gsl_STATE_TYPE global_state;
static	int point_n;
static	int point_num;
static	int prev_num;
static	unsigned int point_shake;
static	unsigned int reset_mask_send;
static	unsigned int reset_mask_max;
static	unsigned int reset_mask_count;
static	gsl_FLAG_TYPE global_flag;
static	gsl_ID_FLAG_TYPE id_flag;
static	unsigned int id_first_coe;
static	unsigned int id_speed_coe;
static	unsigned int id_static_coe;
static	unsigned int report_delay;
static	unsigned int delay_key;
static	unsigned int report_ahead;
static	unsigned int report_delete;
static	unsigned int shake_min;
static	int match_y[2];
static	int match_x[2];
static	int ignore_y[2];
static	int ignore_x[2];
static	int screen_y_max;
static	int screen_x_max;
static	int point_num_max;
static	unsigned int drv_num;
static	unsigned int sen_num;
static	unsigned int drv_num_nokey;
static	unsigned int sen_num_nokey;
static	unsigned int coordinate_correct_able;
static	unsigned char coordinate_correct_coe_x[64];
static	unsigned char coordinate_correct_coe_y[64];
static	unsigned int edge_cut[4];
static	unsigned int stretch_array[4*4*2];
static	unsigned int shake_all_array[2*8];
static	unsigned int reset_mask_dis;
static	unsigned int reset_mask_type;
static	unsigned int key_map_able;
static	unsigned int key_range_array[8*3];
static	int  filter_able;
static	unsigned int filter_coe[4];
static	unsigned int multi_x_array[4], multi_y_array[4];
static	unsigned char multi_group[4][64];
static	int ps_coe[4][8], pr_coe[4][8];
static	int point_extend;
static	unsigned int press_mask;
static	unsigned int stretch_mult;

static void SortBubble(int t[], int size)
{
	int temp = 0;
	int m, n;
	for (m = 0; m < size; m++) {
		for (n = m + 1; n < size; n++) {
			temp = t[m];
			if (temp > t[n]) {
				t[m] = t[n];
				t[n] = temp;
			}
		}
	}
}

static int Sqrt(int d)
{
	int ret = 0;
	int i;
	for (i = 14; i >= 0; i--) {
		if ((ret + (0x1 << i)) * (ret + (0x1 << i)) <= d)
			ret |= (0x1 << i);
	}
	return ret;
}

static UINT PointRange(int x0, int y0, int x1, int y1)
{
	if (x0 < 1) {
		if (x0 != x1)
			y0 = y1 + (y0 - y1)*(1 - x1)/(x0 - x1);
		x0 = 1;
	}
	if (x0 >= (int)drv_num_nokey * 64) {
		if (x0 != x1) {
			y0 = y1 + (y0 - y1) *
			((int)drv_num_nokey * 64 - x1) / (x0 - x1);
		}
			x0 = drv_num_nokey * 64 - 1;
	}
	if (y0 < 1) {
		if (y0 != y1)
			x0 = x1 + (x0 - x1) * (1 - y1) / (y0 - y1);
		y0 = 1;
	}
	if (y0 >= (int)sen_num_nokey * 64) {
		if (y0 != y1) {
			x0 = x1 + (x0 - x1) *
			((int)sen_num_nokey * 64 - y1) / (y0 - y1);
		}
		y0 = sen_num_nokey * 64-1;
	}
	if (x0 < 1)
		x0 = 1;
	if (x0 >= (int)drv_num_nokey * 64)
		x0 = drv_num_nokey * 64-1;
	if (y0 < 1)
		y0 = 1;
	if (y0 >= (int)sen_num_nokey * 64)
		y0 = sen_num_nokey * 64-1;
	return (x0 << 16) + y0;
}

static void PointCoor(void)
{
	int i;
	for (i = 0; i < point_num; i++) {
		if (global_state.ex) {
			point_now[i].all &= (FLAG_COOR_EX |
			FLAG_KEY | FLAG_ABLE);
		} else
			point_now[i].all &= (FLAG_COOR | FLAG_KEY | FLAG_ABLE);
	}
}

static void PointRepeat(void)
{

}

static void PointPointer(void)
{
	int i, pn;
	point_n++;
	if (point_n >= PP_DEEP * PS_DEEP * PR_DEEP * PRESSURE_DEEP)
		point_n = 0;
	pn = point_n % PP_DEEP;
	for (i = 0; i < PP_DEEP; i++) {
		pp[i] = point_array[pn];
		if (pn == 0)
			pn = PP_DEEP - 1;
		else
			pn--;
	}
	pn = point_n % PS_DEEP;
	for (i = 0; i < PS_DEEP; i++) {
		ps[i] = point_array[pn+PP_DEEP];
		if (pn == 0)
			pn = PS_DEEP - 1;
		else
			pn--;
	}
	pn = point_n % PR_DEEP;
	for (i = 0; i < PR_DEEP; i++) {
		pr[i] = point_array[pn+PP_DEEP+PS_DEEP];
		if (pn == 0)
			pn = PR_DEEP - 1;
		else
			pn--;
	}
	pn = point_n % PRESSURE_DEEP;
	for (i = 0; i < PRESSURE_DEEP; i++) {
		if (pn == 0)
			pn = PRESSURE_DEEP - 1;
		else
			pn--;
	}
	for (i = 0; i < POINT_MAX; i++) {
		pp[0][i].all = 0;
		ps[0][i].all = 0;
		pr[0][i].all = 0;
	}
}

static void PointPredictOne(unsigned int n)
{
	pp[0][n].all = pp[1][n].all & FLAG_COOR;
	pp[0][n].predict = 0;
}

static void PointPredictTwo(unsigned int n)
{
	int x, y;
	x = (int)pp[1][n].x * 2 - (int)pp[2][n].x;
	y = (int)pp[1][n].y * 2 - (int)pp[2][n].y;
	pp[0][n].all = PointRange(x, y, pp[1][n].x, pp[1][n].y);
	pp[0][n].predict = 1;
}


static void PointPredictThree(unsigned int n)
{
	int x, y;
	x = (int)pp[1][n].x * 5 + (int)pp[3][n].x - (int)pp[2][n].x * 4;
	x /= 2;
	y = (int)pp[1][n].y * 5 + (int)pp[3][n].y - (int)pp[2][n].y * 4;
	y /= 2;
	pp[0][n].all = PointRange(x, y, pp[1][n].x, pp[1][n].y);
	pp[0][n].predict = 1;
}

static void PointPredict(void)
{
	int i;
	for (i = 0; i < POINT_MAX; i++) {
		if (pp[1][i].all != 0) {
			if (global_state.interpolation
				|| pp[2][i].all  == 0
				|| pp[2][i].fill != 0
				|| pp[3][i].fill != 0
				|| pp[1][i].key  != 0
				|| global_state.only) {
				PointPredictOne(i);
			} else if (pp[2][i].all != 0) {
				if (pp[3][i].all != 0)
					PointPredictThree(i);
				else
					PointPredictTwo(i);
			}
			pp[0][i].all |= FLAG_FILL;
		} else
			pp[0][i].all = 0x0fff0fff;
		if (pp[1][i].key)
			pp[0][i].all |= FLAG_KEY;
	}
}

static unsigned int PointDistance(gsl_POINT_TYPE *p1, gsl_POINT_TYPE *p2)
{
	int a, b, ret;
	if (id_flag.reso_y) {
		a = p1->dis.x;
		b = p2->dis.x;
		ret = (a - b) * (a - b);
		a = p1->dis.y * 64 * (int)screen_y_max/
		(int)screen_x_max*((int)drv_num_nokey * 64) /
		((int)sen_num_nokey * 64) / 64;
		b = p2->dis.y * 64 * (int)screen_y_max/
		(int)screen_x_max*((int)drv_num_nokey * 64) /
		((int)sen_num_nokey * 64) / 64;
		ret += (a-b)*(a-b);
	} else if (id_flag.reso_x) {
		a = p1->dis.x * 64 * (int)screen_x_max/
		(int)screen_y_max*((int)sen_num_nokey * 64) /
		((int)drv_num_nokey * 64) / 64;
		b = p2->dis.x * 64 * (int)screen_x_max/
		(int)screen_y_max*((int)sen_num_nokey * 64) /
		((int)drv_num_nokey * 64) / 64;
		ret = (a-b) * (a-b);
		a = p1->dis.y;
		b = p2->dis.y;
		ret += (a-b) * (a-b);
	} else {
		a = p1->dis.x;
		b = p2->dis.x;
		ret = (a - b) * (a - b);
		a = p1->dis.y;
		b = p2->dis.y;
		ret += (a - b) * (a - b);
	}
	return ret;
}

static void DistanceInit(gsl_DISTANCE_TYPE *p)
{
	int i;
	unsigned int *p_int = &(p->d[0][0]);
	for (i = 0; i < POINT_MAX * POINT_MAX; i++)
		*p_int++ = 0x7fffffff;
}

static int DistanceMin(gsl_DISTANCE_TYPE *p)
{
	int i, j;
	p->min = 0x7fffffff;
	for (j = 0; j < POINT_MAX; j++) {
		for (i = 0; i < POINT_MAX; i++) {
			if (p->d[j][i] < p->min) {
				p->i = i;
				p->j = j;
				p->min = p->d[j][i];
			}
		}
	}
	if (p->min == 0x7fffffff)
		return 0;
	return 1;
}

static void DistanceIgnore(gsl_DISTANCE_TYPE *p)
{
	int i, j;
	for (i = 0; i < POINT_MAX; i++)
		p->d[p->j][i] = 0x7fffffff;
	for (j = 0; j < POINT_MAX; j++)
		p->d[j][p->i] = 0x7fffffff;
}

static int SpeedGet(int d)
{
	int i;
	for (i = 8; i > 0; i--) {
		if (d > 0x100 << i)
			break;
	}
	return i;
}

static void PointId(void)
{
	int i, j;
	gsl_DISTANCE_TYPE distance;
	unsigned int id_speed[POINT_MAX];
	DistanceInit(&distance);
	for (i = 0; i < POINT_MAX; i++) {
		if (pp[0][i].predict == 0 || pp[1][i].fill != 0)
			id_speed[i] = id_first_coe;
		else {
			id_speed[i] = SpeedGet(PointDistance(&pp[1][i],
			&pp[0][i]));
			j = SpeedGet(PointDistance(&pp[2][i], &pp[1][i]));
			if (id_speed[i] < (unsigned int)j)
				id_speed[i] = j;
		}
	}
	for (i = 0; i < POINT_MAX; i++) {
		if (pp[0][i].all == FLAG_COOR)
			continue;
		for (j = 0; j < point_num && j < POINT_MAX; j++) {
			distance.d[j][i] = PointDistance(&point_now[j],
			&pp[0][i]);
		}
	}
	if (point_num == 0)
		return;
	for (j = 0; j < point_num && j < POINT_MAX; j++) {
		if (DistanceMin(&distance) == 0)
			break;
		if (distance.min >= (id_static_coe +
			id_speed[distance.i] * id_speed_coe))
			continue;
		pp[0][distance.i].all = point_now[distance.j].all;
		point_now[distance.j].all = 0;
		DistanceIgnore(&distance);
	}
}

static int ClearLenPP(int i)
{
	int n;
	for (n = 0; n < PP_DEEP; n++) {
		if (pp[n][i].all)
			break;
	}
	return n;
}

static void PointNewId(void)
{
	int id, j;
	for (j = 0; j < POINT_MAX; j++)
		if ((pp[0][j].all & FLAG_COOR) == FLAG_COOR)
			pp[0][j].all = 0;
	for (j = 0; j < POINT_MAX; j++) {
		if (point_now[j].all != 0) {
			if (point_now[j].able)
				continue;
			for (id = 1; id <= POINT_MAX; id++) {
				if (ClearLenPP(id-1) > (int)(1+1)) {
					pp[0][id-1].all = point_now[j].all;
					point_now[j].all = 0;
					break;
				}
			}
		}
	}
}

static void PointOrder(void)
{
	int i;
	for (i = 0; i < POINT_MAX; i++) {
		if (pp[0][i].fill == 0)
			continue;
		if (pp[1][i].all == 0 || pp[1][i].fill != 0 ||
			filter_able == 0 || filter_able == 1) {
			pp[0][i].all = 0;
		}
	}
}

static void PointCross(void)
{
}

static void GetPointNum(gsl_POINT_TYPE *pt)
{
	int i;
	point_num = 0;
	for (i = 0; i < POINT_MAX; i++)
		if (pt[i].all != 0)
			point_num++;
}

static void PointDelay(void)
{
	int i, j;
	for (i = 0; i < POINT_MAX; i++) {
		if (report_delay == 0 && delay_key == 0) {
			point_delay[i].all = 0;
			if (pp[0][i].all)
				point_delay[i].able = 1;
			if (pr[0][i].all == 0)
				point_delay[i].mask = 0;
			continue;
		}
		if (pp[0][i].all != 0 && point_delay[i].init == 0
			&& point_delay[i].able == 0) {
			if (point_num == 0)
				continue;
			if (delay_key && pp[0][i].key) {
				point_delay[i].delay  = (delay_key >> 3*
				((point_num > 10 ? 10 : point_num) - 1)) & 0x7;
				point_delay[i].report = 0;
				point_delay[i].dele = 0;
			} else {
				point_delay[i].delay  = (report_delay  >> 3 *
						((point_num > 10 ? 10 : point_num) - 1)) & 0x7;
				point_delay[i].report = (report_ahead  >> 3 *
						((point_num > 10 ? 10 : point_num) - 1)) & 0x7;
				point_delay[i].dele = (report_delete >> 3 *
						((point_num > 10 ? 10 : point_num) - 1)) & 0x7;
				if (point_delay[i].report > point_delay[i].delay)
					point_delay[i].report = point_delay[i].delay;

				point_delay[i].report = point_delay[i].delay -
					point_delay[i].report;
				if (point_delay[i].dele > point_delay[i].report)
					point_delay[i].dele = point_delay[i].report;
				point_delay[i].dele = point_delay[i].report -
				point_delay[i].dele;
			}
			point_delay[i].init = 1;
		}
		if (pp[0][i].all == 0)
			point_delay[i].init = 0;
		if (point_delay[i].able == 0 && point_delay[i].init != 0) {
			for (j = 0; j <= (int)point_delay[i].delay; j++)
				if (pp[j][i].all == 0 || pp[j][i].fill != 0 ||
					pp[j][i].able != 0)
					break;
			if (j <= (int)point_delay[i].delay)
				continue;
			point_delay[i].able = 1;
		}
		if (pp[point_delay[i].dele][i].all == 0) {
			point_delay[i].able = 0;
			point_delay[i].mask = 0;
			continue;
		}
		if (point_delay[i].able == 0)
			continue;
		if (report_delete == 0 && point_delay[i].report) {
			if (PointDistance(&pp[point_delay[i].report][i],
				&pp[point_delay[i].report-1][i]) < 3*3) {
				point_delay[i].report--;
				if (point_delay[i].dele)
					point_delay[i].dele--;
			}
		}
	}
}

static void PointMenu(void)
{
}

static void FilterOne(int i, int *ps_c, int *pr_c, int denominator)
{
	int j;
	int x = 0, y = 0;
	pr[0][i].all = ps[0][i].all;
	if (pr[0][i].all == 0)
		return;
	if (denominator <= 0)
		return;
	for (j = 0; j < 8; j++) {
		x += (int)pr[j][i].x * (int)pr_c[j] +
			(int)ps[j][i].x * (int)ps_c[j];
		y += (int)pr[j][i].y * (int)pr_c[j] +
			(int)ps[j][i].y * (int)ps_c[j];
	}
	x = (x + denominator/2) / denominator;
	y = (y + denominator/2) / denominator;
	if (x < 0)
		x = 0;
	if (x > 0xffff)
		x = 0xffff;
	if (y < 0)
		y = 0;
	if (y > 0xfff)
		y = 0xfff;
	pr[0][i].x = x;
	pr[0][i].y = y;
}

static void PointFilter(void)
{
	int i, j;
	int ps_c[8];
	int pr_c[8];
	for (i = 0; i < POINT_MAX; i++)
		pr[0][i].all = ps[0][i].all;
	for (i = 0; i < POINT_MAX; i++) {
		if (pr[0][i].all != 0 && pr[1][i].all == 0) {
			for (j = 1; j < PR_DEEP; j++)
				pr[j][i].all = ps[0][i].all;
			for (j = 1; j < PS_DEEP; j++)
				ps[j][i].all = ps[0][i].all;
		}
	}
	if (filter_able >= 0 && filter_able <= 1)
		return;
	if (filter_able > 1) {
		for (i = 0; i < 8; i++) {
			ps_c[i] = (filter_coe[i/4] >> ((i%4)*8)) & 0xff;
			pr_c[i] = (filter_coe[i/4+2] >> ((i%4)*8)) & 0xff;
			if (ps_c[i] >= 0x80)
				ps_c[i] |= 0xffffff00;
			if (pr_c[i] >= 0x80)
				pr_c[i] |= 0xffffff00;
		}
		for (i = 0; i < POINT_MAX; i++)
			FilterOne(i, ps_c, pr_c, filter_able);
	}
}

static unsigned int KeyMap(int *drv, int *sen)
{
	typedef struct {
		unsigned int up_down, left_right;
		unsigned int coor;
	} KEY_TYPE_RANGE;
	KEY_TYPE_RANGE *key_range = (KEY_TYPE_RANGE *)key_range_array;
	int i;
	for (i = 0; i < 8; i++) {
		if ((unsigned int)*drv >= (key_range[i].up_down >> 16)
		&& (unsigned int)*drv <= (key_range[i].up_down & 0xffff)
		&& (unsigned int)*sen >= (key_range[i].left_right >> 16)
		&& (unsigned int)*sen <= (key_range[i].left_right & 0xffff)) {
			*sen = key_range[i].coor >> 16;
			*drv = key_range[i].coor & 0xffff;
			return key_range[i].coor;
		}
	}
	return 0;
}

static unsigned int ScreenResolution(gsl_POINT_TYPE *p)
{
	int x, y;
	x = p->x;
	y = p->y;
	if (p->key == FALSE) {
		y = ((y - match_y[1]) * match_y[0] + 2048)/4096;
		x = ((x - match_x[1]) * match_x[0] + 2048)/4096;
	}
	y = y * (int)screen_y_max / ((int)sen_num_nokey * 64);
	x = x * (int)screen_x_max / ((int)drv_num_nokey * 64);
	if (p->key == FALSE) {
		if (id_flag.ignore_pri == 0) {
			if (ignore_y[0] != 0 || ignore_y[1] != 0) {
				if (y < ignore_y[0])
					return 0;
				if (ignore_y[1] <= screen_y_max/2 &&
					y > screen_y_max - ignore_y[1])
					return 0;
				if (ignore_y[1] >= screen_y_max/2 &&
					y > ignore_y[1])
					return 0;
			}
			if (ignore_x[0] != 0 || ignore_x[1] != 0) {
				if (x < ignore_x[0])
					return 0;
				if (ignore_x[1] <= screen_x_max/2 &&
					x > screen_x_max - ignore_x[1])
					return 0;
				if (ignore_x[1] >= screen_x_max/2 &&
					x > ignore_x[1])
					return 0;
			}
		}
		if (y <= (int)edge_cut[2])
			y = (int)edge_cut[2] + 1;
		if (y >= screen_y_max - (int)edge_cut[3])
			y = screen_y_max - (int)edge_cut[3] - 1;
		if (x <= (int)edge_cut[0])
			x = (int)edge_cut[0] + 1;
		if (x >= screen_x_max - (int)edge_cut[1])
			x = screen_x_max - (int)edge_cut[1] - 1;
		if (global_flag.opposite_x)
			y = screen_y_max - y;
		if (global_flag.opposite_y)
			x = screen_x_max - x;
		if (global_flag.opposite_xy) {
			y ^= x;
			x ^= y;
			y ^= x;
		}
	} else {
		if (y < 0)
			y = 0;
		if (x < 0)
			x = 0;
		if ((key_map_able & 0x1) != FALSE && KeyMap(&x, &y) == 0)
			return 0;
	}
	return ((y << 16) & 0x0fff0000) + (x & 0x0000ffff);
}

static void PointReport(struct gsl_touch_info *cinfo)
{
	int i;
	unsigned int data[POINT_MAX];
	int num = 0;
	if (point_num > point_num_max && global_flag.over_report_mask != 0) {
		point_num = 0;
		cinfo->finger_num = 0;
		prec_id.all = 0;
		return;
	}
	for (i = 0; i < POINT_MAX; i++)
		data[i] = 0;
	num = 0;
	for (i = 0; i < point_num_max && i < POINT_MAX; i++) {
		if (point_delay[i].mask || point_delay[i].able == 0)
			continue;
		if (point_delay[i].report >= PR_DEEP)
			continue;
		data[num] = ScreenResolution(&pr[point_delay[i].report][i]);
		if (data[num])
			data[num++] |= (i+1)<<28;
	}
	num = 0;
	for (i = 0; i < POINT_MAX; i++) {
		if (data[i] == 0)
			continue;
		point_now[num].all = data[i];
		cinfo->x[num] = (data[i] >> 16) & 0xfff;
		cinfo->y[num] = data[i] & 0xfff;
		cinfo->id[num] = data[i] >> 28;
		num++;
	}
	for (i = num; i < POINT_MAX; i++)
		point_now[i].all = 0;
	point_num = num;
	cinfo->finger_num = point_num;
}

static void PointRound(void)
{
}

static void PointEdge(void)
{
}

static void PointStretch(void)
{
	static int save_dr[POINT_MAX], save_dn[POINT_MAX];
	typedef struct {
		int dis;
		int coe;
	} SHAKE_TYPE;
	SHAKE_TYPE *shake_all = (SHAKE_TYPE *)shake_all_array;
	int i, j;
	int dn;
	int dr;
	int dc[9], ds[9];
	int len = 8;
	unsigned int temp;
	for (i = 0; i < POINT_MAX; i++)
		ps[0][i].all = pp[0][i].all;
	for (i = 0; i < len; i++) {
		if (shake_all[i].dis == 0) {
			len = i;
			break;
		}
	}
	if (len >= 2) {
		temp = 0;
		for (i = 0; i < POINT_MAX; i++)
			if (pp[0][i].all)
				temp++;
		if (temp > 5)
			temp = 5;
		for (i = 0; i < 8 && i < len; i++) {
			if (stretch_mult)
				ds[i+1] = (stretch_mult*(temp > 1 ? temp - 1 : 0) + 0x80)
					* shake_all[i].dis / 0x80;
			else
				ds[i+1] = shake_all[i].dis;
			dc[i+1] = shake_all[i].coe;
		}
		if (shake_all[0].coe >= 128 ||
				shake_all[0].coe <= shake_all[1].coe) {
			ds[0] = ds[1];
			dc[0] = dc[1];
		} else {
			ds[0] = ds[1] + (128 - shake_all[0].coe) * (ds[1]-ds[2])/
				(shake_all[0].coe - shake_all[1].coe);
			dc[0] = 128;
		}
		for (i = 0; i < POINT_MAX; i++) {
			if (ps[1][i].all == 0) {
				for (j = 1; j < PS_DEEP; j++)
					ps[j][i].all = ps[0][i].all;
				save_dr[i] = 128;
				save_dn[i] = 0;
				continue;
			}
			if (id_flag.first_avg && point_delay[i].able == 0)
				continue;
				dn = PointDistance(&pp[0][i], &ps[1][i]);
				dn = Sqrt(dn);
				if (dn >= ds[0])
					continue;
				if (dn < save_dn[i]) {
					dr = save_dr[i];
					save_dn[i] = dn;
					ps[0][i].x = (int)ps[1][i].x +
						(((int)pp[0][i].x - (int)ps[1][i].x) * dr) / 128;
					ps[0][i].y = (int)ps[1][i].y +
						(((int)pp[0][i].y - (int)ps[1][i].y) * dr) / 128;
					continue;
				}
				for (j = 0; j <= len; j++) {
					if (j == len || dn == 0) {
						ps[0][i].x = ps[1][i].x;
						ps[0][i].y = ps[1][i].y;
						break;
					} else if (ds[j] > dn && dn >= ds[j+1]) {
						dr = dc[j+1] + ((dn - ds[j+1]) * (dc[j] - dc[j+1])) /
							(ds[j] - ds[j+1]);
						save_dr[i] = dr;
						save_dn[i] = dn;
						ps[0][i].x = (int)ps[1][i].x +
							(((int)pp[0][i].x - (int)ps[1][i].x) * dr+64) / 128;
						ps[0][i].y = (int)ps[1][i].y +
							(((int)pp[0][i].y - (int)ps[1][i].y) * dr+64) / 128;
						break;
					}
				}
		}
	} else {
		return;
	}
}

static void ResetMask(void)
{
	if (reset_mask_send)
		reset_mask_send = 0;
	if (global_state.mask)
		return;
	if (reset_mask_dis == 0 || reset_mask_type == 0)
		return;
	if (reset_mask_max == 0xfffffff1) {
		if (point_num == 0)
			reset_mask_max = 0xf0000000 + 1;
		return;
	}
	if (reset_mask_max >  0xf0000000) {
		reset_mask_max--;
		if (reset_mask_max == 0xf0000000) {
			reset_mask_send = reset_mask_type;
			global_state.mask = 1;
		}
		return;
	}
	if (point_num > 1 || pp[0][0].all == 0) {
		reset_mask_count = 0;
		reset_mask_max = 0;
		reset_mask_count = 0;
		return;
	}
	reset_mask_count++;
	if (reset_mask_max == 0)
		reset_mask_max = pp[0][0].all;
	else
		if (PointDistance((gsl_POINT_TYPE *)(&reset_mask_max), pp[0]) >
				(((unsigned int)reset_mask_dis) & 0xffffff) &&
				reset_mask_count > (((unsigned int)reset_mask_dis) >> 24))
			reset_mask_max = 0xfffffff1;
}

static void PointDiagonal(void)
{
}

static void PointExtend(void)
{
}

int  gsl_PressMove(void)
{
	return 0;
}
EXPORT_SYMBOL(gsl_PressMove);

void gsl_ReportPressure(unsigned int *p)
{
}
EXPORT_SYMBOL(gsl_ReportPressure);

int  gsl_TouchNear(void)
{
		return 0;
}
EXPORT_SYMBOL(gsl_TouchNear);

static void gsl_id_reg_init(int flag)
{
	int i, j;
	for (j = 0; j < POINT_DEEP; j++)
		for (i = 0; i < POINT_MAX; i++)
			point_array[j][i].all = 0;
	for (i = 0; i < POINT_MAX; i++)
		point_delay[i].all = 0;
	point_n = 0;
	if (flag)
		point_num = 0;
	prev_num = 0;
	point_shake = 0;
	reset_mask_send = 0;
	reset_mask_max = 0;
	reset_mask_count = 0;
	global_state.all = 0;
	global_state.cc_128 = 0;
	prec_id.all = 0;
	for (i = 0; i < 64; i++) {
		if (coordinate_correct_coe_x[i] > 64
				|| coordinate_correct_coe_y[i] > 64) {
			global_state.cc_128 = 1;
			break;
		}
	}
}

static int DataCheck(void)
{
	if (drv_num == 0 || drv_num_nokey == 0
			|| sen_num == 0 || sen_num_nokey == 0)
		return 0;
	if (screen_x_max == 0 || screen_y_max == 0)
		return 0;
	return 1;
}

void gsl_DataInit(unsigned int *conf)
{
	int i, j;
	gsl_id_reg_init(1);
	for (i = 0; i < POINT_MAX; i++)
		point_now[i].all = 0;

		global_flag.all = conf[0x10];
		point_num_max = conf[0x11];
		drv_num = conf[0x12]&0xffff;
		sen_num = conf[0x12]>>16;
		drv_num_nokey = conf[0x13]&0xffff;
		sen_num_nokey = conf[0x13]>>16;
		screen_x_max = conf[0x14]&0xffff;
		screen_y_max = conf[0x14]>>16;
		reset_mask_dis = conf[0x16];
		reset_mask_type = conf[0x17];
		point_extend = conf[0x1b];
		press_mask = conf[0x1e];
		id_flag.all = conf[0x1f];
		id_first_coe = conf[0x20];
		id_speed_coe = conf[0x21];
		id_static_coe = conf[0x22];
		match_y[0] = conf[0x23]>>16;
		match_y[1] = conf[0x23]&0xffff;
		match_x[0] = conf[0x24]>>16;
		match_x[1] = conf[0x24]&0xffff;
		ignore_y[0] = conf[0x25]>>16;
		ignore_y[1] = conf[0x25]&0xffff;
		ignore_x[0] = conf[0x26]>>16;
		ignore_x[1] = conf[0x26]&0xffff;
		edge_cut[0] = (conf[0x27]>>24) & 0xff;
		edge_cut[1] = (conf[0x27]>>16) & 0xff;
		edge_cut[2] = (conf[0x27]>>8) & 0xff;
		edge_cut[3] = (conf[0x27]>>0) & 0xff;
		report_delay = conf[0x28];
		shake_min = conf[0x29];
		for (i = 0; i < 16; i++) {
			stretch_array[i*2+0] = conf[0x2a + i] & 0xffff;
			stretch_array[i*2+1] = conf[0x2a + i] >> 16;
		}
		for (i = 0; i < 8; i++) {
			shake_all_array[i*2+0] = conf[0x3a+i] & 0xffff;
			shake_all_array[i*2+1] = conf[0x3a+i] >> 16;
		}
		report_ahead			= conf[0x42];
		delay_key				= conf[0x4a];
		report_delete			= conf[0x4b];
		stretch_mult			= conf[0x4c];

		key_map_able = conf[0x60];
		for (i = 0; i < 8 * 3; i++)
			key_range_array[i] = conf[0x61 + i];

		coordinate_correct_able = conf[0x100];
		for (i = 0; i < 4; i++) {
			multi_x_array[i] = conf[0x101 + i];
			multi_y_array[i] = conf[0x105 + i];
		}
		for (i = 0; i < 64; i++) {
			coordinate_correct_coe_x[i] = (conf[0x109 + i/4] >>
					(i % 4 * 8)) & 0xff;
			coordinate_correct_coe_y[i] = (conf[0x109 + 16 + i / 4]
					>> (i % 4 * 8)) & 0xff;
		}
		for (j = 0; j < 4; j++)
			for (i = 0; i < 64; i++)
				multi_group[j][i] = (conf[0x109 + 32 + (i + j * 64) / 4]
						>> ((i + j * 64) % 4 * 8)) & 0xff;

		filter_able = conf[0x180];
		for (i = 0; i < 4; i++)
			filter_coe[i] = conf[0x181 + i];
		for (j = 0; j < 4; j++) {
			for (i = 0; i < 8; i++) {
				ps_coe[j][i] = conf[0x189 + i + j * 8];
				pr_coe[j][i] = conf[0x189 + i + j * 8 + 32];
			}
		}
	gsl_id_reg_init(0);
	for (i = 0; i < 8; i++) {
		if (shake_all_array[i*2] & 0x8000)
			shake_all_array[i*2] = shake_all_array[i*2] & ~0x8000;
		else
			shake_all_array[i*2] = Sqrt(shake_all_array[i * 2]);
	}
	for (i = 0; i < 2; i++) {
		if (match_x[i] & 0x8000)
			match_x[i] |= 0xffff0000;
		if (match_y[i] & 0x8000)
			match_y[i] |= 0xffff0000;
		if (ignore_x[i] & 0x8000)
			ignore_x[i] |= 0xffff0000;
		if (ignore_y[i] & 0x8000)
			ignore_y[i] |= 0xffff0000;
	}
}

unsigned int gsl_version_id(void)
{
	return GSL_VERSION;
}

unsigned int gsl_mask_tiaoping(void)
{
	return reset_mask_send;
}

static void GetFlag(void)
{
	int i = 0;
	int num_save;
	if (((point_num & 0x100) != 0)
	|| ((point_num & 0x200) != 0 && global_state.reset == 1)) {
		gsl_id_reg_init(0);
	}
	if ((point_num & 0x300) == 0)
		global_state.reset = 1;
	if (point_num & 0x400)
		global_state.only = 1;
	else
		global_state.only = 0;
	if (point_num & 0x2000)
		global_state.interpolation = INTE_INIT;
	else if (global_state.interpolation)
		global_state.interpolation--;
	if (point_num & 0x4000)
		global_state.ex = 1;
	else
		global_state.ex = 0;
	num_save = point_num & 0xff;
	if (num_save > POINT_MAX)
		num_save = POINT_MAX;
	for (i = 0; i < POINT_MAX; i++) {
		if (i >= num_save)
			point_now[i].all = 0;
	}
	point_num = (point_num & (~0xff)) + num_save;
}

static void PointIgnore(void)
{
}

void gsl_alg_id_main(struct gsl_touch_info *cinfo)
{
	int i;
	point_num = cinfo->finger_num;
	for (i = 0; i < POINT_MAX; i++)
		point_now[i].all = (cinfo->id[i]<<28) | (cinfo->x[i]<<16)
			| cinfo->y[i];
	GetFlag();
	if (DataCheck() == 0) {
		point_num = 0;
		cinfo->finger_num = 0;
		return;
	}
	point_num &= 0xff;
	PointIgnore();
	PointCoor();
	PointEdge();
	PointRound();
	PointRepeat();
	GetPointNum(point_now);
	PointPointer();
	PointPredict();
	PointId();
	PointNewId();
	PointOrder();
	PointCross();
	GetPointNum(pp[0]);
	prev_num = point_num;
	ResetMask();
	PointStretch();
	PointDiagonal();
	PointFilter();
	GetPointNum(pr[0]);
	PointDelay();
	PointMenu();
	PointExtend();
	PointReport(cinfo);
}
