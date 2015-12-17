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

ifdef CONFIG_GMIN_INTEL_MID
obj-$(CONFIG_VIDEO_ATOMISP) += css2400b0_v21_build/
obj-$(CONFIG_VIDEO_ATOMISP) += css2401a0_v21_build/
else
ifeq ($(CONFIG_64BIT), )
	obj-$(CONFIG_VIDEO_ATOMISP) += css2300_build/
endif
obj-$(CONFIG_VIDEO_ATOMISP) += css2400b0_v21_build/
obj-$(CONFIG_VIDEO_ATOMISP) += css2401a0_legacy_v21_build/
obj-$(CONFIG_VIDEO_ATOMISP) += css2401a0_v21_build/
endif
