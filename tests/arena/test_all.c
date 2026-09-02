#include <string.h>

#include <allocator/allocator.h>
#include <arena/arena.h>
#include <arena/arena_linear.h>
#include <arena/arena_pool.h>
#include <arena/arena_stack.h>
#include <process/process.h>
#include <test/test.h>

/* The three arenas had no suite at all, which is how a set of alignment and release defects sat
 * in them undetected: every slice landed at a fixed odd offset because only the SIZES were
 * rounded and never the base pointer, arena_stack_free's guard was off by the header so every
 * non-null release was silently refused, and a null buffer popped a live block instead of being
 * ignored. Each test below pins one of those, so a regression fails here rather than in whatever
 * unrelated module happens to hold the misaligned type. */

/* Every arena hands out slices of ONE memory_alloc block, so the guarantee under test is that a
 * slice is as aligned as the block is - not merely that it is non-null. */
static bool _aligned(void const *const buffer) {
    /* Cast straight to USize: the framework has no pointer-width integer alias, and USize is
     * pointer-width on every target this builds for. */
    return ((USize) buffer % MEMORY_ALIGNMENT) == 0;
}

/* Path this binary was invoked with, reused as the program every child-mode spawn runs. Set
 * once at the top of main, before any test that spawns a child reads it. */
static char const *_program = nullptr;

static LogConfig _child_log_config(void) {
    return (LogConfig) {
        .level = LOG_LEVEL_ERROR,
        .stream = LOG_STREAM_STDOUT,
        .timestamp_enabled = true,
        .autoflush = true,
    };
}

/**
 * @brief arena_linear_alloc on an exhausted arena must abort. A clean return is the bug -
 * unlike arena_linear_try_alloc, which is the whole point of having two entry points.
 * @return Exit code observed only if the call failed to abort.
 */
static I32 _child_linear_alloc_exhausted(void) {
    log_init(_child_log_config());

    ArenaLinear *const arena = arena_linear_new(64);

    arena_linear_alloc(arena, 4096);

    return 0;
}

/**
 * @brief arena_linear_alloc on a byte_count near USIZE_MAX must also abort. Regression this
 * pins: MEMORY_ALIGN_UP(byte_count) wraps for byte_count this large, so the out-of-bound check
 * ahead of try_alloc computes a small aligned value and slips past its own bound test - the
 * corner used to fall through as a silent nullptr instead of aborting. The trailing
 * error_check_message on a null buffer is what closes that gap.
 * @return Exit code observed only if the call failed to abort.
 */
static I32 _child_linear_alloc_wrap_corner(void) {
    log_init(_child_log_config());

    ArenaLinear *const arena = arena_linear_new(64);

    arena_linear_alloc(arena, USIZE_MAX);

    return 0;
}

/**
 * @brief arena_pool_alloc must abort when block_count exceeds the pool's total capacity
 * (a request that can never be satisfied), as opposed to merely being full right now.
 * @return Exit code observed only if the call failed to abort.
 */
static I32 _child_pool_alloc_block_count_exceeds_capacity(void) {
    log_init(_child_log_config());

    ArenaPool *const pool = arena_pool_new(32, 4);

    arena_pool_alloc(pool, 5);

    return 0;
}

/**
 * @brief arena_stack_alloc on an exhausted arena must abort. Contract fix (memsec audit): this
 * used to fall through to try_alloc's nullptr, an asymmetry with arena_linear_alloc that was
 * ruled a defect - the arena.h/allocator.h "allocate" contract now aborts uniformly.
 * @return Exit code observed only if the call failed to abort.
 */
static I32 _child_stack_alloc_exhausted(void) {
    log_init(_child_log_config());

    ArenaStack *const arena = arena_stack_new(64);

    arena_stack_alloc(arena, 4096);

    return 0;
}

/**
 * @brief arena_pool_alloc must abort when the pool is currently full, even for a block_count
 * that is otherwise within total capacity. Contract fix (memsec audit): this is distinct from
 * _child_pool_alloc_block_count_exceeds_capacity above (an impossible-ever request) - here the
 * request is in range but the pool has no free space right now, which used to return a silent
 * nullptr and now aborts too.
 * @return Exit code observed only if the call failed to abort.
 */
static I32 _child_pool_alloc_full(void) {
    log_init(_child_log_config());

    ArenaPool *const pool = arena_pool_new(32, 2);

    arena_pool_alloc(pool, 1);
    arena_pool_alloc(pool, 1);
    arena_pool_alloc(pool, 1);

    return 0;
}

/**
 * @brief Spawn this binary with flag, and assert it aborted while logging expected_substring.
 * @param test Test context.
 * @param case_name Case label.
 * @param flag --child-* flag identifying which probe to run.
 * @param expected_substring Text the abort's log line must contain.
 */
static void _test_abort_probe(Test *const test, char const *const case_name, char const *const flag, char const *const expected_substring) {
    char const *const argv_vector[] = { _program, flag, nullptr };
    ProcessSpec const spec = { .argv = argv_vector, .timeout_milliseconds = 5000 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;

    Result const result = process_run(spec, &outcome);

    test_expect_true(test, case_name, result_is_success(result));
    test_expect_false(test, "child did not time out", outcome.timed_out);
    test_expect_true(test, "child hit abort() status, not a plain exit()", process_outcome_aborted(&outcome));
    test_expect_true(test, "abort logged before terminating",
        !memory_empty((void*) outcome.output) && strstr(outcome.output, expected_substring) != nullptr);

    process_outcome_uninit(&outcome);
}

static void _test_linear_alignment(Test *const test) {
    test_case_begin(test, "arena_linear alignment");

    ArenaLinear *arena = arena_linear_new(4096);

    test_expect_true(test, "arena created", !memory_empty(arena));

    /* Deliberately odd sizes. A 1-byte slice is what used to poison every later pointer: without
     * rounding the bump the next block starts at offset 1, and stays odd forever after. */
    void *const first = arena_linear_try_alloc(arena, 1);

    test_expect_true(test, "first slice non-null", !memory_empty(first));
    test_expect_true(test, "first slice aligned", _aligned(first));

    bool all_aligned = true;

    for (USize i = 0; i < 64; i += 1) {
        void *const block = arena_linear_try_alloc(arena, (i % 17) + 1);

        if (memory_empty(block) || !_aligned(block)) {
            all_aligned = false;

            break;
        }
    }

    test_expect_true(test, "every odd-sized slice stays aligned", all_aligned);

    arena_linear_clear(arena);

    test_expect_true(test, "clear resets to empty", arena_linear_empty(arena));

    arena_linear_delete(&arena);

    test_case_end(test);
}

static void _test_linear_exhaustion(Test *const test) {
    test_case_begin(test, "arena_linear exhaustion returns null");

    ArenaLinear *arena = arena_linear_new(64);

    /* try_alloc is the untrusted-input entry point: exhaustion must be a null, never an abort,
     * or a request body sized to drain the arena becomes a process kill. */
    test_expect_true(test, "oversized request refused", memory_empty(arena_linear_try_alloc(arena, 4096)));
    test_expect_true(test, "zero-sized request refused", memory_empty(arena_linear_try_alloc(arena, 0)));

    /* Rounding up must not be allowed to wrap into a small value that then passes the bound
     * check - the arena pairs MEMORY_ALIGN_UP with an `aligned < byte_count` test for this. */
    test_expect_true(test, "near-USIZE_MAX request refused", memory_empty(arena_linear_try_alloc(arena, USIZE_MAX - 4)));

    arena_linear_delete(&arena);

    test_case_end(test);
}

static void _test_linear_alloc_aborts_on_exhaustion(Test *const test) {
    test_case_begin(test, "arena_linear_alloc aborts on exhaustion (subprocess)");

    /* arena_linear_alloc, unlike arena_linear_try_alloc, treats exhaustion as a programmer
     * error and ends the process - this is the entire reason the two entry points exist, and
     * it had zero coverage: every other case in this suite exercises try_alloc only. */
    _test_abort_probe(test, "arena_linear_alloc(exhausted)", "--child-linear-alloc-exhausted", "OUT_OF_BOUND_UINT");

    test_case_end(test);
}

static void _test_linear_alloc_aborts_on_wrap_corner(Test *const test) {
    test_case_begin(test, "arena_linear_alloc aborts on a byte_count near USIZE_MAX (subprocess)");

    /* Regression this pins: MEMORY_ALIGN_UP(byte_count) wraps for byte_count this large, so the
     * out-of-bound check ahead of try_alloc computed a small ALIGNED value and slipped past its
     * own bound test - this corner used to return a silent nullptr instead of aborting. The
     * trailing error_check_message("arena_linear_alloc: arena exhausted") is the fix, and its
     * own message - not OUT_OF_BOUND_UINT - is what must appear here, since the out-of-bound
     * check is exactly what the wrap defeats. */
    _test_abort_probe(test, "arena_linear_alloc(wrap corner)", "--child-linear-alloc-wrap-corner", "arena_linear_alloc: arena exhausted");

    test_case_end(test);
}

static void _test_linear_clear_zeroes_memory(Test *const test) {
    test_case_begin(test, "arena_linear_clear hands back zeroed memory");

    ArenaLinear *arena = arena_linear_new(128);

    void *const slice = arena_linear_try_alloc(arena, 16);

    test_expect_true(test, "slice allocated", !memory_empty(slice));

    memory_set(slice, 16, 0xEE);

    arena_linear_clear(arena);

    void *const reused = arena_linear_try_alloc(arena, 16);

    test_expect_true(test, "clear rewinds the bump to the same offset", reused == slice);

    bool all_zero = true;

    for (USize i = 0; i < 16; i += 1) {
        if (((U8*) reused)[i] != 0) {
            all_zero = false;

            break;
        }
    }

    /* Pins the ACTUAL behavior (arena_linear_clear memset()s the whole buffer), not merely
     * "reusable" - a caller relying on always-zeroed reuse needs this contract stated, and a
     * future change that skips the memset for speed would be silent otherwise. */
    test_expect_true(test, "reused slice reads zero, not the stale 0xEE pattern", all_zero);

    arena_linear_delete(&arena);

    test_case_end(test);
}

static void _test_stack_round_trip(Test *const test) {
    test_case_begin(test, "arena_stack alloc/free round trip");

    ArenaStack *arena = arena_stack_new(4096);

    void *const first = arena_stack_try_alloc(arena, 32);

    test_expect_true(test, "first block non-null", !memory_empty(first));
    test_expect_true(test, "first block aligned", _aligned(first));
    test_expect_false(test, "arena not empty after alloc", arena_stack_empty(arena));

    /* The regression: the release guard compared against `data - data_size`, one header short of
     * what try_alloc actually returned, so this call did nothing and the arena only ever grew. */
    arena_stack_free(arena, first);

    test_expect_true(test, "release of the top block is accepted", arena_stack_empty(arena));

    void *const second = arena_stack_try_alloc(arena, 32);

    test_expect_true(test, "freed space is reusable", second == first);

    arena_stack_free(arena, second);

    arena_stack_delete(&arena);

    test_case_end(test);
}

static void _test_stack_null_free_is_noop(Test *const test) {
    test_case_begin(test, "arena_stack null free is a no-op");

    ArenaStack *arena = arena_stack_new(4096);

    void *const live = arena_stack_try_alloc(arena, 64);

    test_expect_true(test, "live block allocated", !memory_empty(live));

    memory_set(live, 64, 0xAB);

    /* This is the reachable hazard, not a hypothetical: allocator_release forwards straight to
     * arena_stack_free, and al_u64_uninit and its ~20 siblings pass self->data unguarded, which
     * is nullptr for a container created but never grown. A null used to fall through the LIFO
     * guard and pop - and ZERO - whatever was on top, handing a live block back to the bump
     * pointer silently. */
    arena_stack_free(arena, nullptr);

    test_expect_false(test, "null free did not pop the live block", arena_stack_empty(arena));
    test_expect_true(test, "live block contents survived", *(U8*) live == 0xAB);

    void *const next = arena_stack_try_alloc(arena, 64);

    test_expect_true(test, "next block does not overlap the live one", next != live);

    arena_stack_delete(&arena);

    test_case_end(test);
}

static void _test_stack_out_of_order_free(Test *const test) {
    test_case_begin(test, "arena_stack refuses out-of-order release");

    ArenaStack *arena = arena_stack_new(4096);

    void *const first  = arena_stack_try_alloc(arena, 32);
    void *const second = arena_stack_try_alloc(arena, 32);

    test_expect_true(test, "both blocks allocated", !memory_empty(first) && !memory_empty(second));
    test_expect_true(test, "second block aligned", _aligned(second));

    memory_set(second, 32, 0xCD);

    /* LIFO: releasing the older block must be refused outright. Leaking it is the correct
     * trade - popping it would strand `second` on top of reclaimed space. */
    arena_stack_free(arena, first);

    test_expect_true(test, "newer block untouched by a stale release", *(U8*) second == 0xCD);

    arena_stack_delete(&arena);

    test_case_end(test);
}

static void _test_stack_header_size_agrees(Test *const test) {
    test_case_begin(test, "arena_stack public header size matches consumption");

    /* Callers budget arena capacity with the PUBLIC macro (main.c:124 reserves
     * ARENA_STACK_HEADER_SIZE * 4). When it read sizeof(USize) while the implementation consumed
     * MEMORY_ALIGN_UP(sizeof(USize)), every such arena was quietly undersized and allocator_borrow
     * began returning nullptr into consumers that do not check it. */
    test_expect_true(test, "public header size is aligned", (ARENA_STACK_HEADER_SIZE % MEMORY_ALIGNMENT) == 0);

    USize const block_size  = 32;
    USize const block_count = 4;

    ArenaStack *arena = arena_stack_new((block_size + ARENA_STACK_HEADER_SIZE) * block_count);

    bool all_allocated = true;

    for (USize i = 0; i < block_count; i += 1) {
        if (memory_empty(arena_stack_try_alloc(arena, block_size))) {
            all_allocated = false;

            break;
        }
    }

    test_expect_true(test, "a budget sized by the public macro holds every block", all_allocated);

    arena_stack_delete(&arena);

    test_case_end(test);
}

static void _test_stack_alloc_aborts_on_exhaustion(Test *const test) {
    test_case_begin(test, "arena_stack_alloc aborts on exhaustion (subprocess)");

    /* Contract fix (memsec audit): arena_stack_alloc now aborts on exhaustion, uniform with
     * arena_linear_alloc/arena_pool_alloc - the old asymmetry (this fell through to try_alloc's
     * nullptr while linear already aborted) was ruled a defect, since arena.h/allocator.h
     * document "allocate" as always treating exhaustion as a programmer error. */
    _test_abort_probe(test, "arena_stack_alloc(exhausted)", "--child-stack-alloc-exhausted", "arena_stack_alloc: arena exhausted");

    test_case_end(test);
}

static void _test_linear_new_zero_capacity_returns_null(Test *const test) {
    test_case_begin(test, "arena_linear_new(0) returns null (not abort)");

    /* Contract fix (memsec audit): arena_linear_new used to error_check_non_value_uint(capacity)
     * and abort on 0; it now returns nullptr, matching the header's refusal-not-abort promise
     * and letting arena_init_2's own size_invalid guard forward a computed 0 safely instead of
     * relying on this constructor to survive it. */
    test_expect_true(test, "zero capacity refused, not aborted", memory_empty(arena_linear_new(0)));

    test_case_end(test);
}

static void _test_stack_free_zeroes_reused_memory(Test *const test) {
    test_case_begin(test, "arena_stack_free hands back zeroed memory on reuse");

    ArenaStack *arena = arena_stack_new(4096);

    void *const first = arena_stack_try_alloc(arena, 32);

    test_expect_true(test, "first block allocated", !memory_empty(first));

    memory_set(first, 32, 0xEE);

    arena_stack_free(arena, first);

    void *const second = arena_stack_try_alloc(arena, 32);

    test_expect_true(test, "freed space reused at the same address", second == first);

    bool all_zero = true;

    for (USize i = 0; i < 32; i += 1) {
        if (((U8*) second)[i] != 0) {
            all_zero = false;

            break;
        }
    }

    /* Pins the ACTUAL behavior (arena_stack_free memset()s the released block before handing
     * the pointer back to the bump), not merely "reusable pointer". */
    test_expect_true(test, "reused block reads zero, not the stale 0xEE pattern", all_zero);

    arena_stack_delete(&arena);

    test_case_end(test);
}

static void _test_stack_new_zero_capacity_returns_null(Test *const test) {
    test_case_begin(test, "arena_stack_new(0) returns null (not abort)");

    /* Same contract fix as arena_linear_new: refusal, never abort. */
    test_expect_true(test, "zero capacity refused, not aborted", memory_empty(arena_stack_new(0)));

    test_case_end(test);
}

static void _test_stack_stride_alignment(Test *const test) {
    test_case_begin(test, "arena_stack every odd-sized slice stays aligned");

    /* Same regression class as _test_linear_alignment, but for the stack's data+header stride:
     * a header sized wrong relative to MEMORY_ALIGNMENT would misalign every block after the
     * first no matter how well individual block sizes are rounded. */
    ArenaStack *arena = arena_stack_new(8192);

    bool all_aligned = true;

    for (USize i = 0; i < 64; i += 1) {
        void *const block = arena_stack_try_alloc(arena, (i % 23) + 1);

        if (memory_empty(block) || !_aligned(block)) {
            all_aligned = false;

            break;
        }
    }

    test_expect_true(test, "every odd-sized slice stays aligned", all_aligned);

    arena_stack_delete(&arena);

    test_case_end(test);
}

static void _test_pool_alignment_and_reuse(Test *const test) {
    test_case_begin(test, "arena_pool alignment and reuse");

    /* An odd block_size is the case that used to misalign every block after the first, since
     * blocks are handed out at data + block_size * index. */
    ArenaPool *pool = arena_pool_new(17, 8);

    test_expect_true(test, "pool created", !memory_empty(pool));

    void *const first  = arena_pool_try_alloc(pool, 1);
    void *const second = arena_pool_try_alloc(pool, 1);

    test_expect_true(test, "first block aligned", _aligned(first));
    test_expect_true(test, "second block aligned", _aligned(second));

    arena_pool_free(pool, second);
    arena_pool_free(pool, first);

    test_expect_true(test, "pool empty after releasing both", arena_pool_empty(pool));

    void *const reused = arena_pool_try_alloc(pool, 1);

    test_expect_true(test, "released block is handed out again", reused == first);

    arena_pool_delete(&pool);

    test_case_end(test);
}

static void _test_pool_double_free_survives(Test *const test) {
    test_case_begin(test, "arena_pool double free is absorbed");

    ArenaPool *pool = arena_pool_new(32, 4);

    void *const block = arena_pool_try_alloc(pool, 1);

    test_expect_true(test, "block allocated", !memory_empty(block));

    arena_pool_free(pool, block);

    test_expect_true(test, "pool empty after release", arena_pool_empty(pool));

    /* Releasing the pool's last block drives size to 0. That size check used to be an
     * error_check that aborted, so the second release killed the process outright; it is the
     * early return there - not the free_list guard further down - that absorbs this case. */
    arena_pool_free(pool, block);

    test_expect_true(test, "second release did not abort", arena_pool_empty(pool));

    /* The range/modulo guard needs a pointer that is INSIDE the pool but not on a block
     * boundary. An outside address (a stack local) proves nothing here: it makes the computed
     * offset absurdly large, so the `offset >= size` check absorbs it and the assertion holds
     * even with the range guard deleted. A misaligned interior pointer is the only input that
     * reaches the modulo test - without it, the offset truncates to a live block and
     * memory_set zeroes block_size bytes starting mid-block. */
    void *const block_one = arena_pool_try_alloc(pool, 1);
    void *const block_two = arena_pool_try_alloc(pool, 1);

    test_expect_true(test, "two blocks allocated", !memory_empty(block_one) && !memory_empty(block_two));

    memory_set(block_two, 32, 0xEF);

    arena_pool_free(pool, (void*) ((U8*) block_two + 1));

    test_expect_false(test, "misaligned interior pointer did not empty the pool", arena_pool_empty(pool));
    test_expect_true(test, "block contents survived a misaligned release", *(U8*) block_two == 0xEF);

    /* THIS is the discriminating assertion - do not delete it as redundant. With the modulo
     * guard removed the offset truncates to 1 and zeroing starts at byte 1, so byte 0 survives
     * and the pool stays non-empty: the two assertions above pass either way. Only a byte at or
     * past offset 1 detects the missing guard. */
    test_expect_true(test, "the rest of the block survived too", *((U8*) block_two + 8) == 0xEF);

    arena_pool_delete(&pool);

    test_case_end(test);
}

static void _test_pool_multi_block_round_trip(Test *const test) {
    test_case_begin(test, "arena_pool releases a whole multi-block run");

    ArenaPool *pool = arena_pool_new(32, 6);

    void *const run = arena_pool_try_alloc(pool, 3);

    test_expect_true(test, "three-block run allocated", !memory_empty(run));
    test_expect_true(test, "run start aligned", _aligned(run));

    arena_pool_free(pool, run);

    /* The leak this pins: free used to mark only the block it was handed, so blocks 1 and 2
     * stayed flagged used with no pointer left that could ever release them. The pool would
     * report non-empty here, and the capacity would be permanently down by two blocks. */
    test_expect_true(test, "whole run released, pool empty", arena_pool_empty(pool));

    void *const reused = arena_pool_try_alloc(pool, 3);

    test_expect_true(test, "the full run is allocatable again", reused == run);

    /* And the reclaimed capacity is genuinely usable, not just reported free. */
    void *const tail = arena_pool_try_alloc(pool, 3);

    test_expect_true(test, "remaining capacity still serves a second run", !memory_empty(tail));
    test_expect_true(test, "the two runs do not overlap", tail != reused);

    arena_pool_delete(&pool);

    test_case_end(test);
}

static void _test_pool_run_interior_pointer_refused(Test *const test) {
    test_case_begin(test, "arena_pool refuses a run-interior block pointer");

    ArenaPool *pool = arena_pool_new(32, 4);

    void *const run = arena_pool_try_alloc(pool, 2);

    test_expect_true(test, "two-block run allocated", !memory_empty(run));

    memory_set(run, 64, 0xA5);

    /* Block-ALIGNED but not a run start. The modulo guard cannot catch this one - the address
     * is a legitimate block boundary - so rejection rests entirely on run_size reading 0 at an
     * index that does not begin an allocation. Without that test the run would be half-released
     * and block 1 handed out while the caller still holds a pointer covering it. */
    arena_pool_free(pool, (void*) ((U8*) run + 32));

    test_expect_false(test, "interior release refused, pool still in use", arena_pool_empty(pool));
    test_expect_true(test, "run contents intact at block 0", *(U8*) run == 0xA5);
    test_expect_true(test, "run contents intact at block 1", *((U8*) run + 32) == 0xA5);

    arena_pool_delete(&pool);

    test_case_end(test);
}

static void _test_pool_recycles_at_capacity(Test *const test) {
    test_case_begin(test, "arena_pool recycles interior blocks at capacity");

    ArenaPool *pool = arena_pool_new(32, 3);

    void *const first  = arena_pool_try_alloc(pool, 1);
    void *const second = arena_pool_try_alloc(pool, 1);
    void *const third  = arena_pool_try_alloc(pool, 1);

    test_expect_true(test, "pool filled to capacity", !memory_empty(first) && !memory_empty(second) && !memory_empty(third));
    test_expect_true(test, "further allocation refused while full", memory_empty(arena_pool_try_alloc(pool, 1)));

    /* Release an INTERIOR block. The high-water mark cannot retract, because the trailing block
     * is still live - so the old gate (`block_count > capacity - size`) rejected every later
     * request and the pool stopped recycling exactly when recycling was the only option. */
    arena_pool_free(pool, second);

    void *const recycled = arena_pool_try_alloc(pool, 1);

    test_expect_true(test, "the released interior block is handed out again", recycled == second);
    test_expect_true(test, "pool is full once more", memory_empty(arena_pool_try_alloc(pool, 1)));

    arena_pool_delete(&pool);

    test_case_end(test);
}

static void _test_pool_stride_alignment(Test *const test) {
    test_case_begin(test, "arena_pool every block past the first stays aligned");

    /* Blocks are handed out at data + block_size * index, so an odd block_size that misaligns
     * the stride would only show up a few blocks in - the earlier alignment case only ever
     * allocated 2 blocks from a 17-byte pool. This one walks 32. */
    ArenaPool *pool = arena_pool_new(17, 32);

    bool all_aligned = true;

    for (USize i = 0; i < 32; i += 1) {
        void *const block = arena_pool_try_alloc(pool, 1);

        if (memory_empty(block) || !_aligned(block)) {
            all_aligned = false;

            break;
        }
    }

    test_expect_true(test, "every block index stays aligned", all_aligned);

    arena_pool_delete(&pool);

    test_case_end(test);
}

static void _test_pool_free_zeroes_reused_memory(Test *const test) {
    test_case_begin(test, "arena_pool_free hands back zeroed memory on reuse");

    ArenaPool *pool = arena_pool_new(32, 4);

    void *const block = arena_pool_try_alloc(pool, 1);

    test_expect_true(test, "block allocated", !memory_empty(block));

    memory_set(block, 32, 0xEE);

    arena_pool_free(pool, block);

    void *const reused = arena_pool_try_alloc(pool, 1);

    test_expect_true(test, "released block reused at the same address", reused == block);

    bool all_zero = true;

    for (USize i = 0; i < 32; i += 1) {
        if (((U8*) reused)[i] != 0) {
            all_zero = false;

            break;
        }
    }

    /* Pins the ACTUAL behavior (arena_pool_free memset()s the run before marking it free), not
     * merely "reusable pointer". */
    test_expect_true(test, "reused block reads zero, not the stale 0xEE pattern", all_zero);

    arena_pool_delete(&pool);

    test_case_end(test);
}

static void _test_pool_alloc_aborts_when_full(Test *const test) {
    test_case_begin(test, "arena_pool_alloc aborts when currently full (subprocess)");

    /* Contract fix (memsec audit): an in-range block_count the pool cannot CURRENTLY satisfy now
     * aborts too, not just a block_count exceeding total capacity - the old silent nullptr here
     * was the asymmetry the audit ruled a defect, uniform with arena_linear_alloc/
     * arena_stack_alloc. The over-total-capacity abort stays a separate probe below: that one
     * fires from arena_pool_alloc's own error_check_out_of_bound_uint before try_alloc is ever
     * called, while this one fires from the new error_check_message AFTER try_alloc returns
     * null - two different guards, so both need their own case. */
    _test_abort_probe(test, "arena_pool_alloc(full)", "--child-pool-alloc-full", "arena_pool_alloc: pool exhausted");

    test_case_end(test);
}

static void _test_pool_null_free_is_noop(Test *const test) {
    test_case_begin(test, "arena_pool null free is a no-op");

    /* Matches arena_stack_free: arena_pool_free now ignores a null buffer instead of aborting,
     * since the generic allocator_release seam forwards an empty owner's self->data straight
     * through routinely (a container created and never grown). */
    ArenaPool *pool = arena_pool_new(32, 4);

    void *const live = arena_pool_try_alloc(pool, 1);

    test_expect_true(test, "live block allocated", !memory_empty(live));

    memory_set(live, 32, 0xAB);

    arena_pool_free(pool, nullptr);

    test_expect_false(test, "null free did not empty the pool", arena_pool_empty(pool));
    test_expect_true(test, "live block contents survived", *(U8*) live == 0xAB);

    arena_pool_delete(&pool);

    test_case_end(test);
}

static void _test_pool_alloc_aborts_on_block_count_exceeds_capacity(Test *const test) {
    test_case_begin(test, "arena_pool_alloc aborts when block_count exceeds capacity (subprocess)");

    _test_abort_probe(test, "arena_pool_alloc(block_count > capacity)", "--child-pool-alloc-block-count-exceeds-capacity", "OUT_OF_BOUND_UINT");

    test_case_end(test);
}

static void _test_arena_init_1_basic(Test *const test) {
    test_case_begin(test, "arena_init_1 allocates like arena_init_2(1, n, type)");

    /* arena_init_1 had zero coverage: every other facade case in this suite goes through
     * arena_init_2 directly. */
    Arena arena = arena_init_1(128, ARENA_TYPE_LINEAR);

    void *const block = allocator_try_borrow(64, &arena);

    test_expect_true(test, "byte-count constructor produced a working arena", !memory_empty(block));
    test_expect_true(test, "borrowed block aligned", _aligned(block));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    /* arena_uninit now nulls try_allocate too, not just allocate/deallocate/handler - a caller
     * dispatching through allocator_try_borrow after teardown used to reach a dangling
     * function pointer instead of the null-hook nullptr every other hook already gave. */
    test_expect_true(test, "uninit nulls try_allocate too", arena.try_allocate == nullptr);

    test_case_end(test);
}

static void _test_facade_refused_arena_allocator_seam_is_safe(Test *const test) {
    test_case_begin(test, "allocator seam on a refused arena is null/no-op, never abort");

    /* THE high-severity fix this pins: arena_init_2(0, n, LINEAR) leaves handler null while the
     * hooks (allocate/deallocate/try_allocate) stay live function pointers. Before the fix,
     * allocator_borrow/allocator_release/allocator_try_borrow called straight through those hooks
     * with a null handler, which then aborted inside the arena's own error_check_null(self) - a
     * crash site that points away from the actual failure (the refused init). allocator_borrow is
     * the discriminating call here: it is the ABORTING entry point, so if the null-handler guard
     * were missing this case would abort the whole suite rather than merely fail an assertion. */
    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    test_expect_true(test, "refused init leaves a null handler", memory_empty(refused.handler));
    test_expect_true(test, "refused init still has a live allocate hook", refused.allocate != nullptr);

    test_expect_true(test, "allocator_borrow on a refused arena returns null, not abort", memory_empty(allocator_borrow(16, &refused)));
    test_expect_true(test, "allocator_try_borrow on a refused arena returns null", memory_empty(allocator_try_borrow(16, &refused)));

    /* No real buffer was ever handed out by a refused arena, but allocator_release still has to
     * survive being called against one (the null-handler branch must be a no-op, not a crash). */
    U8 unrelated = 0;

    allocator_release((void*) &unrelated, &refused);

    test_expect_true(test, "allocator_release on a refused arena did not abort", true);

    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_facade_stack_deallocate_round_trip(Test *const test) {
    test_case_begin(test, "arena facade deallocate reaches arena_stack_free (regression: arity cast)");

    /* History: arena.deallocate used to be cast from arena_stack_free's real one-parameter
     * signature to the two-parameter FpArenaFree and invoked as deallocate(handler, buffer) -
     * undefined behaviour that silently discarded the buffer argument, so releasing through the
     * facade popped whatever was on top instead of what the caller actually passed. No case in
     * this suite calls allocator_release (equivalently, arena.deallocate) at all - every other
     * release exercises arena_stack_free directly, bypassing the facade dispatch entirely. */
    Arena arena = arena_init_2(32, 8, ARENA_TYPE_STACK);

    void *const first = allocator_borrow(32, &arena);

    test_expect_true(test, "first block borrowed through the facade", !memory_empty(first));

    memory_set(first, 32, 0xEE);

    allocator_release(first, &arena);

    void *const second = allocator_borrow(32, &arena);

    test_expect_true(test, "released block reused at the same address", second == first);

    bool all_zero = true;

    for (USize i = 0; i < 32; i += 1) {
        if (((U8*) second)[i] != 0) {
            all_zero = false;

            break;
        }
    }

    test_expect_true(test, "facade release actually freed the buffer it was given, not the wrong one", all_zero);

    arena_uninit(&arena, ARENA_TYPE_STACK);

    test_case_end(test);
}

static void _test_facade_pool_dispatch_and_uninit(Test *const test) {
    test_case_begin(test, "arena facade dispatches to pool and arena_uninit tears it down");

    /* arena_uninit's ARENA_TYPE_POOL branch, and the facade's allocate/deallocate dispatch for
     * pool, had zero coverage - every pool case in this suite calls arena_pool_new/_delete
     * directly, never through the Arena facade at all. */
    Arena arena = arena_init_2(32, 4, ARENA_TYPE_POOL);

    void *const block = allocator_try_borrow(1, &arena);

    test_expect_true(test, "pool arena served a block through the facade", !memory_empty(block));

    memory_set(block, 32, 0xEE);

    allocator_release(block, &arena);

    void *const reused = allocator_try_borrow(1, &arena);

    test_expect_true(test, "facade release freed the pool block, reused at the same address", reused == block);

    arena_uninit(&arena, ARENA_TYPE_POOL);

    test_expect_true(test, "tearing down a pool arena through the facade survives", true);

    test_case_end(test);
}

static void _test_arena_facade_size_overflow(Test *const test) {
    test_case_begin(test, "arena facade refuses a wrapping size product");

    /* allocator_try_borrow, not allocator_borrow: the latter aborts on failure, and a refusal
     * is precisely what this case has to observe.
     *
     * byte_size * byte_count wrapped silently, so a caller sizing an arena from input got a
     * TINY arena instead of a refusal - and every later allocation returned nullptr into
     * consumers that do not null-check. The product below wraps to a small value. */
    /* Two distinct wrap outcomes, tested separately: `huge * 8` lands on exactly 0, `huge * 9`
     * wraps to a small non-zero value - a real but tiny arena. Both are refused by the SAME
     * size_invalid predicate in arena_init_2 before either value ever reaches arena_linear_new.
     * Note arena_linear_new(0) itself also now returns nullptr rather than aborting (see
     * _test_linear_new_zero_capacity_returns_null), so this guard is belt-and-suspenders for the
     * wrap-to-zero case specifically - the wrap-to-small-nonzero case is the one it alone catches. */
    USize const huge = (USIZE_MAX / 4) + 1;

    Arena linear = arena_init_2(huge, 8, ARENA_TYPE_LINEAR);

    test_expect_true(test, "linear arena refused the size wrapping to zero", memory_empty(allocator_try_borrow(16, &linear)));

    Arena stack = arena_init_2(huge, 8, ARENA_TYPE_STACK);

    test_expect_true(test, "stack arena refused the size wrapping to zero", memory_empty(allocator_try_borrow(16, &stack)));

    Arena truncated = arena_init_2(huge, 9, ARENA_TYPE_LINEAR);

    test_expect_true(test, "linear arena refused the size wrapping to a small value", memory_empty(allocator_try_borrow(16, &truncated)));

    /* A non-wrapping product of the same shape must still produce a working arena, or the guard
     * would be indistinguishable from rejecting everything. */
    /* A zero operand is refused by the SAME predicate as the wrap - not because arena_linear_new(0)
     * would abort (it no longer does), but so a plain zero byte_size/byte_count and a wrapped
     * product are indistinguishable to a caller: both come back as a plain arena_init_2 refusal. */
    Arena zero_size  = arena_init_2(0, 8, ARENA_TYPE_LINEAR);
    Arena zero_count = arena_init_2(64, 0, ARENA_TYPE_STACK);

    test_expect_true(test, "a zero byte_size is refused", memory_empty(allocator_try_borrow(16, &zero_size)));
    test_expect_true(test, "a zero byte_count is refused", memory_empty(allocator_try_borrow(16, &zero_count)));

    Arena good = arena_init_2(64, 8, ARENA_TYPE_LINEAR);

    test_expect_true(test, "an in-range product still allocates", !memory_empty(allocator_try_borrow(16, &good)));

    /* Every refused arena is torn down here, which is the point: a refused init leaves handler
     * null, and arena_uninit used to abort on that - so a caller pairing init with cleanup died
     * on its own error path. These five calls are the regression guard for it. */
    arena_uninit(&linear, ARENA_TYPE_LINEAR);
    arena_uninit(&stack, ARENA_TYPE_STACK);
    arena_uninit(&truncated, ARENA_TYPE_LINEAR);
    arena_uninit(&zero_size, ARENA_TYPE_LINEAR);
    arena_uninit(&zero_count, ARENA_TYPE_STACK);
    arena_uninit(&good, ARENA_TYPE_LINEAR);

    test_expect_true(test, "tearing down refused arenas survives", true);

    test_case_end(test);
}

int main(int argc, char **argv) {
    /* Child modes come first: this process was spawned by a running case (see
     * _test_abort_probe) and must behave as the small probe that case asked for, never
     * touching the Test harness below. */
    if (argc >= 2) {
        if (strcmp(argv[1], "--child-linear-alloc-exhausted") == 0) {
            return _child_linear_alloc_exhausted();
        }

        if (strcmp(argv[1], "--child-linear-alloc-wrap-corner") == 0) {
            return _child_linear_alloc_wrap_corner();
        }

        if (strcmp(argv[1], "--child-pool-alloc-block-count-exceeds-capacity") == 0) {
            return _child_pool_alloc_block_count_exceeds_capacity();
        }

        if (strcmp(argv[1], "--child-stack-alloc-exhausted") == 0) {
            return _child_stack_alloc_exhausted();
        }

        if (strcmp(argv[1], "--child-pool-alloc-full") == 0) {
            return _child_pool_alloc_full();
        }
    }

    LogConfig const log_config = {
        .level = LOG_LEVEL_ERROR,
        .stream = stdout,
        .timestamp_enabled = true,
        .autoflush = true,
    };

    log_init(log_config);

    _program = argv[0];

    Test test = test_init("./test_all.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "arena_linear");
    _test_linear_alignment(&test);
    _test_linear_exhaustion(&test);
    _test_linear_alloc_aborts_on_exhaustion(&test);
    _test_linear_alloc_aborts_on_wrap_corner(&test);
    _test_linear_clear_zeroes_memory(&test);
    _test_linear_new_zero_capacity_returns_null(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "arena_stack");
    _test_stack_round_trip(&test);
    _test_stack_null_free_is_noop(&test);
    _test_stack_out_of_order_free(&test);
    _test_stack_header_size_agrees(&test);
    _test_stack_alloc_aborts_on_exhaustion(&test);
    _test_stack_new_zero_capacity_returns_null(&test);
    _test_stack_free_zeroes_reused_memory(&test);
    _test_stack_stride_alignment(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "arena_pool");
    _test_pool_alignment_and_reuse(&test);
    _test_pool_double_free_survives(&test);
    _test_pool_multi_block_round_trip(&test);
    _test_pool_run_interior_pointer_refused(&test);
    _test_pool_recycles_at_capacity(&test);
    _test_pool_stride_alignment(&test);
    _test_pool_free_zeroes_reused_memory(&test);
    _test_pool_null_free_is_noop(&test);
    _test_pool_alloc_aborts_when_full(&test);
    _test_pool_alloc_aborts_on_block_count_exceeds_capacity(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "arena facade");
    _test_arena_facade_size_overflow(&test);
    _test_arena_init_1_basic(&test);
    _test_facade_stack_deallocate_round_trip(&test);
    _test_facade_pool_dispatch_and_uninit(&test);
    _test_facade_refused_arena_allocator_seam_is_safe(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}