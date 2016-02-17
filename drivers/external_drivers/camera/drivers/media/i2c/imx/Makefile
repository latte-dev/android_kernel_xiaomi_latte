#
#  Copyright (c) 2010 - 2015, Intel Corporation.
#
#  This program is free software; you can redistribute it and/or modify it
#  under the terms and conditions of the GNU General Public License,
#  version 2, as published by the Free Software Foundation.
#
#  This program is distributed in the hope it will be useful, but WITHOUT
#  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
#  more details.
#

obj-$(CONFIG_VIDEO_IMX) += imx1x5.o

imx1x5-objs := imx.o drv201.o ad5816g.o dw9714.o dw9719.o dw9718.o vcm.o otp.o otp_imx.o otp_brcc064_e2prom.o otp_e2prom.o

ov8858_driver-objs := ../ov8858.o dw9718.o dw9714.o vcm.o
obj-$(CONFIG_VIDEO_OV8858)     += ov8858_driver.o

ccflags-y += -Werror
