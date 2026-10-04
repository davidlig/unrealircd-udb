SHELL := /bin/sh
ROOT := $(abspath ..)
UNREALIRCD_SOURCE ?= $(abspath $(ROOT)/../../../..)
CMOCKA_PREFIX ?= $(ROOT)/tests/.build/cmocka
PROFILE ?= normal
CC ?= cc
OUT := $(ROOT)/tests/.build/$(PROFILE)

# Keep the daemon's configured headers/types and compiler dialect. Do not build
# the production module from this Makefile or modify its source composition.
INCLUDES := -I$(ROOT) -I$(ROOT)/tests/support/c -I$(UNREALIRCD_SOURCE)/include \
    -I$(UNREALIRCD_SOURCE)/extras/jansson/include -I$(UNREALIRCD_SOURCE)/extras/argon2/include \
    -I$(CMOCKA_PREFIX)/include
FLAGS := -std=gnu17 -funsigned-char -fno-strict-aliasing -g -O1 \
    -ffunction-sections -fdata-sections -Wno-format-truncation
LINK := -Wl,--gc-sections -Wl,--wrap=safe_alloc -Wl,--wrap=our_strdup \
    -L$(CMOCKA_PREFIX)/lib -Wl,-rpath,$(CMOCKA_PREFIX)/lib -lcmocka -lcrypto -lpcre2-8
ifeq ($(PROFILE),asan)
FLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LINK += -fsanitize=address,undefined
else ifneq ($(PROFILE),normal)
$(error PROFILE must be normal or asan)
endif
DAEMON := $(UNREALIRCD_SOURCE)/src/match.c $(UNREALIRCD_SOURCE)/src/support.c
SOURCES := $(wildcard $(ROOT)/tests/unit/test_*.c)
BINARIES := $(patsubst $(ROOT)/tests/unit/%.c,$(OUT)/%,$(SOURCES))

.PHONY: all
all: $(BINARIES)

$(OUT)/%: $(ROOT)/tests/unit/%.c $(wildcard $(ROOT)/src/*) $(wildcard $(ROOT)/tests/support/c/*) $(DAEMON) Makefile
	@mkdir -p $(OUT)
	$(CC) $(INCLUDES) $(FLAGS) $< $(ROOT)/tests/support/c/host.c $(DAEMON) $(LINK) -o $@
