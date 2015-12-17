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

obj-$(CONFIG_VIDEO_IMXVB) += imx1x5.o

# Select the correct sensor objects.
imx1x5-objs-$(CONFIG_VIDEO_IMXVB)	+= dw9719.o

imx1x5-objs	:= imx.o $(imx1x5-objs-m)

ccflags-y += -Werror
