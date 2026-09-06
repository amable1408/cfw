#include <http/service/permission/permission.h>
#include <test/test.h>

/* Allocation-refusal suite for the permission service's add contract.
 *
 * WHY THIS EXISTS. The add_last decline sweep changed every http_service_permission_*_add
 * from `void` to `bool`, so a refused allocation now REPORTS rather than silently dropping
 * the entry. None of those branches had ever executed. That matters more here than in most
 * modules: `denies` is matched BEFORE the allow checks, so a marker that silently fails to
 * store does not degrade to "slightly less configured" - it PERMITS exactly what the caller
 * asked to forbid. The backlog this closes put it plainly: treat a passing build on those
 * paths as evidence of nothing.
 *
 * WHY AN ARENA AND NOT -Wl,--wrap=calloc. The first version of this file used the linker-wrap
 * injection that tests/dir and tests/benchmark use, and it did not test what it claimed. On
 * the HEAP path the refusal is unreachable: _char_to_str -> str_init_static -> memory_alloc,
 * and memory_alloc ABORTS on a null calloc rather than returning it, so the process dies
 * before al_str_add_last is ever entered. The wrap build proved that directly - it aborted in
 * memory_alloc at str.c:1806 on the very first injected ordinal.
 *
 * So the decline contract these functions now expose is, today, an ARENA contract. With an
 * arena the refusal is real and recoverable at both of its two sites:
 *   - str_alloc_init_static returns the EMPTY Str when the arena will not take the copy, and
 *   - al_str_add_last declines the growth and leaves the size alone.
 * Both are driven here by exhausting a deliberately small arena, which needs no link seam.
 *
 * WHAT THIS DOES NOT COVER, stated so the file does not overclaim: the heap path's behaviour
 * on allocation failure is still an abort inside memory_alloc, and no test here changes that.
 * Making the contract end-to-end means giving _char_to_str a memory_try_alloc-based path;
 * until then a heap-backed permission service does not degrade, it dies. */

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/

/* Small enough that a modest number of markers exhausts it, large enough that the service's
 * own init succeeds first. The exact figure is not load-bearing: the sweep below keeps adding
 * until the arena refuses, rather than assuming any particular marker count. */
#define _ARENA_BYTES 2048

// A marker long enough that each one takes a visible bite out of the arena.
#define _MARKER "wallet.write.permission.marker"

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_refusal_is_reported_and_stores_nothing(Test *const test) {
    test_case_begin(test, "an arena refusal reports false and leaves the deny list untouched");

    Arena                   arena       = arena_init_1(_ARENA_BYTES, ARENA_TYPE_LINEAR);
    HTTP_Service_Permission permission  = http_service_permission_alloc_init_1(&arena);

    /* Fill until the arena refuses. Every marker is distinct so the duplicate short-circuit
     * in _list_add never fires - a duplicate returns true without allocating, which would
     * end this loop on a success that proves nothing. */
    USize   stored  = 0;
    bool    refused = false;

    for (USize i = 0; i < 4096 && !refused; i += 1) {
        char marker[64] = DEFAULT_INITIALIZATION;

        char_format(marker, sizeof(marker), "%s.%zu", _MARKER, i);

        USize const size_before = al_str_get_size(&permission.denies);

        if (http_service_permission_deny_add(&permission, marker)) {
            stored += 1;

            test_expect_u(test, "a successful add grows the list by exactly one", size_before + 1, al_str_get_size(&permission.denies));
        }
        else {
            refused = true;

            /* The whole point of the round: a refusal must not half-land. */
            test_expect_u(test, "a refused add leaves the list exactly as it was", size_before, al_str_get_size(&permission.denies));
            test_expect_false(test, "the refused marker is not reported as present", http_service_permission_deny_has(&permission, marker));
        }
    }

    /* Two positive anchors. Without them this case would still pass if deny_add had been
     * broken into always returning false, or if the arena had refused from the very first
     * call - the all-negative shape that has produced vacuous green suites in this campaign. */
    test_expect_true(test, "the arena did refuse within the loop", refused);
    test_expect_true(test, "markers were stored before the refusal", stored > 0);

    http_service_permission_uninit(&permission);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_refusal_does_not_corrupt_later_reads(Test *const test) {
    test_case_begin(test, "the markers stored before a refusal remain readable and correct");

    Arena                   arena       = arena_init_1(_ARENA_BYTES, ARENA_TYPE_LINEAR);
    HTTP_Service_Permission permission  = http_service_permission_alloc_init_1(&arena);

    char    first[64]   = DEFAULT_INITIALIZATION;
    USize   stored      = 0;

    char_format(first, sizeof(first), "%s.first", _MARKER);

    test_expect_true(test, "the first marker is stored", http_service_permission_deny_add(&permission, first));

    for (USize i = 0; i < 4096; i += 1) {
        char marker[64] = DEFAULT_INITIALIZATION;

        char_format(marker, sizeof(marker), "%s.%zu", _MARKER, i);

        if (!http_service_permission_deny_add(&permission, marker)) {
            break;
        }

        stored += 1;
    }

    /* A partial append would have desynchronised the list from its own size, and this is what
     * that looks like from the reading side: the entry added BEFORE the refusal must still be
     * there and still match by value. */
    test_expect_true(test, "the pre-refusal marker survives the refusal", http_service_permission_deny_has(&permission, first));
    test_expect_u(test, "the list length matches what was accepted", stored + 1, al_str_get_size(&permission.denies));

    http_service_permission_uninit(&permission);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_success_path_is_unaffected(Test *const test) {
    test_case_begin(test, "with room to spare the add still succeeds and is visible");

    Arena                   arena       = arena_init_1(64 * 1024, ARENA_TYPE_LINEAR);
    HTTP_Service_Permission permission  = http_service_permission_alloc_init_1(&arena);

    test_expect_true(test, "an unrefused add returns true", http_service_permission_deny_add(&permission, _MARKER));
    test_expect_u(test, "the marker is stored exactly once", 1, al_str_get_size(&permission.denies));
    test_expect_true(test, "the marker is reported as present", http_service_permission_deny_has(&permission, _MARKER));

    /* A duplicate is success without a second entry - the contract the early return states. */
    test_expect_true(test, "a duplicate add reports success", http_service_permission_deny_add(&permission, _MARKER));
    test_expect_u(test, "a duplicate add does not grow the list", 1, al_str_get_size(&permission.denies));

    http_service_permission_uninit(&permission);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

I32 main(void) {
    Test test = test_init("./test_oom.c");

    test_verbose_set(&test, false);

    test_suite_begin(&test, "http_service_permission allocation refusal");
    _test_refusal_is_reported_and_stores_nothing(&test);
    _test_refusal_does_not_corrupt_later_reads(&test);
    _test_success_path_is_unaffected(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}