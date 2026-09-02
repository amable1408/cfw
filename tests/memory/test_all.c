/*
 * test_all.c - Behavioral tests for include/memory/memory.c.
 *
 * memory_alloc/memory_try_alloc/memory_copy_1/memory_copy_2/memory_delete/memory_free/
 * memory_realloc/memory_set all guard their arguments with error_check_* under
 * ERROR_CHECK_ENABLED, which aborts the process on failure. The abort half of each contract
 * cannot be observed in-process without killing the runner, so this suite spawns itself as a
 * child (re-entered through the --child-* flags below, the same pattern tests/error/test_all.c
 * and tests/process/test_all.c use) and asserts on the child's exit status, the same way
 * tests/error does. Everything that returns rather than aborts (memory_alloc's happy path,
 * memory_try_alloc, memory_realloc's grow/shrink zeroing, memory_delete's null-out,
 * memory_empty, memory_fit_size) is pinned in-process with the shared Test harness.
 *
 * memory_fit_size(byte_size=0, ...) and memory_fit_size(..., byte_count=0) also abort rather
 * than returning 0: error_check_non_value_uint fires on either argument before the function's
 * own "byte_size == 0 || byte_count == 0" guard ever runs, so that half of the guard is dead
 * code. Pinned as abort probes, not as in-process zero-returns.
 */
#include <string.h>

#include <log/log.h>
#include <memory/memory.h>
#include <process/process.h>
#include <test/test.h>

/*==============================================================================
 * MARK: - File Scope
 *============================================================================*/

/** Path this binary was invoked with, reused as the program every child-mode spawn runs. */
static char const *_program = nullptr;

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/**
 * @brief Spawn this binary with flag and assert it aborted while logging expected_substring.
 * @param test               Test handle.
 * @param case_name          Case label.
 * @param flag               --child-* flag identifying which probe to run.
 * @param expected_substring Text the failure's log line must contain.
 */
static void _test_abort_probe(Test *const test, char const *const case_name, char const *const flag, char const *const expected_substring) {
    char const *const argv_vector[] = { _program, flag, nullptr };
    ProcessSpec const spec = { .argv = argv_vector, .timeout_milliseconds = 5000 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;

    Result const result = process_run(spec, &outcome);

    char label[256] = DEFAULT_INITIALIZATION;

    snprintf(label, sizeof label, "%s: process_run reports no OS error", case_name);
    test_expect_true(test, label, result_is_success(result));

    snprintf(label, sizeof label, "%s: child did not time out", case_name);
    test_expect_false(test, label, outcome.timed_out);

    snprintf(label, sizeof label, "%s: child hit abort() status", case_name);
    test_expect_true(test, label, process_outcome_aborted(&outcome));

    snprintf(label, sizeof label, "%s: abort logged before terminating", case_name);
    test_expect_true(test, label, outcome.output != nullptr && strstr(outcome.output, expected_substring) != nullptr);

    process_outcome_uninit(&outcome);
}

static LogConfig _child_log_config(void) {
    return (LogConfig) {
        .level = LOG_LEVEL_ERROR,
        .stream = LOG_STREAM_STDOUT,
        .timestamp_enabled = true,
        .autoflush = true,
    };
}

/*==============================================================================
 * MARK: - Child Modes
 *============================================================================*/

static I32 _child_alloc_zero(void) {
    log_init(_child_log_config());

    memory_alloc(0);

    return 0;
}

static I32 _child_copy1_null_dst(void) {
    log_init(_child_log_config());

    U8 src[4] = { 1, 2, 3, 4 };

    memory_copy_1(nullptr, src, sizeof src);

    return 0;
}

static I32 _child_copy2_out_of_bound(void) {
    log_init(_child_log_config());

    U8 dst[4] = DEFAULT_INITIALIZATION;
    U8 src[8] = DEFAULT_INITIALIZATION;

    memory_copy_2(dst, sizeof dst, src, sizeof src);

    return 0;
}

static I32 _child_delete_null_address(void) {
    log_init(_child_log_config());

    memory_delete(nullptr);

    return 0;
}

static I32 _child_delete_null_target(void) {
    log_init(_child_log_config());

    void *pointer = nullptr;

    memory_delete(&pointer);

    return 0;
}

static I32 _child_free_null(void) {
    log_init(_child_log_config());

    memory_free(nullptr);

    return 0;
}

static I32 _child_realloc_zero(void) {
    log_init(_child_log_config());

    void *const buffer = memory_alloc(8);

    memory_realloc(buffer, 8, 0);

    return 0;
}

static I32 _child_set_null(void) {
    log_init(_child_log_config());

    memory_set(nullptr, 4, 0);

    return 0;
}

static I32 _child_fit_size_zero_size(void) {
    log_init(_child_log_config());

    memory_fit_size(0, 100);

    return 0;
}

static I32 _child_fit_size_zero_count(void) {
    log_init(_child_log_config());

    memory_fit_size(100, 0);

    return 0;
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_alloc(Test *const test) {
    test_case_begin(test, "memory_alloc");

    USize const size = 256;
    U8 *const buffer = (U8*) memory_alloc(size);

    test_expect_not_null(test, "memory_alloc returns non-null", buffer);

    bool all_zero = true;

    for (USize i = 0; i < size; i += 1) {
        if (buffer[i] != 0) {
            all_zero = false;

            break;
        }
    }

    test_expect_true(test, "memory_alloc(256) is fully zeroed", all_zero);

    memory_free(buffer);

    test_case_end(test);
}

static void _test_try_alloc(Test *const test) {
    test_case_begin(test, "memory_try_alloc");

    void *const zero_size = memory_try_alloc(0);

    test_expect_null(test, "memory_try_alloc(0) returns nullptr (recoverable, no abort)", zero_size);

    USize const size = 128;
    U8 *const buffer = (U8*) memory_try_alloc(size);

    test_expect_not_null(test, "memory_try_alloc(128) returns non-null", buffer);

    bool all_zero = true;

    for (USize i = 0; i < size; i += 1) {
        if (buffer[i] != 0) {
            all_zero = false;

            break;
        }
    }

    test_expect_true(test, "memory_try_alloc(128) is fully zeroed", all_zero);

    memory_free(buffer);

    test_case_end(test);
}

static void _test_copy(Test *const test) {
    test_case_begin(test, "memory_copy_1 and memory_copy_2");

    U8 const src[5] = { 10, 20, 30, 40, 50 };
    U8 dst_1[5] = DEFAULT_INITIALIZATION;

    memory_copy_1(dst_1, src, sizeof src);

    test_expect_true(test, "memory_copy_1 copies all bytes", memcmp(dst_1, src, sizeof src) == 0);

    U8 dst_2[8] = DEFAULT_INITIALIZATION;

    memory_copy_2(dst_2, sizeof dst_2, src, sizeof src);

    test_expect_true(test, "memory_copy_2 copies src bytes into a larger dst", memcmp(dst_2, src, sizeof src) == 0);
    test_expect_true(test, "memory_copy_2 leaves the untouched tail alone", dst_2[5] == 0 && dst_2[6] == 0 && dst_2[7] == 0);

    test_case_end(test);
}

static void _test_delete(Test *const test) {
    test_case_begin(test, "memory_delete");

    void *pointer = memory_alloc(16);

    test_expect_not_null(test, "allocation before delete is non-null", pointer);

    memory_delete(&pointer);

    test_expect_null(test, "memory_delete nulls the caller's pointer (MEMORY_NON_DANGLING_POINTER)", pointer);

    test_case_end(test);
}

static void _test_empty(Test *const test) {
    test_case_begin(test, "memory_empty");

    test_expect_true(test, "memory_empty(nullptr) is true", memory_empty(nullptr));

    USize marker = 7;

    test_expect_false(test, "memory_empty(&marker) is false", memory_empty(&marker));

    test_case_end(test);
}

static void _test_fit_size(Test *const test) {
    test_case_begin(test, "memory_fit_size");

    test_expect_u(test, "fit_size(8, 100) rounds 800 up to 1024", 1024, memory_fit_size(8, 100));
    test_expect_u(test, "fit_size(4, 4) is already a power of two", 16, memory_fit_size(4, 4));
    test_expect_u(test, "fit_size(1, 1) is the minimum fit", 1, memory_fit_size(1, 1));
    /* byte_size == 0 and byte_count == 0 are NOT reachable in-process: error_check_non_value_uint
     * aborts on either before the function's own "return 0" guard for those cases ever runs -
     * dead code, pinned as abort probes below instead of here. */
    test_expect_u(test, "fit_size overflowing the size*count multiply returns 0", 0, memory_fit_size(2, USIZE_MAX / 2 + 1));
    test_expect_u(test, "fit_size at the doubling ceiling falls back to byte_total", USIZE_MAX, memory_fit_size(1, USIZE_MAX));

    test_case_end(test);
}

static void _test_set(Test *const test) {
    test_case_begin(test, "memory_set");

    U8 buffer[16];

    memory_set(buffer, sizeof buffer, 0xAB);

    bool all_match = true;

    for (USize i = 0; i < sizeof buffer; i += 1) {
        if (buffer[i] != 0xAB) {
            all_match = false;

            break;
        }
    }

    test_expect_true(test, "memory_set fills every byte with value", all_match);

    test_case_end(test);
}

static void _test_realloc(Test *const test) {
    test_case_begin(test, "memory_realloc");

    void *const from_null = memory_realloc(nullptr, 0, 32);

    test_expect_not_null(test, "memory_realloc(nullptr, ...) allocates fresh memory", from_null);

    U8 *const zero_check = (U8*) from_null;
    bool all_zero = true;

    for (USize i = 0; i < 32; i += 1) {
        if (zero_check[i] != 0) {
            all_zero = false;

            break;
        }
    }

    test_expect_true(test, "memory_realloc(nullptr, ...) result is zeroed like memory_alloc", all_zero);

    /* Grow: fill the original block with a non-zero pattern first, so a grown-tail check that
     * happened to pass on an already-zero block (vacuous) cannot be confused with a real zero. */
    USize const old_size = 16;
    USize const new_size = 64;
    U8 *buffer = (U8*) memory_alloc(old_size);

    memory_set(buffer, old_size, 0xCD);

    U8 *const grown = (U8*) memory_realloc(buffer, old_size, new_size);

    test_expect_not_null(test, "memory_realloc grow returns non-null", grown);

    bool prefix_preserved = true;

    for (USize i = 0; i < old_size; i += 1) {
        if (grown[i] != 0xCD) {
            prefix_preserved = false;

            break;
        }
    }

    test_expect_true(test, "memory_realloc grow preserves the original bytes untouched", prefix_preserved);

    bool tail_zeroed = true;

    for (USize i = old_size; i < new_size; i += 1) {
        if (grown[i] != 0) {
            tail_zeroed = false;

            break;
        }
    }

    test_expect_true(test, "memory_realloc grow zeroes exactly the grown tail", tail_zeroed);

    /* Shrink: the preserved prefix must survive; nothing is asserted about zeroing since the
     * contract explicitly zeroes nothing on shrink. */
    USize const shrink_size = 8;
    U8 *const shrunk = (U8*) memory_realloc(grown, new_size, shrink_size);

    test_expect_not_null(test, "memory_realloc shrink returns non-null", shrunk);

    bool shrink_prefix_preserved = true;

    for (USize i = 0; i < shrink_size; i += 1) {
        if (shrunk[i] != 0xCD) {
            shrink_prefix_preserved = false;

            break;
        }
    }

    test_expect_true(test, "memory_realloc shrink preserves the surviving prefix", shrink_prefix_preserved);

    memory_free(from_null);
    memory_free(shrunk);

    test_case_end(test);
}

static void _test_abort_probes(Test *const test) {
    test_case_begin(test, "abort probes (subprocess)");

    _test_abort_probe(test, "memory_alloc(0)", "--child-alloc-zero", "NON_VALUE");
    _test_abort_probe(test, "memory_copy_1(nullptr dst, ...)", "--child-copy1-null-dst", "NULL_POINTER");
    _test_abort_probe(test, "memory_copy_2(src_size > dst_size)", "--child-copy2-out-of-bound", "OUT_OF_BOUND_UINT");
    _test_abort_probe(test, "memory_delete(nullptr address)", "--child-delete-null-address", "NULL_POINTER");
    _test_abort_probe(test, "memory_delete(&null target)", "--child-delete-null-target", "NULL_POINTER");
    _test_abort_probe(test, "memory_free(nullptr)", "--child-free-null", "NULL_POINTER");
    _test_abort_probe(test, "memory_realloc(..., byte_count=0)", "--child-realloc-zero", "NON_VALUE");
    _test_abort_probe(test, "memory_set(nullptr, ...)", "--child-set-null", "NULL_POINTER");
    _test_abort_probe(test, "memory_fit_size(byte_size=0, ...)", "--child-fit-size-zero-size", "NON_VALUE");
    _test_abort_probe(test, "memory_fit_size(..., byte_count=0)", "--child-fit-size-zero-count", "NON_VALUE");

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry point
 *============================================================================*/

int main(int argc, char **argv) {
    /* Child modes come first: this process was spawned by a running case and must behave as
     * the small probe that case asked for, never touching the harness below. */
    if (argc >= 2) {
        if (strcmp(argv[1], "--child-alloc-zero") == 0) {
            return _child_alloc_zero();
        }

        if (strcmp(argv[1], "--child-copy1-null-dst") == 0) {
            return _child_copy1_null_dst();
        }

        if (strcmp(argv[1], "--child-copy2-out-of-bound") == 0) {
            return _child_copy2_out_of_bound();
        }

        if (strcmp(argv[1], "--child-delete-null-address") == 0) {
            return _child_delete_null_address();
        }

        if (strcmp(argv[1], "--child-delete-null-target") == 0) {
            return _child_delete_null_target();
        }

        if (strcmp(argv[1], "--child-free-null") == 0) {
            return _child_free_null();
        }

        if (strcmp(argv[1], "--child-realloc-zero") == 0) {
            return _child_realloc_zero();
        }

        if (strcmp(argv[1], "--child-set-null") == 0) {
            return _child_set_null();
        }

        if (strcmp(argv[1], "--child-fit-size-zero-size") == 0) {
            return _child_fit_size_zero_size();
        }

        if (strcmp(argv[1], "--child-fit-size-zero-count") == 0) {
            return _child_fit_size_zero_count();
        }
    }

    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    _program = argv[0];

    Test test = test_init("./test_all.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "memory");
    _test_alloc(&test);
    _test_try_alloc(&test);
    _test_copy(&test);
    _test_delete(&test);
    _test_empty(&test);
    _test_fit_size(&test);
    _test_set(&test);
    _test_realloc(&test);
    _test_abort_probes(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}