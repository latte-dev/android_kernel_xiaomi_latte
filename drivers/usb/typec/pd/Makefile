#
# Makefile for USBC PD policy and System policy
#

pd_policy-y	:= devpolicy_mgr.o
pd_policy-y	+= protocol.o
pd_policy-y	+= policy_engine.o
pd_policy-y	+= vdm_process.o

obj-$(CONFIG_USBC_PD_POLICY)		+= pd_policy.o
obj-$(CONFIG_USBC_SYSTEM_POLICY)	+= system_policy.o
