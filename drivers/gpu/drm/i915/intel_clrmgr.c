/*
 * Copyright © 2008 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *Shashank Sharma <shashank.sharma@intel.com>
 *Uma Shankar <uma.shankar@intel.com>
 *Shobhit Kumar <skumar40@intel.com>
 */

#include "drmP.h"
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_clrmgr.h"

/* Gamma lookup table for Sprite planes */
u32 gamma_sprite_softlut[GAMMA_SP_MAX_COUNT] = {
	0, 0, 0, 0, 0, 1023
};

struct gamma_lut_data chv_gamma_lut[MAX_PIPES_CHV][CHV_GAMMA_MAX_VALS];

u32 degamma_softlut[DEGAMMA_CORRECT_MAX_COUNT_CHV] = {
	0x00000000, 0x00000000, 0x00140014, 0x00000014, 0x00280028, 0x00000028,
	0x003c003c, 0x0000003c, 0x00540054, 0x00000054, 0x00720072, 0x00000072,
	0x00950095, 0x00000095, 0x00bd00bd, 0x000000bd, 0x00eb00eb, 0x000000eb,
	0x011f011f, 0x0000011f, 0x01590159, 0x00000159, 0x019a019a, 0x0000019a,
	0x01e101e1, 0x000001e1, 0x022e022e, 0x0000022e, 0x02830283, 0x00000283,
	0x02df02df, 0x000002df, 0x03420342, 0x00000342, 0x03ac03ac, 0x000003ac,
	0x041d041d, 0x0000041d, 0x04970497, 0x00000497, 0x05180518, 0x00000518,
	0x05a105a1, 0x000005a1, 0x06320632, 0x00000632, 0x06cb06cb, 0x000006cb,
	0x076d076d, 0x0000076d, 0x08170817, 0x00000817, 0x08c908c9, 0x000008c9,
	0x09840984, 0x00000984, 0x0a480a48, 0x00000a48, 0x0b150b15, 0x00000b15,
	0x0beb0beb, 0x00000beb, 0x0cca0cca, 0x00000cca, 0x0db30db3, 0x00000db3,
	0x0ea40ea4, 0x00000ea4, 0x0f9f0f9f, 0x00000f9f, 0x10a410a4, 0x000010a4,
	0x11b211b2, 0x000011b2, 0x12ca12ca, 0x000012ca, 0x13ec13ec, 0x000013ec,
	0x15181518, 0x00001518, 0x164e164e, 0x0000164e, 0x178e178e, 0x0000178e,
	0x18d818d8, 0x000018d8, 0x1a2c1a2c, 0x00001a2c, 0x1b8b1b8b, 0x00001b8b,
	0x1cf51cf5, 0x00001cf5, 0x1e681e68, 0x00001e68, 0x1fe71fe7, 0x00001fe7,
	0x21702170, 0x00002170, 0x23052305, 0x00002305, 0x24a424a4, 0x000024a4,
	0x264e264e, 0x0000264e, 0x28032803, 0x00002803, 0x29c429c4, 0x000029c4,
	0x2b8f2b8f, 0x00002b8f, 0x2d662d66, 0x00002d66, 0x2f482f48, 0x00002f48,
	0x31363136, 0x00003136, 0x33303330, 0x00003330, 0x35353535, 0x00003535,
	0x37453745, 0x00003745, 0x39623962, 0x00003962, 0x3b8a3b8a, 0x00003b8a,
	0x3dbf3dbf, 0x00003dbf, 0x3fff3fff, 0x00003fff
};

struct gamma_lut_data chv_gamma_default[CHV_CGM_GAMMA_MATRIX_MAX_VALS] = {
	{0x0000, 0x0000, 0x0000}, {0x0cc0, 0x0cc0, 0x0cc0}, {0x15c0, 0x15c0, 0x15c0},
	{0x1c40, 0x1c40, 0x1c40}, {0x21c0, 0x21c0, 0x21c0}, {0x2640, 0x2640, 0x2640},
	{0x2a80, 0x2a80, 0x2a80}, {0x2e40, 0x2e40, 0x2e40}, {0x3180, 0x3180, 0x3180},
	{0x34c0, 0x34c0, 0x34c0}, {0x37c0, 0x37c0, 0x37c0}, {0x3ac0, 0x3ac0, 0x3ac0},
	{0x3d40, 0x3d40, 0x3d40}, {0x4000, 0x4000, 0x4000}, {0x4240, 0x4240, 0x4240},
	{0x44c0, 0x44c0, 0x44c0}, {0x4700, 0x4700, 0x4700}, {0x4900, 0x4900, 0x4900},
	{0x4b40, 0x4b40, 0x4b40}, {0x4d40, 0x4d40, 0x4d40}, {0x4f40, 0x4f40, 0x4f40},
	{0x5100, 0x5100, 0x5100}, {0x5300, 0x5300, 0x5300}, {0x54c0, 0x54c0, 0x54c0},
	{0x5680, 0x5680, 0x5680}, {0x5840, 0x5840, 0x5840}, {0x5a00, 0x5a00, 0x5a00},
	{0x5b80, 0x5b80, 0x5b80}, {0x5d40, 0x5d40, 0x5d40}, {0x5ec0, 0x5ec0, 0x5ec0},
	{0x6040, 0x6040, 0x6040}, {0x6200, 0x6200, 0x6200}, {0x6380, 0x6380, 0x6380},
	{0x64c0, 0x64c0, 0x64c0}, {0x6640, 0x6640, 0x6640}, {0x67c0, 0x67c0, 0x67c0},
	{0x6900, 0x6900, 0x6900}, {0x6a80, 0x6a80, 0x6a80}, {0x6bc0, 0x6bc0, 0x6bc0},
	{0x6d00, 0x6d00, 0x6d00}, {0x6e80, 0x6e80, 0x6e80}, {0x6fc0, 0x6fc0, 0x6fc0},
	{0x7100, 0x7100, 0x7100}, {0x7240, 0x7240, 0x7240}, {0x7380, 0x7380, 0x7380},
	{0x74c0, 0x74c0, 0x74c0}, {0x7600, 0x7600, 0x7600}, {0x7700, 0x7700, 0x7700},
	{0x7840, 0x7840, 0x7840}, {0x7980, 0x7980, 0x7980}, {0x7a80, 0x7a80, 0x7a80},
	{0x7bc0, 0x7bc0, 0x7bc0}, {0x7cc0, 0x7cc0, 0x7cc0}, {0x7e00, 0x7e00, 0x7e00},
	{0x7f00, 0x7f00, 0x7f00}, {0x8000, 0x8000, 0x8000}, {0x8140, 0x8140, 0x8140},
	{0x8240, 0x8240, 0x8240}, {0x8340, 0x8340, 0x8340}, {0x8440, 0x8440, 0x8440},
	{0x8540, 0x8540, 0x8540}, {0x8640, 0x8640, 0x8640}, {0x8740, 0x8740, 0x8740},
	{0x8840, 0x8840, 0x8840}, {0x8940, 0x8940, 0x8940}, {0x8a40, 0x8a40, 0x8a40},
	{0x8b40, 0x8b40, 0x8b40}, {0x8c40, 0x8c40, 0x8c40}, {0x8d40, 0x8d40, 0x8d40},
	{0x8e40, 0x8e40, 0x8e40}, {0x8f00, 0x8f00, 0x8f00}, {0x9000, 0x9000, 0x9000},
	{0x9100, 0x9100, 0x9100}, {0x9200, 0x9200, 0x9200}, {0x92c0, 0x92c0, 0x92c0},
	{0x93c0, 0x93c0, 0x93c0}, {0x9480, 0x9480, 0x9480}, {0x9580, 0x9580, 0x9580},
	{0x9640, 0x9640, 0x9640}, {0x9740, 0x9740, 0x9740}, {0x9800, 0x9800, 0x9800},
	{0x9900, 0x9900, 0x9900}, {0x99c0, 0x99c0, 0x99c0}, {0x9ac0, 0x9ac0, 0x9ac0},
	{0x9b80, 0x9b80, 0x9b80}, {0x9c40, 0x9c40, 0x9c40}, {0x9d40, 0x9d40, 0x9d40},
	{0x9e00, 0x9e00, 0x9e00}, {0x9ec0, 0x9ec0, 0x9ec0}, {0x9fc0, 0x9fc0, 0x9fc0},
	{0xa080, 0xa080, 0xa080}, {0xa140, 0xa140, 0xa140}, {0xa200, 0xa200, 0xa200},
	{0xa300, 0xa300, 0xa300}, {0xa3c0, 0xa3c0, 0xa3c0}, {0xa480, 0xa480, 0xa480},
	{0xa540, 0xa540, 0xa540}, {0xa600, 0xa600, 0xa600}, {0xa6c0, 0xa6c0, 0xa6c0},
	{0xa780, 0xa780, 0xa780}, {0xa840, 0xa840, 0xa840}, {0xa900, 0xa900, 0xa900},
	{0xa9c0, 0xa9c0, 0xa9c0}, {0xaa80, 0xaa80, 0xaa80}, {0xab40, 0xab40, 0xab40},
	{0xac00, 0xac00, 0xac00}, {0xacc0, 0xacc0, 0xacc0}, {0xad80, 0xad80, 0xad80},
	{0xae40, 0xae40, 0xae40}, {0xaf00, 0xaf00, 0xaf00}, {0xafc0, 0xafc0, 0xafc0},
	{0xb080, 0xb080, 0xb080}, {0xb140, 0xb140, 0xb140}, {0xb1c0, 0xb1c0, 0xb1c0},
	{0xb280, 0xb280, 0xb280}, {0xb340, 0xb340, 0xb340}, {0xb400, 0xb400, 0xb400},
	{0xb4c0, 0xb4c0, 0xb4c0}, {0xb540, 0xb540, 0xb540}, {0xb600, 0xb600, 0xb600},
	{0xb6c0, 0xb6c0, 0xb6c0}, {0xb780, 0xb780, 0xb780}, {0xb800, 0xb800, 0xb800},
	{0xb8c0, 0xb8c0, 0xb8c0}, {0xb980, 0xb980, 0xb980}, {0xba00, 0xba00, 0xba00},
	{0xbac0, 0xbac0, 0xbac0}, {0xbb80, 0xbb80, 0xbb80}, {0xbc00, 0xbc00, 0xbc00},
	{0xbcc0, 0xbcc0, 0xbcc0}, {0xbd80, 0xbd80, 0xbd80}, {0xbe00, 0xbe00, 0xbe00},
	{0xbec0, 0xbec0, 0xbec0}, {0xbf40, 0xbf40, 0xbf40}, {0xc000, 0xc000, 0xc000},
	{0xc080, 0xc080, 0xc080}, {0xc140, 0xc140, 0xc140}, {0xc1c0, 0xc1c0, 0xc1c0},
	{0xc280, 0xc280, 0xc280}, {0xc340, 0xc340, 0xc340}, {0xc3c0, 0xc3c0, 0xc3c0},
	{0xc480, 0xc480, 0xc480}, {0xc500, 0xc500, 0xc500}, {0xc580, 0xc580, 0xc580},
	{0xc640, 0xc640, 0xc640}, {0xc6c0, 0xc6c0, 0xc6c0}, {0xc780, 0xc780, 0xc780},
	{0xc800, 0xc800, 0xc800}, {0xc8c0, 0xc8c0, 0xc8c0}, {0xc940, 0xc940, 0xc940},
	{0xca00, 0xca00, 0xca00}, {0xca80, 0xca80, 0xca80}, {0xcb00, 0xcb00, 0xcb00},
	{0xcbc0, 0xcbc0, 0xcbc0}, {0xcc40, 0xcc40, 0xcc40}, {0xccc0, 0xccc0, 0xccc0},
	{0xcd80, 0xcd80, 0xcd80}, {0xce00, 0xce00, 0xce00}, {0xce80, 0xce80, 0xce80},
	{0xcf40, 0xcf40, 0xcf40}, {0xcfc0, 0xcfc0, 0xcfc0}, {0xd040, 0xd040, 0xd040},
	{0xd100, 0xd100, 0xd100}, {0xd180, 0xd180, 0xd180}, {0xd200, 0xd200, 0xd200},
	{0xd2c0, 0xd2c0, 0xd2c0}, {0xd340, 0xd340, 0xd340}, {0xd3c0, 0xd3c0, 0xd3c0},
	{0xd440, 0xd440, 0xd440}, {0xd500, 0xd500, 0xd500}, {0xd580, 0xd580, 0xd580},
	{0xd600, 0xd600, 0xd600}, {0xd680, 0xd680, 0xd680}, {0xd700, 0xd700, 0xd700},
	{0xd7c0, 0xd7c0, 0xd7c0}, {0xd840, 0xd840, 0xd840}, {0xd8c0, 0xd8c0, 0xd8c0},
	{0xd940, 0xd940, 0xd940}, {0xd9c0, 0xd9c0, 0xd9c0}, {0xda80, 0xda80, 0xda80},
	{0xdb00, 0xdb00, 0xdb00}, {0xdb80, 0xdb80, 0xdb80}, {0xdc00, 0xdc00, 0xdc00},
	{0xdc80, 0xdc80, 0xdc80}, {0xdd00, 0xdd00, 0xdd00}, {0xdd80, 0xdd80, 0xdd80},
	{0xde40, 0xde40, 0xde40}, {0xdec0, 0xdec0, 0xdec0}, {0xdf40, 0xdf40, 0xdf40},
	{0xdfc0, 0xdfc0, 0xdfc0}, {0xe040, 0xe040, 0xe040}, {0xe0c0, 0xe0c0, 0xe0c0},
	{0xe140, 0xe140, 0xe140}, {0xe1c0, 0xe1c0, 0xe1c0}, {0xe240, 0xe240, 0xe240},
	{0xe2c0, 0xe2c0, 0xe2c0}, {0xe340, 0xe340, 0xe340}, {0xe3c0, 0xe3c0, 0xe3c0},
	{0xe440, 0xe440, 0xe440}, {0xe4c0, 0xe4c0, 0xe4c0}, {0xe580, 0xe580, 0xe580},
	{0xe600, 0xe600, 0xe600}, {0xe680, 0xe680, 0xe680}, {0xe700, 0xe700, 0xe700},
	{0xe780, 0xe780, 0xe780}, {0xe800, 0xe800, 0xe800}, {0xe880, 0xe880, 0xe880},
	{0xe900, 0xe900, 0xe900}, {0xe980, 0xe980, 0xe980}, {0xea00, 0xea00, 0xea00},
	{0xea80, 0xea80, 0xea80}, {0xeac0, 0xeac0, 0xeac0}, {0xeb40, 0xeb40, 0xeb40},
	{0xebc0, 0xebc0, 0xebc0}, {0xec40, 0xec40, 0xec40}, {0xecc0, 0xecc0, 0xecc0},
	{0xed40, 0xed40, 0xed40}, {0xedc0, 0xedc0, 0xedc0}, {0xee40, 0xee40, 0xee40},
	{0xeec0, 0xeec0, 0xeec0}, {0xef40, 0xef40, 0xef40}, {0xefc0, 0xefc0, 0xefc0},
	{0xf040, 0xf040, 0xf040}, {0xf0c0, 0xf0c0, 0xf0c0}, {0xf140, 0xf140, 0xf140},
	{0xf180, 0xf180, 0xf180}, {0xf200, 0xf200, 0xf200}, {0xf280, 0xf280, 0xf280},
	{0xf300, 0xf300, 0xf300}, {0xf380, 0xf380, 0xf380}, {0xf400, 0xf400, 0xf400},
	{0xf480, 0xf480, 0xf480}, {0xf500, 0xf500, 0xf500}, {0xf540, 0xf540, 0xf540},
	{0xf5c0, 0xf5c0, 0xf5c0}, {0xf640, 0xf640, 0xf640}, {0xf6c0, 0xf6c0, 0xf6c0},
	{0xf740, 0xf740, 0xf740}, {0xf7c0, 0xf7c0, 0xf7c0}, {0xf840, 0xf840, 0xf840},
	{0xf880, 0xf880, 0xf880}, {0xf900, 0xf900, 0xf900}, {0xf980, 0xf980, 0xf980},
	{0xfa00, 0xfa00, 0xfa00}, {0xfa80, 0xfa80, 0xfa80}, {0xfac0, 0xfac0, 0xfac0},
	{0xfb40, 0xfb40, 0xfb40}, {0xfbc0, 0xfbc0, 0xfbc0}, {0xfc40, 0xfc40, 0xfc40},
	{0xfcc0, 0xfcc0, 0xfcc0}, {0xfd00, 0xfd00, 0xfd00}, {0xfd80, 0xfd80, 0xfd80},
	{0xfe00, 0xfe00, 0xfe00}, {0xfe80, 0xfe80, 0xfe80}, {0xfec0, 0xfec0, 0xfec0},
	{0xff40, 0xff40, 0xff40}, {0xffc0, 0xffc0, 0xffc0}
};

/* Gamma soft lookup table for default gamma =1.0 */
u32 gamma_softlut[MAX_PIPES_CHV][GAMMA_CORRECT_MAX_COUNT] =  {
	{0x000000, 0x0, 0x020202, 0x0, 0x040404, 0x0, 0x060606, 0x0,
	 0x080808, 0x0, 0x0A0A0A, 0x0, 0x0C0C0C, 0x0, 0x0E0E0E, 0x0,
	 0x101010, 0x0, 0x121212, 0x0, 0x141414, 0x0, 0x161616, 0x0,
	 0x181818, 0x0, 0x1A1A1A, 0x0, 0x1C1C1C, 0x0, 0x1E1E1E, 0x0,
	 0x202020, 0x0, 0x222222, 0x0, 0x242424, 0x0, 0x262626, 0x0,
	 0x282828, 0x0, 0x2A2A2A, 0x0, 0x2C2C2C, 0x0, 0x2E2E2E, 0x0,
	 0x303030, 0x0, 0x323232, 0x0, 0x343434, 0x0, 0x363636, 0x0,
	 0x383838, 0x0, 0x3A3A3A, 0x0, 0x3C3C3C, 0x0, 0x3E3E3E, 0x0,
	 0x404040, 0x0, 0x424242, 0x0, 0x444444, 0x0, 0x464646, 0x0,
	 0x484848, 0x0, 0x4A4A4A, 0x0, 0x4C4C4C, 0x0, 0x4E4E4E, 0x0,
	 0x505050, 0x0, 0x525252, 0x0, 0x545454, 0x0, 0x565656, 0x0,
	 0x585858, 0x0, 0x5A5A5A, 0x0, 0x5C5C5C, 0x0, 0x5E5E5E, 0x0,
	 0x606060, 0x0, 0x626262, 0x0, 0x646464, 0x0, 0x666666, 0x0,
	 0x686868, 0x0, 0x6A6A6A, 0x0, 0x6C6C6C, 0x0, 0x6E6E6E, 0x0,
	 0x707070, 0x0, 0x727272, 0x0, 0x747474, 0x0, 0x767676, 0x0,
	 0x787878, 0x0, 0x7A7A7A, 0x0, 0x7C7C7C, 0x0, 0x7E7E7E, 0x0,
	 0x808080, 0x0, 0x828282, 0x0, 0x848484, 0x0, 0x868686, 0x0,
	 0x888888, 0x0, 0x8A8A8A, 0x0, 0x8C8C8C, 0x0, 0x8E8E8E, 0x0,
	 0x909090, 0x0, 0x929292, 0x0, 0x949494, 0x0, 0x969696, 0x0,
	 0x989898, 0x0, 0x9A9A9A, 0x0, 0x9C9C9C, 0x0, 0x9E9E9E, 0x0,
	 0xA0A0A0, 0x0, 0xA2A2A2, 0x0, 0xA4A4A4, 0x0, 0xA6A6A6, 0x0,
	 0xA8A8A8, 0x0, 0xAAAAAA, 0x0, 0xACACAC, 0x0, 0xAEAEAE, 0x0,
	 0xB0B0B0, 0x0, 0xB2B2B2, 0x0, 0xB4B4B4, 0x0, 0xB6B6B6, 0x0,
	 0xB8B8B8, 0x0, 0xBABABA, 0x0, 0xBCBCBC, 0x0, 0xBEBEBE, 0x0,
	 0xC0C0C0, 0x0, 0xC2C2C2, 0x0, 0xC4C4C4, 0x0, 0xC6C6C6, 0x0,
	 0xC8C8C8, 0x0, 0xCACACA, 0x0, 0xCCCCCC, 0x0, 0xCECECE, 0x0,
	 0xD0D0D0, 0x0, 0xD2D2D2, 0x0, 0xD4D4D4, 0x0, 0xD6D6D6, 0x0,
	 0xD8D8D8, 0x0, 0xDADADA, 0x0, 0xDCDCDC, 0x0, 0xDEDEDE, 0x0,
	 0xE0E0E0, 0x0, 0xE2E2E2, 0x0, 0xE4E4E4, 0x0, 0xE6E6E6, 0x0,
	 0xE8E8E8, 0x0, 0xEAEAEA, 0x0, 0xECECEC, 0x0, 0xEEEEEE, 0x0,
	 0xF0F0F0, 0x0, 0xF2F2F2, 0x0, 0xF4F4F4, 0x0, 0xF6F6F6, 0x0,
	 0xF8F8F8, 0x0, 0xFAFAFA, 0x0, 0xFCFCFC, 0x0, 0xFEFEFE, 0x0},
	{0x000000, 0x0, 0x020202, 0x0, 0x040404, 0x0, 0x060606, 0x0,
	 0x080808, 0x0, 0x0A0A0A, 0x0, 0x0C0C0C, 0x0, 0x0E0E0E, 0x0,
	 0x101010, 0x0, 0x121212, 0x0, 0x141414, 0x0, 0x161616, 0x0,
	 0x181818, 0x0, 0x1A1A1A, 0x0, 0x1C1C1C, 0x0, 0x1E1E1E, 0x0,
	 0x202020, 0x0, 0x222222, 0x0, 0x242424, 0x0, 0x262626, 0x0,
	 0x282828, 0x0, 0x2A2A2A, 0x0, 0x2C2C2C, 0x0, 0x2E2E2E, 0x0,
	 0x303030, 0x0, 0x323232, 0x0, 0x343434, 0x0, 0x363636, 0x0,
	 0x383838, 0x0, 0x3A3A3A, 0x0, 0x3C3C3C, 0x0, 0x3E3E3E, 0x0,
	 0x404040, 0x0, 0x424242, 0x0, 0x444444, 0x0, 0x464646, 0x0,
	 0x484848, 0x0, 0x4A4A4A, 0x0, 0x4C4C4C, 0x0, 0x4E4E4E, 0x0,
	 0x505050, 0x0, 0x525252, 0x0, 0x545454, 0x0, 0x565656, 0x0,
	 0x585858, 0x0, 0x5A5A5A, 0x0, 0x5C5C5C, 0x0, 0x5E5E5E, 0x0,
	 0x606060, 0x0, 0x626262, 0x0, 0x646464, 0x0, 0x666666, 0x0,
	 0x686868, 0x0, 0x6A6A6A, 0x0, 0x6C6C6C, 0x0, 0x6E6E6E, 0x0,
	 0x707070, 0x0, 0x727272, 0x0, 0x747474, 0x0, 0x767676, 0x0,
	 0x787878, 0x0, 0x7A7A7A, 0x0, 0x7C7C7C, 0x0, 0x7E7E7E, 0x0,
	 0x808080, 0x0, 0x828282, 0x0, 0x848484, 0x0, 0x868686, 0x0,
	 0x888888, 0x0, 0x8A8A8A, 0x0, 0x8C8C8C, 0x0, 0x8E8E8E, 0x0,
	 0x909090, 0x0, 0x929292, 0x0, 0x949494, 0x0, 0x969696, 0x0,
	 0x989898, 0x0, 0x9A9A9A, 0x0, 0x9C9C9C, 0x0, 0x9E9E9E, 0x0,
	 0xA0A0A0, 0x0, 0xA2A2A2, 0x0, 0xA4A4A4, 0x0, 0xA6A6A6, 0x0,
	 0xA8A8A8, 0x0, 0xAAAAAA, 0x0, 0xACACAC, 0x0, 0xAEAEAE, 0x0,
	 0xB0B0B0, 0x0, 0xB2B2B2, 0x0, 0xB4B4B4, 0x0, 0xB6B6B6, 0x0,
	 0xB8B8B8, 0x0, 0xBABABA, 0x0, 0xBCBCBC, 0x0, 0xBEBEBE, 0x0,
	 0xC0C0C0, 0x0, 0xC2C2C2, 0x0, 0xC4C4C4, 0x0, 0xC6C6C6, 0x0,
	 0xC8C8C8, 0x0, 0xCACACA, 0x0, 0xCCCCCC, 0x0, 0xCECECE, 0x0,
	 0xD0D0D0, 0x0, 0xD2D2D2, 0x0, 0xD4D4D4, 0x0, 0xD6D6D6, 0x0,
	 0xD8D8D8, 0x0, 0xDADADA, 0x0, 0xDCDCDC, 0x0, 0xDEDEDE, 0x0,
	 0xE0E0E0, 0x0, 0xE2E2E2, 0x0, 0xE4E4E4, 0x0, 0xE6E6E6, 0x0,
	 0xE8E8E8, 0x0, 0xEAEAEA, 0x0, 0xECECEC, 0x0, 0xEEEEEE, 0x0,
	 0xF0F0F0, 0x0, 0xF2F2F2, 0x0, 0xF4F4F4, 0x0, 0xF6F6F6, 0x0,
	 0xF8F8F8, 0x0, 0xFAFAFA, 0x0, 0xFCFCFC, 0x0, 0xFEFEFE, 0x0},
	{0x000000, 0x0, 0x020202, 0x0, 0x040404, 0x0, 0x060606, 0x0,
	 0x080808, 0x0, 0x0A0A0A, 0x0, 0x0C0C0C, 0x0, 0x0E0E0E, 0x0,
	 0x101010, 0x0, 0x121212, 0x0, 0x141414, 0x0, 0x161616, 0x0,
	 0x181818, 0x0, 0x1A1A1A, 0x0, 0x1C1C1C, 0x0, 0x1E1E1E, 0x0,
	 0x202020, 0x0, 0x222222, 0x0, 0x242424, 0x0, 0x262626, 0x0,
	 0x282828, 0x0, 0x2A2A2A, 0x0, 0x2C2C2C, 0x0, 0x2E2E2E, 0x0,
	 0x303030, 0x0, 0x323232, 0x0, 0x343434, 0x0, 0x363636, 0x0,
	 0x383838, 0x0, 0x3A3A3A, 0x0, 0x3C3C3C, 0x0, 0x3E3E3E, 0x0,
	 0x404040, 0x0, 0x424242, 0x0, 0x444444, 0x0, 0x464646, 0x0,
	 0x484848, 0x0, 0x4A4A4A, 0x0, 0x4C4C4C, 0x0, 0x4E4E4E, 0x0,
	 0x505050, 0x0, 0x525252, 0x0, 0x545454, 0x0, 0x565656, 0x0,
	 0x585858, 0x0, 0x5A5A5A, 0x0, 0x5C5C5C, 0x0, 0x5E5E5E, 0x0,
	 0x606060, 0x0, 0x626262, 0x0, 0x646464, 0x0, 0x666666, 0x0,
	 0x686868, 0x0, 0x6A6A6A, 0x0, 0x6C6C6C, 0x0, 0x6E6E6E, 0x0,
	 0x707070, 0x0, 0x727272, 0x0, 0x747474, 0x0, 0x767676, 0x0,
	 0x787878, 0x0, 0x7A7A7A, 0x0, 0x7C7C7C, 0x0, 0x7E7E7E, 0x0,
	 0x808080, 0x0, 0x828282, 0x0, 0x848484, 0x0, 0x868686, 0x0,
	 0x888888, 0x0, 0x8A8A8A, 0x0, 0x8C8C8C, 0x0, 0x8E8E8E, 0x0,
	 0x909090, 0x0, 0x929292, 0x0, 0x949494, 0x0, 0x969696, 0x0,
	 0x989898, 0x0, 0x9A9A9A, 0x0, 0x9C9C9C, 0x0, 0x9E9E9E, 0x0,
	 0xA0A0A0, 0x0, 0xA2A2A2, 0x0, 0xA4A4A4, 0x0, 0xA6A6A6, 0x0,
	 0xA8A8A8, 0x0, 0xAAAAAA, 0x0, 0xACACAC, 0x0, 0xAEAEAE, 0x0,
	 0xB0B0B0, 0x0, 0xB2B2B2, 0x0, 0xB4B4B4, 0x0, 0xB6B6B6, 0x0,
	 0xB8B8B8, 0x0, 0xBABABA, 0x0, 0xBCBCBC, 0x0, 0xBEBEBE, 0x0,
	 0xC0C0C0, 0x0, 0xC2C2C2, 0x0, 0xC4C4C4, 0x0, 0xC6C6C6, 0x0,
	 0xC8C8C8, 0x0, 0xCACACA, 0x0, 0xCCCCCC, 0x0, 0xCECECE, 0x0,
	 0xD0D0D0, 0x0, 0xD2D2D2, 0x0, 0xD4D4D4, 0x0, 0xD6D6D6, 0x0,
	 0xD8D8D8, 0x0, 0xDADADA, 0x0, 0xDCDCDC, 0x0, 0xDEDEDE, 0x0,
	 0xE0E0E0, 0x0, 0xE2E2E2, 0x0, 0xE4E4E4, 0x0, 0xE6E6E6, 0x0,
	 0xE8E8E8, 0x0, 0xEAEAEA, 0x0, 0xECECEC, 0x0, 0xEEEEEE, 0x0,
	 0xF0F0F0, 0x0, 0xF2F2F2, 0x0, 0xF4F4F4, 0x0, 0xF6F6F6, 0x0,
	 0xF8F8F8, 0x0, 0xFAFAFA, 0x0, 0xFCFCFC, 0x0, 0xFEFEFE, 0x0}
};

/* GCMAX soft lookup table */
u32 gcmax_softlut[MAX_PIPES_CHV][GC_MAX_COUNT] =  {
	{0x10000, 0x10000, 0x10000},
	{0x10000, 0x10000, 0x10000},
	{0x10000, 0x10000, 0x10000}
};

/* Hue Saturation defaults */
struct hue_saturationlut savedhsvalues[NO_SPRITE_REG_CHV] = {
	{SPRITEA, 0x1000000},
	{SPRITEB, 0x1000000},
	{SPRITEC, 0x1000000},
	{SPRITED, 0x1000000},
	{SPRITEE, 0x1000000},
	{SPRITEF, 0x1000000}
};

/* Contrast brightness defaults */
struct cont_brightlut savedcbvalues[NO_SPRITE_REG_CHV] = {
	{SPRITEA, 0x80},
	{SPRITEB, 0x80},
	{SPRITEC, 0x80},
	{SPRITED, 0x80},
	{SPRITEE, 0x80},
	{SPRITEF, 0x80}
};

/* Color space conversion coff's */
u32 csc_softlut[MAX_PIPES_CHV][CSC_MAX_COEFF_COUNT] = {
	{ 0x1000, 0x0, 0x1000, 0x0, 0x1000, 0x0},
	{ 0x1000, 0x0, 0x1000, 0x0, 0x1000, 0x0},
	{ 0x1000, 0x0, 0x1000, 0x0, 0x1000, 0x0},
};
u32 chv_csc_default[CSC_MAX_COEFF_COUNT_CHV] = {
				0x1000, 0x0, 0x1000, 0x0, 0x1000};

/*
 * Gen 6 SOC allows following color correction values:
 *     - CSC(wide gamut) with 3x3 matrix = 9 csc correction values.
 *     - Gamma correction with 128 gamma values.
 */
struct clrmgr_property gen6_pipe_color_corrections[] = {
	{
		.tweak_id = cgm_csc,
		.type = DRM_MODE_PROP_BLOB,
		.len = CHV_CGM_CSC_MATRIX_MAX_VALS,
		.name = "cgm-csc-correction",
		.set_property = intel_clrmgr_set_cgm_csc,
	},
	{
		.tweak_id = cgm_gamma,
		.type = DRM_MODE_PROP_BLOB,
		.len = CHV_CGM_GAMMA_MATRIX_MAX_VALS,
		.name = "cgm-gamma-correction",
		.set_property = intel_clrmgr_set_cgm_gamma,
	},
	{
		.tweak_id = cgm_degamma,
		.type = DRM_MODE_PROP_BLOB,
		.len = CHV_CGM_DEGAMMA_MATRIX_MAX_VALS,
		.name = "cgm-degamma-correction",
		.set_property = intel_clrmgr_set_cgm_degamma,
	},
};

static u32 cgm_ctrl[] = {
	PIPEA_CGM_CTRL,
	PIPEB_CGM_CTRL,
	PIPEC_CGM_CTRL
};

static u32 cgm_degamma_st[] = {
	PIPEA_CGM_DEGAMMA_ST,
	PIPEB_CGM_DEGAMMA_ST,
	PIPEC_CGM_DEGAMMA_ST
};

static u32 cgm_csc_st[] = {
	PIPEA_CGM_CSC_ST,
	PIPEB_CGM_CSC_ST,
	PIPEC_CGM_CSC_ST
};

static u32 cgm_gamma_st[] = {
	PIPEA_CGM_GAMMA_ST,
	PIPEB_CGM_GAMMA_ST,
	PIPEC_CGM_GAMMA_ST
};

static bool chv_apply_cgm_gamma(struct intel_crtc *intel_crtc,
			const struct gamma_lut_data *data, bool enable);

void chv_save_gamma_lut(uint *dest, enum pipe pipe, enum color color)
{
	int i;

	switch (color) {
		case RED_OFFSET:
			for (i = 0; i < CHV_GAMMA_MAX_VALS; i++)
				chv_gamma_lut[pipe][i].red = dest[i];
			break;
		case GREEN_OFFSET:
			for (i = 0; i < CHV_GAMMA_MAX_VALS; i++)
				chv_gamma_lut[pipe][i].green = dest[i];
			break;
		case BLUE_OFFSET:
			for (i = 0; i < CHV_GAMMA_MAX_VALS; i++)
				chv_gamma_lut[pipe][i].blue = dest[i];
			break;
		default:
			DRM_ERROR("Wrong Color input for gamma\n");
	}
}

int chv_set_pipe_degamma(struct drm_crtc *crtc, bool enable, bool is_internal)
{
	struct intel_crtc *intel_crtc;
	struct drm_device *dev;
	struct drm_i915_private *dev_priv;
	u32 i = 0;
	enum pipe pipe;
	u32 cgm_degamma_reg, cgm_ctrl_reg;
	u32 val;

	/* Validate input */
	if (!crtc) {
		DRM_ERROR("Invalid CRTC object input to gamma enable\n");
		return -EINVAL;
	}

	intel_crtc = to_intel_crtc(crtc);
	if (intel_crtc == NULL)
		return false;

	pipe = intel_crtc->pipe;
	dev = crtc->dev;
	dev_priv = dev->dev_private;

	if (!is_internal)
		dev_priv->degamma_enabled[pipe] = enable;
	cgm_ctrl_reg = dev_priv->info.display_mmio_offset +
		cgm_ctrl[intel_crtc->pipe];
	cgm_degamma_reg = dev_priv->info.display_mmio_offset +
		cgm_degamma_st[intel_crtc->pipe];
	if (enable) {
		for (i = 0; i < DEGAMMA_CORRECT_MAX_COUNT_CHV; i++)
			I915_WRITE(cgm_degamma_reg + 4 * i, degamma_softlut[i]);

		chv_apply_cgm_gamma(intel_crtc, chv_gamma_default, true);
		val = I915_READ(cgm_ctrl_reg) | CGM_DEGAMMA_EN;
		I915_WRITE(cgm_ctrl_reg, val);

	} else {
		val = I915_READ(cgm_ctrl_reg) & ~(CGM_DEGAMMA_EN |
				CGM_GAMMA_EN);
		I915_WRITE(cgm_ctrl_reg, val);
	}
	return 0;
}

int chv_set_csc(struct drm_device *dev, struct drm_crtc *crtc, bool enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = NULL;
	u32 cgm_csc_reg;
	u32 cgm_ctrl_reg;
	int pipe;
	int i;

	intel_crtc = to_intel_crtc(crtc);
	if (!intel_crtc) {
		DRM_ERROR("Invalid or null intel_crtc\n");
		return -EINVAL;
	}
	pipe = intel_crtc->pipe;
	dev_priv->csc_enabled[pipe] = enable;
	cgm_ctrl_reg = dev_priv->info.display_mmio_offset +
		cgm_ctrl[intel_crtc->pipe];
	cgm_csc_reg = dev_priv->info.display_mmio_offset +
		cgm_csc_st[intel_crtc->pipe];

	if (enable) {
		DRM_DEBUG_DRIVER("Setting CSC on pipe = %d\n", pipe);

		/* program CGM CSC values */
		for (i = 0; i < CSC_MAX_COEFF_COUNT_CHV; i++)
			I915_WRITE(cgm_csc_reg + 4 * i, csc_softlut[pipe][i]);

		/* CSC require Degamma & Gamma to be enabled for correct */
		chv_set_pipe_degamma(crtc, true, true);

		/* enable csc in CGM block*/
		I915_WRITE(cgm_ctrl_reg, I915_READ(cgm_ctrl_reg) | CGM_CSC_EN);
	} else {
		/* program CGM CSC values */
		for (i = 0; i < CSC_MAX_COEFF_COUNT_CHV; i++)
			I915_WRITE(cgm_csc_reg + 4 * i, chv_csc_default[i]);
		chv_set_pipe_degamma(crtc, false, true);

		/* disable csc in CGM block */
		I915_WRITE(cgm_ctrl_reg, I915_READ(cgm_ctrl_reg) | CGM_CSC_EN);
	}
	return 0;

}


int chv_set_pipe_gamma(struct intel_crtc *intel_crtc, bool enable)
{
	struct drm_i915_private *dev_priv;
	enum pipe pipe;
	u32 cgm_ctrl_reg;

	if (intel_crtc == NULL)
		return false;

	dev_priv = intel_crtc->base.dev->dev_private;
	pipe = intel_crtc->pipe;
	cgm_ctrl_reg = dev_priv->info.display_mmio_offset + cgm_ctrl[pipe];

	if (enable) {
		chv_apply_cgm_gamma(intel_crtc, chv_gamma_lut[pipe], true);
		DRM_DEBUG("CGM Gamma enabled on Pipe %d\n", pipe);
	} else {
		if ((I915_READ(cgm_ctrl_reg) & CGM_DEGAMMA_EN))
			return chv_apply_cgm_gamma(intel_crtc,
					chv_gamma_default, true);
		chv_apply_cgm_gamma(intel_crtc, NULL, false);
	}
	return 0;
}


/* Enable color space conversion on PIPE */
int
do_intel_enable_csc(struct drm_device *dev, void *data, struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = NULL;
	u32 pipeconf = 0;
	int pipe = 0;
	u32 csc_reg = 0;
	int i = 0, j = 0;

	if (!data) {
		DRM_ERROR("NULL input to enable CSC");
		return -EINVAL;
	}

	if (IS_CHERRYVIEW(dev))
		return chv_set_csc(dev, crtc, true);

	intel_crtc = to_intel_crtc(crtc);
	pipe = intel_crtc->pipe;
	DRM_DEBUG_DRIVER("pipe = %d\n", pipe);
	pipeconf = I915_READ(PIPECONF(pipe));
	pipeconf |= PIPECONF_CSC_ENABLE;

	if (pipe == 0)
		csc_reg = _PIPEACSC;
	else if (pipe == 1)
		csc_reg = _PIPEBCSC;
	else if (IS_CHERRYVIEW(dev) && (PIPEID(pipe) == PIPEC))
		csc_reg = _CHV_PIPECCSC;
	else {
		DRM_ERROR("Invalid pipe input");
		return -EINVAL;
	}

	/* Enable csc correction */
	I915_WRITE(PIPECONF(pipe), pipeconf);
	dev_priv->csc_enabled[pipe] = true;
	POSTING_READ(PIPECONF(pipe));

	/* Write csc coeff to csc regs */
	for (i = 0; i < 6; i++) {
		I915_WRITE(csc_reg + j, ((u32 *)data)[i]);
		j = j + 0x4;
	}
	return 0;
}

/* Disable color space conversion on PIPE */
void
do_intel_disable_csc(struct drm_device *dev, struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = NULL;
	u32 pipeconf = 0;
	int pipe = 0;

	if (IS_CHERRYVIEW(dev))
		chv_set_csc(dev, crtc, false);

	intel_crtc = to_intel_crtc(crtc);
	pipe = intel_crtc->pipe;
	pipeconf = I915_READ(PIPECONF(pipe));
	pipeconf &= ~(PIPECONF_CSC_ENABLE);

	/* Disable CSC on PIPE */
	I915_WRITE(PIPECONF(pipe), pipeconf);
	dev_priv->csc_enabled[pipe] = false;
	POSTING_READ(PIPECONF(pipe));
	return;
}

/* Parse userspace input coming from dev node*/
int parse_clrmgr_input(uint *dest, char *src, int max, int *num_bytes)
{
	int size = 0;
	int bytes = 0;
	char *populate = NULL;

	/* Check for trailing comma or \n */
	if (!dest || !src || *src == ',' || *src == '\n' || !(*num_bytes)) {
		DRM_ERROR("Invalid input to parse");
		return -EINVAL;
	}

	/* limit check */
	if (*num_bytes < max) {
		DRM_ERROR("Invalid input to parse");
		return -EINVAL;
	}

	/* Extract values from buffer */
	while ((size < max) && (*src != '\n')) {
		populate = strsep(&src, ",");
		if (!populate)
			break;

		bytes += (strlen(populate)+1);
		if (kstrtouint((const char *)populate, CLRMGR_BASE,
					&dest[size++])) {
			DRM_ERROR("Parse: Invalid limit");
			return -EINVAL;
		}
		if (src == NULL || *src == '\0')
			break;
	}
	/* Fill num_bytes with number of bytes read */
	*num_bytes = bytes;

	/* Return number of tokens parsed */
	return size;
}

/* Gamma correction for sprite planes on External display */
int intel_enable_external_sprite_gamma(struct drm_crtc *crtc, int planeid)
{
	DRM_ERROR("This functionality is not implemented yet\n");
	return -ENOSYS;
}

/* Gamma correction for External display plane*/
int intel_enable_external_gamma(struct drm_crtc *crtc)
{
	DRM_ERROR("This functionality is not implemented yet\n");
	return -ENOSYS;
}

/* Gamma correction for External pipe */
int intel_enable_external_pipe_gamma(struct drm_crtc *crtc)
{
	DRM_ERROR("This functionality is not implemented yet\n");
	return -ENOSYS;
}

/* Gamma correction for sprite planes on Primary display */
int intel_enable_sprite_gamma(struct drm_crtc *crtc, int planeid)
{
	u32 count = 0;
	u32 status = 0;
	u32 controlreg = 0;
	u32 correctreg = 0;

	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	switch (planeid) {
	case SPRITEA:
		correctreg = GAMMA_SPA_GAMC0;
		controlreg = GAMMA_SPA_CNTRL;
		break;

	case SPRITEB:
		correctreg = GAMMA_SPB_GAMC0;
		controlreg = GAMMA_SPB_CNTRL;
		break;

	case SPRITEC:
		correctreg = GAMMA_SPC_GAMC0;
		controlreg = GAMMA_SPC_CNTRL;
		break;

	case SPRITED:
		correctreg = GAMMA_SPD_GAMC0;
		controlreg = GAMMA_SPD_CNTRL;
		break;

	case SPRITEE:
		correctreg = GAMMA_SPE_GAMC0;
		controlreg = GAMMA_SPE_CNTRL;
		break;

	case SPRITEF:
		correctreg = GAMMA_SPF_GAMC0;
		controlreg = GAMMA_SPF_CNTRL;
		break;

	default:
		DRM_ERROR("Invalid sprite object gamma enable\n");
		return -EINVAL;
	}

	/* Write gamma cofficients in gamma regs*/
	while (count < GAMMA_SP_MAX_COUNT) {
		/* Write and read */
		I915_WRITE(correctreg - 4 * count, gamma_sprite_softlut[count]);
		status = I915_READ(correctreg - 4 * count++);
	}

	/* Enable gamma on plane */
	status = I915_READ(controlreg);
	status |= GAMMA_ENABLE_SPR;
	I915_WRITE(controlreg, status);

	DRM_DEBUG("Gamma applied on plane sprite%c\n",
		(planeid == SPRITEA) ? 'A' : 'B');

	return 0;
}

/* Gamma correction at Plane level */
int intel_enable_primary_gamma(struct drm_crtc *crtc)
{
	u32 odd = 0;
	u32 even = 0;
	u32 count = 0;
	u32 palreg = 0;
	u32 status = 0;
	u32 pipe = 0;
	struct intel_crtc *intel_crtc;
	struct drm_device *dev;
	struct drm_i915_private *dev_priv;

	/* Validate input */
	if (!crtc) {
		DRM_ERROR("Invalid CRTC object input to gamma enable\n");
		return -EINVAL;
	}

	intel_crtc = to_intel_crtc(crtc);
	pipe = intel_crtc->pipe;
	dev = crtc->dev;
	dev_priv = dev->dev_private;

	palreg = PALETTE(pipe);
	 /* 10.6 mode Gamma Implementation */
	while (count < GAMMA_CORRECT_MAX_COUNT) {
		/* Get the gamma corrected value from table */
		odd = gamma_softlut[pipe][count];
		even = gamma_softlut[pipe][count + 1];

		/* Write even and odd parts in palette regs*/
		I915_WRITE(palreg + 4 * count, even);
		I915_WRITE(palreg + 4 * ++count, odd);
		count++;
	}

	/* Write max values in 11.6 format */
	I915_WRITE(PIPE_GAMMA_MAX_BLUE(pipe), gcmax_softlut[pipe][0]);
	I915_WRITE(PIPE_GAMMA_MAX_GREEN(pipe), gcmax_softlut[pipe][1]);
	I915_WRITE(PIPE_GAMMA_MAX_RED(pipe), gcmax_softlut[pipe][2]);

	/* Enable gamma on PIPE  */
	status = I915_READ(PIPECONF(pipe));
	status |= PIPECONF_GAMMA;
	I915_WRITE(PIPECONF(pipe), status);
	DRM_DEBUG("Gamma enabled on Plane A\n");

	return 0;
}


/*
 * chv_set_cgm_csc
 * Cherryview specific csc correction method on PIPE.
 * inputs:
 * - intel_crtc*
 * - color manager registered property for cgm_csc_correction
 * - data: pointer to correction values to be applied
 */
bool chv_set_cgm_csc(struct intel_crtc *intel_crtc,
	const struct clrmgr_regd_prop *cgm_csc, const int *data, bool enable)
{
	u32 cgm_csc_reg, cgm_ctrl_reg, data_size, i;
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_property *property;

	property = cgm_csc->property;
	data_size = property->num_values;

	/* Validate input */
	if (data_size != CHV_CGM_CSC_MATRIX_MAX_VALS) {
		DRM_ERROR("Unexpected value count for CSC LUT\n");
		return false;
	}
	cgm_ctrl_reg = dev_priv->info.display_mmio_offset +
			cgm_ctrl[intel_crtc->pipe];

	if (enable) {
		DRM_DEBUG_DRIVER("Setting CSC on pipe = %d\n",
						intel_crtc->pipe);

		/* program CGM CSC values */
		cgm_csc_reg = dev_priv->info.display_mmio_offset +
					cgm_csc_st[intel_crtc->pipe];

		/*
		 * the input data is 32 bit signed int array
		 * of 9 coefficients and for 5 registers
		 * C0 - 16 bit (LSB)
		 * C1 - 16 bit (HSB)
		 * C8 - 16 bit (LSB) and HSB- reserved.
		 */
		for (i = 0; i < CGM_CSC_MAX_REGS; i++) {
			I915_WRITE(cgm_csc_reg + i*4,
					(data[EVEN(i)] >> 16) |
					((data[ODD(i)] >> 16) << 16));

			/* The last register has only valid LSB */
			if (i == 4)
				I915_WRITE(cgm_csc_reg + i*4,
					(data[EVEN(i)] >> 16));
		}

		/* enable csc if not enabled */
		if (!(I915_READ(cgm_ctrl_reg) & CGM_CSC_EN))
			I915_WRITE(cgm_ctrl_reg,
					I915_READ(cgm_ctrl_reg) | CGM_CSC_EN);
	} else {
		I915_WRITE(cgm_ctrl_reg,
			I915_READ(cgm_ctrl_reg) & ~CGM_CSC_EN);
	}
	return true;
}

/*
 * chv_apply_cgm_gamma
 * Cherryview specific u0.10 cgm gamma correction method on PIPE.
 * inputs:
 * - intel_crtc*
 * - data: pointer to correction values to be applied
 */
static bool chv_apply_cgm_gamma(struct intel_crtc *intel_crtc,
			const struct gamma_lut_data *data, bool enable)
{
	u32 i = 0;
	u32 cgm_gamma_reg = 0;
	u32 cgm_ctrl_reg = 0;

	struct drm_device *dev;
	struct drm_i915_private *dev_priv;

	/* Validate input */
	if (!intel_crtc) {
		DRM_ERROR("Invalid CRTC object input to CGM gamma enable\n");
		return false;
	}

	dev = intel_crtc->base.dev;
	dev_priv = dev->dev_private;

	cgm_ctrl_reg = dev_priv->info.display_mmio_offset +
			cgm_ctrl[intel_crtc->pipe];
	if (enable) {
		/*
		 * program CGM Gamma values is in
		 * u0.10 while i/p is 16 bit
		 */
		cgm_gamma_reg = dev_priv->info.display_mmio_offset +
				cgm_gamma_st[intel_crtc->pipe];

		for (i = 0; i < CHV_CGM_GAMMA_MATRIX_MAX_VALS; i++) {

			/* Red coefficent needs to be updated in D1 registers*/
			I915_WRITE(cgm_gamma_reg + 4 * ODD(i),
						(data[i].red) >> 6);

			/*
			 * green and blue coefficients
			 * need to be updated in D0 registers
			 */
			I915_WRITE(cgm_gamma_reg + 4 * EVEN(i),
					(((data[i].green) >> 6) << 16) |
					((data[i].blue) >> 6));
		}

		if (!(I915_READ(cgm_ctrl_reg) & CGM_GAMMA_EN)) {
			I915_WRITE(cgm_ctrl_reg,
				I915_READ(cgm_ctrl_reg) | CGM_GAMMA_EN);
			DRM_DEBUG("CGM Gamma enabled on Pipe %d\n",
							intel_crtc->pipe);
		}

	} else {
		I915_WRITE(cgm_ctrl_reg,
				I915_READ(cgm_ctrl_reg) & ~CGM_GAMMA_EN);
	}
	return true;
}

/*
 * chv_set_cgm_gamma
 * Cherryview specific u0.10 cgm gamma correction method on PIPE.
 * inputs:
 * - intel_crtc*
 * - color manager registered property for cgm_csc_correction
 * - data: pointer to correction values to be applied
 */
bool chv_set_cgm_gamma(struct intel_crtc *intel_crtc,
			const struct clrmgr_regd_prop *cgm_gamma,
			const struct gamma_lut_data *data, bool enable)
{
	struct drm_property *property;

	property = cgm_gamma->property;

	if (intel_crtc == NULL)
		return false;

	return chv_apply_cgm_gamma(intel_crtc, data, enable);
}

/*
 * chv_set_cgm_degamma
 * Cherryview specific cgm degamma correction method on PIPE.
 *  inputs:
 * - intel_crtc*
 * - color manager registered property for cgm_csc_correction
 * - data: pointer to correction values to be applied.
 */
bool chv_set_cgm_degamma(struct intel_crtc *intel_crtc,
			const struct clrmgr_regd_prop *cgm_degamma,
			const struct gamma_lut_data *data, bool enable)
{
	struct drm_device *dev;
	struct drm_i915_private *dev_priv;
	struct drm_property *property;
	u32 i = 0;
	u32 cgm_degamma_reg = 0;
	u32 cgm_ctrl_reg = 0;
	property = cgm_degamma->property;

	if (intel_crtc == NULL)
		return false;

	dev = intel_crtc->base.dev;
	dev_priv = dev->dev_private;

	/* Validate input */
	if (!intel_crtc) {
		DRM_ERROR("Invalid CRTC object i/p to CGM degamma enable\n");
		return -EINVAL;
	}
	cgm_ctrl_reg = dev_priv->info.display_mmio_offset +
			cgm_ctrl[intel_crtc->pipe];

	if (enable) {
		/* program CGM Gamma values is in u0.10 */
		cgm_degamma_reg = dev_priv->info.display_mmio_offset +
					cgm_degamma_st[intel_crtc->pipe];

		for (i = 0; i < CHV_CGM_DEGAMMA_MATRIX_MAX_VALS; i++) {
			/* Red coefficent needs to be updated in D1 registers*/
			I915_WRITE(cgm_degamma_reg + 4 * ODD(i),
						((data[i].red) >> 2));

			/*
			 * green and blue coefficients
			 * need to be updated in D0 registers
			 */
			I915_WRITE(cgm_degamma_reg + 4 * EVEN(i),
					(((data[i].green) >> 2) << 16) |
						((data[i].blue) >> 2));
		}

		/* If already enabled, do not enable again */
		if (!(I915_READ(cgm_ctrl_reg) & CGM_DEGAMMA_EN)) {
			I915_WRITE(cgm_ctrl_reg,
				I915_READ(cgm_ctrl_reg) | CGM_DEGAMMA_EN);
			DRM_DEBUG("CGM Degamma enabled on Pipe %d\n",
							intel_crtc->pipe);
		}

	 } else {
		I915_WRITE(cgm_ctrl_reg,
			I915_READ(cgm_ctrl_reg) & ~CGM_DEGAMMA_EN);
	}
	return true;
}

/*
 * intel_clrmgr_set_csc
 * CSC correction method is different across various
 * gen devices. c
 * inputs:
 * - intel_crtc *
 * - color manager registered property for csc correction
 * - data: pointer to correction values to be applied
 */
bool intel_clrmgr_set_cgm_csc(void *crtc,
	const struct clrmgr_regd_prop *cgm_csc, const struct lut_info *info)
{
	struct intel_crtc *intel_crtc = crtc;
	struct drm_device *dev = intel_crtc->base.dev;
	int *data;
	int ret = false;

	/* Validate input */
	if (!info || !info->data || !cgm_csc || !cgm_csc->property) {
		DRM_ERROR("Invalid input to set cgm_csc\n");
		return ret;
	}

#ifdef CLRMGR_DEBUG
	DRM_DEBUG_DRIVER("Clrmgr: Set csc: data len=%d\n",
			cgm_csc->property->num_values);
#endif
	data = kmalloc(sizeof(int) * (cgm_csc->property->num_values),
							GFP_KERNEL);
	if (!data) {
		DRM_ERROR("Out of memory\n");
		return ret;
	}

	if (copy_from_user(data, (const int __user *)info->data,
			cgm_csc->property->num_values * sizeof(int))) {
		DRM_ERROR("Failed to copy all data\n");
		goto free;
	}

	/* CHV CGM CSC color correction */
	if (IS_CHERRYVIEW(dev)) {
		if (chv_set_cgm_csc(intel_crtc, cgm_csc,
					data, info->enable))
			ret = true;
		goto free;
	}

	/* Todo: Support other gen devices */
	DRM_ERROR("CGM correction is supported only on CHV\n");

free:	kfree(data);
	return ret;
}

/*
* Gamma correction at PIPE level:
* This function applies gamma correction Primary as well as Sprite planes
* assosiated with this PIPE. Assumptions are:
* Plane A is internal display primary panel.
* Sprite A and B are interal display's sprite planes.
*/
int intel_enable_pipe_gamma(struct drm_crtc *crtc)
{
	u32 odd = 0;
	u32 even = 0;
	u32 count = 0;
	u32 palreg = 0;
	u32 status = 0;
	u32 pipe = 0;
	struct intel_crtc *intel_crtc;
	struct drm_device *dev;
	struct drm_i915_private *dev_priv;

	/* Validate input */
	if (!crtc) {
		DRM_ERROR("Invalid CRTC object input to gamma enable\n");
		return -EINVAL;
	}

	intel_crtc = to_intel_crtc(crtc);
	pipe = intel_crtc->pipe;
	dev = crtc->dev;
	dev_priv = dev->dev_private;
	dev_priv->gamma_enabled[pipe] = true;

	if (IS_CHERRYVIEW(dev))
		return chv_set_pipe_gamma(intel_crtc, true);

	palreg = PALETTE(pipe);
	 /* 10.6 mode Gamma Implementation */
	while (count < GAMMA_CORRECT_MAX_COUNT) {
		/* Get the gamma corrected value from table */
		odd = gamma_softlut[pipe][count];
		even = gamma_softlut[pipe][count + 1];

		/* Write even and odd parts in palette regs*/
		I915_WRITE(palreg + 4 * count, even);
		I915_WRITE(palreg + 4 * ++count, odd);
		count++;
	}

	/* Write max values in 11.6 format */
	I915_WRITE(PIPE_GAMMA_MAX_BLUE(pipe), gcmax_softlut[pipe][0]);
	I915_WRITE(PIPE_GAMMA_MAX_GREEN(pipe), gcmax_softlut[pipe][1]);
	I915_WRITE(PIPE_GAMMA_MAX_RED(pipe), gcmax_softlut[pipe][2]);

	/* Enable gamma for Plane A  */
	status = I915_READ(PIPECONF(pipe));
	status |= PIPECONF_GAMMA;
	I915_WRITE(PIPECONF(pipe), status);

	/* Enable gamma on Sprite plane A*/
	status = I915_READ(GAMMA_SP1_CNTRL(pipe));
	status |= GAMMA_ENABLE_SPR;
	I915_WRITE(GAMMA_SP1_CNTRL(pipe), status);

	/* Enable gamma on Sprite plane B*/
	status = I915_READ(GAMMA_SP2_CNTRL(pipe));
	status |= GAMMA_ENABLE_SPR;
	I915_WRITE(GAMMA_SP2_CNTRL(pipe), status);

	DRM_DEBUG("Gamma enabled on Pipe A\n");
	return 0;
}

/* Load gamma correction values corresponding to supplied
gamma and program palette accordingly */
int intel_crtc_enable_gamma(struct drm_crtc *crtc, u32 identifier)
{
	switch (identifier) {
	/* Whole pipe level correction */
	case PIPEA:
	case PIPEB:
	case PIPEC:
		return intel_enable_pipe_gamma(crtc);
	/* Primary display planes */
	case PLANEA:
		return intel_enable_primary_gamma(crtc);
	case PLANEB:
		return intel_enable_external_gamma(crtc);
	/* Sprite planes */
	case SPRITEA:
	case SPRITEB:
	case SPRITEC:
	case SPRITED:
	case SPRITEE:
	case SPRITEF:
		return intel_enable_sprite_gamma(crtc, identifier);
	default:
		DRM_ERROR("Invalid panel ID to Gamma enabled\n");
		return -EINVAL;
	}
}

int intel_disable_external_sprite_gamma(struct drm_crtc *crtc, u32 planeid)
{
	DRM_ERROR("This functionality is not implemented yet\n");
	return -EINVAL;
}

/* Disable Gamma correction on external display */
int intel_disable_external_gamma(struct drm_crtc *crtc)
{
	DRM_ERROR("This functionality is not implemented yet\n");
	return -EINVAL;
}

/*
* intel_clrmgr_set_gamma
* Gamma correction method is different across various
* gen devices. This is a wrapper function which will call
* the platform specific gamma set function
* inputs:
* - intel_crtc*
* - color manager registered property for gamma correction
* - data: pointer to correction values to be applied
*/
bool intel_clrmgr_set_cgm_gamma(void *crtc,
		const struct clrmgr_regd_prop *cgm_gamma,
				const struct lut_info  *info)
{
	struct intel_crtc *intel_crtc = crtc;
	struct drm_device *dev;
	struct gamma_lut_data *data;
	int ret = false;

	if (intel_crtc == NULL)
		return false;

	dev = intel_crtc->base.dev;

	/* Validate input */
	if (!info->data || !cgm_gamma || !cgm_gamma->property) {
		DRM_ERROR("Invalid input to set_gamma\n");
		return ret;
	}

	DRM_DEBUG_DRIVER("Setting gamma correction, len=%d\n",
		cgm_gamma->property->num_values);
#ifdef CLRMGR_DEBUG
	DRM_DEBUG_DRIVER("Clrmgr: Set gamma: len=%d\n",
				cgm_gamma->property->num_values);
#endif
	data = kmalloc(sizeof(struct gamma_lut_data) *
				(cgm_gamma->property->num_values),
				GFP_KERNEL);
	if (!data) {
		DRM_ERROR("Out of memory\n");
		return ret;
	}

	if (copy_from_user(data,
		(const struct gamma_lut_data __user *) info->data,
		cgm_gamma->property->num_values *
		sizeof(struct gamma_lut_data))) {

		DRM_ERROR("Failed to copy all data\n");
		goto free;
	}

	/* CHV has CGM gamma correction */
	if (IS_CHERRYVIEW(dev)) {
		if (chv_set_cgm_gamma(intel_crtc,
				cgm_gamma, data, info->enable))
			ret = true;
		goto free;
	}

	/* Todo: Support other gen devices */
	DRM_ERROR("Color correction is supported only on VLV for now\n");
free:	kfree(data);
	return ret;
}

/*
* intel_clrmgr_set_cgm_degamma
* Gamma correction method is different across various
* gen devices. This is a wrapper function which will call
* the platform specific gamma set function
* inputs:
* - intel_crtc*
* - color manager registered property for gamma correction
* - data: pointer to correction values to be applied
*/
bool intel_clrmgr_set_cgm_degamma(void *crtc,
		const struct clrmgr_regd_prop *cgm_degamma,
				const struct lut_info *info)
{
	struct intel_crtc *intel_crtc = crtc;
	struct drm_device *dev = intel_crtc->base.dev;
	struct gamma_lut_data *data;
	int ret = false;

	/* Validate input */
	if (!info->data || !cgm_degamma || !cgm_degamma->property) {
		DRM_ERROR("Invalid input to set_gamma\n");
		return ret;
	}

	DRM_DEBUG_DRIVER("Setting gamma correction, len=%d\n",
		cgm_degamma->property->num_values);
#ifdef CLRMGR_DEBUG
	DRM_DEBUG_DRIVER("Clrmgr: Set gamma: len=%d\n",
			cgm_degamma->property->num_values);
#endif
	data = kmalloc(sizeof(struct gamma_lut_data) *
				(cgm_degamma->property->num_values),
				GFP_KERNEL);
	if (!data) {
		DRM_ERROR("Out of memory\n");
		goto free;
	}

	if (copy_from_user(data,
			(const struct gamma_lut_data __user *) info->data,
			cgm_degamma->property->num_values *
			sizeof(struct gamma_lut_data))) {
		DRM_ERROR("Failed to copy all data\n");
		goto free;
	}

	/* CHV has CGM degamma correction */
	if (IS_CHERRYVIEW(dev)) {
		if (chv_set_cgm_degamma(intel_crtc,
			cgm_degamma, data, info->enable))
			ret = true;
		goto free;
	}
	/* Todo: Support other gen devices */
	DRM_ERROR("Color correction is supported only on VLV for now\n");

free:	kfree(data);
	return ret;
}


/*
 * intel_clrmgr_set_property
 * Set the value of a DRM color correction property
 * and program the corresponding registers
 * Inputs:
 *  - intel_crtc *
 *  - color manager registered property * which encapsulates
 *    drm_property and additional data.
 * - value is the new value to be set
 */
bool intel_clrmgr_set_pipe_property(struct intel_crtc *intel_crtc,
		struct clrmgr_regd_prop *cp, uint64_t value)
{
	bool ret = false;
	struct lut_info *info;

	/* Sanity */
	if (!cp || !cp->property || !value) {
		DRM_ERROR("NULL input to set_property\n");
		return false;
	}

	DRM_DEBUG_DRIVER("Property %s len:%d\n",
				cp->property->name, cp->property->num_values);

	info = kmalloc(sizeof(struct lut_info), GFP_KERNEL);
	if (!info) {
		DRM_ERROR("Out of memory\n");
		return false;
	}

	info = (struct lut_info *) (uintptr_t) value;

	/* call the corresponding set property */
	if (cp->set_property) {
		if (!cp->set_property((void *)intel_crtc, cp, info)) {
			DRM_ERROR("Set property for %s failed\n",
						cp->property->name);
			return ret;
		} else {
			ret = true;
			cp->enabled = true;
			DRM_DEBUG_DRIVER("Set property %s successful\n",
				cp->property->name);
		}
	}

	return ret;
}

/* Disable gamma correction for sprite planes on primary display */
int intel_disable_sprite_gamma(struct drm_crtc *crtc, u32 planeid)
{
	u32 status = 0;
	u32 controlreg = 0;

	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	switch (planeid) {
	case SPRITEA:
		controlreg = GAMMA_SPA_CNTRL;
		break;

	case SPRITEB:
		controlreg = GAMMA_SPB_CNTRL;
		break;

	case SPRITEC:
		controlreg = GAMMA_SPC_CNTRL;
		break;

	case SPRITED:
		controlreg = GAMMA_SPD_CNTRL;
		break;

	case SPRITEE:
		controlreg = GAMMA_SPE_CNTRL;
		break;

	case SPRITEF:
		controlreg = GAMMA_SPF_CNTRL;
		break;

	default:
		DRM_ERROR("Invalid sprite object gamma enable\n");
		return -EINVAL;
	}

	/* Reset pal regs */
	intel_crtc_load_lut(crtc);

	/* Disable gamma on PIPE config  */
	status = I915_READ(controlreg);
	status &= ~(GAMMA_ENABLE_SPR);
	I915_WRITE(controlreg, status);

	/* TODO: Reset gamma table default */
	DRM_DEBUG("Gamma on Sprite %c disabled\n",
		(planeid == SPRITEA) ? 'A' : 'B');

	return 0;
}

/* Disable gamma correction on Primary display */
int intel_disable_primary_gamma(struct drm_crtc *crtc)
{
	u32 status = 0;
	struct drm_device *dev = crtc->dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* Reset pal regs */
	intel_crtc_load_lut(crtc);

	/* Disable gamma on PIPE config  */
	status = I915_READ(PIPECONF(intel_crtc->pipe));
	status &= ~(PIPECONF_GAMMA);
	I915_WRITE(PIPECONF(intel_crtc->pipe), status);

	/* TODO: Reset gamma table default */
	DRM_DEBUG("Gamma disabled on Pipe\n");
	return 0;
}

/*
 * intel_clrmgr_register:
 * Register color correction properties as DRM propeties
 */
struct drm_property *intel_clrmgr_register(struct drm_device *dev,
	struct drm_mode_object *obj, const struct clrmgr_property *cp)
{
	struct drm_property *property;

	/* Create drm property */
	switch (cp->type) {

	case DRM_MODE_PROP_BLOB:
		property = drm_property_create(dev,
				DRM_MODE_PROP_BLOB,
					cp->name, cp->len);
		if (!property) {
			DRM_ERROR("Failed to create property %s\n",
							cp->name);
			return NULL;
		}

		/* Attach property to object */
		drm_object_attach_property(obj, property, 0);
		break;

	case DRM_MODE_PROP_RANGE:
		property = drm_property_create_range(dev,
				DRM_MODE_PROP_RANGE, cp->name,
						cp->min, cp->max);
		if (!property) {
			DRM_ERROR("Failed to create property %s\n",
							cp->name);
			return NULL;
		}
		drm_object_attach_property(obj, property, 0);
		break;

	default:
		DRM_ERROR("Unsupported type for property %s\n",
							cp->name);
		return NULL;
	}

	DRM_DEBUG_DRIVER("Registered property %s\n", property->name);
	return property;
}

bool intel_clrmgr_register_pipe_property(struct intel_crtc *intel_crtc,
		struct clrmgr_reg_request *features)
{
	u32 count = 0;
	struct clrmgr_property *cp;
	struct clrmgr_regd_prop *regd_property;
	struct drm_property *property;
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_mode_object *obj = &intel_crtc->base.base;
	struct clrmgr_status *status = intel_crtc->color_status;

	/* Color manager initialized? */
	if (!status) {
		DRM_ERROR("Request wihout pipe init\n");
		return false;
	}

	/* Validate input */
	if (!features || !features->no_of_properties) {
		DRM_ERROR("Invalid input to color manager register\n");
		return false;
	}

	/* Create drm property */
	while (count < features->no_of_properties) {
		cp = &features->cp[count++];
		property = intel_clrmgr_register(dev, obj, cp);
		if (!property) {
			DRM_ERROR("Failed to register property %s\n",
							property->name);
			goto error;
		}

		/* Add the property in global pipe status */
		regd_property = kzalloc(sizeof(struct clrmgr_regd_prop),
								GFP_KERNEL);
		regd_property->property = property;
		regd_property->enabled = false;
		regd_property->set_property = cp->set_property;
		status->cp[status->no_of_properties++] = regd_property;
	}
	/* Successfully registered all */
	DRM_DEBUG_DRIVER("Registered color properties on pipe %c\n",
		pipe_name(intel_crtc->pipe));
	return true;

error:
	if (--count) {
		DRM_ERROR("Can only register following properties\n");
		while (count--)
			DRM_ERROR("%s", status->cp[count]->property->name);
	} else
		DRM_ERROR("Can not register any property\n");
	return false;
}

/* Disable gamma correction on Primary display */
int intel_disable_pipe_gamma(struct drm_crtc *crtc)
{
	u32 status = 0;
	struct drm_device *dev = crtc->dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_i915_private *dev_priv = dev->dev_private;

	dev_priv->gamma_enabled[intel_crtc->pipe] = false;

	if (IS_CHERRYVIEW(dev))
		return chv_set_pipe_gamma(intel_crtc, false);

	/* Reset pal regs */
	intel_crtc_load_lut(crtc);

	/* Disable gamma on PIPE config  */
	status = I915_READ(PIPECONF(intel_crtc->pipe));
	status &= ~(PIPECONF_GAMMA);
	I915_WRITE(PIPECONF(intel_crtc->pipe), status);

	/* Disable gamma on SpriteA  */
	status = I915_READ(GAMMA_SP1_CNTRL(intel_crtc->pipe));
	status &= ~(GAMMA_ENABLE_SPR);
	I915_WRITE(GAMMA_SP1_CNTRL(intel_crtc->pipe), status);

	/* Disable gamma on SpriteB  */
	status = I915_READ(GAMMA_SP2_CNTRL(intel_crtc->pipe));
	status &= ~(GAMMA_ENABLE_SPR);
	I915_WRITE(GAMMA_SP2_CNTRL(intel_crtc->pipe), status);

	/* TODO: Reset gamma table default */
	DRM_DEBUG("Gamma disabled on Pipe %d\n", intel_crtc->pipe);
	return 0;
}

/*
* intel_clrmgr_deregister
* De register color manager properties
* destroy the DRM property and cleanup
* Should be called from CRTC/Plane .destroy function
* input:
* - struct drm device *dev
* - status: attached colot status
*/
void intel_clrmgr_deregister(struct drm_device *dev,
	struct clrmgr_status *status)
{
	u32 count = 0;
	struct clrmgr_regd_prop *cp;

	/* Free drm property */
	while (count < status->no_of_properties) {
		cp = status->cp[count++];

		/* Destroy property */
		drm_property_destroy(dev, cp->property);

		/* Release the color property */
		kfree(status->cp[count]);
		status->cp[count] = NULL;
	}

	/* Successfully deregistered all */
	DRM_DEBUG_DRIVER("De-registered all color properties\n");
}

/* Load gamma correction values corresponding to supplied
gamma and program palette accordingly */
int intel_crtc_disable_gamma(struct drm_crtc *crtc, u32 identifier)
{
	switch (identifier) {
	/* Whole pipe level correction */
	case PIPEA:
	case PIPEB:
	case PIPEC:
		return intel_disable_pipe_gamma(crtc);
	/* Primary planes */
	case PLANEA:
		return intel_disable_primary_gamma(crtc);
	case PLANEB:
		return intel_disable_external_gamma(crtc);
	/* Sprite plane */
	case SPRITEA:
	case SPRITEB:
	case SPRITEC:
	case SPRITED:
	case SPRITEE:
	case SPRITEF:
		return intel_disable_sprite_gamma(crtc, identifier);
	default:
		DRM_ERROR("Invalid panel ID to Gamma enabled\n");
		return -EINVAL;
	}
	return 0;
}

/* Tune Contrast Brightness Value for Sprite */
int intel_sprite_cb_adjust(struct drm_i915_private *dev_priv,
		struct cont_brightlut *cb_ptr)
{
	if (!dev_priv || !cb_ptr) {
		DRM_ERROR("Contrast Brightness: Invalid Arguments\n");
		return -EINVAL;
	}

	switch (cb_ptr->sprite_no) {
	/* Sprite plane */
	case SPRITEA:
		if (is_sprite_enabled(dev_priv, 0, 0) || dev_priv->is_resuming)
			I915_WRITE(SPRITEA_CB_REG, cb_ptr->val);
	break;
	case SPRITEB:
		if (is_sprite_enabled(dev_priv, 0, 1) || dev_priv->is_resuming)
			I915_WRITE(SPRITEB_CB_REG, cb_ptr->val);
	break;
	case SPRITEC:
		if (is_sprite_enabled(dev_priv, 1, 0) || dev_priv->is_resuming)
			I915_WRITE(SPRITEC_CB_REG, cb_ptr->val);
	break;
	case SPRITED:
		if (is_sprite_enabled(dev_priv, 1, 1) || dev_priv->is_resuming)
			I915_WRITE(SPRITED_CB_REG, cb_ptr->val);
	break;
	case SPRITEE:
		if (is_sprite_enabled(dev_priv, 2, 0) || dev_priv->is_resuming)
			I915_WRITE(SPRITEE_CB_REG, cb_ptr->val);
	break;
	case SPRITEF:
		if (is_sprite_enabled(dev_priv, 2, 1) || dev_priv->is_resuming)
			I915_WRITE(SPRITEF_CB_REG, cb_ptr->val);
	break;
	default:
		DRM_ERROR("Invalid Sprite Number\n");
		return -EINVAL;
	}
	return 0;
}

/*
* intel_attach_pipe_color_correction:
* register color correction properties as DRM CRTC properties
* for a particular device
* input:
* - intel_crtc : CRTC to attach color correcection with
*/
void
intel_attach_pipe_color_correction(struct intel_crtc *intel_crtc)
{
	struct clrmgr_reg_request *features;
	struct drm_crtc *crtc = &intel_crtc->base;
	struct drm_device *dev = crtc->dev;

	/* Color manager initialized? */
	if (!intel_crtc->color_status) {
		DRM_ERROR("Color manager not initialized for PIPE %d\n",
			intel_crtc->pipe);
		return;
	}

	features = kzalloc(sizeof(struct clrmgr_reg_request), GFP_KERNEL);
	if (!features) {
		DRM_ERROR("kzalloc failed: pipe color features\n");
		return;
	}

	features->no_of_properties = ARRAY_SIZE(gen6_pipe_color_corrections);
	memcpy(features->cp, gen6_pipe_color_corrections,
			features->no_of_properties
				* sizeof(struct clrmgr_property));

	/* Register pipe level color properties */
	if (!intel_clrmgr_register_pipe_property(intel_crtc, features))
		DRM_ERROR("Register pipe color property failed\n");
	else
		DRM_DEBUG_DRIVER("Attached colot corrections for pipe %d\n",
		intel_crtc->pipe);

	/* WA: Enable CGM block CSC with Unity Matrix, disabling CGM block on
	 * the fly leads to display blank-out, So keep CSC always ON
	 */
	if (IS_CHERRYVIEW(dev))
		chv_set_csc(dev, crtc, false);
	kfree(features);
}


/*
* intel_clrmgr_init:
* allocate memory to save color correction status
* input: struct drm_device
*/
struct clrmgr_status *intel_clrmgr_init(struct drm_device *dev)
{
	struct clrmgr_status *status;

	/* Sanity */
	if (!IS_VALLEYVIEW(dev)) {
		DRM_ERROR("Color manager is supported for VLV for now\n");
		return NULL;
	}

	/* Allocate and attach color status tracker */
	status = kzalloc(sizeof(struct clrmgr_status), GFP_KERNEL);
	if (!status) {
		DRM_ERROR("Out of memory, cant init color manager\n");
		return NULL;
	}
	DRM_DEBUG_DRIVER("\n");
	return status;
}

/*
* intel_clrmgr_exit
* Free allocated memory for color status
* Should be called from CRTC/Plane .destroy function
* input: color status
*/
void intel_clrmgr_exit(struct drm_device *dev, struct clrmgr_status *status)
{
	/* First free the DRM property, then status */
	if (status) {
		intel_clrmgr_deregister(dev, status);
		kfree(status);
	}
}

/* Tune Hue Saturation Value for Sprite */
int intel_sprite_hs_adjust(struct drm_i915_private *dev_priv,
		struct hue_saturationlut *hs_ptr)
{
	if (!dev_priv || !hs_ptr) {
		DRM_ERROR("Hue Saturation: Invalid Arguments\n");
		return -EINVAL;
	}

	switch (hs_ptr->sprite_no) {
	/* Sprite plane */
	case SPRITEA:
		if (is_sprite_enabled(dev_priv, 0, 0) || dev_priv->is_resuming)
			I915_WRITE(SPRITEA_HS_REG, hs_ptr->val);
	break;
	case SPRITEB:
		if (is_sprite_enabled(dev_priv, 0, 1) || dev_priv->is_resuming)
			I915_WRITE(SPRITEB_HS_REG, hs_ptr->val);
	break;
	case SPRITEC:
		if (is_sprite_enabled(dev_priv, 1, 0) || dev_priv->is_resuming)
			I915_WRITE(SPRITEC_HS_REG, hs_ptr->val);
	break;
	case SPRITED:
		if (is_sprite_enabled(dev_priv, 1, 1) || dev_priv->is_resuming)
			I915_WRITE(SPRITED_HS_REG, hs_ptr->val);
	break;
	case SPRITEE:
		if (is_sprite_enabled(dev_priv, 2, 0) || dev_priv->is_resuming)
			I915_WRITE(SPRITEE_HS_REG, hs_ptr->val);
	break;
	case SPRITEF:
		if (is_sprite_enabled(dev_priv, 2, 1) || dev_priv->is_resuming)
			I915_WRITE(SPRITEF_HS_REG, hs_ptr->val);
	break;
	default:
		DRM_ERROR("Invalid Sprite Number\n");
		return -EINVAL;
	}
	return 0;
}

static bool intel_restore_cb(struct drm_device *dev)
{
	int count = 0;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int no_spr_reg = IS_CHERRYVIEW(dev) ? NO_SPRITE_REG_CHV : NO_SPRITE_REG;

	while (count < no_spr_reg) {
		if (intel_sprite_cb_adjust(dev_priv, &savedcbvalues[count++])) {
			DRM_ERROR("Color Restore: Error restoring CB\n");
			return false;
		}
	}

	return true;
}

static bool intel_restore_hs(struct drm_device *dev)
{
	int count = 0;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int no_spr_reg = IS_CHERRYVIEW(dev) ? NO_SPRITE_REG_CHV : NO_SPRITE_REG;

	while (count < no_spr_reg) {
		if (intel_sprite_hs_adjust(dev_priv, &savedhsvalues[count++])) {
			DRM_ERROR("Color Restore: Error restoring HS\n");
			return false;
		}
	}

	return true;
}

bool intel_restore_clr_mgr_status(struct drm_device *dev)
{
	struct drm_crtc *crtc = NULL;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pipe = 0;

	/* Validate input */
	if (!dev_priv) {
		DRM_ERROR("Color Restore: Invalid input\n");
		return false;
	}

	/* Search for a CRTC */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		pipe = to_intel_crtc(crtc)->pipe;

		/* If csc enabled, restore csc */
		if (dev_priv->csc_enabled[pipe]) {
			if (do_intel_enable_csc(dev,
					(void *) csc_softlut[pipe], crtc)) {
				DRM_ERROR("Color Restore: CSC failed\n");
				return false;
			}
		} else if (IS_CHERRYVIEW(dev)) {
			/* WA: Keep CGM block active in CHT */
			if (chv_set_csc(dev, crtc, false)) {
				DRM_ERROR("Color Restore: CSC failed\n");
				return false;
			}
		}

		/* If degamma enabled, restore degamma */
		if (IS_CHERRYVIEW(dev) && dev_priv->degamma_enabled[pipe]) {
			if (chv_set_pipe_degamma(crtc, true, false)) {
				DRM_ERROR("Color Restore: degamma failed\n");
				return false;
			}
		}
		/* If gamma enabled, restore gamma */
		if (dev_priv->gamma_enabled[pipe]) {
			if (intel_crtc_enable_gamma(crtc, PIPEID(pipe))) {
				DRM_ERROR("Color Restore: gamma failed\n");
				return false;
			}
		}


	}

	if (!intel_restore_hs(dev)) {
		DRM_ERROR("Color Restore: Restore hue/sat failed\n");
		return false;
	}

	if (!intel_restore_cb(dev)) {
		DRM_ERROR("Color Restore: Restore CB failed\n");
		return false;
	}

	DRM_DEBUG("Color Restore: Restore success\n");
	return true;
}
EXPORT_SYMBOL(intel_restore_clr_mgr_status);

void intel_save_cb_status(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	savedcbvalues[0].val = I915_READ(SPRITEA_CB_REG);
	savedcbvalues[1].val = I915_READ(SPRITEB_CB_REG);
	savedcbvalues[2].val = I915_READ(SPRITEC_CB_REG);
	savedcbvalues[3].val = I915_READ(SPRITED_CB_REG);
	if (IS_CHERRYVIEW(dev)) {
		savedcbvalues[4].val = I915_READ(SPRITEE_CB_REG);
		savedcbvalues[5].val = I915_READ(SPRITEF_CB_REG);
	}
}

void intel_save_hs_status(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	savedhsvalues[0].val = I915_READ(SPRITEA_HS_REG);
	savedhsvalues[1].val = I915_READ(SPRITEB_HS_REG);
	savedhsvalues[2].val = I915_READ(SPRITEC_HS_REG);
	savedhsvalues[3].val = I915_READ(SPRITED_HS_REG);
	if (IS_CHERRYVIEW(dev)) {
		savedhsvalues[4].val = I915_READ(SPRITEE_HS_REG);
		savedhsvalues[5].val = I915_READ(SPRITEF_HS_REG);
	}
}

void intel_save_clr_mgr_status(struct drm_device *dev)
{
	intel_save_hs_status(dev);
	intel_save_cb_status(dev);
}
EXPORT_SYMBOL(intel_save_clr_mgr_status);
