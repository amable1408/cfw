#include <dir/dir.h>
#include <file/file.h>
#include <test/test.h>

/* Allocation-failure sweep for the dir module.
 *
 * WHY THIS EXISTS. The design pass that hardened this module added a recoverable out-of-memory
 * path to every allocating site - joins, entry names, the copy buffer, the growth array - and
 * NONE of those branches ever executed under test.
 *
 * COVERAGE LIMIT, stated so this file does not overclaim: the growth array's REALLOC branch is
 * NOT covered. memory_realloc calls realloc directly for a non-null block, which --wrap=calloc
 * cannot fail, and the 3-entry fixture never reaches _DIR_ENTRIES_INITIAL_CAPACITY (16) anyway.
 * Only the first push - the calloc - is exercised. Covering the rest needs --wrap=realloc and a
 * larger fixture. Two full suites (123 assertions on Windows,
 * 134 on Linux) plus a clean ASan/UBSan run all passed while one of those branches was a
 * guaranteed process abort: the cleanup called memory_free on a pointer it had just proved was
 * null, and memory_free aborts on null. Reading found that one; the first run of THIS file found
 * a second (dir_create_all_1 still aborting inside a "recoverable" walk) that three rounds of
 * reading had missed.
 *
 * HOW. memory_try_alloc calls calloc DIRECTLY (memory.c) and does not route through
 * MemoryHooks, so hook injection cannot intercept it. The seam that works is the linker's:
 * -Wl,--wrap=calloc redirects every calloc to __wrap_calloc, which this file defines. The sweep
 * fails the k-th allocation for k = 1..N.
 *
 * WHAT EACH ITERATION ASSERTS. An earlier version of this file counted only "the process is
 * still alive", which made its assertion literally `depth == depth` - it printed the same green
 * result whether it verified every injection or none. Worse, the remove_all sweep was silently
 * testing an already-deleted directory for most of its run. Each iteration now carries its own
 * oracle:
 *   - the injection actually FIRED (the operation reached its k-th allocation),
 *   - a false return carries a readable ENOMEM / ERROR_NOT_ENOUGH_MEMORY, which is the exact
 *     property the module was rewritten to provide and the one a reader cannot verify,
 *   - a failed operation leaves the SOURCE tree intact, the worst realistic outcome,
 *   - and the process survives, which remains the exit-code oracle that caught the first bug.
 *
 * SINGLE-THREADED BY REQUIREMENT. The injection counters below are plain statics. Nothing in
 * this binary spawns a thread today; if that changes, another thread's callocs would both race
 * these and steal injection ordinals, quietly turning this into a test of the logger. */

extern void* __real_calloc(USize const count, USize const size);

// Allocation ordinal to fail; 0 disables injection. Set per sweep iteration.
static USize _fail_at   = 0;
static USize _seen      = 0;
static bool  _injecting = false;

// Only the NAME is fixed by -Wl,--wrap=calloc; the parameters follow the usual const default.
void* __wrap_calloc(USize const count, USize const size) {
    if (_injecting) {
        _seen += 1;

        if (_seen == _fail_at) {
            return nullptr;
        }
    }

    return __real_calloc(count, size);
}

/* Fixture names, created in the CURRENT directory - i.e. inside the repository.
 *
 * That matters because this repository lives inside Dropbox, and on Windows _dir_delete_retry
 * re-issues a failed delete up to 10 times when something holds a transient handle on a fresh
 * entry - which is exactly what a sync client does. Every retry re-converts the path and
 * ALLOCATES, so the same operation on a byte-identical tree has been measured at 39, 37 and 36
 * allocations, in correlated bursts rather than independently.
 *
 * Relocating the fixture to the system temp directory would remove the trigger outright and was
 * tried first. It is not here because neither getenv("TEMP") nor GetTempPathA returns a usable
 * path from THIS binary - the only one in the tree linked with -Wl,--wrap=calloc - while an
 * otherwise identical unwrapped build reads both fine. That interaction was not worth chasing for
 * a test harness; the variance is handled in _assert_sweep instead, by asserting exact injection
 * coverage only for the operations that never reach the retry path. Linux has no retry path and
 * is deterministic either way. */
#define _TEST_COPY "cfw_dir_oom_copy"
#define _TEST_TREE "cfw_dir_oom_tree"

static bool _last_error_is_oom(void) {
#ifdef __linux__
    return errno == ENOMEM;
#elif OS_WINDOWS
    return GetLastError() == ERROR_NOT_ENOUGH_MEMORY;
#else
    return true;
#endif
}

static bool _touch(char const *const path) {
    File *file = file_open_try_1(path, "wb");

    if (memory_empty(file)) {
        return false;
    }

    file_write_1(file, "x", 1, 1);
    file_close(&file);

    return true;
}

// A small fixed tree: enough entries to drive the growth array, a subdirectory to drive the
// recursive walks, and files to drive the copy path.
static bool _tree_build(void) {
    bool ok = dir_create_all_1(_TEST_TREE "/sub");

    ok = _touch(_TEST_TREE "/one.txt") && ok;
    ok = _touch(_TEST_TREE "/two.txt") && ok;
    ok = _touch(_TEST_TREE "/sub/three.txt") && ok;

    return ok;
}

// The source tree must survive a failed operation intact - every file present and readable.
static bool _tree_intact(void) {
    char const *const files[3] = { _TEST_TREE "/one.txt", _TEST_TREE "/two.txt", _TEST_TREE "/sub/three.txt" };

    if (!dir_exists_1(_TEST_TREE) || !dir_exists_1(_TEST_TREE "/sub")) {
        return false;
    }

    for (USize i = 0; i < 3; i += 1) {
        char buffer[2] = DEFAULT_INITIALIZATION;

        File *file = file_open_try_1(files[i], "rb");

        if (memory_empty(file)) {
            return false;
        }

        USize const read = file_read_1(file, buffer, 1, 1);

        file_close(&file);

        if (read != 1 || buffer[0] != 'x') {
            return false;
        }
    }

    return true;
}

static void _tree_remove(void) {
    _injecting = false;

    dir_remove_all_1(_TEST_TREE);
    dir_remove_all_1(_TEST_COPY);
}

typedef struct {
    /** @brief Iterations after which the source tree was damaged. */
    USize damaged;
    /** @brief Iterations where the operation reported failure. */
    USize failed;
    /** @brief Iterations whose injection point was actually reached. */
    USize fired;
    /** @brief Failures that arrived WITHOUT a readable allocation reason. */
    USize silent;
} SweepResult;

/* How many allocations the operation makes when nothing fails, as the MINIMUM over a few runs.
 *
 * Measuring rather than hardcoding is what keeps the sweep exhaustive: a guessed depth larger
 * than the real count silently produces iterations that inject nothing, which is the
 * partial-vacuity the coverage assertion exists to expose. It caught exactly that twice - a
 * sweep of 20 against list_entries' real count of 7, and 10 against create_all's 6.
 *
 * The minimum, not a single sample, because the count is not stable on Windows: _dir_delete_retry
 * re-issues a failed delete up to 10 times and each retry allocates. It is worth being precise
 * that this does NOT make the count deterministic - it only lowers the floor toward the no-retry
 * baseline. Sampling was tried at 3 and at 12 and both still failed intermittently, in bursts,
 * because sync activity is correlated over minutes rather than independent per run. That is why
 * exactness is asserted only for the operations that never touch the retry path; see
 * _assert_sweep. */
#define _MEASURE_SAMPLES 3

static USize _measure(bool (*const run)(void), void (*const prepare)(void)) {
    USize lowest = 0;

    for (USize i = 0; i < _MEASURE_SAMPLES; i += 1) {
        _injecting = false;

        prepare();

        _seen      = 0;
        _fail_at   = 0;
        _injecting = true;

        run();

        _injecting = false;

        if (i == 0 || _seen < lowest) {
            lowest = _seen;
        }
    }

    return lowest;
}

/* Runs one operation with the k-th allocation failing, for k = 1..depth. `prepare` restores
 * whatever state the operation consumes, with injection OFF, so every iteration exercises the
 * same starting conditions - without it a destructive operation sweeps a tree that its own first
 * successful iteration already deleted.
 *
 * Failing at k implies allocations 1..k-1 all succeeded, so the operation followed its ordinary
 * path and genuinely reached allocation k - which is why `fired` must equal the measured count
 * exactly, not merely be non-zero. */
static SweepResult _sweep(bool (*const run)(void), void (*const prepare)(void), USize const depth, bool const check_source, bool const guaranteed) {
    SweepResult result = DEFAULT_INITIALIZATION;

    for (USize k = 1; k <= depth; k += 1) {
        _injecting = false;

        prepare();

        _seen      = 0;
        _fail_at   = k;
        _injecting = true;

        bool const ran = run();

        _injecting = false;

        // The injection fired only if the operation reached its k-th allocation.
        if (_seen >= k) {
            result.fired += 1;
        }

        if (!ran) {
            result.failed += 1;

            /* Only the SINGLE-OPERATION functions carry the readable-reason guarantee. dir.h
             * explicitly exempts dir_copy_all and dir_remove_all: they aggregate many failures
             * across a walk, so the surviving error names whichever entry failed last rather
             * than the operation. Asserting it for them demanded a property the contract does
             * not offer - and the Windows run passed only because the last Win32 call happened
             * to leave ERROR_NOT_ENOUGH_MEMORY behind. Linux exposed it. */
            if (guaranteed && !_last_error_is_oom()) {
                result.silent += 1;
            }
        }

        if (check_source && !_tree_intact()) {
            result.damaged += 1;
        }
    }

    return result;
}

static void _prepare_source(void) {
    if (!dir_exists_1(_TEST_TREE)) {
        _tree_build();
    }

    dir_remove_all_1(_TEST_COPY);
}

// remove_all consumes its target, so the copy must be rebuilt before EVERY iteration - without
// this the sweep spent most of its run calling remove_all on a path that no longer existed.
static void _prepare_copy(void) {
    _prepare_source();

    dir_copy_all_1(_TEST_TREE, _TEST_COPY);
}

static void _prepare_create(void) {
    if (!dir_exists_1(_TEST_TREE)) {
        _tree_build();
    }

    dir_remove_all_1(_TEST_TREE "/a");
}

static bool _run_copy_all(void) {
    return dir_copy_all_1(_TEST_TREE, _TEST_COPY);
}

static bool _run_create_all(void) {
    return dir_create_all_1(_TEST_TREE "/a/b/c");
}

static bool _run_list_entries(void) {
    DirEntry *entries = nullptr;
    USize     count   = 0;

    bool const listed = dir_list_entries_1(_TEST_TREE, &entries, &count);

    dir_list_entries_uninit(entries, count);

    return listed;
}

static bool _run_remove_all(void) {
    return dir_remove_all_1(_TEST_COPY);
}

static void _assert_sweep(Test *const test, char const *const name, SweepResult const result, USize const depth, bool const guaranteed, bool const exact_depth) {
    char label[128] = DEFAULT_INITIALIZATION;

    /* The load-bearing property is that the sweep actually REACHED the code - without it, a
     * sweep that injected nothing still reported success, which is how the remove_all case once
     * spent its whole run on an already-deleted directory.
     *
     * Exactness is asserted only where the allocation count is deterministic. It is NOT for the
     * two operations that go through the Windows _dir_delete_retry path: every retry re-converts
     * the path and allocates, retries fire when a sync client holds a transient handle, and this
     * repository lives inside Dropbox - so the same operation on a byte-identical tree has been
     * measured at 39, 37 and 36 allocations. A measured floor does not fix that; it was tried at
     * 3 and at 12 samples and both still failed intermittently, in bursts, because the sync
     * activity is correlated over minutes rather than independent per run. Demanding exactness
     * there would make the suite flaky for an environmental reason with no bearing on the code
     * under test, so those two assert coverage instead: the injection fired on every iteration
     * up to the floor the run itself observed, and at least one failure was produced. */
    if (exact_depth) {
        char_format(label, sizeof label, "%s: every injection was actually reached", name);
        test_expect_u(test, label, depth, result.fired);
    }
    else {
        char_format(label, sizeof label, "%s: the sweep reached the operation", name);
        test_expect_true(test, label, result.fired > 0);

        /* EVERY injection that fired produced a failure. This is the oracle that makes the
         * non-exact branch worth anything: `fired > 0` alone is satisfied by depth == 1, so a
         * fixture that silently stopped being creatable would degenerate to one iteration
         * against a missing path, report one "failure" that has nothing to do with OOM, and go
         * green having covered one allocation site out of a dozen - the same vacuity the
         * tautological assertion had, one level further out.
         *
         * It holds regardless of retry variance: every allocation site reachable from these
         * operations propagates to a false return, including inside _dir_delete_retry, where an
         * ENOMEM is not one of the three errors that continue the retry loop and so breaks it. */
        /* This holds only while the fixture contains NO symbolic links. The Windows link-target
         * resolution in dir_list_entries is the one allocation these operations TOLERATE failing
         * - it skips target resolution and still succeeds - so a symlink in the fixture would
         * make an injection fire without producing a failure, and this assertion would report a
         * dir.c regression that is really a fixture change. */
        char_format(label, sizeof label, "%s: every injection that fired produced a failure", name);
        test_expect_u(test, label, result.fired, result.failed);
    }

    /* The property the module was rewritten to provide. A recoverable OOM is only useful if the
     * caller can tell WHY - a false with a stale error is what result_from_os would misclassify
     * as success. This is the assertion that catches a guarantee hole; reading missed one. */
    if (guaranteed) {
        char_format(label, sizeof label, "%s: no failure returned without a readable OOM reason", name);
        test_expect_u(test, label, 0, result.silent);
    }

    char_format(label, sizeof label, "%s: the source tree was never damaged", name);
    test_expect_u(test, label, 0, result.damaged);
}

static void _test_oom_copy_all(Test *const test) {
    test_case_begin(test, "dir_copy_all survives every allocation failure");

    /* The deepest sweep: copy_all allocates a join per entry, a source and destination path per
     * level, and a 64 KiB body buffer per file - the loop whose OOM cleanup was a guaranteed
     * abort before this pass, and whose create_all call was a second one. */
    USize const depth = _measure(_run_copy_all, _prepare_source);

    test_expect_true(test, "copy_all: the operation allocates at all", depth > 0);

    SweepResult const result = _sweep(_run_copy_all, _prepare_source, depth, true, false);

    _assert_sweep(test, "copy_all", result, depth, false, false);

    test_case_end(test);
}

static void _test_oom_create_all(Test *const test) {
    test_case_begin(test, "dir_create_all survives every allocation failure");

    USize const depth = _measure(_run_create_all, _prepare_create);

    test_expect_true(test, "create_all: the operation allocates at all", depth > 0);

    SweepResult const result = _sweep(_run_create_all, _prepare_create, depth, true, true);

    _assert_sweep(test, "create_all", result, depth, true, true);

    test_case_end(test);
}

static void _test_oom_list_entries(Test *const test) {
    test_case_begin(test, "dir_list_entries survives every allocation failure");

    USize const depth = _measure(_run_list_entries, _prepare_source);

    test_expect_true(test, "list_entries: the operation allocates at all", depth > 0);

    SweepResult const result = _sweep(_run_list_entries, _prepare_source, depth, true, true);

    _assert_sweep(test, "list_entries", result, depth, true, true);

    test_case_end(test);
}

static void _test_oom_no_false_success(Test *const test) {
    test_case_begin(test, "a failed allocation never reports success");

    /* The direction that matters. A recoverable OOM path is only useful if it reports failure -
     * returning true after failing to copy a tree would be worse than aborting, because the
     * caller would proceed on an incomplete result. Failing the FIRST allocation guarantees the
     * operation cannot have completed. */
    _injecting = false;

    _prepare_source();

    _seen      = 0;
    _fail_at   = 1;
    _injecting = true;

    bool const copied = dir_copy_all_1(_TEST_TREE, _TEST_COPY);

    _injecting = false;

    test_expect_false(test, "copy_all reports failure when its first allocation fails", copied);

    /* No reason assertion here: copy_all is one of the two operations dir.h exempts from the
     * readable-error guarantee. What matters for a recursive op is that it reports failure and
     * does not damage the source. */
    test_expect_true(test, "and leaves the source tree intact", _tree_intact());

    dir_remove_all_1(_TEST_COPY);

    test_case_end(test);
}

static void _test_oom_remove_all(Test *const test) {
    test_case_begin(test, "dir_remove_all survives every allocation failure");

    USize const depth = _measure(_run_remove_all, _prepare_copy);

    test_expect_true(test, "remove_all: the operation allocates at all", depth > 0);

    /* A floor, because `depth > 0` is not enough for THIS operation on Windows. If the fixture
     * copy ever silently fails to be created, dir_remove_all_1 short-circuits on a missing target
     * - but only after _dir_win_is_real_directory has converted one wide path, so the run yields
     * depth == 1, fired == 1, failed == 1 and an undamaged source, satisfying every other
     * assertion while covering one allocation site out of a dozen. Linux allocates nothing on
     * that path and is already caught by `depth > 0`.
     *
     * 4 is far below the measured Windows minimum of ~36, so it cannot flake on retry variance;
     * it only catches the collapse. */
    test_expect_true(test, "remove_all: the sweep is not degenerate", depth >= 4);

    SweepResult const result = _sweep(_run_remove_all, _prepare_copy, depth, true, false);

    _assert_sweep(test, "remove_all", result, depth, false, false);

    test_case_end(test);
}

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("./test_oom.c");

    test_verbose_set(&test, false);

    _tree_remove();

    if (!_tree_build()) {
        log_message_1(LOG_LEVEL_ERROR, "OOM sweep: could not build the fixture tree\n");

        return 1;
    }

    test_suite_begin(&test, "dir allocation-failure sweep");
    _test_oom_copy_all(&test);
    _test_oom_create_all(&test);
    _test_oom_list_entries(&test);
    _test_oom_no_false_success(&test);
    _test_oom_remove_all(&test);
    test_suite_end(&test);

    _tree_remove();

    return test_uninit(&test);
}