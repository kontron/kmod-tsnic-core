#
# Makefile for TSN-NIC wrapper for the single PCIe resource
#

obj-m += tsnic-core.o
pci-wrapper-objs := tsnic-core.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

all: modules

clean: modules

clean modules:
	$(MAKE) -C $(KERNEL_SRC) M=$$PWD $@
