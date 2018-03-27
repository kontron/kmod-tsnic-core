#
# Makefile for TSN-NIC wrapper for the single PCIe resource
#

obj-m += tsnic-core.o
tsnic-core-objs := tsnic-core-pci.o tsnic-core-i2c.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

all: modules

clean modules modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$$PWD $@
