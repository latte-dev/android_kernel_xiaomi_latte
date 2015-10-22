ccflags-y += -DATOMISP_POSTFIX=\"css2400b0_v21\" -DISP2400B0
ISP2400B0 := y
postfix := css2400b0_v21

ifeq ($(CONFIG_PLATFORM_BTNS),y)
ccflags-y += -DATOMISP_FWNAME=\"shisp_merr.bin\"
endif

include $(srctree)/$(src)/../Makefile.common
