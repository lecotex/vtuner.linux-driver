#
# Makefile for the vtunerc device driver
#

VTUNERC_MAX_ADAPTERS ?= 4

vtunerc-objs = vtunerc_main.o vtunerc_ctrldev.o vtunerc_proxyfe.o

CONFIG_DVB_VTUNERC ?= m

obj-$(CONFIG_DVB_VTUNERC) += vtunerc.o

ccflags-y += -Idrivers/media/dvb/dvb-core
ccflags-y += -Idrivers/media/dvb/frontends
ccflags-y += -Idrivers/media/common/tuners
ccflags-y += -Iinclude
ccflags-y += -DVTUNERC_MAX_ADAPTERS=$(VTUNERC_MAX_ADAPTERS)

#
# for external compilation
#

#KDIR ?= /usr/src/`uname -r`
ifeq ($(origin KDIR), undefined)
	KDIR = /usr/src/linux-$(shell uname -r)
	ifeq "$(wildcard $(KDIR) )" ""
		KDIR = /usr/src/$(shell uname -r)
	endif
endif

PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	rm -f *.o
	rm -f *.ko
	rm -f *.mod.c
	rm -f .*.cmd
	rm -f *~
