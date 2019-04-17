# SPDX-License-Idenfitier: GPL-2.0
MODS  := open
MODS  += read
MODS  += write
MODS  += readv
MODS  += writev
MODS  += scull
MODS  += sleepy
# ldd bus based drivers
MODS  += ldd
MODS  += sculld
obj-m += $(patsubst %,%.o,$(MODS))
TESTS := $(patsubst %,%_test,$(MODS))
KDIR  ?= /lib/modules/$(shell uname -r)/build
all default: modules
install: modules_install
modules modules_install help:
	$(MAKE) -C $(KDIR) M=$(shell pwd) $@
.PHONY: clean load unload reload
clean: clean_tests
	$(MAKE) -C $(KDIR) M=$(shell pwd) $@
load:
	$(info loading modules...)
	@for mod in $(MODS); do insmod ./$${mod}.ko; done
unload:
	$(info unloading modules...)
	@# remove ldd.ko last
	@-for mod in $(filter-out ldd,$(MODS)); do rmmod ./$$mod.ko; done
	@-rmmod ./ldd.ko
reload: unload load
# selftest based unit tests under tests directory.
.PHONY: test run_tests clean_tests
test: modules reload run_tests
run_tests:
	$(MAKE) -C tests top_srcdir=$(KDIR) OUTPUT=$(shell pwd)/tests $@
clean_tests:
	$(MAKE) -C tests top_srcdir=$(KDIR) OUTPUT=$(shell pwd)/tests clean
$(TESTS): modules reload
	$(MAKE) -C tests $@
