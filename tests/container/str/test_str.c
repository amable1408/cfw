#include <string.h>

#include <arena/arena.h>
#include <container/str/str.h>
#include <memory/memory.h>
#include <process/process.h>
#include <test/test.h>

/*
 * First suite for CFW's Str container (zero coverage before this file), pinning the
 * Block E fix set:
 *
 *   1. str_replace_2's value-dependent refusal (the old error_check `>=` bound forbade
 *      replacing the FINAL byte, and compiled away entirely in unchecked builds,
 *      leaving a caller-sized unbounded overwrite). Legal: index + data_size <= size.
 *   2. str_slice's empty tail (index == size is a legal VALUE, the empty Str).
 *   3. str_slice_range's INCLUSIVE [from, to] (str_erase is EXCLUSIVE [from, to) -
 *      the two families differ deliberately).
 *   4. str_at's char_at parity: '\0' for at-or-past-the-end, never an abort.
 *   5. str_add_2's overflow guard (data_size near USIZE_MAX refuses, never wraps).
 *   6. str_move_3's cross-allocator REFUSAL (a real no-op leaving BOTH objects
 *      usable - the old error_check aborted, and only after destroying self).
 *   7. The OWNERSHIP MATRIX: views (init_2/init_3) never release the caller's
 *      buffer; copies and producers own theirs; str_move_2 adopts; arena twins
 *      carry their arena and release in bulk; a REFUSED arena degrades to the
 *      empty Str on every alloc_* constructor.
 *   8. str_repeat count == 0 RELEASES the buffer (data == nullptr) - the documented
 *      str-vs-string difference.
 *
 * str_move_1(&s, &null_ptr) must abort (error_check_null on *data BEFORE the
 * char_length deref); an abort kills the runner, so it is observed via the
 * subprocess probe pattern from tests/char/test_char_add.c.
 */

/*==============================================================================
 * MARK: - Anti-Vacuity Counters
 *============================================================================*/
/** Closed-form number of case functions below; the final case asserts every one ran. */
#define _EXPECTED_CASE_COUNT 37

/** Incremented at the top of every case function; a case that silently never runs
 * (or a dispatcher edit that drops one) fails the closed-form check. */
static USize _case_entered_count = 0;

/** str_at parity sweep bookkeeping: total probes, in-range matches (positive
 * anchor), and out-of-range '\0' answers (negative anchor). */
static USize _at_probe_count = 0;
static USize _at_in_range_match_count = 0;
static USize _at_past_end_zero_count = 0;

/** Path this binary was invoked with, reused when spawning the abort-probe child. */
static char const *_program = nullptr;

/*==============================================================================
 * MARK: - Abort-Probe Child Mode (str_move_1 null diagnostics abort the process)
 *============================================================================*/
/**
 * @brief error_check_null ABORTS the process, so str_move_1's *data == nullptr
 * diagnostic cannot be observed in-process. Spawned by
 * _test_move_1_null_abort_probe with --child-move-null; a clean return is the bug.
 * @return Exit code observed only if the check failed to abort (the bug case).
 */
static I32 _child_move_null(void) {
    log_init((LogConfig) {
        .level             = LOG_LEVEL_ERROR,
        .stream            = LOG_STREAM_STDOUT,
        .timestamp_enabled = true,
        .autoflush         = true
    });

    Str destination = str_init_static("x", 1);
    char *null_pointer = nullptr;

    /* Must abort on the *data null check BEFORE char_length dereferences it. */
    str_move_1(&destination, &null_pointer);

    str_uninit(&destination);

    return 0;
}

/*==============================================================================
 * MARK: - Cases: str_replace_2 Bounds (R1.1)
 *============================================================================*/
static void _test_replace_2_bounds(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_replace_2: last byte and replace-to-end are legal; every out-of-range pair is a NO-OP (never an abort, never an overwrite)");

    /* The exact off-by-one the old `>=` bound forbade: index == size - 1 with
     * data_size == 1 replaces the FINAL byte. */
    Str last = str_init_static("abcd", 4);

    str_replace_2(&last, "Z", 1, 3);

    test_expect_string(test, "final-byte replace (index == size-1, data_size == 1) -> \"abcZ\"", "abcZ", str_get_data(&last));
    test_expect_u(test, "final-byte replace leaves size unchanged", 4, str_get_size(&last));

    str_uninit(&last);

    /* Replace-to-end: data_size == size - index exactly fills the tail. */
    Str tail = str_init_static("abcd", 4);

    str_replace_2(&tail, "XYZ", 3, 1);

    test_expect_string(test, "replace-to-end (data_size == size - index) -> \"aXYZ\"", "aXYZ", str_get_data(&tail));

    str_uninit(&tail);

    /* Over-long data_size: index + data_size > size must be a NO-OP in this build
     * (ERROR_CHECK_ENABLED is on) - content untouched, no abort. */
    Str overlong = str_init_static("abcd", 4);

    str_replace_2(&overlong, "WWW", 3, 2);

    test_expect_string(test, "over-long data_size (2 + 3 > 4) is a NO-OP, content unchanged", "abcd", str_get_data(&overlong));

    /* index == size and index > size: both refused, both no-ops. */
    str_replace_2(&overlong, "Q", 1, 4);

    test_expect_string(test, "index == size is a NO-OP", "abcd", str_get_data(&overlong));

    str_replace_2(&overlong, "Q", 1, 100);

    test_expect_string(test, "index > size is a NO-OP", "abcd", str_get_data(&overlong));

    /* data_size == 0: writing nothing is a no-op, not an error. */
    str_replace_2(&overlong, "Q", 0, 1);

    test_expect_string(test, "data_size == 0 is a NO-OP", "abcd", str_get_data(&overlong));
    test_expect_u(test, "size survived the whole refusal gauntlet", 4, str_get_size(&overlong));

    str_uninit(&overlong);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: str_slice Empty Tail + Owned Copy (R1.2)
 *============================================================================*/
static void _test_slice_empty_tail(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_slice: index == size yields the empty Str; a mid-slice is an OWNED, independent copy");

    Str source = str_init_static("hello", 5);

    /* The tail after the last delimiter: index == size is a VALUE, not caller error. */
    Str tail = str_slice(&source, 5);

    test_expect_u(test, "slice at size has size 0", 0, str_get_size(&tail));
    test_expect_null(test, "slice at size carries no buffer", str_get_data(&tail));
    test_expect_false(test, "slice at size owns nothing", tail.owned);

    str_uninit(&tail);

    /* Slicing an empty Str at 0 is the same empty value. */
    Str empty_source = str_init_1();
    Str empty_slice = str_slice(&empty_source, 0);

    test_expect_u(test, "slice of the empty Str at 0 is empty", 0, str_get_size(&empty_slice));

    str_uninit(&empty_slice);

    /* A mid-slice is an OWNED copy: mutating it must not touch the source. */
    Str middle = str_slice(&source, 2);

    test_expect_string(test, "mid-slice content is the tail copy \"llo\"", "llo", str_get_data(&middle));
    test_expect_true(test, "mid-slice is OWNED (unlike char_slice_*, which borrows)", middle.owned);

    str_replace_2(&middle, "LLO", 3, 0);

    test_expect_string(test, "mutating the slice changed the slice", "LLO", str_get_data(&middle));
    test_expect_string(test, "mutating the slice left the SOURCE untouched", "hello", str_get_data(&source));

    str_uninit(&middle);
    str_uninit(&source);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: str_slice_range Inclusive (R1.3)
 *============================================================================*/
static void _test_slice_range_inclusive(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_slice_range: INCLUSIVE [from, to] - (0,2) of \"abcd\" is \"abc\", from == to is one character, to == size-1 reaches the last byte");

    Str source = str_init_static("abcd", 4);

    Str head = str_slice_range(&source, 0, 2);

    test_expect_string(test, "(0,2) of \"abcd\" keeps BOTH endpoints -> \"abc\"", "abc", str_get_data(&head));
    test_expect_u(test, "(0,2) has 3 bytes, not 2", 3, str_get_size(&head));
    test_expect_true(test, "range slice is OWNED", head.owned);

    Str single = str_slice_range(&source, 1, 1);

    test_expect_string(test, "from == to is the legal single-character slice -> \"b\"", "b", str_get_data(&single));
    test_expect_u(test, "single-character slice has size 1", 1, str_get_size(&single));

    Str last = str_slice_range(&source, 2, 3);

    test_expect_string(test, "to == size-1 reaches the last byte -> \"cd\"", "cd", str_get_data(&last));

    str_uninit(&head);
    str_uninit(&single);
    str_uninit(&last);
    str_uninit(&source);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: str_at char Parity (R1.4)
 *============================================================================*/
static void _test_at_parity(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_at: char_at parity - in-range answers the byte, at-size / far-past / empty all answer '\\0', never an abort");

    char const *const expected = "abcd";
    Str source = str_init_static(expected, 4);

    /* Differential sweep with a closed-form probe count: indices 0..9 over a
     * 4-byte Str give exactly 4 in-range matches and 6 out-of-range '\0's. */
    for (USize index = 0; index < 10; index += 1) {
        char const actual = str_at(&source, index);

        _at_probe_count += 1;

        if (index < 4) {
            if (actual == expected[index]) {
                _at_in_range_match_count += 1;
            }
        }
        else if (actual == '\0') {
            _at_past_end_zero_count += 1;
        }
    }

    test_expect_u(test, "sweep probe count is the closed form (10)", 10, _at_probe_count);
    test_expect_u(test, "positive anchor: all 4 in-range probes matched their byte", 4, _at_in_range_match_count);
    test_expect_u(test, "negative anchor: all 6 at/past-end probes answered '\\0'", 6, _at_past_end_zero_count);

    str_uninit(&source);

    /* The empty Str carries data == nullptr; index 0 is at-the-end and must
     * still answer '\0' rather than dereference. */
    Str empty = str_init_1();

    test_expect_i(test, "str_at on the empty Str answers '\\0'", 0, (ISize) str_at(&empty, 0));

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: str_erase Exclusive (R1.5)
 *============================================================================*/
static void _test_erase_exclusive(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_erase: EXCLUSIVE [from, to) - [1,3) of \"abcd\" leaves \"ad\", [0,size) empties");

    Str middle = str_init_static("abcd", 4);

    str_erase(&middle, 1, 3);

    test_expect_u(test, "[1,3) removed exactly 2 bytes -> size 2", 2, str_get_size(&middle));
    test_expect_i(test, "byte 0 is 'a'", (ISize) 'a', (ISize) str_at(&middle, 0));
    test_expect_i(test, "byte 1 is 'd' (to == 3 is EXCLUSIVE, so 'd' survives)", (ISize) 'd', (ISize) str_at(&middle, 1));
    test_expect_string(test, "content is \"ad\"", "ad", str_get_data(&middle));

    str_uninit(&middle);

    Str whole = str_init_static("abcd", 4);

    str_erase(&whole, 0, 4);

    test_expect_u(test, "[0,size) empties the Str", 0, str_get_size(&whole));
    test_expect_true(test, "str_empty agrees", str_empty(&whole));

    str_uninit(&whole);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: str_add_2 Overflow Guard (R1.6)
 *============================================================================*/
static void _test_add_overflow_guard(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_add_2: a data_size near USIZE_MAX is refused as a NO-OP (never wraps the borrow small), no abort");

    Str source = str_init_static("abc", 3);

    /* size(3) + data_size + terminator(1) would wrap USize; the guard refuses
     * BEFORE the borrow, so content, size, and the buffer all survive. */
    str_add_2(&source, "x", USIZE_MAX - 3, 0);

    test_expect_u(test, "size unchanged after the refused add", 3, str_get_size(&source));
    test_expect_string(test, "content unchanged after the refused add", "abc", str_get_data(&source));

    /* The object stays fully usable: a normal add still works afterwards. */
    str_add_last_1(&source, "d");

    test_expect_string(test, "the refused object is still usable (\"abc\" + \"d\")", "abcd", str_get_data(&source));

    str_uninit(&source);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: str_move_3 Cross-Allocator Refusal (R1.7)
 *============================================================================*/
static void _test_move_3_cross_allocator(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_move_3: crossing allocators is REFUSED as a real no-op leaving BOTH objects intact; same-allocator move transfers and nulls the source");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    /* Arena-backed source into a heap destination: allocators differ, so the move
     * must refuse BEFORE uninit-ing the destination (the old error_check aborted,
     * and only after str_uninit had already destroyed self). */
    Str *arena_source = str_alloc_new_static("arena", 5, &arena);
    Str heap_destination = str_init_static("heap", 4);

    str_move_3(&heap_destination, &arena_source);

    test_expect_not_null(test, "refused move did NOT null the source pointer", arena_source);
    test_expect_string(test, "source content intact after the refusal", "arena", str_get_data(arena_source));
    test_expect_string(test, "destination content intact after the refusal (uninit never ran)", "heap", str_get_data(&heap_destination));
    test_expect_u(test, "destination size intact", 4, str_get_size(&heap_destination));

    /* Both objects must still tear down cleanly through their OWN allocators. */
    str_uninit(&heap_destination);
    str_delete(&arena_source);

    test_expect_null(test, "str_delete nulled the arena-backed source", arena_source);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    /* Same-allocator (both heap) move: buffer transfers, source is emptied and
     * the caller's pointer nulled. */
    Str moved_source = str_init_static("mv", 2);
    Str *moved_source_pointer = &moved_source;
    Str destination = str_init_1();

    str_move_3(&destination, &moved_source_pointer);

    test_expect_null(test, "same-allocator move nulled the caller's source pointer", moved_source_pointer);
    test_expect_string(test, "destination received the buffer", "mv", str_get_data(&destination));
    test_expect_true(test, "ownership transferred with the buffer", destination.owned);
    test_expect_null(test, "source struct was emptied (no double claim)", moved_source.data);
    test_expect_u(test, "source size cleared", 0, str_get_size(&moved_source));

    str_uninit(&destination);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: str_move_1 Null Diagnostics (R1.8, subprocess probe)
 *============================================================================*/
static void _test_move_1_null_abort_probe(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_move_1(&s, &null_ptr) ABORTS on the *data check BEFORE char_length dereferences it (subprocess probe)");

    char const *const argv_vector[] = { _program, "--child-move-null", nullptr };
    ProcessSpec const spec = { .argv = argv_vector, .timeout_milliseconds = 5000 };
    ProcessOutcome outcome = DEFAULT_INITIALIZATION;

    Result const result = process_run(spec, &outcome);

    test_expect_true(test, "process_run reports no OS-level error", result_is_success(result));
    test_expect_true(test, "child did not time out", !outcome.timed_out);
    test_expect_true(test, "child aborted (nonzero exit) instead of dereferencing null", outcome.exit_code != 0);
    test_expect_string_contains(test, "abort logged the null-pointer category", outcome.output, "NULL_POINTER");

    process_outcome_uninit(&outcome);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Ownership Matrix - Views (R1.9)
 *============================================================================*/
static void _test_ownership_views(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "OWNERSHIP: str_init_2/str_init_3 build VIEWS - owned == false, and str_uninit never frees the caller's buffer");

    char stack_buffer[8] = "view";

    Str view_3 = str_init_3(stack_buffer, 4);

    test_expect_false(test, "str_init_3 view: owned == false", view_3.owned);
    test_expect_true(test, "str_init_3 view aliases the caller's buffer (no copy)", str_get_data(&view_3) == stack_buffer);

    str_uninit(&view_3);

    test_expect_string(test, "the stack buffer still reads its original bytes after uninit (nothing was freed)", "view", stack_buffer);
    test_expect_null(test, "uninit cleared the view's pointer (no dangling borrow)", view_3.data);

    Str view_2 = str_init_2(stack_buffer);

    test_expect_false(test, "str_init_2 view: owned == false", view_2.owned);
    test_expect_u(test, "str_init_2 sized itself from the terminator", 4, str_get_size(&view_2));

    str_uninit(&view_2);

    test_expect_string(test, "the stack buffer survived the second view's uninit too", "view", stack_buffer);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Ownership Matrix - Static Copy (R1.9)
 *============================================================================*/
static void _test_ownership_static_copy(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "OWNERSHIP: str_init_static is an OWNED copy - mutating the Str leaves the source untouched, uninit clean");

    char source_buffer[8] = "src";

    Str copy = str_init_static(source_buffer, 3);

    test_expect_true(test, "str_init_static: owned == true", copy.owned);
    test_expect_true(test, "the copy does NOT alias the source", str_get_data(&copy) != source_buffer);
    test_expect_string(test, "the copy carries the source bytes", "src", str_get_data(&copy));

    str_fill(&copy, 'x');

    test_expect_string(test, "mutating the copy changed the copy", "xxx", str_get_data(&copy));
    test_expect_string(test, "mutating the copy left the SOURCE untouched", "src", source_buffer);

    str_uninit(&copy);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Ownership Matrix - Producers (R1.9, R1.11 heap twin)
 *============================================================================*/
static void _test_ownership_producers(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "OWNERSHIP: producers (format, repeat, from_numbers_*, from_trim) return OWNED buffers that uninit cleanly");

    Str formatted = str_format("n=%d", 42);

    test_expect_true(test, "str_format: owned == true", formatted.owned);
    test_expect_string(test, "str_format content", "n=42", str_get_data(&formatted));
    test_expect_u(test, "str_format size", 4, str_get_size(&formatted));

    str_uninit(&formatted);

    Str repeated = str_init_static("ab", 2);

    str_repeat(&repeated, 3);

    test_expect_true(test, "str_repeat: owned == true", repeated.owned);
    test_expect_string(test, "str_repeat content", "ababab", str_get_data(&repeated));

    str_uninit(&repeated);

    Str from_int = str_from_numbers_int_1(-42);

    test_expect_true(test, "str_from_numbers_int_1: owned == true", from_int.owned);
    test_expect_string(test, "str_from_numbers_int_1(-42)", "-42", str_get_data(&from_int));

    str_uninit(&from_int);

    Str from_uint = str_from_numbers_uint_2(7, 3);

    test_expect_true(test, "str_from_numbers_uint_2: owned == true", from_uint.owned);
    test_expect_string(test, "str_from_numbers_uint_2(7, padding 3) is additive -> \"0007\"", "0007", str_get_data(&from_uint));

    str_uninit(&from_uint);

    Str from_float = str_from_numbers_float_2(1.5, 2);

    test_expect_true(test, "str_from_numbers_float_2: owned == true", from_float.owned);
    test_expect_string(test, "str_from_numbers_float_2(1.5, 2)", "1.50", str_get_data(&from_float));

    str_uninit(&from_float);

    Str padded = str_init_static("  hi  ", 6);
    Str trimmed = str_from_trim(&padded);

    test_expect_true(test, "str_from_trim: owned == true", trimmed.owned);
    test_expect_string(test, "str_from_trim content", "hi", str_get_data(&trimmed));
    test_expect_string(test, "str_from_trim left the source untouched", "  hi  ", str_get_data(&padded));

    str_uninit(&trimmed);
    str_uninit(&padded);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Ownership Matrix - str_move_2 Adoption (R1.9)
 *============================================================================*/
static void _test_ownership_move_2(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "OWNERSHIP: str_move_2 ADOPTS a heap buffer - owned == true, source pointer nulled, uninit frees exactly once");

    char *heap_buffer = memory_alloc(4);

    heap_buffer[0] = 'a';
    heap_buffer[1] = 'b';
    heap_buffer[2] = 'c';
    heap_buffer[3] = '\0';

    Str adopted = str_init_1();

    str_move_2(&adopted, &heap_buffer, 3);

    test_expect_true(test, "str_move_2: owned == true (the move takes ownership)", adopted.owned);
    test_expect_null(test, "the source pointer was nulled by the move", heap_buffer);
    test_expect_string(test, "the adopted buffer's content", "abc", str_get_data(&adopted));
    test_expect_u(test, "the adopted size", 3, str_get_size(&adopted));

    /* Exactly one release: the memory hooks' leak accounting catches both a leak
     * (never freed) and a double free (heap_buffer is null, so no second path). */
    str_uninit(&adopted);

    test_expect_null(test, "uninit cleared the adopted pointer", adopted.data);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Ownership Matrix - Arena Twins (R1.9, R1.11)
 *============================================================================*/
static void _test_arena_twins(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "OWNERSHIP: arena twins carry their arena and release in BULK - construct, assert, arena_uninit, no per-object free");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    Str from_static = str_alloc_init_static("hello", 5, &arena);

    test_expect_true(test, "str_alloc_init_static: owned == true", from_static.owned);
    test_expect_true(test, "str_alloc_init_static carries the arena it borrowed from", from_static.allocator == &arena);
    test_expect_string(test, "str_alloc_init_static content", "hello", str_get_data(&from_static));

    Str formatted = str_alloc_format(&arena, "a%db", 7);

    test_expect_true(test, "str_alloc_format: owned == true", formatted.owned);
    test_expect_string(test, "str_alloc_format content (happy path into a real arena)", "a7b", str_get_data(&formatted));
    test_expect_u(test, "str_alloc_format size", 3, str_get_size(&formatted));

    Str from_int = str_alloc_from_numbers_int_1(-42, &arena);

    test_expect_string(test, "str_alloc_from_numbers_int_1(-42)", "-42", str_get_data(&from_int));
    test_expect_true(test, "str_alloc_from_numbers_int_1: owned == true", from_int.owned);

    Str from_uint = str_alloc_from_numbers_uint_2(7, 3, &arena);

    test_expect_string(test, "str_alloc_from_numbers_uint_2(7, padding 3)", "0007", str_get_data(&from_uint));

    /* Bulk release: the arena reclaims every borrow at once; no str_uninit per
     * object is needed, and none may crash afterwards. */
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_expect_true(test, "arena_uninit released everything in bulk without a crash", true);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Refused-Arena Degradation (R1.10)
 *============================================================================*/
static void _test_refused_arena_degradation(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "REFUSED arena: every alloc_* constructor degrades to the empty Str - no abort, no null-deref");

    /* Degenerate geometry (byte_size 0) leaves the handler null: the documented
     * refused-arena state whose allocator_borrow answers null gracefully. */
    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    test_expect_null(test, "degenerate init left a null handler (the refusal actually happened)", refused.handler);

    Str from_static = str_alloc_init_static("hello", 5, &refused);

    test_expect_u(test, "str_alloc_init_static on a refused arena is the empty Str (size 0)", 0, str_get_size(&from_static));
    test_expect_null(test, "str_alloc_init_static on a refused arena carries no buffer", from_static.data);
    test_expect_false(test, "str_alloc_init_static on a refused arena owns nothing", from_static.owned);

    Str formatted = str_alloc_format(&refused, "x%d", 1);

    test_expect_u(test, "str_alloc_format on a refused arena is the empty Str", 0, str_get_size(&formatted));
    test_expect_null(test, "str_alloc_format on a refused arena carries no buffer", formatted.data);

    Str from_int = str_alloc_from_numbers_int_1(5, &refused);

    test_expect_u(test, "str_alloc_from_numbers_int_1 on a refused arena is the empty Str", 0, str_get_size(&from_int));
    test_expect_null(test, "str_alloc_from_numbers_int_1 on a refused arena carries no buffer", from_int.data);

    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: str_repeat count == 0 (R1.12)
 *============================================================================*/
static void _test_repeat_zero_releases(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_repeat(count == 0): the buffer is RELEASED (data == nullptr, size 0) - the documented str-vs-string difference");

    Str source = str_init_static("abc", 3);

    test_expect_not_null(test, "precondition: the owned source holds a buffer", source.data);

    str_repeat(&source, 0);

    test_expect_null(test, "count == 0 RELEASED the buffer (data == nullptr)", source.data);
    test_expect_u(test, "count == 0 left size 0", 0, str_get_size(&source));
    test_expect_false(test, "nothing left to own", source.owned);

    /* Idempotent tear-down: uninit on the already-released object is a no-op. */
    str_uninit(&source);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: _str_new Refused-Arena Guard (memsec round 2, item 1)
 *============================================================================*/
static void _test_alloc_new_refused_nullptr(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_alloc_new_* on a REFUSED arena returns nullptr instead of segfaulting (the memsec HIGH - _str_new used to deref the refused borrow)");

    Arena refused = arena_init_2(0, 0, ARENA_TYPE_LINEAR);

    test_expect_null(test, "degenerate init left a null handler (the refusal actually happened)", refused.handler);

    Str *const from_new_1 = str_alloc_new_1(&refused);

    test_expect_null(test, "str_alloc_new_1 on a refused arena is nullptr, not a crash", from_new_1);

    Str *const from_new_2 = str_alloc_new_2("x", &refused);

    test_expect_null(test, "str_alloc_new_2 propagates the nullptr", from_new_2);

    Str *const from_new_static = str_alloc_new_static("x", 1, &refused);

    test_expect_null(test, "str_alloc_new_static propagates the nullptr", from_new_static);

    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: str_add_2 Index Refusal (memsec round 2, item 2)
 *============================================================================*/
static void _test_add_2_index_refusal(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_add_2: index past the end (size+1, CHAR_NPOS) is a NO-OP; index == size (append at end) stays legal");

    Str source = str_init_static("abc", 3);

    str_add_2(&source, "x", 1, 4);

    test_expect_string(test, "index == size + 1 is a NO-OP, content unchanged", "abc", str_get_data(&source));
    test_expect_u(test, "index == size + 1 left size unchanged", 3, str_get_size(&source));

    str_add_2(&source, "x", 1, CHAR_NPOS);

    test_expect_string(test, "index == CHAR_NPOS (an unchecked find result) is a NO-OP", "abc", str_get_data(&source));

    str_add_2(&source, "d", 1, 3);

    test_expect_string(test, "index == size still appends -> \"abcd\"", "abcd", str_get_data(&source));
    test_expect_u(test, "the append grew size to 4", 4, str_get_size(&source));

    str_uninit(&source);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: str_erase Span Refusal (memsec round 2, item 3)
 *============================================================================*/
static void _test_erase_span_refusal(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_erase: from > to and to > size are NO-OPS; [size, size) is the LEGAL empty erase; a normal erase still works");

    Str source = str_init_static("abcd", 4);

    str_erase(&source, 5, 2);

    test_expect_string(test, "from > to is a NO-OP, content unchanged", "abcd", str_get_data(&source));

    str_erase(&source, 0, 5);

    test_expect_string(test, "to == size + 1 is a NO-OP", "abcd", str_get_data(&source));
    test_expect_u(test, "size survived both refusals", 4, str_get_size(&source));

    str_erase(&source, 4, 4);

    test_expect_string(test, "[size, size) is the legal empty erase at the end - a no-op, not an abort", "abcd", str_get_data(&source));

    str_erase(&source, 1, 2);

    test_expect_string(test, "a normal erase [1,2) still works -> \"acd\"", "acd", str_get_data(&source));
    test_expect_u(test, "the normal erase shrank size to 3", 3, str_get_size(&source));

    str_uninit(&source);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: str_remove Index Refusal (memsec round 2, item 4)
 *============================================================================*/
static void _test_remove_index_refusal(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_remove: index == size and the empty Str are NO-OPS, no abort; a normal remove still works");

    Str source = str_init_static("abc", 3);

    str_remove(&source, 3);

    test_expect_string(test, "index == size is a NO-OP, content unchanged", "abc", str_get_data(&source));
    test_expect_u(test, "size unchanged after the refusal", 3, str_get_size(&source));

    str_remove(&source, 1);

    test_expect_string(test, "a normal remove(1) still works -> \"ac\"", "ac", str_get_data(&source));
    test_expect_u(test, "the normal remove shrank size to 2", 2, str_get_size(&source));

    str_uninit(&source);

    Str empty = str_init_1();

    str_remove(&empty, 0);

    test_expect_u(test, "remove on the empty Str is a NO-OP (size stays 0)", 0, str_get_size(&empty));
    test_expect_null(test, "the empty Str still carries no buffer", empty.data);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Allocator-Carrying Empties + Arena-Backed Slice (memsec round 2, items 5-6)
 *============================================================================*/
static void _test_allocator_carrying_results(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "empties and slices carry their source's allocator: refused-arena empties keep the arena, and str_slice of an arena-backed source is an arena-backed owner");

    /* Refused-arena empties keep the allocator so later growth stays in-arena. */
    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    Str degraded = str_alloc_from_numbers_int_1(7, &refused);

    test_expect_u(test, "str_alloc_from_numbers_int_1 on a refused arena is empty", 0, str_get_size(&degraded));
    test_expect_true(test, "the degraded empty CARRIES the refused arena's address", degraded.allocator == &refused);

    /* A view carrying the refused arena: its slice families degrade to empties
     * that keep that allocator too. */
    Str refused_view = str_alloc_init_3("abc", 3, &refused);

    Str refused_range = str_slice_range(&refused_view, 0, 1);

    test_expect_u(test, "slice_range through a refused arena degrades to empty", 0, str_get_size(&refused_range));
    test_expect_true(test, "the degraded range slice carries the source's allocator", refused_range.allocator == &refused);

    Str refused_tail = str_slice(&refused_view, 3);

    test_expect_u(test, "the empty tail slice of an arena-backed source is empty", 0, str_get_size(&refused_tail));
    test_expect_true(test, "the empty tail slice carries the source's allocator", refused_tail.allocator == &refused);

    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    /* A REAL arena: str_slice now mirrors slice_range and returns an arena-backed
     * OWNER (str_init_static used to copy to the heap - a correct but inconsistent
     * pairing). Tear-down is arena_uninit ONLY, no per-object free. */
    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    Str arena_source = str_alloc_init_static("hello", 5, &arena);
    Str arena_slice = str_slice(&arena_source, 2);

    test_expect_string(test, "arena slice content is the tail copy \"llo\"", "llo", str_get_data(&arena_slice));
    test_expect_true(test, "arena slice is OWNED", arena_slice.owned);
    test_expect_true(test, "arena slice carries the SOURCE's allocator (in-arena, not heap)", arena_slice.allocator == &arena);

    str_replace_2(&arena_slice, "LLO", 3, 0);

    test_expect_string(test, "mutating the arena slice left the source untouched (independent copy)", "hello", str_get_data(&arena_source));

    Str arena_range = str_slice_range(&arena_source, 0, 1);

    test_expect_string(test, "arena slice_range content", "he", str_get_data(&arena_range));
    test_expect_true(test, "arena slice_range carries the source's allocator", arena_range.allocator == &arena);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_expect_true(test, "arena_uninit released source and both slices in bulk without a crash", true);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Design Round 1 (2026-09-01) - EMPTY answers, keep-self, allocator carrying
 *============================================================================*/
static void _test_trim_replace_twins_agree_on_nothing(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "producers of NOTHING answer the EMPTY Str on BOTH twins: pure-whitespace trim and an all-consumed replace,"
        " heap and arena alike (the arena twins used to keep an owned zero-length block)");

    Str whitespace = str_init_static("   ", 3);
    Str heap_trim = str_from_trim(&whitespace);

    test_expect_null(test, "heap str_from_trim of pure whitespace is EMPTY (data == nullptr)", heap_trim.data);
    test_expect_u(test, "heap trim size 0", 0, str_get_size(&heap_trim));
    test_expect_false(test, "heap trim owns nothing", heap_trim.owned);

    Str abc = str_init_static("abc", 3);
    Str heap_replace = str_from_replace_1(&abc, "abc", "");

    test_expect_null(test, "heap str_from_replace_1 consuming everything is EMPTY", heap_replace.data);
    test_expect_false(test, "heap replace owns nothing", heap_replace.owned);

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    Str arena_whitespace = str_alloc_init_static("   ", 3, &arena);
    Str arena_trim = str_alloc_from_trim(&arena_whitespace, &arena);

    test_expect_null(test, "arena str_alloc_from_trim of pure whitespace is EMPTY too (it used to be an owned \"\")", arena_trim.data);
    test_expect_u(test, "arena trim size 0", 0, str_get_size(&arena_trim));
    test_expect_false(test, "arena trim owns nothing", arena_trim.owned);
    test_expect_true(test, "arena trim still carries the arena", arena_trim.allocator == &arena);

    Str arena_abc = str_alloc_init_static("abc", 3, &arena);
    Str arena_replace = str_alloc_from_replace_1(&arena_abc, "abc", "", &arena);

    test_expect_null(test, "arena str_alloc_from_replace_1 consuming everything is EMPTY", arena_replace.data);
    test_expect_false(test, "arena replace owns nothing", arena_replace.owned);

    /* The twins still produce content when there is some. */
    Str arena_padded = str_alloc_init_static("  hi  ", 6, &arena);
    Str arena_trimmed = str_alloc_from_trim(&arena_padded, &arena);

    test_expect_string(test, "arena trim with content", "hi", str_get_data(&arena_trimmed));
    test_expect_true(test, "arena trim with content is OWNED", arena_trimmed.owned);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    str_uninit(&whitespace);
    str_uninit(&abc);

    test_case_end(test);
}

static void _test_copy_and_trim_keep_self_on_refusal(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_copy_2 and str_alloc_trim on a REFUSED arena keep self exactly as it was (they used to store the EMPTY Str and drop the content)");

    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    test_expect_null(test, "degenerate init left a null handler (the refusal actually happened)", refused.handler);

    char buffer[8] = "abc";
    Str view = str_alloc_init_3(buffer, 3, &refused);

    str_copy_2(&view, "xyz", 3);

    test_expect_string(test, "str_copy_2 refused: content unchanged", "abc", str_get_data(&view));
    test_expect_true(test, "str_copy_2 refused: still the same buffer", view.data == buffer);
    test_expect_false(test, "str_copy_2 refused: still a view", view.owned);

    char padded[8] = " ab ";
    Str padded_view = str_alloc_init_3(padded, 4, &refused);

    str_alloc_trim(&padded_view, &refused);

    test_expect_string(test, "str_alloc_trim refused: content unchanged", " ab ", str_get_data(&padded_view));
    test_expect_true(test, "str_alloc_trim refused: still the same buffer", padded_view.data == padded);

    str_trim(&padded_view);

    test_expect_string(test, "str_trim routes to the arena twin and refuses the same way", " ab ", str_get_data(&padded_view));

    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    /* A LIVE arena: trim promotes to an owner, and pure whitespace becomes EMPTY with the
     * arena kept. */
    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    char live[8] = " ab ";
    Str live_view = str_alloc_init_3(live, 4, &arena);

    str_alloc_trim(&live_view, &arena);

    test_expect_string(test, "live arena trim content", "ab", str_get_data(&live_view));
    test_expect_true(test, "live arena trim PROMOTED to an owner", live_view.owned);
    test_expect_true(test, "live arena trim carries the arena", live_view.allocator == &arena);

    char blank[8] = "   ";
    Str blank_view = str_alloc_init_3(blank, 3, &arena);

    str_alloc_trim(&blank_view, &arena);

    test_expect_null(test, "live arena trim of pure whitespace leaves EMPTY", blank_view.data);
    test_expect_true(test, "and keeps the arena", blank_view.allocator == &arena);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_copy_self_prefix(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_copy_2 from inside self is a real copy (the old alias guard made a self-prefix truncation a silent no-op); str_copy_3(&s, &s) is a correct no-op");

    Str value = str_init_static("abcdef", 6);

    str_copy_2(&value, value.data, 2);

    test_expect_string(test, "str_copy_2(&s, s.data, 2) truncates s to its own prefix", "ab", str_get_data(&value));
    test_expect_u(test, "size follows", 2, str_get_size(&value));
    test_expect_true(test, "still an owner", value.owned);

    str_copy_2(&value, value.data + 1, 1);

    test_expect_string(test, "str_copy_2 from an interior byte", "b", str_get_data(&value));

    str_copy_3(&value, &value);

    test_expect_string(test, "str_copy_3(&s, &s) leaves the content", "b", str_get_data(&value));
    test_expect_u(test, "and the size", 1, str_get_size(&value));

    str_uninit(&value);

    test_case_end(test);
}

static void _test_add_2_self_alias(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_add_2 with data inside self: the new buffer is built before the old is released, so the aliased insert is exact");

    Str value = str_init_static("abc", 3);

    str_add_2(&value, value.data, value.size, 0);

    test_expect_string(test, "prepending self to itself", "abcabc", str_get_data(&value));

    str_add_2(&value, value.data + 1, 2, 3);

    test_expect_string(test, "inserting an interior slice of self in the middle", "abcbcabc", str_get_data(&value));
    test_expect_u(test, "size follows", 8, str_get_size(&value));

    str_uninit(&value);

    test_case_end(test);
}

static void _test_split_carries_allocator(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_split_1..3 tokens carry the source's allocator (an arena source used to yield a HEAP owner that had to be freed on its own)");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    Str source = str_alloc_init_static("key=value", 9, &arena);
    Str token = str_split_1(&source, "=");

    test_expect_string(test, "first token", "key", str_get_data(&token));
    test_expect_true(test, "token is OWNED", token.owned);
    test_expect_true(test, "token carries the source's arena", token.allocator == &arena);

    Str leading = str_alloc_init_static("=value", 6, &arena);
    Str empty_token = str_split_1(&leading, "=");

    test_expect_null(test, "an empty first token is the EMPTY Str", empty_token.data);
    test_expect_true(test, "and still carries the arena", empty_token.allocator == &arena);

    Str whole = str_split_2(&source, "", 0);

    test_expect_string(test, "an empty delimiter yields the whole source as one owned token", "key=value", str_get_data(&whole));
    test_expect_true(test, "carrying the arena", whole.allocator == &arena);

    Str delimiter = str_alloc_init_static("=", 1, &arena);
    Str by_str = str_split_3(&source, &delimiter);

    test_expect_string(test, "str_split_3 agrees", "key", str_get_data(&by_str));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_expect_true(test, "arena_uninit released source and every token in bulk without a crash", true);

    /* Heap source: heap owner, as before. */
    Str heap_source = str_init_static("a,b", 3);
    Str heap_token = str_split_1(&heap_source, ",");

    test_expect_string(test, "heap first token", "a", str_get_data(&heap_token));
    test_expect_true(test, "heap token is OWNED", heap_token.owned);

    str_uninit(&heap_token);
    str_uninit(&heap_source);

    test_case_end(test);
}

static void _test_join_pre_sized(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_join_*: one pre-sized borrow; count == 0 with parts == nullptr is EMPTY (no abort); an overflowing total"
        " and a refused arena refuse the WHOLE join");

    Str a = str_init_static("a", 1);
    Str bb = str_init_static("bb", 2);
    Str empty = str_init_1();
    Str ccc = str_init_static("ccc", 3);
    Str const *parts[4] = { &a, &bb, &empty, &ccc };

    Str joined = str_join_1(parts, 4, "-");

    test_expect_string(test, "join with an empty part in the middle", "a-bb--ccc", str_get_data(&joined));
    test_expect_u(test, "size", 9, str_get_size(&joined));
    test_expect_true(test, "owned", joined.owned);

    Str no_separator = str_join_2(parts, 4, "", 0);

    test_expect_string(test, "join with no separator", "abbccc", str_get_data(&no_separator));

    Str none = str_join_1(nullptr, 0, "-");

    test_expect_null(test, "count == 0 with parts == nullptr is EMPTY, not an abort", none.data);

    Str const *all_empty[2] = { &empty, &empty };
    Str nothing = str_join_2(all_empty, 2, "", 0);

    test_expect_null(test, "two empty parts and no separator is EMPTY", nothing.data);

    /* A lying view whose size would overflow the total: the arithmetic refuses before a
     * single byte is read. */
    char one[2] = "x";
    Str lying = str_init_3(one, 1);

    lying.size = USIZE_MAX - 2;

    Str const *overflowing[2] = { &lying, &bb };
    Str refused = str_join_1(overflowing, 2, "-");

    test_expect_null(test, "an overflowing total refuses the WHOLE join to EMPTY", refused.data);

    Arena refused_arena = arena_init_2(0, 8, ARENA_TYPE_LINEAR);
    Str arena_refused = str_alloc_join_1(parts, 4, "-", &refused_arena);

    test_expect_null(test, "a refused arena refuses the whole join to EMPTY", arena_refused.data);
    test_expect_true(test, "carrying the arena", arena_refused.allocator == &refused_arena);

    arena_uninit(&refused_arena, ARENA_TYPE_LINEAR);

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);
    Str arena_joined = str_alloc_join_1(parts, 4, ", ", &arena);

    test_expect_string(test, "arena join content", "a, bb, , ccc", str_get_data(&arena_joined));
    test_expect_true(test, "arena join carries the arena", arena_joined.allocator == &arena);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    str_uninit(&joined);
    str_uninit(&no_separator);
    str_uninit(&a);
    str_uninit(&bb);
    str_uninit(&ccc);

    test_case_end(test);
}

static void _test_remove_is_erase(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_remove is str_erase of one byte: in place on the SAME buffer (the copy-on-write and its second borrow are gone), a writable view stays a view");

    Str owner = str_init_static("abcd", 4);
    char *const before = owner.data;

    str_remove(&owner, 1);

    test_expect_string(test, "owner content", "acd", str_get_data(&owner));
    test_expect_u(test, "owner size", 3, str_get_size(&owner));
    test_expect_true(test, "owner keeps the SAME buffer (in place)", owner.data == before);

    char buffer[8] = "wxyz";
    Str view = str_init_3(buffer, 4);

    str_remove(&view, 3);

    test_expect_string(test, "view content", "wxy", str_get_data(&view));
    test_expect_false(test, "the view is still a view (no promotion)", view.owned);
    test_expect_string(test, "the caller's buffer was edited in place", "wxy", buffer);

    str_remove(&view, 5);

    test_expect_string(test, "past the end refuses", "wxy", str_get_data(&view));

    str_uninit(&owner);

    test_case_end(test);
}

static void _test_move_3_empty_source(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_move_3 from the EMPTY Str is legal: self becomes EMPTY, the source stays EMPTY, no abort");

    Str destination = str_init_static("abc", 3);
    Str source = str_init_1();
    Str *source_pointer = &source;

    str_move_3(&destination, &source_pointer);

    test_expect_null(test, "destination is EMPTY", destination.data);
    test_expect_u(test, "destination size 0", 0, str_get_size(&destination));
    test_expect_false(test, "destination owns nothing", destination.owned);
    test_expect_null(test, "the source pointer was nulled", source_pointer);

    test_case_end(test);
}

static void _test_set_size_refuses_on_empty(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_set_size: a non-zero size on the EMPTY Str is REFUSED (no buffer to fit in); an owner takes any size the caller vouches for");

    Str empty = str_init_1();

    str_set_size(&empty, 3);

    test_expect_u(test, "EMPTY refuses a non-zero size", 0, str_get_size(&empty));

    Str owner = str_init_static("abc", 3);

    str_set_size(&owner, 2);

    test_expect_u(test, "an owner shrinks", 2, str_get_size(&owner));

    str_set_size(&owner, 0);

    test_expect_u(test, "an owner takes zero", 0, str_get_size(&owner));

    str_uninit(&owner);

    test_case_end(test);
}

static void _test_move_2_self_alias_refused(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_move_2 of a Str's own buffer into itself is REFUSED: the object and the pointer both survive (it used to release the buffer and adopt it dead)");

    Str value = str_init_static("abc", 3);
    char *own = value.data;

    str_move_2(&value, &own, 3);

    test_expect_string(test, "content survives", "abc", str_get_data(&value));
    test_expect_true(test, "the pointer was NOT nulled", own == value.data);
    test_expect_true(test, "still owned", value.owned);

    str_uninit(&value);

    test_case_end(test);
}

static void _test_repeat_overflow_refuses(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_repeat: an overflowing size * count REFUSES with self unchanged in every build (it was an error_check abort on a value)");

    Str value = str_init_static("ab", 2);

    str_repeat(&value, USIZE_MAX);

    test_expect_string(test, "content unchanged", "ab", str_get_data(&value));
    test_expect_u(test, "size unchanged", 2, str_get_size(&value));

    str_repeat(&value, 3);

    test_expect_string(test, "a sane count still repeats", "ababab", str_get_data(&value));

    str_uninit(&value);

    test_case_end(test);
}

static void _test_coverage_anchors(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "coverage anchors for the families the suite never touched: copy_1/3, from_replace_2, lower/upper/reverse,"
        " to_numbers, find/contains/starts/ends both ways, str_delete of new_*");

    Str value = str_init_1();

    str_copy_1(&value, "Hello");

    test_expect_string(test, "str_copy_1 into EMPTY", "Hello", str_get_data(&value));
    test_expect_true(test, "str_copy_1 promoted to an owner", value.owned);

    Str other = str_init_static("World", 5);

    str_copy_3(&value, &other);

    test_expect_string(test, "str_copy_3", "World", str_get_data(&value));

    Str replaced = str_from_replace_2(&value, "o", 1, "0", 1);

    test_expect_string(test, "str_from_replace_2", "W0rld", str_get_data(&replaced));

    str_lower(&value);

    test_expect_string(test, "str_lower", "world", str_get_data(&value));

    str_upper(&value);

    test_expect_string(test, "str_upper", "WORLD", str_get_data(&value));

    str_reverse(&value);

    test_expect_string(test, "str_reverse", "DLROW", str_get_data(&value));

    test_expect_true(test, "str_starts_with_1 true", str_starts_with_1(&value, "DL"));
    test_expect_false(test, "str_starts_with_1 false", str_starts_with_1(&value, "LD"));
    test_expect_true(test, "str_ends_with_1 true", str_ends_with_1(&value, "OW"));
    test_expect_false(test, "str_ends_with_1 false", str_ends_with_1(&value, "WO"));
    test_expect_true(test, "str_contains_1 true", str_contains_1(&value, "RO"));
    test_expect_false(test, "str_contains_1 false", str_contains_1(&value, "OR"));
    test_expect_u(test, "str_find_1 found", 2, str_find_1(&value, 0, "RO"));
    test_expect_u(test, "str_find_1 not found", CHAR_NPOS, str_find_1(&value, 0, "zz"));
    test_expect_u(test, "str_find_reverse_1", 4, str_find_reverse_1(&value, 4, "W"));
    test_expect_u(test, "str_find_count_1", 1, str_find_count_1(&value, "L"));

    Str number = str_init_static("-42.5", 5);

    test_expect_i(test, "str_to_numbers_int", -42, str_to_numbers_int(&number));
    test_expect_u(test, "str_to_numbers_uint stops at the sign", 0, str_to_numbers_uint(&number));
    test_expect_f(test, "str_to_numbers_float", -42.5, str_to_numbers_float(&number), 1e-9);

    Str *heap = str_new_static("heap", 4);

    test_expect_string(test, "str_new_static content", "heap", str_get_data(heap));

    str_delete(&heap);

    test_expect_null(test, "str_delete nulled the pointer", heap);

    str_uninit(&value);
    str_uninit(&other);
    str_uninit(&replaced);
    str_uninit(&number);

    test_case_end(test);
}

static void _test_round_2_pins(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "design round 2 pins: join of empties with a separator is the separator, NaN formats to EMPTY, the int"
        " renderer is char's (ISIZE_MIN, padding), a trim into another arena is REFUSED");

    Str empty = str_init_1();
    Str const *empties[2] = { &empty, &empty };
    Str joined = str_join_1(empties, 2, ",");

    test_expect_string(test, "two empties joined by \",\" are \",\" (the footer used to promise EMPTY)", ",", str_get_data(&joined));

    Str not_a_number = str_from_numbers_float_1(0.0 / 0.0);

    test_expect_null(test, "str_from_numbers_float_1(NaN) is EMPTY", not_a_number.data);

    Str minimum = str_from_numbers_int_1(ISIZE_MIN);

    test_expect_string(test, "ISIZE_MIN renders through char's leaf", "-9223372036854775808", str_get_data(&minimum));
    test_expect_true(test, "and is OWNED", minimum.owned);

    Str padded = str_from_numbers_int_2(-7, 3);

    test_expect_string(test, "additive padding under a sign", "-0007", str_get_data(&padded));

    Str maximum = str_from_numbers_uint_2(USIZE_MAX, 0);

    test_expect_string(test, "USIZE_MAX renders", "18446744073709551615", str_get_data(&maximum));

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);
    Str arena_number = str_alloc_from_numbers_int_1(-42, &arena);

    test_expect_string(test, "arena int renders", "-42", str_get_data(&arena_number));
    test_expect_true(test, "arena int carries the arena", arena_number.allocator == &arena);
    test_expect_true(test, "arena int is OWNED", arena_number.owned);

    Str heap_owner = str_init_static(" ab ", 4);
    char *const before = heap_owner.data;

    str_alloc_trim(&heap_owner, &arena);

    test_expect_string(test, "a trim into another arena is REFUSED: content unchanged", " ab ", str_get_data(&heap_owner));
    test_expect_true(test, "same buffer", heap_owner.data == before);
    test_expect_null(test, "the object's home did not move", heap_owner.allocator);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    str_uninit(&joined);
    str_uninit(&minimum);
    str_uninit(&padded);
    str_uninit(&maximum);
    str_uninit(&heap_owner);

    test_case_end(test);
}

static void _test_delete_null_handle(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "str_delete on a nullptr handle is an idempotent no-op (the header promised it; the body aborted): twice on one object, and on a never-allocated handle");

    Str *heap = str_new_static("twice", 5);

    str_delete(&heap);

    test_expect_null(test, "the first delete nulled the handle", heap);

    str_delete(&heap);

    test_expect_null(test, "the second delete is a no-op, not an abort", heap);

    Str *never = nullptr;

    str_delete(&never);

    test_expect_null(test, "a never-allocated handle is a no-op too", never);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Constructor Sanity (positive/negative content anchors)
 *============================================================================*/
static void _test_constructor_content_anchors(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "content anchors: equality answers BOTH true and false on real data (a stuck comparator cannot pass)");

    Str value = str_init_static("anchor", 6);

    test_expect_true(test, "positive anchor: \"anchor\" equals itself", str_compare_equal_1(&value, "anchor"));
    test_expect_false(test, "negative anchor: \"anchor\" does not equal \"anchOr\"", str_compare_equal_1(&value, "anchOr"));
    test_expect_true(test, "case-insensitive twin does match \"anchOr\"", str_compare_iequal_1(&value, "anchOr"));
    test_expect_false(test, "negative anchor: size mismatch is not equal", str_compare_equal_1(&value, "anchor!"));

    str_uninit(&value);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Slice Family Uninit Round Trip
 *============================================================================*/
static void _test_slice_uninit_round_trip(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "slice producers uninit cleanly and the source survives every one of them");

    Str source = str_init_static("roundtrip", 9);

    Str tail = str_slice(&source, 5);
    Str range = str_slice_range(&source, 0, 4);

    test_expect_string(test, "str_slice(5) of \"roundtrip\"", "trip", str_get_data(&tail));
    test_expect_string(test, "str_slice_range(0,4) of \"roundtrip\"", "round", str_get_data(&range));

    str_uninit(&tail);
    str_uninit(&range);

    test_expect_string(test, "the source survived both producers' uninit", "roundtrip", str_get_data(&source));
    test_expect_u(test, "the source size survived too", 9, str_get_size(&source));

    str_uninit(&source);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Anti-Vacuity Closed Form
 *============================================================================*/
static void _test_anti_vacuity_closed_form(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "anti-vacuity: every case function ran exactly once (closed-form count) and the str_at sweep really iterated");

    test_expect_u(test, "closed-form case-entry count", _EXPECTED_CASE_COUNT, _case_entered_count);
    test_expect_u(test, "the str_at sweep executed all 10 probes (a silently-stopping loop fails here)", 10, _at_probe_count);
    test_expect_true(test, "positive anchor: in-range probes matched", _at_in_range_match_count > 0);
    test_expect_true(test, "negative anchor: out-of-range probes occurred and answered '\\0'", _at_past_end_zero_count > 0);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry Point
 *============================================================================*/
int main(int argc, char **argv) {
    /* Child mode: re-entered by _test_move_1_null_abort_probe. Must run before
     * anything else touches the Test harness or its counters. */
    if (argc >= 2 && strcmp(argv[1], "--child-move-null") == 0) {
        return _child_move_null();
    }

    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    _program = argv[0];

    Test test = test_init("tests/container/str/test_str.c");

    test_suite_begin(&test, "str_replace_slice_erase");
    _test_replace_2_bounds(&test);
    _test_slice_empty_tail(&test);
    _test_slice_range_inclusive(&test);
    _test_erase_exclusive(&test);
    _test_slice_uninit_round_trip(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "str_at_add_guards");
    _test_at_parity(&test);
    _test_add_overflow_guard(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "str_move");
    _test_move_3_cross_allocator(&test);
    _test_move_1_null_abort_probe(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "str_ownership_matrix");
    _test_ownership_views(&test);
    _test_ownership_static_copy(&test);
    _test_ownership_producers(&test);
    _test_ownership_move_2(&test);
    _test_arena_twins(&test);
    _test_refused_arena_degradation(&test);
    _test_repeat_zero_releases(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "str_memsec_round_2");
    _test_alloc_new_refused_nullptr(&test);
    _test_add_2_index_refusal(&test);
    _test_erase_span_refusal(&test);
    _test_remove_index_refusal(&test);
    _test_allocator_carrying_results(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "str_design_round_1");
    _test_trim_replace_twins_agree_on_nothing(&test);
    _test_copy_and_trim_keep_self_on_refusal(&test);
    _test_copy_self_prefix(&test);
    _test_add_2_self_alias(&test);
    _test_split_carries_allocator(&test);
    _test_join_pre_sized(&test);
    _test_remove_is_erase(&test);
    _test_move_3_empty_source(&test);
    _test_set_size_refuses_on_empty(&test);
    _test_move_2_self_alias_refused(&test);
    _test_repeat_overflow_refuses(&test);
    _test_coverage_anchors(&test);
    _test_round_2_pins(&test);
    _test_delete_null_handle(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "str_anchors");
    _test_constructor_content_anchors(&test);
    _test_anti_vacuity_closed_form(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}