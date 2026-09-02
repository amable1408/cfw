#include <allocator/allocator.h>
#include <arena/arena.h>
#include <arena/arena_linear.h>
#include <test/test.h>

/* allocator.h had no suite at all: the vtable dispatch (heap vs arena, and the
 * null-hook no-op branches inside each) was unverified. Each case below pins one branch of
 * allocator_borrow/allocator_release/allocator_try_borrow so a regression fails here first. */

static bool _all_zero(U8 const *const buffer, USize const byte_count) {
    for (USize i = 0; i < byte_count; i += 1) {
        if (buffer[i] != 0) {
            return false;
        }
    }

    return true;
}

static void _test_borrow_heap(Test *const test) {
    test_case_begin(test, "allocator_borrow heap path");

    void *const buffer = allocator_borrow(64, nullptr);

    test_expect_true(test, "heap borrow non-null", !memory_empty(buffer));
    /* memory_alloc's always-zeroed guarantee is a framework contract, not an accident of this
     * one call - the facade must not disturb it on the way through. */
    test_expect_true(test, "heap borrow is zeroed", _all_zero((U8 const*) buffer, 64));

    allocator_release(buffer, nullptr);

    test_case_end(test);
}

static void _test_borrow_arena(Test *const test) {
    test_case_begin(test, "allocator_borrow arena path");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    void *const first = allocator_borrow(32, &arena);

    test_expect_true(test, "arena borrow non-null", !memory_empty(first));
    test_expect_true(test, "arena borrow is zeroed", _all_zero((U8 const*) first, 32));

    void *const second = allocator_borrow(32, &arena);

    test_expect_true(test, "a second borrow does not overlap the first", second != first);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_borrow_null_allocate_hook(Test *const test) {
    test_case_begin(test, "allocator_borrow with a null allocate hook is a safe no-op");

    /* A non-null Arena* whose allocate hook is null: allocator_borrow must detect the missing
     * hook and hand back nullptr instead of calling through a null function pointer. */
    Arena const no_hooks = {
        .allocate = nullptr,
        .deallocate = nullptr,
        .handler = nullptr,
        .try_allocate = nullptr,
    };

    test_expect_true(test, "borrow through a null allocate hook returns nullptr",
        memory_empty(allocator_borrow(16, (Arena*) &no_hooks)));

    test_case_end(test);
}

static void _test_release_null_safety(Test *const test) {
    test_case_begin(test, "allocator_release is null-safe on both paths");

    /* Documented contract: a null buffer is ignored, like free(NULL). Reachable in practice - a
     * container created and never grown holds data == nullptr, and every al_*_uninit forwards it
     * here unguarded. Both the heap and arena paths must survive it without touching a hook. */
    allocator_release(nullptr, nullptr);

    Arena arena = arena_init_1(64, ARENA_TYPE_LINEAR);

    allocator_release(nullptr, &arena);

    test_expect_true(test, "arena untouched by a null-buffer release", arena_linear_empty((ArenaLinear*) arena.handler));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_expect_true(test, "reached this point without aborting", true);

    test_case_end(test);
}

static void _test_release_heap_round_trip(Test *const test) {
    test_case_begin(test, "allocator_release heap round trip");

    void *const buffer = allocator_borrow(48, nullptr);

    test_expect_true(test, "buffer allocated", !memory_empty(buffer));

    memory_set(buffer, 48, 0xAB);

    allocator_release(buffer, nullptr);

    test_expect_true(test, "release completed without aborting", true);

    test_case_end(test);
}

static void _test_release_arena_round_trip(Test *const test) {
    test_case_begin(test, "allocator_release arena path forwards to the arena's own deallocate");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    void *const buffer = allocator_borrow(32, &arena);

    test_expect_true(test, "buffer allocated", !memory_empty(buffer));

    /* arena_linear_free is a documented no-op (a linear arena cannot free individual blocks), so
     * the only observable contract here is that the call reaches the hook and does not crash or
     * corrupt the block. */
    memory_set(buffer, 32, 0xCD);

    allocator_release(buffer, &arena);

    test_expect_true(test, "linear arena's no-op release left the block intact", *(U8*) buffer == 0xCD);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_release_null_deallocate_hook(Test *const test) {
    test_case_begin(test, "allocator_release with a null deallocate hook is a safe no-op");

    Arena const no_hooks = {
        .allocate = nullptr,
        .deallocate = nullptr,
        .handler = nullptr,
        .try_allocate = nullptr,
    };

    /* A genuine heap block, deliberately NOT owned by no_hooks - this pins that a null
     * deallocate hook leaves the buffer untouched rather than calling through a null pointer.
     * Freed by hand afterward via memory_free, bypassing the facade, so the block does not leak. */
    void *const buffer = memory_alloc(16);

    test_expect_true(test, "buffer allocated", !memory_empty(buffer));

    memory_set(buffer, 16, 0xEF);

    allocator_release(buffer, (Arena*) &no_hooks);

    test_expect_true(test, "null deallocate hook left the block untouched", *(U8*) buffer == 0xEF);

    memory_free(buffer);

    test_case_end(test);
}

static void _test_try_borrow_heap(Test *const test) {
    test_case_begin(test, "allocator_try_borrow heap path");

    void *const buffer = allocator_try_borrow(64, nullptr);

    test_expect_true(test, "heap try_borrow non-null", !memory_empty(buffer));
    test_expect_true(test, "heap try_borrow is zeroed", _all_zero((U8 const*) buffer, 64));

    /* memory_try_alloc's documented boundary case: byte_count == 0 must be a rejection, not a
     * zero-sized allocation, and never an abort. */
    test_expect_true(test, "zero-sized heap request refused", memory_empty(allocator_try_borrow(0, nullptr)));

    allocator_release(buffer, nullptr);

    test_case_end(test);
}

static void _test_try_borrow_arena(Test *const test) {
    test_case_begin(test, "allocator_try_borrow arena path, including exhaustion");

    Arena arena = arena_init_1(64, ARENA_TYPE_LINEAR);

    void *const first = allocator_try_borrow(32, &arena);

    test_expect_true(test, "in-budget request succeeds", !memory_empty(first));

    /* Exhaustion must be a null, never an abort - this is the whole point of the try_ twin: a
     * request size that came from outside the program (a body, a header) degrades into a
     * rejection instead of ending the process. */
    test_expect_true(test, "over-budget request is refused, not aborted", memory_empty(allocator_try_borrow(4096, &arena)));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_try_borrow_null_try_allocate_hook(Test *const test) {
    test_case_begin(test, "allocator_try_borrow with a null try_allocate hook is a safe no-op");

    Arena const no_hooks = {
        .allocate = nullptr,
        .deallocate = nullptr,
        .handler = nullptr,
        .try_allocate = nullptr,
    };

    test_expect_true(test, "try_borrow through a null try_allocate hook returns nullptr",
        memory_empty(allocator_try_borrow(16, (Arena*) &no_hooks)));

    test_case_end(test);
}

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("./test_all.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "allocator_borrow");
    _test_borrow_heap(&test);
    _test_borrow_arena(&test);
    _test_borrow_null_allocate_hook(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "allocator_release");
    _test_release_null_safety(&test);
    _test_release_heap_round_trip(&test);
    _test_release_arena_round_trip(&test);
    _test_release_null_deallocate_hook(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "allocator_try_borrow");
    _test_try_borrow_heap(&test);
    _test_try_borrow_arena(&test);
    _test_try_borrow_null_try_allocate_hook(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}