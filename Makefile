# SPDX-License-Idenfitier: GPL-2.0
obj-m += ldd.o
ldd-objs := main.o
KERNDIR ?= /lib/modules/$(shell uname -r)/build
all default: modules
install: modules_install
modules modules_install help clean:
	$(MAKE) -C $(KERNDIR) M=$(shell pwd) $@
# kernel selftest based unittest
run_tests clean_tests:
	$(MAKE) top_srcdir=$(KERNDIR) OUTPUT=$(shell pwd)/tests	\
		CFLAGS="-I$(KERNDIR)/tools/testing/selftests"	\
		-C ./tests/ $@
