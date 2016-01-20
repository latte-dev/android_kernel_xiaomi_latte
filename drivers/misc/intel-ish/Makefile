#
# Makefile - Intel HECI and ISS implementation
# Copyright (c) 2010-2015, Intel Corporation.
#
obj-$(CONFIG_INTEL_ISH) += heci.o
heci-objs := init.o
heci-objs += hbm.o
heci-objs += client.o
heci-objs += heci-api.o
heci-objs += bus.o
heci-$(CONFIG_DEBUG_FS) += debugfs.o

obj-$(CONFIG_INTEL_ISH) += heci-ish.o
heci-ish-objs := hw-ish.o
heci-ish-objs += pci-ish.o

obj-$(CONFIG_INTEL_ISH) += hid-heci-ish.o
hid-heci-ish-objs := heci-hid.o
hid-heci-ish-objs += heci-hid-client.o

obj-$(CONFIG_INTEL_ISH) += ish-hid-drv.o
ish-hid-drv-objs := ish-hid-dd.o

obj-$(CONFIG_INTEL_ISH) += sens-col-core.o
sens-col-core-objs := senscol-core.o

