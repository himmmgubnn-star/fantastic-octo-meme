# Cellar — a Windows compatibility layer for Linux.
#
# Targets:
#   make            build the `cellar` CLI (default)
#   make test       build and run the unit tests
#   make check      alias for `make test`
#   make sample     generate a synthetic PE (samples/hello.exe)
#   make fuzz       run a short loader fuzz campaign
#   make clean      remove build artifacts
#   make libcellar  build only the static library archive
#   make ALSA=1     also build the optional ALSA audio backend (needs libasound)
#
# Optional knobs:
#   CC      the C compiler            (default: cc)
#   CFLAGS  extra compiler flags      (default: see below)
#   ALSA=1  enable the real ALSA audio backend
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
	src/win32/mod_winmm.c \
	src/win32/mod_user32.c \
	src/win32/mod_advapi32.c \
	src/win32/mod_shell32.c \
	src/win32/mod_ole32.c \
	src/win32/mod_comdlg32.c \
	src/win32/mod_gdi32.c \
	src/win32/mod_ws2_32.c \
	src/win32/mod_version.c \
	src/win32/mod_ntdll.c \
	src/win32/init.c \
	src/port/posix.c \
	src/audio/audio.c \
	src/perf/perf.c \
	src/compat/compat.c \
	src/compat/profile.c \
	src/timer/timer.c \
	src/sync/sync.c \
	src/shmem/shmem.c \
	src/trace/trace.c \
	src/gfx/shadercache.c \
	src/plugin/plugin.c \
	src/crash/crash.c \
	src/inspect/inspect.c \
	src/db/db.c \
	src/runtime/runtime.c \
	src/shell/shell.c \
	src/prefix/prefix.c \
	src/display/display.c \
	src/security/security.c \
	src/desktop/desktop.c \
	src/installer/installer.c \
	src/com/com.c \
	src/service/service.c \
	src/notify/notify.c \
	src/a11y/a11y.c \
	src/locale/locale.c \
	src/print/print.c \
	src/device/device.c \
	src/debug/debug.c \
	src/testlab/testlab.c \
	src/workspace/workspace.c

# Optional ALSA backend for real audio on desktop Linux:
#   make ALSA=1            (requires libasound2-dev)
LIB_SRCS += $(if $(ALSA),src/audio/alsa.c)
CFLAGS   += $(if $(ALSA),-DCELLAR_AUDIO_ALSA)
LDLIBS   := $(if $(ALSA),-lasound)

# dlopen (plugin loader) is in glibc>=2.34; add -ldl for older libcs.
LDLIBS   += -ldl

LIB_OBJS = $(patsubst src/%.c,$(BUILD)/obj/%.o,$(LIB_SRCS))
TEST_BINS = test_loader test_audio test_perf test_compat test_kit test_ecosystem test_workspace
GEN       = $(BUILD)/gen_sample_pe
FUZZ      = $(BUILD)/fuzz_loader
SAMPLE    = samples/hello.exe

LIB  = $(BUILD)/libcellar.a
CLI  = $(BUILD)/cellar

all: $(CLI)

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

$(CLI): src/cli.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(BUILD)/test_loader: tests/test_loader.c $(LIB)
	$(CC) $(CFLAGS) tests/test_loader.c $(LIB) $(LDLIBS) -o $@
$(BUILD)/test_audio: tests/test_audio.c $(LIB)
	$(CC) $(CFLAGS) tests/test_audio.c $(LIB) $(LDLIBS) -o $@
$(BUILD)/test_perf: tests/test_perf.c $(LIB)
	$(CC) $(CFLAGS) tests/test_perf.c $(LIB) $(LDLIBS) -o $@
$(BUILD)/test_compat: tests/test_compat.c $(LIB)
	$(CC) $(CFLAGS) tests/test_compat.c $(LIB) $(LDLIBS) -o $@
$(BUILD)/test_kit: tests/test_kit.c $(LIB)
	$(CC) $(CFLAGS) tests/test_kit.c $(LIB) $(LDLIBS) -o $@
$(BUILD)/test_ecosystem: tests/test_ecosystem.c $(LIB)
	$(CC) $(CFLAGS) tests/test_ecosystem.c $(LIB) $(LDLIBS) -o $@
$(BUILD)/test_workspace: tests/test_workspace.c $(LIB)
	$(CC) $(CFLAGS) tests/test_workspace.c $(LIB) $(LDLIBS) -o $@

$(GEN): tools/gen_sample_pe.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) tools/gen_sample_pe.c -o $@

$(FUZZ): tools/fuzz_loader.c $(LIB)
	$(CC) $(CFLAGS) tools/fuzz_loader.c $(LIB) $(LDLIBS) -o $@

$(SAMPLE): $(GEN)
	@mkdir -p $(dir $@)
	./$(GEN) $(SAMPLE)

sample: $(SAMPLE)

fuzz: $(SAMPLE) $(FUZZ)
	./$(FUZZ) $(SAMPLE) 3000 42

$(BUILD)/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

libcellar: $(LIB)

TESTS := $(addprefix $(BUILD)/,$(TEST_BINS))

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

check: test

clean:
	rm -rf $(BUILD)

.PHONY: all libcellar test check sample fuzz clean
