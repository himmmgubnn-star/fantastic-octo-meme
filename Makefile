# Cellar — a Windows compatibility layer for Linux.
#
# Targets:
#   make            build the `cellar` CLI (default)
#   make test       build and run the unit tests
#   make check      alias for `make test`
#   make sample     generate a synthetic PE (samples/hello.exe)
#   make clean      remove build artifacts
#   make libcellar  build only the static library archive
#
# Optional knobs:
#   CC      the C compiler            (default: cc)
#   CFLAGS  extra compiler flags      (default: see below)
#   CELLAR_LOG_LEVEL  compile-time logging threshold (0..4, default 2)

CC      ?= cc
BUILD   ?= build
AR      ?= ar

# Note: -Wpedantic is intentionally omitted. The Win32 export registry stores
# function pointers as `void *` (a dlsym-style design), which pedantic mode
# flags; the conversion is deliberate and portable in practice.
WARN    = -Wall -Wextra -Wconversion -Wno-sign-conversion
OPT     = -O2
STD     = -std=c11
INC     = -Iinclude
LOG     = -DCELLAR_LOG_LEVEL=$(if $(CELLAR_LOG_LEVEL),$(CELLAR_LOG_LEVEL),2)

CFLAGS  ?= $(STD) $(INC) $(LOG) $(WARN) $(OPT)
CPPFLAGS=

LIB_SRCS = \
	src/loader/cellar_util.c \
	src/loader/pe.c \
	src/loader/loader.c \
	src/win32/api.c \
	src/win32/mod_kernel32.c \
	src/win32/init.c

LIB_OBJS = $(patsubst src/%.c,$(BUILD)/obj/%.o,$(LIB_SRCS))
TEST_SRCS = tests/test_loader.c
TEST_OBJS = $(patsubst tests/%.c,$(BUILD)/obj/%.o,$(TEST_SRCS))
GEN       = $(BUILD)/gen_sample_pe
SAMPLE    = samples/hello.exe

LIB  = $(BUILD)/libcellar.a
CLI  = $(BUILD)/cellar
TEST = $(BUILD)/test_loader

all: $(CLI)

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

$(CLI): src/cli.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) -o $@

$(TEST): $(TEST_OBJS) $(LIB)
	$(CC) $(CFLAGS) $(TEST_OBJS) $(LIB) -o $@

$(GEN): tools/gen_sample_pe.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tools/gen_sample_pe.c -o $@

$(SAMPLE): $(GEN)
	@mkdir -p $(dir $@)
	./$(GEN) $(SAMPLE)

sample: $(SAMPLE)

$(BUILD)/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

libcellar: $(LIB)

test: $(TEST)
	./$(TEST)

check: test

clean:
	rm -rf $(BUILD)

.PHONY: all libcellar test check sample clean
