# CFW - portable GNU Makefile for the public export.
#
#   make              build libcfw.a (every include/**/*.c except the test harness)
#   make test         build every tests/**/test_*.c against libcfw.a and run them
#   make test-oom     Linux only: the allocation-failure harness (needs -Wl,--wrap)
#   make clean
#
# Requires a C23 compiler (gcc 14+ / clang 18+) and, as SYSTEM packages:
#   cglm 0.9.6             Debian/Ubuntu: apt install libcglm-dev        MSYS2: pacman -S mingw-w64-ucrt-x86_64-cglm
# Override CC/CFLAGS as usual (e.g. `make CC=clang`). On Windows run under MSYS2 or Git Bash:
# the `test` and `clean` recipes are POSIX-shell loops.

ifeq ($(origin CC),default)
    ifeq ($(OS),Windows_NT)
        CC := gcc
    endif
endif
CSTD     ?= -std=c23
CFLAGS   ?= -O2 -Wall $(CSTD)
# _GNU_SOURCE exposes POSIX (spawn/poll/pthread_rwlock) under strict -std=c23 on glibc; the
# -D switches are the feature macros the modules were audited under.
CPPFLAGS := -Iinclude -D_GNU_SOURCE -DARENA_IMPLEMENTATION -DERROR_CHECK_ENABLED \
            -DLOG_THREAD_IMPLEMENTATION -DMEMORY_HOOKS_IMPLEMENTATION \
            -DMEMORY_NON_DANGLING_POINTER -DTRACELOG_ENABLED
# The per-module system libraries on the line below are computed from the actual export set -
# see SYSTEM_DEPS in the generator - so a module this build doesn't carry never contributes a
# dead flag, and one it does carry can never be silently missing.
LDLIBS   := -lcglm -lm
ifeq ($(OS),Windows_NT)
    # CFW's windows.h asserts the Windows 10 API baseline it was audited against.
    CPPFLAGS += -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00
    # ws2_32 is part of the toolchain, not an installable package, so it is not in SYSTEM_DEPS:
    # platform/windows wraps Winsock (socket/closesocket/inet_ntop/WSACleanup) and its suite
    # calls them directly.
    LDLIBS   += -lws2_32
    EXE      := .exe
else
    LDLIBS += -lpthread
    EXE    :=
endif
AR       ?= ar

rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

HARNESS  := include/test/test.c
LIB_SRC  := $(filter-out $(HARNESS),$(call rwildcard,include,*.c))
LIB_OBJ  := $(LIB_SRC:.c=.o)
TEST_SRC := $(foreach f,$(call rwildcard,tests,*.c),$(if $(filter test_%,$(notdir $f)),$f))
TEST_SRC := $(filter-out %/test_oom.c,$(TEST_SRC))
# The platform/windows suite exercises Winsock and the Win32 clock directly; the module's own
# body is #ifdef _WIN32, so on any other OS there is nothing to test and the suite is skipped.
ifneq ($(OS),Windows_NT)
    TEST_SRC := $(filter-out tests/platform/windows/%,$(TEST_SRC))
endif
TEST_BIN := $(TEST_SRC:.c=$(EXE))
OOM_SRC  := $(filter %/test_oom.c,$(call rwildcard,tests,*.c))
OOM_BIN  := $(OOM_SRC:.c=$(EXE))
OOM_WRAP ?=

all: libcfw.a

libcfw.a: $(LIB_OBJ)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Each suite is one binary: its test_*.c, the harness, and the archive. The math suites carry
# their own reporter (tests/math/check.h) and simply do not call into the harness.
$(TEST_BIN): %$(EXE): %.c $(HARNESS) libcfw.a
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(HARNESS) libcfw.a $(LDLIBS)

test: $(TEST_BIN)
	@status=0; for t in $(TEST_BIN); do echo "== $$t"; ./$$t || status=1; done; exit $$status

# The allocation-failure sweep wraps calloc/realloc/free at link time (GNU ld only) and forks a
# child to observe memory_alloc's abort-on-OOM contract, so it is its own target. Each binary
# below gets exactly the --wrap flags its own test_oom.c defines a __wrap_* body for - a fixed
# set for every binary left --wrap=realloc demanding a __wrap_realloc that a calloc/free-only
# harness (e.g. container/str's) never defines, an undefined-reference link failure.
$(subst .c,$(EXE),tests/container/str/test_oom.c): OOM_WRAP := -Wl,--wrap=calloc -Wl,--wrap=free
$(subst .c,$(EXE),tests/dir/test_oom.c): OOM_WRAP := -Wl,--wrap=calloc
$(subst .c,$(EXE),tests/memory/test_oom.c): OOM_WRAP := -Wl,--wrap=calloc -Wl,--wrap=realloc
$(OOM_BIN): %$(EXE): %.c $(HARNESS) libcfw.a
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(HARNESS) libcfw.a $(OOM_WRAP) $(LDLIBS)

test-oom: $(OOM_BIN)
	@status=0; for t in $(OOM_BIN); do echo "== $$t"; ./$$t || status=1; done; exit $$status

clean:
	-rm -f libcfw.a $(LIB_OBJ) $(TEST_BIN) $(OOM_BIN)

.PHONY: all test test-oom clean
