/*
 * Whiskey Cove  --  Device access for Intel WhiskeyCove PMIC
 *
 * Copyright (C) 2013, 2014 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Author: Yang Bin <bin.yang@intel.com>
 * Author: Kannappan <r.kannappan@intel.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/acpi.h>
#include <linux/version.h>
#include <asm/intel_wcove_bcu.h>
#include <linux/iio/intel_wcove_gpadc.h>
#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/types.h>
#include <linux/power/intel_pmic_ccsm.h>
#include <linux/mfd/intel_soc_pmic_wcove.h>
#include "./intel_soc_pmic_core.h"

#define WHISKEY_COVE_IRQ_NUM	17

#define CHIPID		0x00
#define CHIPVER	0x01

#define IRQLVL1	0x02
#define PWRSRCIRQ	0x03
#define THRM0IRQ	0x04
#define THRM1IRQ	0x05
#define THRM2IRQ	0x06
#define BCUIRQ		0x07
#define THRM3IRQ	0xD9
#define CHGRIRQ	0x0A

#define MIRQLVL1	0x0E
#define MPWRSRCIRQ	0x0F
#define MTHRMIRQ0	0x0D
#define MTHRMIRQ1	0x12
#define MTHRMIRQ2	0x13
#define MTHRMIRQ3	0xDA
#define MCHGRIRQ	0x17

static bool wcove_init_done;

static struct gpadc_regmap_t whiskeycove_gpadc_regmaps[GPADC_NUM_CHANNELS] = {
	{"VBAT",	5,	0x4F03, 0x4F04, 0xFF, 0xFF, 0xFF, 0xFF},
	{"BATID",	4,	0x4F06, 0x4F07, 0xFF, 0xFF, 0xFF, 0xFF},
	{"PMICTEMP",	3,	0x4F42,	0x4F43, 0x4F33, 0x4F34, 0x4F33, 0x4F34},
	{"BATTEMP0",	2,	0x4F15, 0x4F16, 0xFF, 0xFF, 0xFF, 0xFF},
	{"BATTEMP1",	2,	0x4F17, 0x4F18, 0xFF, 0xFF, 0xFF, 0xFF},
	{"SYSTEMP0",	3,	0x4F38, 0x4F39, 0x4F23, 0x4F24, 0x4F25, 0x4F26},
	{"SYSTEMP1",	3,	0x4F3A, 0x4F3B, 0x4F27, 0x4F28, 0x4F29, 0x4F2A},
	{"SYSTEMP2",	3,	0x4F3C, 0x4F3D, 0x4F2B, 0x4F2C, 0x4F2D, 0x4F2E},
	{"USBID",	1,	0x4F08, 0x4F09, 0xFF, 0xFF, 0xFF, 0xFF},
	{"PEAK",	7,	0x4F13, 0x4F14, 0xFF, 0xFF, 0xFF, 0xFF},
	{"AGND",	6,	0x4F0A, 0x4F0B, 0xFF, 0xFF, 0xFF, 0xFF},
	{"VREF",	6,	0x4F0A, 0x4F0B, 0xFF, 0xFF, 0xFF, 0xFF},
};

static struct gpadc_regs_t whiskeycove_gpadc_regs = {
	.gpadcreq	=	0x4F02,
	.gpadcreq_irqen	=	0,
	.gpadcreq_busy	=	(1 << 0),
	.mirqlvl1	=	0x6e0E,
	.mirqlvl1_adc	=	(1 << 3),
	.adc1cntl	=	0x4F05,
	.adcirq		=	0x6E08,
	.madcirq	=	0x6E15,
};

#define MSIC_ADC_MAP(_adc_channel_label,			\
		     _consumer_dev_name,                        \
		     _consumer_channel)                         \
	{                                                       \
		.adc_channel_label = _adc_channel_label,        \
		.consumer_dev_name = _consumer_dev_name,        \
		.consumer_channel = _consumer_channel,          \
	}

static struct iio_map wc_iio_maps[] = {
	MSIC_ADC_MAP("CH0", "VIBAT", "VBAT"),
	MSIC_ADC_MAP("CH1", "BATID", "BATID"),
	MSIC_ADC_MAP("CH2", "PMICTEMP", "PMICTEMP"),
	MSIC_ADC_MAP("CH3", "BATTEMP", "BATTEMP0"),
	MSIC_ADC_MAP("CH4", "BATTEMP", "BATTEMP1"),
	MSIC_ADC_MAP("CH5", "SYSTEMP", "SYSTEMP0"),
	MSIC_ADC_MAP("CH6", "SYSTEMP", "SYSTEMP1"),
	MSIC_ADC_MAP("CH7", "SYSTEMP", "SYSTEMP2"),
	MSIC_ADC_MAP("CH8", "USBID", "USBID"),
	MSIC_ADC_MAP("CH9", "PEAK", "PEAK"),
	MSIC_ADC_MAP("CH10", "GPMEAS", "AGND"),
	MSIC_ADC_MAP("CH11", "GPMEAS", "VREF"),
	{ },
};

#define MSIC_ADC_CHANNEL(_type, _channel, _datasheet_name) \
	{                               \
		.indexed = 1,           \
		.type = _type,          \
		.channel = _channel,    \
		.datasheet_name = _datasheet_name,      \
	}

static const struct iio_chan_spec const wc_adc_channels[] = {
	MSIC_ADC_CHANNEL(IIO_VOLTAGE, 0, "CH0"),
	MSIC_ADC_CHANNEL(IIO_VOLTAGE, 1, "CH1"),
	MSIC_ADC_CHANNEL(IIO_TEMP, 2, "CH2"),
	MSIC_ADC_CHANNEL(IIO_TEMP, 3, "CH3"),
	MSIC_ADC_CHANNEL(IIO_TEMP, 4, "CH4"),
	MSIC_ADC_CHANNEL(IIO_TEMP, 5, "CH5"),
	MSIC_ADC_CHANNEL(IIO_TEMP, 6, "CH6"),
	MSIC_ADC_CHANNEL(IIO_TEMP, 7, "CH7"),
	MSIC_ADC_CHANNEL(IIO_VOLTAGE, 8, "CH8"),
	MSIC_ADC_CHANNEL(IIO_VOLTAGE, 9, "CH9"),
	MSIC_ADC_CHANNEL(IIO_VOLTAGE, 10, "CH10"),
	MSIC_ADC_CHANNEL(IIO_VOLTAGE, 11, "CH11"),
};

enum {
	PWRSRC_LVL1 = 0,
	THRM_LVL1,
	BCU_IRQ,
	ADC_IRQ,
	CHGR_LVL1,
	GPIO_IRQ,
	CRIT_IRQ = 7,
	PWRSRC_IRQ,
	THRM1_IRQ,
	BATALRT_IRQ,
	BATZC_IRQ,
	CHGR_IRQ,
	THRM0_IRQ,
	PMICI2C_IRQ,
	THRM3_IRQ,
	CTYPE_IRQ,
};

struct intel_soc_pmic whiskey_cove_pmic;

static struct temp_lookup th05_lookup_tbl[] = {
	{2241, 125, 0}, {2541, 120, 0},
	{2893, 115, 0}, {3307, 110, 0},
	{3774, 105, 0}, {4130, 100, 0},
	{4954, 95, 0}, {5178, 90, 0},
	{6612, 85, 0}, {7768, 80, 0},
	{8905, 75, 0}, {10360, 70, 0},
	{12080, 65, 0}, {14110, 60, 0},
	{16540, 55, 0}, {19450, 50, 0},
	{22890, 45, 0}, {27260, 40, 0},
	{32520, 35, 0}, {38980, 30, 0},
	{47000, 25, 0}, {56980, 20, 0},
	{69500, 15, 0}, {85320, 10, 0},
	{105400, 5, 0}, {131200, 0, 0},
	{164500, -5, 0}, {207800, -10, 0},
	{264700, -15, 0}, {340200, -20, 0},
	{441500, -25, 0}, {579000, -30, 0},
	{766900, -35, 0}, {1027000, -40, 0},
};

static struct pmic_regs pmic_wcove_regmap = {
	.pmic_id = 0x00,
	.pmic_irqlvl1 = WC_IRQLVL1_ADDR,
	.pmic_mirqlvl1 = WC_IRQLVL1_MASK_ADDR,
	.pmic_chgrirq0 = WC_CHGRIRQ0_ADDR,
	.pmic_schgrirq0 = WC_SCHGRIRQ0_ADDR,
	.pmic_mchgrirq0 = WC_MCHGRIRQ0_ADDR,
	.pmic_chgrirq1 = WC_PWRSRC_ADDR,
	.pmic_schgrirq1 = WC_SPWRSRC_ADDR,
	.pmic_mchgrirq1 = WC_MPWRSRC_ADDR,
	.pmic_chgrctrl0 = WC_CHGRCTRL0_ADDR,
	.pmic_chgrctrl1 = WC_CHGRCTRL1_ADDR,
	.pmic_lowbattdet0 = WC_LOWBATTDET0_ADDR,
	.pmic_lowbattdet1 = WC_LOWBATTDET1_ADDR,
	.pmic_battdetctrl = WC_BATTDETCTRL_ADDR,
	.pmic_vbusdetctrl = WC_VBUSDETCTRL_ADDR,
	.pmic_vdcindetctrl = WC_VDCINDETCTRL_ADDR,
	.pmic_chgrstatus = WC_CHGRSTATUS_ADDR,
	.pmic_usbidctrl = WC_USBIDCTRL_ADDR,
	.pmic_usbidstat = WC_USBIDSTAT_ADDR,
	.pmic_wakesrc = WC_WAKESRC_ADDR,
	.pmic_usbphyctrl = WC_USBPHYCTRL_ADDR,
	.pmic_dbg_usbbc1 = WC_DBGUSBBC1_ADDR,
	.pmic_dbg_usbbc2 = WC_DBGUSBBC2_ADDR,
	.pmic_dbg_usbbcstat = WC_DBGUSBBCSTAT_ADDR,
	.pmic_usbpath = WC_USBPATH_ADDR,
	.pmic_usbsrcdetstat = WC_USBSRCDETSTATUS_ADDR,
	.pmic_chrttaddr = WC_CHRTTADDR_ADDR,
	.pmic_chrttdata = WC_CHRTTDATA_ADDR,
	.pmic_thrmbatzone = WC_THRMBATZONE_ADDR,
	.pmic_thrmzn0h = WC_THRMZN0H_ADDR,
	.pmic_thrmzn0l = WC_THRMZN0L_ADDR,
	.pmic_thrmzn1h = WC_THRMZN1H_ADDR,
	.pmic_thrmzn1l = WC_THRMZN1L_ADDR,
	.pmic_thrmzn2h = WC_THRMZN2H_ADDR,
	.pmic_thrmzn2l = WC_THRMZN2L_ADDR,
	.pmic_thrmzn3h = WC_THRMZN3H_ADDR,
	.pmic_thrmzn3l = WC_THRMZN3L_ADDR,
	.pmic_thrmzn4h = WC_THRMZN4H_ADDR,
	.pmic_thrmzn4l = WC_THRMZN4L_ADDR,
	.pmic_thrmirq0 = WC_THRMIRQ0_ADDR,
	.pmic_mthrmirq0 = WC_MTHRMIRQ0_ADDR,
	.pmic_sthrmirq0 = WC_STHRMIRQ0_ADDR,
	.pmic_thrmirq1 = WC_THRMIRQ1_ADDR,
	.pmic_mthrmirq1 = WC_MTHRMIRQ1_ADDR,
	.pmic_sthrmirq1 = WC_STHRMIRQ1_ADDR,
	.pmic_thrmirq2 = WC_THRMIRQ2_ADDR,
	.pmic_mthrmirq2 = WC_MTHRMIRQ2_ADDR,
	.pmic_sthrmirq2 = WC_STHRMIRQ2_ADDR,
};

static struct pmic_ccsm_int_cfg wc_intmap[] = {
	{ PMIC_INT_VBUS,
		WC_PWRSRC_ADDR, WC_MPWRSRC_ADDR,
		WC_SPWRSRC_ADDR, 0x01 },
	{ PMIC_INT_DCIN,
		WC_PWRSRC_ADDR, WC_MPWRSRC_ADDR,
		WC_SPWRSRC_ADDR, 0x02 },
	{ PMIC_INT_BATTDET,
		WC_PWRSRC_ADDR, WC_MPWRSRC_ADDR,
		WC_SPWRSRC_ADDR, 0x04 },
	{ PMIC_INT_USBIDFLTDET,
		WC_PWRSRC_ADDR, WC_MPWRSRC_ADDR,
		WC_SPWRSRC_ADDR, 0x08 },
	{ PMIC_INT_USBIDGNDDET,
		WC_PWRSRC_ADDR, WC_MPWRSRC_ADDR,
		WC_SPWRSRC_ADDR, 0x10 },
	{ PMIC_INT_CTYP,
		WC_CHGRIRQ0_ADDR, WC_SCHGRIRQ0_ADDR,
		WC_MCHGRIRQ0_ADDR, 0x10 },
	{ PMIC_INT_BZIRQ,
		WC_THRMIRQ1_ADDR, WC_MTHRMIRQ1_ADDR,
		WC_STHRMIRQ1_ADDR, 0x80 },
	{ PMIC_INT_BATCRIT,
		WC_THRMIRQ1_ADDR, WC_MTHRMIRQ1_ADDR,
		WC_STHRMIRQ1_ADDR, 0x10 },
	{ PMIC_INT_BAT0ALRT0,
		WC_THRMIRQ2_ADDR, WC_MTHRMIRQ2_ADDR,
		WC_STHRMIRQ2_ADDR, 0x01 },
	{ PMIC_INT_BAT1ALRT0,
		WC_THRMIRQ2_ADDR, WC_MTHRMIRQ2_ADDR,
		WC_STHRMIRQ2_ADDR, 0x02 },
};

static struct wcove_bcu_platform_data wc_bcu_pdata = {
	.config = {
		{VWARNA_CFG_REG,	0xFF},
		{VWARNB_CFG_REG,	0xFC},
		{VCRIT_CFG_REG,		0xFD},
		{ICCMAXVCC_CFG_REG,	0x06},
		{ICCMAXVNN_CFG_REG,	0x06},
		{ICCMAXVGG_CFG_REG,	0x06},
		{BCUDISB_BEH_REG,	0x01},
		{BCUDISCRIT_BEH_REG,	0x01},
		{BCUVSYS_DRP_BEH_REG,	0x00},
		{MBCUIRQ_REG,		0x18},
	},
	.num_regs = MAX_BCUCFG_REGS,
};

static struct resource gpio_resources[] = {
	{
		.name	= "GPIO",
		.start	= GPIO_IRQ,
		.end	= GPIO_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource pmic_ccsm_resources[] = {
	{
		.start = PWRSRC_IRQ,
		.end   = PWRSRC_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = BATZC_IRQ,
		.end   = BATZC_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = BATALRT_IRQ,
		.end   = BATALRT_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CTYPE_IRQ,
		.end   = CTYPE_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource adc_resources[] = {
	{
		.name  = "ADC",
		.start = ADC_IRQ,
		.end   = ADC_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource charger_resources[] = {
	{
		.name  = "CHARGER",
		.start = CHGR_IRQ,
		.end   = CHGR_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource pmic_i2c_resources[] = {
	{
		.name  = "PMIC_I2C",
		.start = PMICI2C_IRQ,
		.end   = PMICI2C_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource thermal_resources[] = {
	{
		.start = THRM0_IRQ,
		.end   = THRM0_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = THRM1_IRQ,
		.end   = THRM1_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = THRM3_IRQ,
		.end   = THRM3_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource bcu_resources[] = {
	{
		.name  = "BCU",
		.start = BCU_IRQ,
		.end   = BCU_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell whiskey_cove_dev[] = {
	{
		.name = "wcove_gpadc",
		.id = 0,
		.num_resources = ARRAY_SIZE(adc_resources),
		.resources = adc_resources,
	},
	{
		.name = "whiskey_cove_thermal",
		.id = 0,
		.num_resources = ARRAY_SIZE(thermal_resources),
		.resources = thermal_resources,
	},
	{
		.name = "wcove_ccsm",
		.id = 0,
		.num_resources = ARRAY_SIZE(pmic_ccsm_resources),
		.resources = pmic_ccsm_resources,
	},
	{
		.name = "wcove_pmic_i2c",
		.id = 0,
		.num_resources = ARRAY_SIZE(pmic_i2c_resources),
		.resources = pmic_i2c_resources,
	},
	{
		.name = "ext-charger",
		.id = 0,
		.num_resources = ARRAY_SIZE(charger_resources),
		.resources = charger_resources,
	},
	{
		.name = "wcove_bcu",
		.id = 0,
		.num_resources = ARRAY_SIZE(bcu_resources),
		.resources = bcu_resources,
	},
	{
		.name = "whiskey_cove_gpio",
		.id = 0,
		.num_resources = ARRAY_SIZE(gpio_resources),
		.resources = gpio_resources,
	},
	{
		.name = "sw_fuel_gauge",
		.id = 0,
		.num_resources = 0,
		.resources = NULL,
	},
	{
		.name = "sw_fuel_gauge_ha",
		.id = 0,
		.num_resources = 0,
		.resources = NULL,
	},
	{
		.name = "whiskey_cove_region",
	},
	{NULL, },
};

struct intel_pmic_irqregmap whiskey_cove_irqregmap[] = {
	{
		{MIRQLVL1, PWRSRC_LVL1, 1, 0},
		{IRQLVL1, PWRSRC_LVL1, 1, 0},
		INTEL_PMIC_REG_NULL,
	},
	{
		{MIRQLVL1, THRM_LVL1, 1, 0},
		{IRQLVL1, THRM_LVL1, 1, 0},
		INTEL_PMIC_REG_NULL,
	},
	{
		{MIRQLVL1, BCU_IRQ, 1, 0},
		{BCUIRQ, 0, 7, 0},
		{BCUIRQ, 0, 7, 0},
	},
	{
		{MIRQLVL1, ADC_IRQ, 1, 0},
		{IRQLVL1, ADC_IRQ, 1, 0},
		INTEL_PMIC_REG_NULL,
	},
	{
		{MIRQLVL1, CHGR_LVL1, 1, 0},
		{IRQLVL1, CHGR_LVL1, 1, 0},
		INTEL_PMIC_REG_NULL,
	},
	{
		{MIRQLVL1, GPIO_IRQ, 1, 0},
		{IRQLVL1, GPIO_IRQ, 1, 0},
		INTEL_PMIC_REG_NULL,
	},
	{
		INTEL_PMIC_REG_NULL,
		INTEL_PMIC_REG_NULL,
		INTEL_PMIC_REG_NULL,
	},
	{
		{MIRQLVL1, CRIT_IRQ, 1, 0},
		{IRQLVL1, CRIT_IRQ, 1, 0},
		INTEL_PMIC_REG_NULL,
	},
	{
		{MIRQLVL1, 0, 0x1, 0},
		{PWRSRCIRQ, 0, 0x1F, INTEL_PMIC_REG_W1C},
		{PWRSRCIRQ, 0, 0x1F, INTEL_PMIC_REG_W1C},
	},
	{ /* THERM1 IRQ */
		{MIRQLVL1, 1, 0x1, 0},
		{THRM1IRQ, 0, 0xF, INTEL_PMIC_REG_W1C},
		{THRM1IRQ, 0, 0xF, INTEL_PMIC_REG_W1C},
	},
	{ /* THERM2 */
		{MIRQLVL1, 1, 0x1, 0},
		{THRM2IRQ, 0, 0xC3, INTEL_PMIC_REG_W1C},
		{THRM2IRQ, 0, 0xC3, INTEL_PMIC_REG_W1C},
	},
	{ /* BATZONE CHANGED */
		{MIRQLVL1, 1, 0x1, 0},
		{THRM1IRQ, 7, 1, INTEL_PMIC_REG_W1C},
		{THRM1IRQ, 7, 1, INTEL_PMIC_REG_W1C},
	},
	{ /* Ext. Chrgr */
		{MIRQLVL1, 4, 0x1, 0},
		{CHGRIRQ, 0, 1, INTEL_PMIC_REG_W1C},
		{CHGRIRQ, 0, 1, INTEL_PMIC_REG_W1C},
	},
	{ /* THERM0 IRQ */
		{MIRQLVL1, 1, 0x1, 0},
		{THRM0IRQ, 0, 0xFF, INTEL_PMIC_REG_W1C},
		{THRM0IRQ, 0, 0xFF, INTEL_PMIC_REG_W1C},
	},
	{ /* External I2C Transaction */
		{MIRQLVL1, 4, 0x1, 0},
		{CHGRIRQ, 1, 7, INTEL_PMIC_REG_W1C},
		{CHGRIRQ, 1, 7, INTEL_PMIC_REG_W1C},
	},
	{ /* THERM3 */
		{MIRQLVL1, 1, 0x1, 0},
		{THRM3IRQ, 0, 0xF0, INTEL_PMIC_REG_W1C},
		{THRM3IRQ, 0, 0xF0, INTEL_PMIC_REG_W1C},
	},
	{ /* CTYP */
		{MIRQLVL1, 4, 0x1, 0},
		{CHGRIRQ, 4, 1, INTEL_PMIC_REG_W1C},
		{CHGRIRQ, 4, 1, INTEL_PMIC_REG_W1C},
	},
};

static struct pmic_gpio_data whiskey_cove_gpio_data = {
	.type = WHISKEY_COVE,
	.num_gpio = 10,
	.num_vgpio = 0x5e,
};

static void wc_set_gpio_pdata(void)
{
	intel_soc_pmic_set_pdata("whiskey_cove_gpio",
				(void *)&whiskey_cove_gpio_data,
				sizeof(whiskey_cove_gpio_data), 0);
}

static void wc_set_adc_pdata(void)
{
	static struct intel_wcove_gpadc_platform_data wc_adc_pdata;
	wc_adc_pdata.channel_num = GPADC_NUM_CHANNELS;
	wc_adc_pdata.intr_mask = MUSBID | MPEAK | MBATTEMP
		| MSYSTEMP | MBATT | MVIBATT | MGPMEAS | MCCTICK;
	wc_adc_pdata.gpadc_iio_maps = wc_iio_maps;
	wc_adc_pdata.gpadc_regmaps = whiskeycove_gpadc_regmaps;
	wc_adc_pdata.gpadc_regs = &whiskeycove_gpadc_regs;
	wc_adc_pdata.gpadc_channels = wc_adc_channels;

	intel_soc_pmic_set_pdata("wcove_gpadc", (void *)&wc_adc_pdata,
			sizeof(wc_adc_pdata), 0);
}

static void wcove_set_ccsm_config(void)
{
	static struct intel_pmic_ccsm_platform_data pdata;
	pdata.intmap = wc_intmap;
	pdata.intmap_size = ARRAY_SIZE(wc_intmap);
	pdata.reg_map = &pmic_wcove_regmap;
	pdata.max_tbl_row_cnt =
			ARRAY_SIZE(th05_lookup_tbl);
	pdata.adc_tbl = th05_lookup_tbl;
	intel_soc_pmic_set_pdata("wcove_ccsm", &pdata,
		sizeof(pdata), 0);
}

static void wcove_set_bcu_pdata(void)
{
	intel_soc_pmic_set_pdata("wcove_bcu", (void *)&wc_bcu_pdata,
			sizeof(struct wcove_bcu_platform_data), 0);
}

static int whiskey_cove_init(void)
{
	pr_info("Whiskey Cove: ID 0x%02X, VERSION 0x%02X\n",
		intel_soc_pmic_readb(CHIPID), intel_soc_pmic_readb(CHIPVER));

	wcove_set_ccsm_config();
	wcove_set_bcu_pdata();
	wc_set_adc_pdata();
	wc_set_gpio_pdata();
	wcove_init_done = true;

	return 0;
}

struct intel_soc_pmic whiskey_cove_pmic = {
	.label		= "whiskey cove",
	.irq_flags	= IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
	.init		= whiskey_cove_init,
	.cell_dev	= whiskey_cove_dev,
	.irq_regmap	= whiskey_cove_irqregmap,
	.irq_num	= WHISKEY_COVE_IRQ_NUM,
};

#define TT_I2CDADDR_ADDR		0x00
static u8 pmic_read_tt(u8 addr)
{
	int ret;

	ret = intel_soc_pmic_writeb(pmic_wcove_regmap.pmic_chrttaddr,
			addr);

	/* Delay the TT read by 2ms to ensure that the data is populated
	 * in data register
	 */
	usleep_range(2000, 3000);

	return intel_soc_pmic_readb(pmic_wcove_regmap.pmic_chrttdata);
}


static void __init register_external_charger(void)
{
	static struct i2c_board_info i2c_info;

	if (!wcove_init_done)
		return;

	strncpy(i2c_info.type, "ext-charger", I2C_NAME_SIZE);
	i2c_info.addr = pmic_read_tt(TT_I2CDADDR_ADDR);
	i2c_info.irq = INTEL_PMIC_IRQBASE + CHGR_IRQ;
	i2c_new_device(wcove_pmic_i2c_adapter, &i2c_info);
}
late_initcall(register_external_charger);

MODULE_LICENSE("GPL V2");
MODULE_AUTHOR("Yang Bin <bin.yang@intel.com");
