# CFW - portable GNU Makefile for the public export.
#
#   make              build libcfw.a (every include/**/*.c except the test harness)
#   make test         build every tests/**/test_*.c against libcfw.a and run them
#   make test-oom     Linux only: the allocation-failure harness (needs -Wl,--wrap)
#   make test-unchecked  every tests/**/test_unchecked.c, built WITHOUT ERROR_CHECK_ENABLED
#                     against its own archive - the inert-fallback half of a module's
#                     contract, which no checked build can observe
#   make check        the family drift gates under tools/ (a hand-cloned or generated
#                     family's members still match their canonical file); needs python3,
#                     skipped with a note without it. `make test` runs it first.
#   make clean
#
# Requires a C23 compiler (gcc 14+ / clang 18+) and, as SYSTEM packages:
#   cglm 0.9.6             Debian: apt install libcglm-dev (Debian 13; Ubuntu 24.04's 0.9.2 is too old) MSYS2: pacman -S mingw-w64-ucrt-x86_64-cglm
#   PCRE2 (8-bit)          Debian: apt install libpcre2-dev           MSYS2: pacman -S mingw-w64-ucrt-x86_64-pcre2
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
LDLIBS   := -lcglm -lpcre2-8 -lm
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
TEST_SRC := $(filter-out %/test_oom.c %/test_unchecked.c,$(TEST_SRC))
# The platform/windows suite exercises Winsock and the Win32 clock directly; the module's own
# body is #ifdef _WIN32, so on any other OS there is nothing to test and the suite is skipped.
ifneq ($(OS),Windows_NT)
    TEST_SRC := $(filter-out tests/platform/windows/%,$(TEST_SRC))
endif
TEST_BIN := $(TEST_SRC:.c=$(EXE))
OOM_SRC  := $(filter %/test_oom.c,$(call rwildcard,tests,*.c))
OOM_BIN  := $(OOM_SRC:.c=$(EXE))
OOM_WRAP ?=
# The unchecked suites pin what a module does with ERROR_CHECK_ENABLED compiled OUT - the
# inert fallbacks a checked build can only abort on. They need the library built the same
# way, so they get their own objects (a separate suffix: an object compiled under other
# defines must never be reused) and their own archive; neither is part of `all`.
CPPFLAGS_UNCHECKED := $(filter-out -DERROR_CHECK_ENABLED,$(CPPFLAGS))
LIB_OBJ_UNCHECKED  := $(LIB_SRC:.c=.unchecked.o)
UNCHECKED_SRC      := $(filter %/test_unchecked.c,$(call rwildcard,tests,*.c))
UNCHECKED_BIN      := $(UNCHECKED_SRC:.c=$(EXE))

all: libcfw.a

libcfw.a: $(LIB_OBJ)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Each suite is one binary: its test_*.c, the harness, and the archive. The math suites carry
# their own reporter (tests/math/check.h) and simply do not call into the harness.
$(TEST_BIN): %$(EXE): %.c $(HARNESS) libcfw.a
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(HARNESS) libcfw.a $(LDLIBS)

# A suite that exits non-zero without printing a failure (a crash after its summary, a
# timing-dependent exit) would otherwise leave the log with nothing to name: say which
# binary and what code, at the point it happens and again at the end.
# A family module (container/arrayList, container/map) is many near-identical files
# whose review stamps rest on a measured equivalence to one canonical file. The gate
# that measures it ships under tools/ with the family, so the public tree can prove
# the same thing the private one does. No gate in this export means nothing to run.
GATES := $(wildcard tools/*_divergence.py)

check:
	@if [ -z "$(GATES)" ]; then echo "check: no family gates in this export"; exit 0; fi; \
	if ! command -v python3 >/dev/null 2>&1; then echo "check: python3 not found - family gates skipped"; exit 0; fi; \
	status=0; \
	for g in $(GATES); do echo "== $$g"; python3 $$g || status=1; done; \
	exit $$status

test: $(TEST_BIN) check
	@status=0; failed=""; \
	for t in $(TEST_BIN); do \
	    echo "== $$t"; ./$$t; rc=$$?; \
	    if [ $$rc -ne 0 ]; then echo "!! $$t exited with $$rc"; failed="$$failed $$t"; status=1; fi; \
	done; \
	if [ -n "$$failed" ]; then echo "FAILED SUITES:$$failed"; fi; exit $$status

# The allocation-failure sweep wraps calloc/realloc/free at link time (GNU ld only) and forks a
# child to observe memory_alloc's abort-on-OOM contract, so it is its own target. Each binary
# below gets exactly the --wrap flags its own test_oom.c defines a __wrap_* body for - a fixed
# set for every binary left --wrap=realloc demanding a __wrap_realloc that a calloc/free-only
# harness (e.g. container/str's) never defines, an undefined-reference link failure.
$(subst .c,$(EXE),tests/container/map/test_oom.c): OOM_WRAP := -Wl,--wrap=calloc -Wl,--wrap=free
$(subst .c,$(EXE),tests/container/str/test_oom.c): OOM_WRAP := -Wl,--wrap=calloc -Wl,--wrap=free
$(subst .c,$(EXE),tests/dir/test_oom.c): OOM_WRAP := -Wl,--wrap=calloc
$(subst .c,$(EXE),tests/env/test_oom.c): OOM_WRAP := -Wl,--wrap=calloc -Wl,--wrap=free
$(subst .c,$(EXE),tests/memory/test_oom.c): OOM_WRAP := -Wl,--wrap=calloc -Wl,--wrap=realloc
$(OOM_BIN): %$(EXE): %.c $(HARNESS) libcfw.a
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(HARNESS) libcfw.a $(OOM_WRAP) $(LDLIBS)

test-oom: $(OOM_BIN)
	@status=0; failed=""; \
	for t in $(OOM_BIN); do \
	    echo "== $$t"; ./$$t; rc=$$?; \
	    if [ $$rc -ne 0 ]; then echo "!! $$t exited with $$rc"; failed="$$failed $$t"; status=1; fi; \
	done; \
	if [ -n "$$failed" ]; then echo "FAILED SUITES:$$failed"; fi; exit $$status

libcfw_unchecked.a: $(LIB_OBJ_UNCHECKED)
	$(AR) rcs $@ $^

%.unchecked.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS_UNCHECKED) -c $< -o $@

$(UNCHECKED_BIN): %$(EXE): %.c $(HARNESS) libcfw_unchecked.a
	$(CC) $(CFLAGS) $(CPPFLAGS_UNCHECKED) -o $@ $< $(HARNESS) libcfw_unchecked.a $(LDLIBS)

test-unchecked: $(UNCHECKED_BIN)
	@status=0; failed=""; \
	for t in $(UNCHECKED_BIN); do \
	    echo "== $$t"; ./$$t; rc=$$?; \
	    if [ $$rc -ne 0 ]; then echo "!! $$t exited with $$rc"; failed="$$failed $$t"; status=1; fi; \
	done; \
	if [ -n "$$failed" ]; then echo "FAILED SUITES:$$failed"; fi; exit $$status

clean:
	-rm -f libcfw.a libcfw_unchecked.a $(LIB_OBJ) $(LIB_OBJ_UNCHECKED) $(TEST_BIN) $(OOM_BIN) $(UNCHECKED_BIN)

.PHONY: all check test test-oom test-unchecked clean
