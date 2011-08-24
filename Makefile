#
# Makefile for the vtunerc device driver
#

vtunerc-objs = vtunerc_main.o vtunerc_ctrldev.o vtunerc_proxyfe.o

CONFIG_DVB_VTUNERC ?= m

obj-$(CONFIG_DVB_VTUNERC) += vtunerc.o

ccflags-y += -Idrivers/media/dvb/dvb-core
ccflags-y += -Idrivers/media/dvb/frontends
ccflags-y += -Idrivers/media/common/tuners
ccflags-y += -Iinclude

#
# for external compilation
#

#KDIR ?= /usr/src/`uname -r`
ifeq ($(origin KDIR), undefined)
	KDIR = /usr/src/$(shell uname -r)
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
