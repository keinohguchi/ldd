# SPDX-License-Identifier: GPL-2.0
KDIR           ?= /lib/modules/$(shell uname -r)/build
CFLAGS         += -Wall
CFLAGS         += -O2
CFLAGS         += -D_GNU_SOURCE
CFLAGS         += -I$(KDIR)/tools/testing/selftests
LDLIBS         += -lpthread
TEST_FILES     := $(sort $(wildcard *_test.c))
TESTS          ?= $(patsubst %.c,%,$(TEST_FILES))
TEST_GEN_PROGS := $(TESTS)
.PHONY: test $(TESTS)
test: $(TESTS)
$(TESTS):
	@$(CC) $(CFLAGS) -o $@ $@.c $(LDLIBS)
	@echo ldd/tests/$@
	@echo ========================================
	@if ./$@; then echo "ok $@ [PASS]"; else echo "not ok $@ [FAIL]"; exit 1; fi
include $(KDIR)/tools/testing/selftests/lib.mk
