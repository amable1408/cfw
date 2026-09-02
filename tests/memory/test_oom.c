#include <allocator/allocator.h>
#include <arena/arena.h>
#include <arena/arena_linear.h>
#include <arena/arena_pool.h>
#include <arena/arena_stack.h>
#include <memory/memory.h>
#include <test/test.h>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

/* Allocation-failure sweep for memory/memory.c, allocator/allocator.c, and the three arena
 * strategies (arena_linear.c, arena_stack.c, arena_pool.c) plus the generic arena.c facade.
 *
 * LINUX-ONLY, --wrap-DRIVEN. This file only builds and links with -Wl,--wrap=calloc
 * -Wl,--wrap=realloc, which is not part of any per-server build-linux makefile - it is a
 * standalone fault-injection harness invoked directly with gcc, mirroring tests/dir/test_oom.c and
 * tests/benchmark/test_oom.c. It must be built WITHOUT ARENA's own suites (tests/arena is
 * mid-pin-flip) but WITH ERROR_CHECK_ENABLED, because the abort-on-OOM contract of
 * memory_alloc is exactly what case 2 below pins - unlike the dir/benchmark harnesses, this one
 * needs the aborting path to actually fire (in a forked child) rather than staying disabled.
 *
 * THE INJECTOR is deliberately single-shot rather than ordinal-counted like the dir/benchmark
 * harnesses: every call site under test here makes exactly ONE calloc (or, for the realloc
 * case, one realloc) on the path being probed, so "fail the very next call of this kind" is
 * unambiguous and does not need a size- or ordinal-based match. Each case still records whether
 * the arm was actually consumed (`_fired_*`), so a case that never reaches its allocation site
 * is reported as a harness failure rather than a silent false pass - the same vacuity guard the
 * dir/benchmark files use, just against a one-shot arm instead of a running count.
 *
 * DEATH TESTING (case 2) uses fork(), not execv of a second binary: the wrapped calloc symbol
 * only exists inside THIS process image, and fork() gives an isolated copy of it for free - the
 * child arms its own copy of the flag, aborts, and the parent inspects its exit status. Nothing
 * else in this file forks, so there is no cross-talk between the child's armed state and the
 * parent's.
 *
 * CASE 3b is the regression pin for a since-fixed "split OOM personality": memory_realloc's
 * data == nullptr branch used to call the ABORTING memory_alloc, so memory_realloc(nullptr, 0, n)
 * recovered or killed the process depending only on whether data happened to be null - the same
 * call, two different contracts. It now forwards to memory_try_alloc, so this case asserts the
 * SAME nullptr-not-abort behavior as case 1, through the other entry point. */

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

#define _TEST_ANCHOR_BYTES 64

/*==============================================================================
 * MARK: - Injector
 *============================================================================*/

void *__real_calloc(USize const count, USize const size);
void *__real_realloc(void *const pointer, USize const size);

static bool _armed_calloc = false;
static bool _fired_calloc = false;
static bool _armed_realloc = false;
static bool _fired_realloc = false;

/* Only the NAME is fixed by -Wl,--wrap=calloc; the parameters keep the project's const style. */
void *__wrap_calloc(USize const count, USize const size) {
    if (_armed_calloc) {
        _armed_calloc = false;
        _fired_calloc = true;

        return nullptr;
    }

    return __real_calloc(count, size);
}

void *__wrap_realloc(void *const pointer, USize const size) {
    if (_armed_realloc) {
        _armed_realloc = false;
        _fired_realloc = true;

        return nullptr;
    }

    return __real_realloc(pointer, size);
}

static void _arm_calloc(void) {
    _armed_calloc = true;
    _fired_calloc = false;
}

static void _arm_realloc(void) {
    _armed_realloc = true;
    _fired_realloc = false;
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

/* 1. memory_try_alloc under a failing calloc: nullptr, no abort, no crash. */
static void _test_memory_try_alloc_returns_null_on_failed_calloc(Test *const test) {
    test_case_begin(test, "memory_try_alloc returns nullptr on a failed calloc, without aborting");

    /* Anchor: the harness itself must produce a real, successful allocation first - an
     * all-refusals run would make every negative assertion below vacuous. */
    void *const anchor = memory_try_alloc(_TEST_ANCHOR_BYTES);

    test_expect_true(test, "the anchor allocation succeeded", !memory_empty(anchor));

    memory_free(anchor);

    _arm_calloc();

    void *const starved = memory_try_alloc(_TEST_ANCHOR_BYTES);

    test_expect_true(test, "the injection actually fired", _fired_calloc);
    test_expect_true(test, "a failed calloc yields nullptr, not a crash", memory_empty(starved));

    /* A second, unarmed call proves the process is still alive and the allocator still works -
     * the one property a crash would have made impossible to observe. */
    void *const recovered = memory_try_alloc(_TEST_ANCHOR_BYTES);

    test_expect_true(test, "the allocator still works after a starved call", !memory_empty(recovered));

    memory_free(recovered);

    test_case_end(test);
}

/* 2. memory_alloc under a failing calloc: ABORTS via error_check_null. Verified as a real
 * process death, in a forked child, not merely argued from reading error_check_null's contract. */
static void _test_memory_alloc_aborts_on_failed_calloc(Test *const test) {
    test_case_begin(test, "memory_alloc aborts on a failed calloc (checked build contract)");

    /* Anchor in the PARENT: an ordinary, unarmed memory_alloc must still succeed, so the death
     * below is attributable to the injected failure and not to memory_alloc being broken outright. */
    void *const anchor = memory_alloc(_TEST_ANCHOR_BYTES);

    test_expect_true(test, "the anchor allocation succeeded", !memory_empty(anchor));

    memory_free(anchor);

    /* Flushed before forking so the parent's still-buffered progress output (this framework does
     * not flush per line) is not duplicated into the child's copy of the same FILE buffer - the
     * child's own log_init(autoflush=true) messages would otherwise flush that inherited,
     * unrelated backlog together with its own TRACE/ERROR lines, printing every earlier case a
     * second time. Cosmetic only - it never changed an assertion count - but worth keeping the
     * transcript honest. */
    fflush(nullptr);

    pid_t const pid = fork();

    test_expect_true(test, "fork succeeded", pid >= 0);

    if (pid == 0) {
        /* Child: arm its own copy of the flag (fork gives a private copy of all statics) and
         * call the aborting path directly. This line must never return. */
        _arm_calloc();

        void *const doomed = memory_alloc(_TEST_ANCHOR_BYTES);

        (void) doomed;

        _exit(111); /* Reached only if memory_alloc failed to abort - a real defect, not a harness slip. */
    }

    if (pid > 0) {
        int status = 0;

        waitpid(pid, &status, 0);

        test_expect_true(test, "the child died from a signal rather than exiting normally", WIFSIGNALED(status));
        test_expect_true(test, "the child died specifically from SIGABRT",
            WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    }

    test_case_end(test);
}

/* 3. memory_realloc growth under a failing realloc: nullptr, and the ORIGINAL block is intact
 * and readable - checked by writing a pattern first and re-reading it after the failure. */
static void _test_memory_realloc_growth_survives_a_failed_realloc(Test *const test) {
    test_case_begin(test, "memory_realloc returns nullptr on a failed growth and leaves the original block intact");

    /* Anchor: an ordinary, unarmed growth first, on its OWN block - proves the growth path
     * itself works (old bytes preserved, new bytes zeroed) before the negative case runs. */
    U8 *const grown_anchor_source = (U8*) memory_alloc(_TEST_ANCHOR_BYTES);

    test_expect_true(test, "the anchor block allocated", !memory_empty(grown_anchor_source));

    for (USize i = 0; i < _TEST_ANCHOR_BYTES; i += 1) {
        grown_anchor_source[i] = (U8) (i + 1);
    }

    U8 *const grown_anchor = (U8*) memory_realloc(grown_anchor_source, _TEST_ANCHOR_BYTES, _TEST_ANCHOR_BYTES * 2);

    test_expect_true(test, "the anchor growth succeeded", !memory_empty(grown_anchor));

    bool anchor_preserved = true;

    for (USize i = 0; i < _TEST_ANCHOR_BYTES && anchor_preserved; i += 1) {
        anchor_preserved = grown_anchor[i] == (U8) (i + 1);
    }

    test_expect_true(test, "the anchor growth preserved the original bytes", anchor_preserved);

    memory_free(grown_anchor);

    /* Negative case: a fresh block, pattern written, then a failing realloc. */
    U8 *const block = (U8*) memory_alloc(_TEST_ANCHOR_BYTES);

    test_expect_true(test, "the block under test allocated", !memory_empty(block));

    for (USize i = 0; i < _TEST_ANCHOR_BYTES; i += 1) {
        block[i] = (U8) (0xA0 + (i % 16));
    }

    _arm_realloc();

    void *const grown = memory_realloc(block, _TEST_ANCHOR_BYTES, _TEST_ANCHOR_BYTES * 4);

    test_expect_true(test, "the injection actually fired", _fired_realloc);
    test_expect_true(test, "a failed growth returns nullptr", memory_empty(grown));

    bool block_intact = true;

    for (USize i = 0; i < _TEST_ANCHOR_BYTES && block_intact; i += 1) {
        block_intact = block[i] == (U8) (0xA0 + (i % 16));
    }

    test_expect_true(test, "the original block is still readable and unchanged", block_intact);

    memory_free(block);

    test_case_end(test);
}

/* 3b. memory_realloc's NULL-DATA branch under a failing calloc: nullptr, not an abort. Regression
 * pin for the "split OOM personality" fix - this branch used to call the ABORTING memory_alloc, so
 * memory_realloc(nullptr, 0, n) recovered or died depending on whether data happened to be null,
 * which is precisely the inconsistency the fix closed. It now forwards to memory_try_alloc, so a
 * failed calloc here must behave exactly like case 1, not like case 2. */
static void _test_memory_realloc_null_data_returns_null_on_failed_calloc(Test *const test) {
    test_case_begin(test, "memory_realloc(nullptr, ...) returns nullptr on a failed calloc, without aborting");

    /* Anchor: the null-data branch succeeds unarmed first. */
    void *const anchor = memory_realloc(nullptr, 0, _TEST_ANCHOR_BYTES);

    test_expect_true(test, "the anchor grow-from-null succeeded", !memory_empty(anchor));

    memory_free(anchor);

    _arm_calloc();

    void *const starved = memory_realloc(nullptr, 0, _TEST_ANCHOR_BYTES);

    test_expect_true(test, "the injection actually fired", _fired_calloc);
    test_expect_true(test, "a failed calloc through the null-data branch yields nullptr, not a crash", memory_empty(starved));

    /* Proves the process is still alive - the process death case 2 pins is reachable through
     * memory_alloc, but must NOT be reachable through this branch anymore. */
    void *const recovered = memory_realloc(nullptr, 0, _TEST_ANCHOR_BYTES);

    test_expect_true(test, "the allocator still works after a starved call through this branch", !memory_empty(recovered));

    memory_free(recovered);

    test_case_end(test);
}

/* 4. arena_linear_new / arena_stack_new / arena_pool_new under a failing calloc: nullptr, not an
 * abort. This is the regression pin for the fix that made linear/stack behave like pool already
 * did - both used to abort here. */
static void _test_arena_constructors_return_null_on_failed_calloc(Test *const test) {
    test_case_begin(test, "arena_linear_new, arena_stack_new, and arena_pool_new return nullptr on a failed calloc");

    ArenaLinear *linear_anchor = arena_linear_new(_TEST_ANCHOR_BYTES);

    test_expect_true(test, "arena_linear_new anchor succeeded", !memory_empty(linear_anchor));

    arena_linear_delete(&linear_anchor);

    _arm_calloc();

    ArenaLinear *const linear_starved = arena_linear_new(_TEST_ANCHOR_BYTES);

    test_expect_true(test, "the injection fired for arena_linear_new", _fired_calloc);
    test_expect_true(test, "arena_linear_new returns nullptr rather than aborting", memory_empty(linear_starved));

    ArenaStack *stack_anchor = arena_stack_new(_TEST_ANCHOR_BYTES);

    test_expect_true(test, "arena_stack_new anchor succeeded", !memory_empty(stack_anchor));

    arena_stack_delete(&stack_anchor);

    _arm_calloc();

    ArenaStack *const stack_starved = arena_stack_new(_TEST_ANCHOR_BYTES);

    test_expect_true(test, "the injection fired for arena_stack_new", _fired_calloc);
    test_expect_true(test, "arena_stack_new returns nullptr rather than aborting", memory_empty(stack_starved));

    ArenaPool *pool_anchor = arena_pool_new(sizeof(U64), 8);

    test_expect_true(test, "arena_pool_new anchor succeeded", !memory_empty(pool_anchor));

    arena_pool_delete(&pool_anchor);

    _arm_calloc();

    ArenaPool *const pool_starved = arena_pool_new(sizeof(U64), 8);

    test_expect_true(test, "the injection fired for arena_pool_new", _fired_calloc);
    test_expect_true(test, "arena_pool_new returns nullptr rather than aborting (already correct)", memory_empty(pool_starved));

    test_case_end(test);
}

/* 5. arena_init_1/arena_init_2 under a failing allocator: a refused Arena with a null handler;
 * allocator_borrow/allocator_try_borrow on it then return nullptr, and allocator_release is a
 * no-op - none of the three abort. End-to-end regression pin for the just-fixed HIGH. */
static void _test_arena_init_failure_propagates_through_the_allocator_seam(Test *const test) {
    test_case_begin(test, "a refused arena_init leaves a null handler that allocator_borrow/try_borrow/release all survive");

    /* Anchor: an ordinary, unarmed init works end to end first. */
    Arena working = arena_init_1(_TEST_ANCHOR_BYTES, ARENA_TYPE_LINEAR);

    test_expect_true(test, "the anchor arena has a live handler", !memory_empty(working.handler));

    void *const working_borrow = allocator_borrow(16, &working);

    test_expect_true(test, "the anchor arena serves a borrow", !memory_empty(working_borrow));

    allocator_release(working_borrow, &working);
    arena_uninit(&working, ARENA_TYPE_LINEAR);

    /* Negative case: arm the calloc that arena_linear_new makes inside arena_init_1/2. */
    _arm_calloc();

    Arena refused = arena_init_1(_TEST_ANCHOR_BYTES, ARENA_TYPE_LINEAR);

    test_expect_true(test, "the injection fired for arena_init_1", _fired_calloc);
    test_expect_true(test, "a refused init leaves a null handler", memory_empty(refused.handler));

    void *const borrowed = allocator_borrow(16, &refused);

    test_expect_true(test, "allocator_borrow on a null-handler arena returns nullptr, not a call-through", memory_empty(borrowed));

    void *const try_borrowed = allocator_try_borrow(16, &refused);

    test_expect_true(test, "allocator_try_borrow on a null-handler arena returns nullptr", memory_empty(try_borrowed));

    /* allocator_release must be a no-op against a null-handler arena: no deallocate hook is
     * called, so any non-null address is safe to pass - nothing is dereferenced through it. */
    int marker = 0;

    allocator_release((void*) &marker, &refused);

    test_expect_true(test, "allocator_release against a null-handler arena did not corrupt local state", marker == 0);

    /* arena_uninit on a refused (null-handler) arena must also not abort. */
    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/* 6. allocator_try_borrow's heap path (allocator == nullptr) under a failing calloc: nullptr. */
static void _test_allocator_try_borrow_heap_path_returns_null_on_failed_calloc(Test *const test) {
    test_case_begin(test, "allocator_try_borrow's heap path returns nullptr on a failed calloc");

    void *const anchor = allocator_try_borrow(_TEST_ANCHOR_BYTES, nullptr);

    test_expect_true(test, "the anchor heap borrow succeeded", !memory_empty(anchor));

    allocator_release(anchor, nullptr);

    _arm_calloc();

    void *const starved = allocator_try_borrow(_TEST_ANCHOR_BYTES, nullptr);

    test_expect_true(test, "the injection fired for allocator_try_borrow's heap path", _fired_calloc);
    test_expect_true(test, "a failed heap borrow returns nullptr", memory_empty(starved));

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Runner
 *============================================================================*/

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/memory/test_oom.c");

    test_suite_begin(&test, "memory/allocator/arena allocation-failure sweep (Linux, --wrap=calloc/realloc)");

    test_section_begin(&test, "recoverable out of memory");
    _test_memory_try_alloc_returns_null_on_failed_calloc(&test);
    _test_memory_realloc_growth_survives_a_failed_realloc(&test);
    _test_memory_realloc_null_data_returns_null_on_failed_calloc(&test);
    _test_arena_constructors_return_null_on_failed_calloc(&test);
    _test_arena_init_failure_propagates_through_the_allocator_seam(&test);
    _test_allocator_try_borrow_heap_path_returns_null_on_failed_calloc(&test);
    test_section_end(&test);

    test_section_begin(&test, "unrecoverable out of memory (abort contract)");
    _test_memory_alloc_aborts_on_failed_calloc(&test);
    test_section_end(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}
// Linux-only: this file requires -Wl,--wrap=calloc -Wl,--wrap=realloc, which no per-server
// build-linux makefile passes. Build and run it directly with gcc under WSL/Linux; it is not
// part of the standard `make` target for any module.