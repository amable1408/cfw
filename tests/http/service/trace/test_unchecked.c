#include <http/service/trace/trace.h>
#include <test/test.h>

/* Allocation-refusal suite for http_service_trace_exclude_add.
 *
 * str_alloc_init_static degrades to the EMPTY Str when the arena will not take the copy, and
 * appending that to exclude_paths is worse than dropping it: an empty prefix matches every
 * path, so one refused allocation would silently switch the access log off. This suite pins
 * that http_service_trace_exclude_add reports the refusal (false, list untouched) and that an
 * unrelated path keeps logging afterward - the caller-empty-string case is covered separately
 * as a value refusal, not an allocation one.
 *
 * Built WITHOUT ERROR_CHECK_ENABLED (this suite's makefile): with the checks armed, arena
 * exhaustion aborts inside arena_linear_alloc's bounds check before the caller ever sees a
 * false, so the refusal path is only reachable in the checks-off configuration. The heap path
 * still aborts on allocation failure rather than degrading; nothing here changes that. */

/* Small enough that a modest number of prefixes exhausts it, large enough that init succeeds. */
#define _ARENA_BYTES 2048

#define _PREFIX "/health/probe/endpoint/prefix"

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

// Bytes the service wrote for one request. The oracle for "is anything still being logged".
static USize _logged_bytes(HTTP_Service_Trace *const trace, FILE *const stream, char const *const path) {
    long const before = ftell(stream);

    http_service_trace_log(trace, "203.0.113.7", "GET", path, "HTTP/1.1", 200, 1024, 3);

    fflush(stream);

    long const after = ftell(stream);

    return (USize) (after > before ? after - before : 0);
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_refusal_is_reported_and_stores_nothing(Test *const test) {
    test_case_begin(test, "a refused exclude_add reports false and leaves the list untouched");

    Arena arena = arena_init_1(_ARENA_BYTES, ARENA_TYPE_LINEAR);
    FILE *const stream = tmpfile();

    /* Checked BEFORE the service is built on it: every assertion below reads through this
     * stream, so a failed tmpfile() would make the case undefined rather than red. */
    if (!test_expect_not_null(test, "the fixture stream opened", (void*) stream)) {
        arena_uninit(&arena, ARENA_TYPE_LINEAR);

        test_case_end(test);

        return;
    }

    HTTP_Service_Trace trace = http_service_trace_alloc_init_2(stream, HTTP_SERVICE_TRACE_FORMAT_COMMON, &arena);

    USize stored = 0;
    bool refused = false;

    for (USize i = 0; i < 4096 && !refused; i += 1) {
        char prefix[64] = DEFAULT_INITIALIZATION;

        char_format(prefix, sizeof(prefix), "%s/%zu", _PREFIX, i);

        USize const size_before = al_str_get_size(&trace.exclude_paths);

        if (http_service_trace_exclude_add(&trace, prefix)) {
            stored += 1;
        }
        else {
            refused = true;

            test_expect_u(test, "a refused add leaves the list exactly as it was", size_before, al_str_get_size(&trace.exclude_paths));
        }
    }

    /* Positive anchors: without these the case would still pass if exclude_add always
     * returned false, or if the arena refused on the very first call. */
    test_expect_true(test, "the arena did refuse within the loop", refused);
    test_expect_true(test, "prefixes were stored before the refusal", stored > 0);

    http_service_trace_uninit(&trace);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    fclose(stream);

    test_case_end(test);
}

static void _test_refusal_does_not_silence_the_log(Test *const test) {
    test_case_begin(test, "after a refusal an unrelated path is still logged");

    Arena arena = arena_init_1(_ARENA_BYTES, ARENA_TYPE_LINEAR);
    FILE *const stream = tmpfile();

    /* Checked BEFORE the service is built on it: every assertion below reads through this
     * stream, so a failed tmpfile() would make the case undefined rather than red. */
    if (!test_expect_not_null(test, "the fixture stream opened", (void*) stream)) {
        arena_uninit(&arena, ARENA_TYPE_LINEAR);

        test_case_end(test);

        return;
    }

    HTTP_Service_Trace trace = http_service_trace_alloc_init_2(stream, HTTP_SERVICE_TRACE_FORMAT_COMMON, &arena);

    /* An unrelated request logs normally before any refusal has happened. This is the
     * baseline the assertion below is measured against, and it is also the positive anchor:
     * if trace never logged anything, the post-refusal assertion would pass vacuously. */
    test_expect_true(test, "a request is logged before any refusal", _logged_bytes(&trace, stream, "/api/orders") > 0);

    /* Reaching the empty-Str path takes CARE, and getting it wrong makes this case prove
     * nothing. Simply exhausting the arena does not do it: once the arena is dry the list
     * growth fails too, so add_last declines and the entry never lands - the decline guard
     * masks the empty guard, and the suite passes with the empty guard deleted. That was
     * measured, not assumed.
     *
     * The window that does reach it is: the COPY refused while the LIST still has spare
     * capacity, so add_last needs no allocation and happily stores the empty Str. So: a few
     * short prefixes first, which leaves al_str grown with slots to spare, and then one
     * prefix longer than the entire arena, whose copy cannot possibly fit. */
    for (USize i = 0; i < 4; i += 1) {
        char prefix[64] = DEFAULT_INITIALIZATION;

        char_format(prefix, sizeof(prefix), "%s/%zu", _PREFIX, i);

        test_expect_true(test, "the short prefixes are stored", http_service_trace_exclude_add(&trace, prefix));
    }

    char oversized[_ARENA_BYTES * 2] = DEFAULT_INITIALIZATION;

    memory_set(oversized, sizeof(oversized) - 1, 'a');

    oversized[0]                        = '/';
    oversized[sizeof(oversized) - 1]    = '\0';

    USize const size_before = al_str_get_size(&trace.exclude_paths);
    bool  const refused     = !http_service_trace_exclude_add(&trace, oversized);

    test_expect_true(test, "a prefix larger than the arena is refused", refused);
    test_expect_u(test, "the refused prefix did not land in the list", size_before, al_str_get_size(&trace.exclude_paths));

    /* THE ASSERTION THIS SUITE EXISTS FOR. Before the guard, a refusal appended an empty Str,
     * and an empty prefix matches every path - so this request would have produced no output
     * at all and the access log would have gone silent for good. */
    test_expect_true(test, "an unrelated path is still logged after the refusal", _logged_bytes(&trace, stream, "/api/orders") > 0);

    http_service_trace_uninit(&trace);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    fclose(stream);

    test_case_end(test);
}

static void _test_exclusion_still_works(Test *const test) {
    test_case_begin(test, "with room to spare an added prefix is stored and suppresses its path");

    Arena arena = arena_init_1(64 * 1024, ARENA_TYPE_LINEAR);
    FILE *const stream = tmpfile();

    /* Checked BEFORE the service is built on it: every assertion below reads through this
     * stream, so a failed tmpfile() would make the case undefined rather than red. */
    if (!test_expect_not_null(test, "the fixture stream opened", (void*) stream)) {
        arena_uninit(&arena, ARENA_TYPE_LINEAR);

        test_case_end(test);

        return;
    }

    HTTP_Service_Trace trace = http_service_trace_alloc_init_2(stream, HTTP_SERVICE_TRACE_FORMAT_COMMON, &arena);

    test_expect_true(test, "an unrefused add returns true", http_service_trace_exclude_add(&trace, "/health"));
    test_expect_u(test, "the prefix is stored exactly once", 1, al_str_get_size(&trace.exclude_paths));

    test_expect_u(test, "the excluded path produces no output", 0, _logged_bytes(&trace, stream, "/health/live"));
    test_expect_true(test, "an unrelated path still produces output", _logged_bytes(&trace, stream, "/api/orders") > 0);

    http_service_trace_uninit(&trace);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    fclose(stream);

    test_case_end(test);
}

I32 main(void) {
    Test test = test_init("tests/http/service/trace/test_unchecked.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "http_service_trace allocation refusal");
    _test_refusal_is_reported_and_stores_nothing(&test);
    _test_refusal_does_not_silence_the_log(&test);
    _test_exclusion_still_works(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}