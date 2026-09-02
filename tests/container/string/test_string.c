#include <string.h>

#include <arena/arena.h>
#include <container/str/str.h>
#include <container/string/string.h>
#include <memory/memory.h>
#include <test/test.h>

/*
 * First suite for CFW's String container (zero coverage before this file), pinning
 * the Block E fix set:
 *
 *   1. string_format's GROW-AND-RETRY (the block's CRITICAL): the rendered length
 *      is data - it once carried a User-Agent header - so overflowing the capacity
 *      must grow and re-render, never abort. A VIEW grown here becomes an OWNER
 *      and nothing writes past the view's claimed capacity.
 *   2. string_erase's EXCLUSIVE [from, to) with an HONEST size: the old body passed
 *      `to` into char_erase_2's INCLUSIVE parameter while shrinking size by only
 *      to - from, so bytes and size disagreed after every call. Out-of-range spans
 *      refuse as no-ops; [x, x) - [size, size) included - is the legal empty erase.
 *      str_erase and string_erase now share the convention.
 *   3. string_alloc_init_2 on a REFUSED arena returns the coherent empty String
 *      (capacity 0, data null, owned false) instead of the owned=true/capacity=768/
 *      data=null liar string_format then null-derefed (the logged Block-C defect).
 *   4. string_alloc_new_* on a refused arena propagates nullptr, no crash.
 *   5. string_add_2's index refusals (size+1 and CHAR_NPOS no-op; index == size
 *      appends), a growth-crossing append, and the append-from-self alias.
 *   6. string_at's char_at/str_at parity: '\0' at or past the end, never an abort.
 *   7. string_slice / string_slice_range: empty tail at size, empty source,
 *      INCLUSIVE [from, to], arena-carrying owned copies, mutation independence.
 *   8. string_move_3/_4 cross-allocator REFUSAL leaving BOTH objects intact
 *      (move_4's check used to run AFTER the destination was destroyed), the
 *      same-allocator move, and move_3 preserving the source Str's view-ness.
 *   9. The OWNERSHIP MATRIX: init_4 views never release the caller's buffer,
 *      producers own theirs, string_move_2 adopts, string_repeat(0) EMPTIES but
 *      KEEPS the allocation (the documented str contrast), arena twins bulk-release.
 *  10. string_wrap adoption: capacity == size + 1 (the adopted char_wrap_2 buffer),
 *      empty-producing wraps free cleanly.
 *  11. string_reserve grows a view into an owner; string_shrink keeps bytes and the
 *      terminator; reserve through a refused arena is a refusal leaving the object
 *      coherent.
 *
 * Memsec expansion round (3 HIGHs, 3 MEDs, 2 LOWs, all fixed in string.c):
 *
 *  12. Self-append ACROSS GROWTH (the UAF HIGH): string_add_2 re-bases an aliasing
 *      source after string_reserve releases the old buffer - the old code read
 *      through the freed block and survived only by CRT accident.
 *  13. string_alloc_init_static on a refused arena degrades to the coherent empty
 *      (never size > 0 with data == nullptr), no abort.
 *  14. string_repeat past a refused reserve is a WHOLE no-op - on a VIEW the copy
 *      loop would have stomped the CALLER's adjacent memory (canary-verified).
 *  15. string_alloc_from_numbers_int_1/uint_1 on a refused arena answer the empty
 *      String carrying the allocator (the error_check abort is gone).
 *  16. string_remove refuses at/past size and on the empty String; a normal remove
 *      still moves bytes AND size together.
 *  17. string_replace_2's capacity refusal is LIVE control flow in checked builds
 *      (the abort that fired before the dead runtime guard is gone).
 *  18. string_format on the DEFAULT EMPTY String (data null, capacity 0) measures
 *      via vsnprintf(nullptr, 0) and grows - legal formatter input, no abort.
 *  19. Growth arithmetic is integer doubling (1 -> 2 -> 4 -> 8), not the old
 *      math_round_f float round-trip.
 */

/*==============================================================================
 * MARK: - Anti-Vacuity Counters
 *============================================================================*/
/** Closed-form number of case functions below; the final case asserts every one ran. */
#define _EXPECTED_CASE_COUNT 59

/** Incremented at the top of every case function; a case that silently never runs
 * (or a dispatcher edit that drops one) fails the closed-form check. */
static USize _case_entered_count = 0;

/** string_at parity sweep bookkeeping: total probes, in-range matches (positive
 * anchor), and out-of-range '\0' answers (negative anchor). */
static USize _at_probe_count = 0;
static USize _at_in_range_match_count = 0;
static USize _at_past_end_zero_count = 0;

/*==============================================================================
 * MARK: - Cases: string_format Grow-and-Retry (E1, the CRITICAL)
 *============================================================================*/
static void _test_format_grow_and_retry(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_format: the traymon shape - init_2(768) formatting \"%s:%s:%s:%s\" with a 780-byte operand GROWS and holds the full text (no abort, no truncation)");

    /* The exact live shape: a ~780-byte operand (it carried a User-Agent header)
     * rendered through a 768-byte String. */
    static char big_operand[781] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 780; i += 1) {
        big_operand[i] = 'A';
    }

    big_operand[780] = '\0';

    String subject = string_init_2(768);

    string_format(&subject, "%s:%s:%s:%s", "user", "agent", big_operand, "tail");

    /* 4 + 1 + 5 + 1 + 780 + 1 + 4 = 796 rendered bytes. */
    test_expect_u(test, "exact rendered size (796 bytes - nothing truncated)", 796, string_get_size(&subject));
    test_expect_true(test, "capacity grew past the original 768", string_get_capacity(&subject) >= 797);
    test_expect_true(test, "content prefix survived the re-render", string_starts_with_1(&subject, "user:agent:AAAA"));
    test_expect_true(test, "content suffix survived the re-render", string_ends_with_1(&subject, "AAAA:tail"));
    test_expect_u(test, "all three ':' separators are present", 3, string_find_count_1(&subject, ":"));
    test_expect_i(test, "the final byte is 'l' (terminated exactly at 796)", (ISize) 'l', (ISize) string_at(&subject, 795));
    test_expect_u(test, "the terminator sits at 796 (strlen agrees with size)", 796, (USize) strlen(string_get_data(&subject)));
    test_expect_true(test, "the grown String is an OWNER", subject.owned);

    string_uninit(&subject);

    test_case_end(test);
}

static void _test_format_fits_no_growth(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_format: a render that fits stays in place - no growth, exact content and size");

    String subject = string_init_2(32);

    string_format(&subject, "%s-%d", "ab", 7);

    test_expect_string(test, "content is \"ab-7\"", "ab-7", string_get_data(&subject));
    test_expect_u(test, "size is 4", 4, string_get_size(&subject));
    test_expect_u(test, "capacity did NOT grow (32 was enough)", 32, string_get_capacity(&subject));
    test_expect_true(test, "still an owner", subject.owned);

    /* Re-format in place: the same buffer re-renders shorter without stale tail. */
    string_format(&subject, "%d", 9);

    test_expect_string(test, "re-format replaced the content", "9", string_get_data(&subject));
    test_expect_u(test, "re-format shrank the size honestly", 1, string_get_size(&subject));

    string_uninit(&subject);

    test_case_end(test);
}

static void _test_format_view_grow_to_owner(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_format: a VIEW (init_4 over a stack buffer) that overflows grows into an OWNER, and nothing ever writes past the view's claimed capacity");

    /* View over the first 8 bytes (claimed capacity 9); sentinels behind it catch
     * any write past the claimed capacity. */
    char stack_buffer[32] = "abcdefgh";

    for (USize i = 9; i < 31; i += 1) {
        stack_buffer[i] = '~';
    }

    stack_buffer[31] = '\0';

    String view = string_init_4(stack_buffer, 8);

    test_expect_false(test, "precondition: init_4 built a VIEW (owned == false)", view.owned);
    test_expect_u(test, "precondition: the view claims capacity 9", 9, string_get_capacity(&view));

    string_format(&view, "%s", "0123456789ABCDEFGHIJ");

    test_expect_true(test, "the overflowing format promoted the view to an OWNER", view.owned);
    test_expect_true(test, "the owner's buffer is NOT the stack buffer anymore", string_get_data(&view) != stack_buffer);
    test_expect_string(test, "the full 20-byte render is held (no truncation)", "0123456789ABCDEFGHIJ", string_get_data(&view));
    test_expect_u(test, "size is the full rendered length", 20, string_get_size(&view));

    USize sentinel_intact_count = 0;

    for (USize i = 9; i < 31; i += 1) {
        if (stack_buffer[i] == '~') {
            sentinel_intact_count += 1;
        }
    }

    test_expect_u(test, "no write past the claimed capacity: all 22 sentinel bytes intact", 22, sentinel_intact_count);

    string_uninit(&view);

    test_case_end(test);
}

static void _test_format_grow_from_empty(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_format on the DEFAULT EMPTY String: vsnprintf(nullptr, 0) measures, the grow path allocates - \"n=42\" comes out, no abort (the old precondition aborted)");

    String subject = string_init_1();

    test_expect_null(test, "precondition: the default empty carries no buffer", subject.data);
    test_expect_u(test, "precondition: the default empty has capacity 0", 0, string_get_capacity(&subject));

    string_format(&subject, "n=%d", 42);

    test_expect_string(test, "the render landed in a grown buffer", "n=42", string_get_data(&subject));
    test_expect_u(test, "size is 4", 4, string_get_size(&subject));
    test_expect_true(test, "capacity grew to hold render + terminator", string_get_capacity(&subject) >= 5);
    test_expect_true(test, "the grown String is an OWNER", subject.owned);

    string_uninit(&subject);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: string_erase Exclusive + Honest Size (E2)
 *============================================================================*/
static void _test_erase_exclusive_honest_size(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_erase: EXCLUSIVE [from, to) with size agreeing with the bytes - [1,3) of \"abcd\" is \"ad\" size 2, [2,4) is \"ab\" (to == size legal)");

    String middle = string_init_static("abcd", 4);

    string_erase(&middle, 1, 3);

    /* The old code left bytes and size DISAGREEING; assert both independently. */
    test_expect_string(test, "[1,3) bytes are \"ad\" ('d' survives - to is EXCLUSIVE)", "ad", string_get_data(&middle));
    test_expect_u(test, "[1,3) size is 2 (agrees with the bytes)", 2, string_get_size(&middle));
    test_expect_i(test, "byte 0 is 'a'", (ISize) 'a', (ISize) string_at(&middle, 0));
    test_expect_i(test, "byte 1 is 'd'", (ISize) 'd', (ISize) string_at(&middle, 1));

    string_uninit(&middle);

    String tail = string_init_static("abcd", 4);

    string_erase(&tail, 2, 4);

    test_expect_string(test, "[2,4) erases through the end -> \"ab\" (to == size is legal)", "ab", string_get_data(&tail));
    test_expect_u(test, "[2,4) size is 2", 2, string_get_size(&tail));

    string_uninit(&tail);

    test_case_end(test);
}

static void _test_erase_span_refusal(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_erase: [x,x) - [size,size) included - is the legal empty erase; from > to and to > size are refused NO-OPS");

    String subject = string_init_static("abcd", 4);

    string_erase(&subject, 2, 2);

    test_expect_string(test, "[2,2) is the legal empty erase, content unchanged", "abcd", string_get_data(&subject));

    string_erase(&subject, 4, 4);

    test_expect_string(test, "[size, size) is legal too - a no-op, not an abort", "abcd", string_get_data(&subject));

    string_erase(&subject, 3, 1);

    test_expect_string(test, "from > to is a refused NO-OP", "abcd", string_get_data(&subject));

    string_erase(&subject, 0, 5);

    test_expect_string(test, "to > size is a refused NO-OP", "abcd", string_get_data(&subject));
    test_expect_u(test, "size survived the whole refusal gauntlet", 4, string_get_size(&subject));

    string_erase(&subject, 0, 4);

    test_expect_u(test, "[0, size) empties the String", 0, string_get_size(&subject));
    test_expect_true(test, "string_empty agrees", string_empty(&subject));

    string_uninit(&subject);

    test_case_end(test);
}

static void _test_erase_str_parity(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "convention parity: str_erase and string_erase answer the SAME result for the same inputs (the shared exclusive [from, to))");

    Str str_side = str_init_static("abcd", 4);
    String string_side = string_init_static("abcd", 4);

    str_erase(&str_side, 1, 3);
    string_erase(&string_side, 1, 3);

    test_expect_string(test, "str_erase [1,3) of \"abcd\" -> \"ad\"", "ad", str_get_data(&str_side));
    test_expect_string(test, "string_erase [1,3) of \"abcd\" -> \"ad\" (identical)", "ad", string_get_data(&string_side));
    test_expect_u(test, "both report size 2 - str side", 2, str_get_size(&str_side));
    test_expect_u(test, "both report size 2 - string side", 2, string_get_size(&string_side));

    str_uninit(&str_side);
    string_uninit(&string_side);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Refused-Arena Constructors (E3, E4)
 *============================================================================*/
static void _test_alloc_init_2_refused_coherent_empty(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_alloc_init_2 on a REFUSED arena: the coherent empty String (capacity 0, data null, owned false), not the owned/capacity-768/data-null liar");

    /* Degenerate geometry (byte_size 0) leaves the handler null: the documented
     * refused-arena state whose allocator_borrow answers null gracefully. */
    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    test_expect_null(test, "degenerate init left a null handler (the refusal actually happened)", refused.handler);

    String degraded = string_alloc_init_2(768, &refused);

    test_expect_u(test, "capacity is 0 (not the requested 768)", 0, string_get_capacity(&degraded));
    test_expect_null(test, "data is null", degraded.data);
    test_expect_false(test, "owned is false - the object's invariants no longer lie", degraded.owned);
    test_expect_u(test, "size is 0", 0, string_get_size(&degraded));
    test_expect_true(test, "the empty still CARRIES the refused arena's address", degraded.allocator == &refused);

    string_uninit(&degraded);
    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_alloc_init_2_real_arena(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_alloc_init_2 on a REAL arena: a working owner, growable, released in bulk by arena_uninit");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    String subject = string_alloc_init_2(64, &arena);

    test_expect_u(test, "capacity is the requested 64", 64, string_get_capacity(&subject));
    test_expect_not_null(test, "a real buffer was borrowed", subject.data);
    test_expect_true(test, "owned == true", subject.owned);
    test_expect_true(test, "the String carries its arena", subject.allocator == &arena);

    string_add_last_1(&subject, "hi");

    test_expect_string(test, "the arena-backed String is usable", "hi", string_get_data(&subject));

    /* Bulk release: no per-object uninit needed, and none may crash afterwards. */
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_expect_true(test, "arena_uninit released the borrow in bulk without a crash", true);

    test_case_end(test);
}

static void _test_alloc_new_refused_nullptr(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_alloc_new_1/_2/_static on a REFUSED arena: nullptr propagated, no crash");

    Arena refused = arena_init_2(0, 0, ARENA_TYPE_LINEAR);

    test_expect_null(test, "degenerate init left a null handler (the refusal actually happened)", refused.handler);

    String *const from_new_1 = string_alloc_new_1(&refused);

    test_expect_null(test, "string_alloc_new_1 on a refused arena is nullptr, not a crash", from_new_1);

    String *const from_new_2 = string_alloc_new_2(16, &refused);

    test_expect_null(test, "string_alloc_new_2 propagates the nullptr", from_new_2);

    String *const from_new_static = string_alloc_new_static("x", 1, &refused);

    test_expect_null(test, "string_alloc_new_static propagates the nullptr", from_new_static);

    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_reserve_refused_arena_no_op(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_reserve on a refused-arena-backed empty: the borrow guard refuses and the object stays coherent (a real no-op, no crash)");

    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    String degraded = string_alloc_init_2(768, &refused);

    string_reserve(&degraded, 32);

    test_expect_u(test, "reserve through the refused arena left capacity 0", 0, string_get_capacity(&degraded));
    test_expect_null(test, "data stayed null", degraded.data);
    test_expect_false(test, "owned stayed false", degraded.owned);

    /* The whole growth path refuses coherently: an append cannot get a buffer, so
     * it must refuse wholly rather than write past a null. */
    string_add_last_1(&degraded, "x");

    test_expect_u(test, "an append through the refused arena refused wholly (size 0)", 0, string_get_size(&degraded));
    test_expect_null(test, "and never planted a buffer", degraded.data);

    string_uninit(&degraded);
    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_alloc_init_static_refused(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_alloc_init_static on a REFUSED arena: the coherent empty (data null, size 0) - NEVER size > 0 with data null - and no abort");

    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    test_expect_null(test, "degenerate init left a null handler (the refusal actually happened)", refused.handler);

    String degraded = string_alloc_init_static("hello", 5, &refused);

    test_expect_u(test, "size is 0 (checked BEFORE the size assignment - not the size-5/data-null liar)", 0, string_get_size(&degraded));
    test_expect_null(test, "data is null", degraded.data);
    test_expect_u(test, "capacity is 0", 0, string_get_capacity(&degraded));
    test_expect_false(test, "owned is false", degraded.owned);
    test_expect_true(test, "the empty carries the refused arena's address", degraded.allocator == &refused);

    string_uninit(&degraded);
    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_repeat_refused_reserve_view_no_op(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_repeat past a REFUSED reserve: a WHOLE no-op - on a VIEW the old copy loop stomped the CALLER's adjacent stack memory (canary-verified)");

    /* The view's 4 content bytes sit at [8..11] with canary bytes on BOTH sides:
     * the old code copied unit * count bytes into the small view buffer anyway. */
    char stack_buffer[32] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 32; i += 1) {
        stack_buffer[i] = '~';
    }

    stack_buffer[8]  = 'a';
    stack_buffer[9]  = 'b';
    stack_buffer[10] = 'c';
    stack_buffer[11] = 'd';
    stack_buffer[12] = '\0';

    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    String view = string_alloc_init_4(stack_buffer + 8, 4, &refused);

    test_expect_false(test, "precondition: alloc_init_4 built a view", view.owned);
    test_expect_u(test, "precondition: the view claims capacity 5", 5, string_get_capacity(&view));

    string_repeat(&view, 4);

    test_expect_u(test, "size unchanged (the repeat refused wholly)", 4, string_get_size(&view));
    test_expect_u(test, "capacity unchanged", 5, string_get_capacity(&view));
    test_expect_i(test, "byte 0 unchanged", (ISize) 'a', (ISize) string_at(&view, 0));
    test_expect_i(test, "byte 3 unchanged", (ISize) 'd', (ISize) string_at(&view, 3));

    USize canary_intact_count = 0;

    for (USize i = 0; i < 8; i += 1) {
        if (stack_buffer[i] == '~') {
            canary_intact_count += 1;
        }
    }

    for (USize i = 13; i < 32; i += 1) {
        if (stack_buffer[i] == '~') {
            canary_intact_count += 1;
        }
    }

    test_expect_u(test, "all 27 canary bytes around the view intact (no caller-memory stomp)", 27, canary_intact_count);

    string_uninit(&view);
    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_alloc_from_numbers_refused(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_alloc_from_numbers_int_1/uint_1 on a REFUSED arena: the empty String carrying the allocator, no abort (the error_check abort is gone)");

    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    String from_int = string_alloc_from_numbers_int_1(-42, &refused);

    test_expect_u(test, "int_1 through the refused arena is empty", 0, string_get_size(&from_int));
    test_expect_null(test, "int_1 carries no buffer", from_int.data);
    test_expect_true(test, "int_1's empty carries the refused arena", from_int.allocator == &refused);

    String from_uint = string_alloc_from_numbers_uint_1(7, &refused);

    test_expect_u(test, "uint_1 through the refused arena is empty", 0, string_get_size(&from_uint));
    test_expect_null(test, "uint_1 carries no buffer", from_uint.data);
    test_expect_true(test, "uint_1's empty carries the refused arena", from_uint.allocator == &refused);

    string_uninit(&from_int);
    string_uninit(&from_uint);
    arena_uninit(&refused, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: string_add_2 (E5)
 *============================================================================*/
static void _test_add_2_index_refusal(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_add_2: index size+1 and CHAR_NPOS are NO-OPS; index == size stays the legal append-at-end");

    String subject = string_init_static("abc", 3);

    string_add_2(&subject, "x", 1, 4);

    test_expect_string(test, "index == size + 1 is a NO-OP, content unchanged", "abc", string_get_data(&subject));
    test_expect_u(test, "index == size + 1 left size unchanged", 3, string_get_size(&subject));

    string_add_2(&subject, "x", 1, CHAR_NPOS);

    test_expect_string(test, "index == CHAR_NPOS (an unchecked find result) is a NO-OP", "abc", string_get_data(&subject));

    string_add_2(&subject, "d", 1, 3);

    test_expect_string(test, "index == size still appends -> \"abcd\"", "abcd", string_get_data(&subject));
    test_expect_u(test, "the append grew size to 4", 4, string_get_size(&subject));

    string_uninit(&subject);

    test_case_end(test);
}

static void _test_add_2_growth_crossing(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_add_2: a growth-crossing append (init_2(4) + 100 bytes) holds the full content, terminated");

    static char hundred[101] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 100; i += 1) {
        hundred[i] = 'x';
    }

    hundred[100] = '\0';

    String subject = string_init_2(4);

    string_add_2(&subject, hundred, 100, 0);

    test_expect_u(test, "size is the full 100", 100, string_get_size(&subject));
    test_expect_true(test, "capacity crossed the growth (>= 101)", string_get_capacity(&subject) >= 101);
    test_expect_i(test, "first byte is 'x'", (ISize) 'x', (ISize) string_at(&subject, 0));
    test_expect_i(test, "last byte is 'x'", (ISize) 'x', (ISize) string_at(&subject, 99));
    test_expect_u(test, "the buffer is terminated exactly at 100 (strlen agrees)", 100, (USize) strlen(string_get_data(&subject)));

    string_uninit(&subject);

    test_case_end(test);
}

static void _test_add_2_self_alias(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_add_2: appending a String's OWN data to itself survives (capacity pre-reserved, so no realloc invalidates the source)");

    String subject = string_init_2(16);

    string_add_last_1(&subject, "abcd");

    /* The source pointer aliases subject's own buffer; capacity 16 already fits
     * 8 + terminator, so no growth invalidates it mid-append. */
    string_add_2(&subject, string_get_data(&subject), 4, 4);

    test_expect_string(test, "self-append doubled the content -> \"abcdabcd\"", "abcdabcd", string_get_data(&subject));
    test_expect_u(test, "size is 8", 8, string_get_size(&subject));

    string_uninit(&subject);

    test_case_end(test);
}

static void _test_add_2_self_alias_across_growth(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_add_2: self-append ACROSS GROWTH (the UAF HIGH) - the reserve releases the old buffer, so the aliasing source must be RE-BASED, not read through the freed block");

    /* "abcdef" in an 8-byte buffer: the self-append needs 13 (6 + 6 + terminator),
     * so string_reserve reallocates and frees the old buffer MID-APPEND. The old
     * code kept reading the caller's `data` pointer - a use-after-free that only
     * passed on the Windows CRT by accident. */
    String subject = string_init_2(8);

    string_add_last_1(&subject, "abcdef");

    test_expect_u(test, "precondition: capacity is still the original 8 (growth not yet crossed)", 8, string_get_capacity(&subject));

    string_add_last_2(&subject, string_get_data(&subject), string_get_size(&subject));

    test_expect_string(test, "the growth-crossing self-append doubled the content -> \"abcdefabcdef\"", "abcdefabcdef", string_get_data(&subject));
    test_expect_u(test, "exact size 12", 12, string_get_size(&subject));
    test_expect_true(test, "capacity crossed the growth (>= 13)", string_get_capacity(&subject) >= 13);
    test_expect_u(test, "terminated exactly at 12 (strlen agrees - no stale-tail read)", 12, (USize) strlen(string_get_data(&subject)));

    string_uninit(&subject);

    test_case_end(test);
}

static void _test_growth_arithmetic_doubling(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "growth arithmetic: repeated 1-byte appends from init_2(1) double 1 -> 2 -> 4 -> 8 with exact contents (pins the integer doubling that replaced math_round_f)");

    String subject = string_init_2(1);

    string_add_last_2(&subject, "a", 1);

    test_expect_u(test, "after 'a': capacity doubled 1 -> 2", 2, string_get_capacity(&subject));

    string_add_last_2(&subject, "b", 1);

    test_expect_u(test, "after 'b': capacity doubled 2 -> 4", 4, string_get_capacity(&subject));

    string_add_last_2(&subject, "c", 1);

    test_expect_u(test, "after 'c': 4 still fits (3 + terminator), no growth", 4, string_get_capacity(&subject));

    string_add_last_2(&subject, "d", 1);

    test_expect_u(test, "after 'd': capacity doubled 4 -> 8", 8, string_get_capacity(&subject));

    string_add_last_2(&subject, "e", 1);

    test_expect_u(test, "after 'e': 8 still fits, no growth", 8, string_get_capacity(&subject));
    test_expect_string(test, "exact contents landed across every doubling", "abcde", string_get_data(&subject));
    test_expect_u(test, "exact size 5", 5, string_get_size(&subject));

    string_uninit(&subject);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: string_at Parity (E6)
 *============================================================================*/
static void _test_at_parity(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_at: char_at/str_at parity - in-range answers the byte, at-size / far-past / empty all answer '\\0', never an abort");

    char const *const expected = "abcd";
    String subject = string_init_static(expected, 4);

    /* Differential sweep with a closed-form probe count: indices 0..9 over a
     * 4-byte String give exactly 4 in-range matches and 6 out-of-range '\0's. */
    for (USize index = 0; index < 10; index += 1) {
        char const actual = string_at(&subject, index);

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

    string_uninit(&subject);

    /* The empty String carries data == nullptr; index 0 is at-the-end and must
     * still answer '\0' rather than dereference. */
    String empty = string_init_1();

    test_expect_i(test, "string_at on the empty String answers '\\0'", 0, (ISize) string_at(&empty, 0));
    test_expect_i(test, "string_at far past the empty String answers '\\0' too", 0, (ISize) string_at(&empty, 1000));

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: string_slice / string_slice_range (E7)
 *============================================================================*/
static void _test_slice_empty_tail(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_slice: index == size yields the empty String (the tail after the last delimiter); an empty source slices to empty; a mid-slice is independent");

    String source = string_init_static("hello", 5);

    String tail = string_slice(&source, 5);

    test_expect_u(test, "slice at size has size 0", 0, string_get_size(&tail));
    test_expect_null(test, "slice at size carries no buffer", tail.data);
    test_expect_false(test, "slice at size owns nothing", tail.owned);

    string_uninit(&tail);

    String empty_source = string_init_1();
    String empty_slice = string_slice(&empty_source, 0);

    test_expect_u(test, "slice of the empty String at 0 is empty", 0, string_get_size(&empty_slice));

    string_uninit(&empty_slice);

    /* A mid-slice is an OWNED copy: mutating it must not touch the source. */
    String middle = string_slice(&source, 2);

    test_expect_string(test, "mid-slice content is the tail copy \"llo\"", "llo", string_get_data(&middle));
    test_expect_true(test, "mid-slice is OWNED", middle.owned);

    string_fill(&middle, 'Z');

    test_expect_string(test, "mutating the slice changed the slice", "ZZZ", string_get_data(&middle));
    test_expect_string(test, "mutating the slice left the SOURCE untouched", "hello", string_get_data(&source));

    string_uninit(&middle);
    string_uninit(&source);

    test_case_end(test);
}

static void _test_slice_range_inclusive(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_slice_range: INCLUSIVE [from, to] - (0,2) of \"abcd\" is \"abc\", from == to is one character, to == size-1 reaches the last byte");

    String source = string_init_static("abcd", 4);

    String head = string_slice_range(&source, 0, 2);

    test_expect_string(test, "(0,2) of \"abcd\" keeps BOTH endpoints -> \"abc\"", "abc", string_get_data(&head));
    test_expect_u(test, "(0,2) has 3 bytes, not 2", 3, string_get_size(&head));
    test_expect_true(test, "range slice is OWNED", head.owned);

    String single = string_slice_range(&source, 1, 1);

    test_expect_string(test, "from == to is the legal single-character slice -> \"b\"", "b", string_get_data(&single));
    test_expect_u(test, "single-character slice has size 1", 1, string_get_size(&single));

    String last = string_slice_range(&source, 2, 3);

    test_expect_string(test, "to == size-1 reaches the last byte -> \"cd\"", "cd", string_get_data(&last));

    string_uninit(&head);
    string_uninit(&single);
    string_uninit(&last);
    string_uninit(&source);

    test_case_end(test);
}

static void _test_slice_arena_carrying(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "arena-backed slices: string_slice and string_slice_range of an arena source are arena-carrying OWNED copies, independent of the source, bulk-released");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    String source = string_alloc_init_static("hello", 5, &arena);

    String tail = string_slice(&source, 2);

    test_expect_string(test, "arena slice content is the tail copy \"llo\"", "llo", string_get_data(&tail));
    test_expect_true(test, "arena slice is OWNED", tail.owned);
    test_expect_true(test, "arena slice carries the SOURCE's allocator", tail.allocator == &arena);

    string_fill(&tail, 'Z');

    test_expect_string(test, "mutating the arena slice left the source untouched", "hello", string_get_data(&source));

    String range = string_slice_range(&source, 0, 1);

    test_expect_string(test, "arena slice_range content is \"he\"", "he", string_get_data(&range));
    test_expect_true(test, "arena slice_range carries the source's allocator", range.allocator == &arena);
    test_expect_true(test, "arena slice_range is OWNED", range.owned);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_expect_true(test, "arena_uninit released source and both slices in bulk without a crash", true);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: string_move_3 / string_move_4 (E8)
 *============================================================================*/
static void _test_move_3_cross_allocator_refusal(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_move_3: crossing allocators is REFUSED as a real no-op leaving BOTH the Str source and the String destination intact");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    Str *arena_source = str_alloc_new_static("arena", 5, &arena);
    String heap_destination = string_init_static("heap", 4);

    string_move_3(&heap_destination, &arena_source);

    test_expect_not_null(test, "refused move did NOT null the source pointer", arena_source);
    test_expect_string(test, "Str source content intact after the refusal", "arena", str_get_data(arena_source));
    test_expect_string(test, "String destination content intact after the refusal", "heap", string_get_data(&heap_destination));
    test_expect_u(test, "destination size intact", 4, string_get_size(&heap_destination));

    /* Both objects must still tear down cleanly through their OWN allocators. */
    string_uninit(&heap_destination);
    str_delete(&arena_source);

    test_expect_null(test, "str_delete nulled the arena-backed source", arena_source);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_move_4_cross_allocator_refusal(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_move_4: the cross-allocator check runs BEFORE the destination is destroyed - a refusal leaves the destination alive (the old check ran after string_uninit)");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    String *arena_source = string_alloc_new_static("arena", 5, &arena);
    String heap_destination = string_init_static("heap", 4);

    string_move_4(&heap_destination, &arena_source);

    test_expect_not_null(test, "refused move did NOT null the source pointer", arena_source);
    test_expect_string(test, "source content intact after the refusal", "arena", string_get_data(arena_source));
    test_expect_not_null(test, "the DESTINATION's buffer is still alive (uninit never ran)", heap_destination.data);
    test_expect_string(test, "destination content intact after the refusal", "heap", string_get_data(&heap_destination));
    test_expect_u(test, "destination size intact", 4, string_get_size(&heap_destination));
    test_expect_true(test, "destination still owns its buffer", heap_destination.owned);

    string_uninit(&heap_destination);
    string_delete(&arena_source);

    test_expect_null(test, "string_delete nulled the arena-backed source", arena_source);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_move_4_same_allocator(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_move_4 same-allocator: the buffer transfers, the source is CLEARED (no retained-alias double free), the caller's pointer is nulled");

    String source = string_init_static("mv", 2);
    String *source_pointer = &source;
    String destination = string_init_static("old", 3);

    string_move_4(&destination, &source_pointer);

    test_expect_null(test, "the caller's source pointer was nulled", source_pointer);
    test_expect_string(test, "the destination received the buffer", "mv", string_get_data(&destination));
    test_expect_u(test, "the destination received the size", 2, string_get_size(&destination));
    test_expect_true(test, "ownership transferred with the buffer", destination.owned);
    test_expect_null(test, "the SOURCE struct was cleared (no double claim)", source.data);
    test_expect_u(test, "the source size was cleared", 0, string_get_size(&source));
    test_expect_false(test, "the source no longer owns anything", source.owned);

    /* Exactly one release: the source is cleared, so its uninit is a no-op and the
     * memory hooks' leak accounting sees one alloc, one free. */
    string_uninit(&source);
    string_uninit(&destination);

    test_case_end(test);
}

static void _test_move_3_preserves_viewness(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_move_3 preserves the source Str's VIEW-ness: a view Str moved in yields a String that is NOT owned, and uninit never frees the caller's buffer");

    char stack_buffer[8] = "view";

    Str view = str_init_3(stack_buffer, 4);

    test_expect_false(test, "precondition: str_init_3 built a view (owned == false)", view.owned);

    Str *view_pointer = &view;
    String destination = string_init_1();

    string_move_3(&destination, &view_pointer);

    test_expect_null(test, "the caller's source pointer was nulled", view_pointer);
    test_expect_false(test, "the String is NOT owned - the view-ness carried over", destination.owned);
    test_expect_true(test, "the String aliases the caller's stack buffer", string_get_data(&destination) == stack_buffer);
    test_expect_string(test, "content reads through the view", "view", string_get_data(&destination));
    test_expect_null(test, "the source Str was emptied", view.data);

    string_uninit(&destination);

    test_expect_string(test, "the stack buffer survived the String's uninit (nothing was freed)", "view", stack_buffer);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Ownership Matrix (E9)
 *============================================================================*/
static void _test_ownership_views(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "OWNERSHIP: string_init_4 builds a VIEW - owned == false, uninit leaves the caller's buffer intact");

    char stack_buffer[8] = "view";

    String view = string_init_4(stack_buffer, 4);

    test_expect_false(test, "string_init_4 view: owned == false", view.owned);
    test_expect_true(test, "the view aliases the caller's buffer (no copy)", string_get_data(&view) == stack_buffer);
    test_expect_u(test, "the view claims size + terminator as capacity", 5, string_get_capacity(&view));

    string_uninit(&view);

    test_expect_string(test, "the stack buffer still reads its original bytes after uninit (nothing was freed)", "view", stack_buffer);
    test_expect_null(test, "uninit cleared the view's pointer (no dangling borrow)", view.data);

    test_case_end(test);
}

static void _test_ownership_producers(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "OWNERSHIP: string_init_static and the _string_adopt producers return OWNED buffers that uninit cleanly");

    char source_buffer[8] = "src";

    String copy = string_init_static(source_buffer, 3);

    test_expect_true(test, "string_init_static: owned == true", copy.owned);
    test_expect_true(test, "the copy does NOT alias the source", string_get_data(&copy) != source_buffer);
    test_expect_string(test, "the copy carries the source bytes", "src", string_get_data(&copy));

    string_fill(&copy, 'x');

    test_expect_string(test, "mutating the copy left the SOURCE untouched", "src", source_buffer);

    string_uninit(&copy);

    String from_int = string_from_numbers_int_1(-42);

    test_expect_true(test, "string_from_numbers_int_1 (an adopt producer): owned == true", from_int.owned);
    test_expect_string(test, "string_from_numbers_int_1(-42)", "-42", string_get_data(&from_int));

    string_uninit(&from_int);

    String from_uint = string_from_numbers_uint_2(7, 3);

    test_expect_true(test, "string_from_numbers_uint_2: owned == true", from_uint.owned);
    test_expect_string(test, "string_from_numbers_uint_2(7, padding 3) is additive -> \"0007\"", "0007", string_get_data(&from_uint));

    string_uninit(&from_uint);

    test_case_end(test);
}

static void _test_ownership_move_2_adoption(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "OWNERSHIP: string_move_2 ADOPTS a heap buffer - owned == true, source pointer nulled, uninit frees exactly once");

    char *heap_buffer = memory_alloc(4);

    heap_buffer[0] = 'a';
    heap_buffer[1] = 'b';
    heap_buffer[2] = 'c';
    heap_buffer[3] = '\0';

    String adopted = string_init_1();

    string_move_2(&adopted, &heap_buffer, 3);

    test_expect_true(test, "string_move_2: owned == true (the move takes ownership)", adopted.owned);
    test_expect_null(test, "the source pointer was nulled by the move", heap_buffer);
    test_expect_string(test, "the adopted buffer's content", "abc", string_get_data(&adopted));
    test_expect_u(test, "the adopted size", 3, string_get_size(&adopted));
    test_expect_u(test, "the adopted capacity is size + terminator", 4, string_get_capacity(&adopted));

    /* Exactly one release: the memory hooks' leak accounting catches both a leak
     * (never freed) and a double free (heap_buffer is null, so no second path). */
    string_uninit(&adopted);

    test_expect_null(test, "uninit cleared the adopted pointer", adopted.data);

    test_case_end(test);
}

static void _test_repeat_zero_keeps_allocation(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_repeat(count == 0): EMPTIES but KEEPS the allocation (owned true, size 0, capacity > 0) - the documented contrast with str_repeat, which releases");

    String subject = string_init_static("abc", 3);

    test_expect_not_null(test, "precondition: the owned subject holds a buffer", subject.data);

    string_repeat(&subject, 0);

    test_expect_u(test, "count == 0 emptied the String (size 0)", 0, string_get_size(&subject));
    test_expect_true(test, "the allocation was KEPT (owned == true)", subject.owned);
    test_expect_true(test, "capacity is still positive", string_get_capacity(&subject) > 0);
    test_expect_not_null(test, "the buffer is still held (unlike str_repeat's release)", subject.data);

    /* The kept allocation is still usable. */
    string_add_last_1(&subject, "ok");

    test_expect_string(test, "the emptied-but-allocated String is still usable", "ok", string_get_data(&subject));

    string_uninit(&subject);

    /* Positive anchor: a real repeat multiplies the content. */
    String repeated = string_init_static("ab", 2);

    string_repeat(&repeated, 3);

    test_expect_string(test, "string_repeat(3) of \"ab\" -> \"ababab\"", "ababab", string_get_data(&repeated));
    test_expect_u(test, "repeat size is 6", 6, string_get_size(&repeated));

    string_uninit(&repeated);

    test_case_end(test);
}

static void _test_arena_twins_bulk_release(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "OWNERSHIP: arena twins carry their arena and release in BULK - construct, assert, arena_uninit, no per-object free");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    String from_static = string_alloc_init_static("hello", 5, &arena);

    test_expect_true(test, "string_alloc_init_static: owned == true", from_static.owned);
    test_expect_true(test, "string_alloc_init_static carries the arena it borrowed from", from_static.allocator == &arena);
    test_expect_string(test, "string_alloc_init_static content", "hello", string_get_data(&from_static));

    String from_int = string_alloc_from_numbers_int_1(-42, &arena);

    test_expect_string(test, "string_alloc_from_numbers_int_1(-42)", "-42", string_get_data(&from_int));
    test_expect_true(test, "string_alloc_from_numbers_int_1: owned == true", from_int.owned);

    String from_uint = string_alloc_from_numbers_uint_2(7, 3, &arena);

    test_expect_string(test, "string_alloc_from_numbers_uint_2(7, padding 3)", "0007", string_get_data(&from_uint));
    test_expect_true(test, "string_alloc_from_numbers_uint_2 carries the arena", from_uint.allocator == &arena);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_expect_true(test, "arena_uninit released everything in bulk without a crash", true);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: string_wrap Adoption (E10)
 *============================================================================*/
static void _test_wrap_adoption(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_wrap: greedy reflow content is exact AND the adoption contract holds - capacity == size + 1 (the char_wrap_2 buffer is adopted, not copied)");

    String source = string_init_static("one two three four five", 23);

    String wrapped = string_wrap(&source, 9);

    test_expect_string(test, "greedy reflow at width 9", "one two\nthree\nfour five", string_get_data(&wrapped));
    test_expect_u(test, "wrapped size is 23", 23, string_get_size(&wrapped));
    test_expect_u(test, "ADOPTION contract: capacity == size + 1", 24, string_get_capacity(&wrapped));
    test_expect_true(test, "the adopted buffer is owned", wrapped.owned);
    test_expect_null(test, "the heap adoption carries no arena (uninit pairs with free)", wrapped.allocator);

    string_uninit(&wrapped);

    test_expect_string(test, "the source survived the wrap", "one two three four five", string_get_data(&source));

    string_uninit(&source);

    test_case_end(test);
}

static void _test_wrap_empty_paths(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_wrap: a whitespace-only source produces the empty String and frees the scratch buffer cleanly; wrapping the empty String is empty");

    /* All-whitespace: char_wrap_2 emits nothing, so the zero-size branch must
     * DELETE the scratch buffer instead of adopting it (leak check covers it). */
    String spaces = string_init_static("   ", 3);

    String wrapped_spaces = string_wrap(&spaces, 5);

    test_expect_u(test, "whitespace-only wrap is the empty String", 0, string_get_size(&wrapped_spaces));
    test_expect_null(test, "no buffer was adopted for the empty result", wrapped_spaces.data);

    string_uninit(&wrapped_spaces);
    string_uninit(&spaces);

    String empty = string_init_1();

    String wrapped_empty = string_wrap(&empty, 10);

    test_expect_u(test, "wrapping the empty String is empty", 0, string_get_size(&wrapped_empty));
    test_expect_null(test, "and carries no buffer", wrapped_empty.data);

    string_uninit(&wrapped_empty);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: string_reserve / string_shrink (E11)
 *============================================================================*/
static void _test_reserve_view_to_owner(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_reserve: growing a VIEW copies the bytes into an OWNED buffer and never releases the caller's stack buffer");

    char stack_buffer[8] = "view";

    String view = string_init_4(stack_buffer, 4);

    string_reserve(&view, 64);

    test_expect_true(test, "the grown String is an OWNER", view.owned);
    test_expect_true(test, "the owner's buffer is NOT the stack buffer", string_get_data(&view) != stack_buffer);
    test_expect_u(test, "capacity is the requested 64", 64, string_get_capacity(&view));
    test_expect_string(test, "the borrowed bytes were copied over", "view", string_get_data(&view));
    test_expect_string(test, "the caller's stack buffer was NOT released or altered", "view", stack_buffer);

    string_uninit(&view);

    test_expect_string(test, "the stack buffer survived the owner's uninit too", "view", stack_buffer);

    test_case_end(test);
}

static void _test_shrink_keeps_bytes(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_shrink after content removal: bytes and terminator survive at capacity == size + 1");

    String subject = string_init_2(64);

    string_add_last_1(&subject, "hello world");
    string_erase(&subject, 5, 11);

    test_expect_string(test, "precondition: the erase left \"hello\"", "hello", string_get_data(&subject));
    test_expect_u(test, "precondition: capacity is still the roomy 64", 64, string_get_capacity(&subject));

    string_shrink(&subject);

    test_expect_u(test, "shrink tightened capacity to size + terminator", 6, string_get_capacity(&subject));
    test_expect_string(test, "the bytes survived the shrink", "hello", string_get_data(&subject));
    test_expect_u(test, "size is unchanged", 5, string_get_size(&subject));
    test_expect_u(test, "the terminator survived (strlen agrees with size)", 5, (USize) strlen(string_get_data(&subject)));
    test_expect_true(test, "still an owner", subject.owned);

    string_uninit(&subject);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: string_remove / string_replace_2 Refusals (memsec expansion)
 *============================================================================*/
static void _test_remove_refusals(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_remove: index == size, index past size, and the empty String are NO-OPS (no abort, no size wrap); a normal remove moves bytes AND size together");

    String subject = string_init_static("abc", 3);

    string_remove(&subject, 3);

    test_expect_string(test, "index == size is a NO-OP, content unchanged", "abc", string_get_data(&subject));
    test_expect_u(test, "size unchanged after the at-size refusal", 3, string_get_size(&subject));

    string_remove(&subject, 100);

    test_expect_string(test, "index far past size is a NO-OP", "abc", string_get_data(&subject));

    string_remove(&subject, 1);

    test_expect_string(test, "a normal remove(1) still works -> \"ac\"", "ac", string_get_data(&subject));
    test_expect_u(test, "the normal remove shrank size to 2 (bytes and size agree)", 2, string_get_size(&subject));

    string_uninit(&subject);

    String empty = string_init_1();

    string_remove(&empty, 0);

    test_expect_u(test, "remove on the empty String is a NO-OP (size stays 0, no wrap to USIZE_MAX)", 0, string_get_size(&empty));
    test_expect_null(test, "the empty String still carries no buffer", empty.data);

    test_case_end(test);
}

static void _test_replace_2_live_refusal(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_replace_2: the capacity refusal is LIVE control flow - out-of-range index/data_size are no-ops (the abort that fired before the dead guard is gone); the in-range capacity-scoped write works");

    String subject = string_init_2(8);

    string_add_last_1(&subject, "abc");

    /* In-range, within size: the capacity-scoped overwrite. */
    string_replace_2(&subject, "XY", 2, 1);

    test_expect_string(test, "in-range replace overwrote bytes 1..2 -> \"aXY\"", "aXY", string_get_data(&subject));
    test_expect_u(test, "replace_2 never moves size", 3, string_get_size(&subject));

    /* index == capacity: refused as a live no-op, not an abort. */
    string_replace_2(&subject, "Z", 1, 8);

    test_expect_string(test, "index == capacity is a live NO-OP", "aXY", string_get_data(&subject));

    /* index far past capacity (data-shaped, an unchecked find result). */
    string_replace_2(&subject, "Z", 1, 100);

    test_expect_string(test, "index far past capacity is a live NO-OP", "aXY", string_get_data(&subject));

    /* data_size overrunning capacity - index: the unbounded-write shape. */
    string_replace_2(&subject, "WWWWWWWWW", 9, 0);

    test_expect_string(test, "data_size > capacity - index is a live NO-OP (no unbounded write)", "aXY", string_get_data(&subject));
    test_expect_u(test, "size survived the whole refusal gauntlet", 3, string_get_size(&subject));

    string_uninit(&subject);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Round 2 (shrink idempotence, VIEW writes, arena slices, join, capacity wrap)
 *============================================================================*/
static void _test_shrink_idempotent(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_shrink twice: an exact-fit String is left untouched - no reallocation, same buffer");

    String subject = string_init_2(64);

    string_add_last_1(&subject, "hello");
    string_shrink(&subject);

    char const *const first = string_get_data(&subject);

    test_expect_u(test, "first shrink: exact fit", 6, string_get_capacity(&subject));

    string_shrink(&subject);

    test_expect_u(test, "second shrink: capacity unchanged", 6, string_get_capacity(&subject));
    test_expect_true(test, "second shrink: the SAME buffer (it used to reallocate and copy)", string_get_data(&subject) == first);
    test_expect_string(test, "bytes intact", "hello", string_get_data(&subject));

    string_uninit(&subject);

    test_case_end(test);
}

static void _test_view_append_in_place(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "append after lowering on a VIEW: the write lands in the caller's buffer while it fits the claimed capacity - no promotion");

    char buffer[16] = "hello";
    String view = string_init_4(buffer, 5);

    string_erase(&view, 3, 5);
    string_add_last_1(&view, "p");

    test_expect_string(test, "the view reads \"help\"", "help", string_get_data(&view));
    test_expect_string(test, "written THROUGH to the caller's buffer", "help", buffer);
    test_expect_true(test, "still a view over that buffer", !view.owned && view.data == buffer);

    string_add_last_1(&view, "ers");

    test_expect_string(test, "an append past the claimed capacity promotes", "helpers", string_get_data(&view));
    test_expect_true(test, "now an owner with its own buffer", view.owned && view.data != buffer);

    string_uninit(&view);

    test_case_end(test);
}

static void _test_view_copy_in_place(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_copy into a VIEW that fits: written in place, still a view");

    char buffer[16] = "hello";
    String view = string_init_4(buffer, 5);
    String source = string_init_3("ab");

    string_copy(&view, &source);

    test_expect_string(test, "the view reads the copy", "ab", string_get_data(&view));
    test_expect_string(test, "written THROUGH to the caller's buffer", "ab", buffer);
    test_expect_true(test, "still a view over that buffer", !view.owned && view.data == buffer);

    string_uninit(&source);
    string_uninit(&view);

    test_case_end(test);
}

static void _test_split_wrap_carry_arena(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_split_2 and string_wrap on an arena String: the result carries the arena (released in bulk), never a heap slice");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);
    String source = string_alloc_init_static("alpha,beta", 10, &arena);
    String token = string_split_2(&source, ",", 1);

    test_expect_string(test, "the first token", "alpha", string_get_data(&token));
    test_expect_true(test, "the token carries the source's arena", token.owned && token.allocator == &arena);

    String text = string_alloc_init_static("aaa bbb ccc", 11, &arena);
    String wrapped = string_wrap(&text, 4);

    test_expect_true(test, "the wrapped result carries the source's arena", wrapped.owned && wrapped.allocator == &arena);
    test_expect_true(test, "and holds the reflowed text", string_contains_1(&wrapped, "aaa") && string_get_size(&wrapped) >= 11);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_join_refuses_wholly(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_alloc_join_2 on a REFUSED arena: the EMPTY String (one pre-sized reserve decides the whole join"
        " - an exhausted arena in an unchecked build could otherwise hand back a truncated join that reads as success)");

    Arena small = arena_init_2(0, 8, ARENA_TYPE_LINEAR);
    String part = string_init_3("0123456789012345678901234567890123456789");
    String const *const parts[3] = { &part, &part, &part };
    String joined = string_alloc_join_2(parts, 3, ",", 1, &small);

    test_expect_u(test, "size 0", 0, string_get_size(&joined));
    test_expect_true(test, "the EMPTY state (no buffer), not a partial join", joined.data == nullptr);

    Arena roomy = arena_init_1(4096, ARENA_TYPE_LINEAR);
    String whole = string_alloc_join_2(parts, 3, ",", 1, &roomy);

    test_expect_u(test, "the same join fits a roomy arena: 3 x 40 + 2 separators", 122, string_get_size(&whole));
    test_expect_u(test, "reserved once, exact", 123, string_get_capacity(&whole));

    string_uninit(&part);
    arena_uninit(&roomy, ARENA_TYPE_LINEAR);
    arena_uninit(&small, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_capacity_wrap_refused(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_init_4 / string_move_2 with data_size USIZE_MAX (capacity would wrap to 0): refused - the EMPTY String, and a no-op");

    char buffer[8] = "abc";
    String view = string_init_4(buffer, USIZE_MAX);

    test_expect_u(test, "init_4: size 0", 0, string_get_size(&view));
    test_expect_true(test, "init_4: the EMPTY String", view.data == nullptr && view.capacity == 0);

    String subject = string_init_3("keep");
    char *moved = char_new_2("x");
    char *const before = moved;

    string_move_2(&subject, &moved, USIZE_MAX);

    test_expect_string(test, "move_2: self untouched", "keep", string_get_data(&subject));
    test_expect_true(test, "move_2: the source pointer untouched", moved == before);

    char_delete(moved);
    string_uninit(&subject);

    test_case_end(test);
}

static void _test_add_last_on_empty(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_add_last_* on an EMPTY / zero-length String: an append at size 0 (the forward to string_add_* at self->size)");

    String subject = string_init_1();
    String const tail = string_init_3("cd");
    Str middle = str_init_static("b", 1);

    string_add_last_1(&subject, "a");
    string_add_last_3(&subject, &middle);
    string_add_last_4(&subject, &tail);
    string_add_last_2(&subject, "ef", 2);

    test_expect_string(test, "all four forms appended in order", "abcdef", string_get_data(&subject));
    test_expect_u(test, "size 6", 6, string_get_size(&subject));

    str_uninit(&middle);
    string_uninit(&subject);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Round 3 (aliased insert, wrap on an empty arena String)
 *============================================================================*/
static void _test_add_2_aliased_insert(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_add_2 with data pointing INTO self at an index BELOW size: the source is re-based past the shift"
        " (or copied backward when it straddles the hole) - it used to read the shifted bytes");

    String a = string_init_2(16);

    string_add_last_1(&a, "abcdef");
    string_add_2(&a, string_get_data(&a) + 3, 3, 0);

    test_expect_string(test, "A: own [3,6) inserted at 0 (source above the hole, re-based)", "defabcdef", string_get_data(&a));

    String b = string_init_2(16);

    string_add_last_1(&b, "abcdef");
    string_add_2(&b, string_get_data(&b) + 1, 3, 2);

    test_expect_string(test, "B: own [1,4) inserted at 2 (source straddles the hole, copied backward)", "abbcdcdef", string_get_data(&b));

    String c = string_init_2(16);

    string_add_last_1(&c, "abcdef");
    string_add_2(&c, string_get_data(&c), 2, 4);

    test_expect_string(test, "C: own [0,2) inserted at 4 (source wholly below the hole)", "abcdabef", string_get_data(&c));

    String d = string_init_2(7);

    string_add_last_1(&d, "abcdef");
    string_add_2(&d, string_get_data(&d) + 3, 3, 0);

    test_expect_string(test, "D: case A across a growth (capacity 7 -> reallocated, alias re-based twice)", "defabcdef", string_get_data(&d));

    String e = string_init_2(16);

    string_add_last_1(&e, "abcdef");
    string_add_2(&e, string_get_data(&e) + 3, 3, 6);

    test_expect_string(test, "E: the self-append that always worked", "abcdefdef", string_get_data(&e));

    String f = string_init_2(16);

    string_add_last_1(&f, "abcd");
    string_add_2(&f, string_get_data(&f) + 3, 8, 0);

    test_expect_string(test, "F: a self-aliased source running past size is REFUSED (it used to over-read the block)", "abcd", string_get_data(&f));

    String g = string_init_2(8);

    string_add_last_1(&g, "abcdef");
    string_add_2(&g, string_get_data(&g) + 6, 2, 0);

    test_expect_string(test, "G: a source in the spare capacity [size, capacity) is REFUSED (it used to dangle across the growth)", "abcdef", string_get_data(&g));
    test_expect_u(test, "G: and no growth happened", 8, string_get_capacity(&g));

    String h = string_init_2(7);

    string_add_last_1(&h, "abcdef");
    string_add_2(&h, string_get_data(&h) + 1, 3, 2);

    test_expect_string(test, "H: case B's straddle (own [1,4) inserted at 2) across a growth (capacity 7 -> reallocated, backward copy after the growth)", "abbcdcdef", string_get_data(&h));

    string_uninit(&f);
    string_uninit(&g);
    string_uninit(&h);
    string_uninit(&a);
    string_uninit(&b);
    string_uninit(&c);
    string_uninit(&d);
    string_uninit(&e);

    test_case_end(test);
}

static void _test_wrap_empty_arena_carries_allocator(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_wrap of an empty or whitespace-only ARENA String: the EMPTY result still carries the arena, so a later append lands in the arena, not on the heap");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);
    String blank = string_alloc_init_static("   ", 3, &arena);
    String wrapped = string_wrap(&blank, 4);

    test_expect_true(test, "whitespace-only: EMPTY (no buffer)", wrapped.data == nullptr && string_get_size(&wrapped) == 0);
    test_expect_true(test, "and carrying the source arena (it used to be heap-flavored)", wrapped.allocator == &arena);

    String none = string_alloc_init_1(&arena);
    String wrapped_none = string_wrap(&none, 4);

    test_expect_true(test, "the EMPTY source: EMPTY result carrying the arena too", wrapped_none.data == nullptr && wrapped_none.allocator == &arena);

    string_add_last_1(&wrapped, "x");

    test_expect_true(test, "the follow-on append borrowed from the arena", wrapped.allocator == &arena && string_get_size(&wrapped) == 1);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_move_3_empty_str(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_move_3 of an EMPTY Str (data nullptr - the family's empty-value convention, not a refusal):"
        " self is released and becomes the EMPTY String too - it used to abort");

    String subject = string_init_3("keep");
    Str empty = str_init_static("", 0);
    Str *empty_pointer = &empty;

    string_move_3(&subject, &empty_pointer);

    test_expect_true(test, "self demoted to the EMPTY String", subject.data == nullptr && string_get_size(&subject) == 0);
    test_expect_true(test, "the source pointer was cleared", empty_pointer == nullptr);

    string_uninit(&subject);

    test_case_end(test);
}

static void _test_join_empty_parts_array(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_join_2 / string_alloc_join_2 with parts nullptr AND count 0 (an empty arrayList"
        " handing over its own null data pointer): the EMPTY String, not an abort - count 0 is checked"
        " INSIDE the null-check condition, so the unused pointer value never fires it");

    String joined = string_join_2(nullptr, 0, ",", 1);

    test_expect_true(test, "heap: the EMPTY String", joined.data == nullptr && string_get_size(&joined) == 0);

    Arena arena = arena_init_1(256, ARENA_TYPE_LINEAR);
    String alloc_joined = string_alloc_join_2(nullptr, 0, ",", 1, &arena);

    test_expect_true(test, "arena: the EMPTY String too", alloc_joined.data == nullptr && string_get_size(&alloc_joined) == 0);

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Content Anchors + Anti-Vacuity Closed Form
 *============================================================================*/
static void _test_content_anchors(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "content anchors: equality answers BOTH true and false on real data (a stuck comparator cannot pass)");

    String value = string_init_static("anchor", 6);

    test_expect_true(test, "positive anchor: \"anchor\" equals itself", string_compare_equal_1(&value, "anchor"));
    test_expect_false(test, "negative anchor: \"anchor\" does not equal \"anchOr\"", string_compare_equal_1(&value, "anchOr"));
    test_expect_true(test, "case-insensitive twin does match \"anchOr\"", string_compare_iequal_1(&value, "anchOr"));
    test_expect_false(test, "negative anchor: size mismatch is not equal", string_compare_equal_1(&value, "anchor!"));

    string_uninit(&value);

    test_case_end(test);
}

static void _test_anti_vacuity_closed_form(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "anti-vacuity: every case function ran exactly once (closed-form count) and the string_at sweep really iterated");

    test_expect_u(test, "closed-form case-entry count", _EXPECTED_CASE_COUNT, _case_entered_count);
    test_expect_u(test, "the string_at sweep executed all 10 probes (a silently-stopping loop fails here)", 10, _at_probe_count);
    test_expect_true(test, "positive anchor: in-range probes matched", _at_in_range_match_count > 0);
    test_expect_true(test, "negative anchor: out-of-range probes occurred and answered '\\0'", _at_past_end_zero_count > 0);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry Point
 *============================================================================*/
static void _test_format_empty_render(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_format: a ZERO-byte render is the empty result - size drops to 0 and the bytes read \"\" (only a negative vsnprintf return is an error)");

    String subject = string_init_2(16);

    string_format(&subject, "%s", "payload");

    test_expect_u(test, "the first render holds 7 bytes", 7, string_get_size(&subject));

    string_format(&subject, "%s", "");

    test_expect_u(test, "an empty render sets size to 0 (it used to leave the stale 7)", 0, string_get_size(&subject));
    test_expect_true(test, "the String reports empty", string_empty(&subject));
    test_expect_i(test, "the terminator sits at index 0", (ISize) 0, (ISize) string_get_data(&subject)[0]);

    string_add_last_1(&subject, "x");

    test_expect_string(test, "an append after the empty render lands at 0, not behind stale bytes", "x", string_get_data(&subject));

    string_uninit(&subject);

    test_case_end(test);
}

static void _test_set_size_refuses(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_set_size: a size past capacity - 1 is REFUSED as a no-op in every build; non-zero on the empty String too; a fitting size is applied");

    String subject = string_init_2(8);

    string_add_last_1(&subject, "abc");
    string_set_size(&subject, 8);

    test_expect_u(test, "size == capacity is refused (no room for the terminator)", 3, string_get_size(&subject));

    string_set_size(&subject, USIZE_MAX);

    test_expect_u(test, "USIZE_MAX is refused", 3, string_get_size(&subject));

    string_set_size(&subject, 7);

    test_expect_u(test, "capacity - 1 is the largest legal size and is applied", 7, string_get_size(&subject));

    string_set_size(&subject, 1);

    test_expect_u(test, "lowering is applied", 1, string_get_size(&subject));

    string_uninit(&subject);

    String empty = string_init_1();

    string_set_size(&empty, 1);

    test_expect_u(test, "non-zero on the EMPTY String is refused", 0, string_get_size(&empty));

    string_set_size(&empty, 0);

    test_expect_u(test, "zero on the EMPTY String is the one legal value", 0, string_get_size(&empty));

    test_case_end(test);
}

static void _test_print_empty_string(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_print: the EMPTY String (data nullptr) prints through the log path without handing nullptr to %.*s");

    String const empty = string_init_1();

    string_print(&empty, "empty", true);

    test_expect_u(test, "still empty after printing", 0, string_get_size(&empty));
    test_expect_null(test, "still carries no buffer", empty.data);

    test_case_end(test);
}

static void _test_copy_grow_refused_arena(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_copy: when the grow branch's copy is refused by the arena, self keeps its old content (the copy is built BEFORE self is released)");

    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    test_expect_null(test, "degenerate init left a null handler (the refusal actually happened)", refused.handler);

    char buffer[4] = "abc";
    String subject = string_alloc_init_4(buffer, 3, &refused);
    String source = string_init_static("a much longer payload", 21);

    string_copy(&subject, &source);

    test_expect_string(test, "self still reads its old content after the refused grow", "abc", string_get_data(&subject));
    test_expect_u(test, "self's size is unchanged", 3, string_get_size(&subject));

    string_uninit(&source);

    test_case_end(test);
}

static void _test_trim_promotes_and_empties(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_trim: always reallocates - a VIEW becomes an OWNER, and an all-whitespace input yields an OWNED zero-length String (not the EMPTY state)");

    char buffer[8] = "  ab  ";
    String subject = string_init_4(buffer, 6);

    test_expect_false(test, "starts as a view", subject.owned);

    string_trim(&subject);

    test_expect_string(test, "trimmed to \"ab\"", "ab", string_get_data(&subject));
    test_expect_true(test, "promoted to an owner (the buffer is no longer the caller's)", subject.owned);
    test_expect_true(test, "the caller's buffer is untouched", buffer[0] == ' ' && buffer[2] == 'a');

    string_uninit(&subject);

    char blank[4] = "   ";
    String whitespace = string_init_4(blank, 3);

    string_trim(&whitespace);

    test_expect_u(test, "all-whitespace trims to size 0", 0, string_get_size(&whitespace));
    test_expect_true(test, "the result is an OWNED zero-length String, not the EMPTY state", whitespace.owned && whitespace.data != nullptr);

    string_uninit(&whitespace);

    test_case_end(test);
}

static void _test_alloc_trim_refused_arena(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_alloc_trim: when the arena refuses the trimmed copy, self keeps its content (no release-for-nothing)");

    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    test_expect_null(test, "degenerate init left a null handler (the refusal actually happened)", refused.handler);

    char buffer[8] = "  ab  ";
    String subject = string_alloc_init_4(buffer, 6, &refused);

    string_alloc_trim(&subject, &refused);

    test_expect_string(test, "self still reads its untrimmed content after the refusal", "  ab  ", string_get_data(&subject));
    test_expect_u(test, "size unchanged", 6, string_get_size(&subject));
    test_expect_false(test, "still a view over the caller's buffer", subject.owned);

    test_case_end(test);
}

static void _test_alloc_trim_live_arena_whitespace(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_alloc_trim on a LIVE arena: all-whitespace trims to an OWNED zero-length String - a result, not a refusal (the guard tests the pointer)");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);
    String subject = string_alloc_init_static("   ", 3, &arena);

    string_alloc_trim(&subject, &arena);

    test_expect_u(test, "trimmed to size 0 (it used to be skipped as a refusal)", 0, string_get_size(&subject));
    test_expect_true(test, "an owned zero-length String, not the EMPTY state", subject.owned && subject.data != nullptr);

    String blank = string_alloc_init_static("  ab  ", 6, &arena);

    string_alloc_trim(&blank, &arena);

    test_expect_string(test, "a non-blank source trims normally on the same arena", "ab", string_get_data(&blank));

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_split_first_token_owned(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "string_split_2: returns an OWNED copy of the FIRST token only; mutating it leaves the source intact; absent delimiter yields the whole self");

    String source = string_init_static("one,two,three", 13);
    String token = string_split_2(&source, ",", 1);

    test_expect_string(test, "the first token", "one", string_get_data(&token));
    test_expect_true(test, "the token is owned (release with string_uninit)", token.owned);

    string_upper(&token);

    test_expect_string(test, "mutating the token does not touch the source", "one,two,three", string_get_data(&source));

    string_uninit(&token);

    String whole = string_split_2(&source, ";", 1);

    test_expect_string(test, "an absent delimiter yields the whole self", "one,two,three", string_get_data(&whole));

    string_uninit(&whole);
    string_uninit(&source);

    test_case_end(test);
}

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/container/string/test_string.c");

    test_suite_begin(&test, "string_format");
    _test_format_grow_and_retry(&test);
    _test_format_fits_no_growth(&test);
    _test_format_view_grow_to_owner(&test);
    _test_format_grow_from_empty(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "string_erase");
    _test_erase_exclusive_honest_size(&test);
    _test_erase_span_refusal(&test);
    _test_erase_str_parity(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "string_refused_arena");
    _test_alloc_init_2_refused_coherent_empty(&test);
    _test_alloc_init_2_real_arena(&test);
    _test_alloc_new_refused_nullptr(&test);
    _test_reserve_refused_arena_no_op(&test);
    _test_alloc_init_static_refused(&test);
    _test_repeat_refused_reserve_view_no_op(&test);
    _test_alloc_from_numbers_refused(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "string_add_at");
    _test_add_2_index_refusal(&test);
    _test_add_2_growth_crossing(&test);
    _test_add_2_self_alias(&test);
    _test_add_2_self_alias_across_growth(&test);
    _test_growth_arithmetic_doubling(&test);
    _test_at_parity(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "string_slice");
    _test_slice_empty_tail(&test);
    _test_slice_range_inclusive(&test);
    _test_slice_arena_carrying(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "string_move");
    _test_move_3_cross_allocator_refusal(&test);
    _test_move_4_cross_allocator_refusal(&test);
    _test_move_4_same_allocator(&test);
    _test_move_3_preserves_viewness(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "string_ownership_matrix");
    _test_ownership_views(&test);
    _test_ownership_producers(&test);
    _test_ownership_move_2_adoption(&test);
    _test_repeat_zero_keeps_allocation(&test);
    _test_arena_twins_bulk_release(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "string_wrap");
    _test_wrap_adoption(&test);
    _test_wrap_empty_paths(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "string_reserve_shrink");
    _test_reserve_view_to_owner(&test);
    _test_shrink_keeps_bytes(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "string_remove_replace_refusals");
    _test_remove_refusals(&test);
    _test_replace_2_live_refusal(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "string_round_1");
    _test_format_empty_render(&test);
    _test_set_size_refuses(&test);
    _test_print_empty_string(&test);
    _test_copy_grow_refused_arena(&test);
    _test_trim_promotes_and_empties(&test);
    _test_alloc_trim_refused_arena(&test);
    _test_alloc_trim_live_arena_whitespace(&test);
    _test_split_first_token_owned(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "string_round_2");
    _test_shrink_idempotent(&test);
    _test_view_append_in_place(&test);
    _test_view_copy_in_place(&test);
    _test_split_wrap_carry_arena(&test);
    _test_join_refuses_wholly(&test);
    _test_capacity_wrap_refused(&test);
    _test_add_last_on_empty(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "string_round_3");
    _test_add_2_aliased_insert(&test);
    _test_wrap_empty_arena_carries_allocator(&test);
    _test_move_3_empty_str(&test);
    _test_join_empty_parts_array(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "string_anchors");
    _test_content_anchors(&test);
    _test_anti_vacuity_closed_form(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}