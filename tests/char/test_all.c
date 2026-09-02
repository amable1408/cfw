#include <arena/arena.h>
#include <container/string/string.h>
#include <test/test.h>

/* Covers the timing-safe (comptime) compare family across char / str / string.
 * Timing itself is not unit-testable, so this asserts functional correctness:
 * equal/unequal, length-mismatch rejection, and ASCII case handling. */

#define _SECRET "s3cr3t-token-value"

static void _test_char_comptime(Test *const test) {
    test_case_begin(test, "char comptime equal / iequal");

    test_expect_true(test, "equal_1 match", char_compare_equal_comptime_1(_SECRET, _SECRET));
    test_expect_false(test, "equal_1 last-byte differ", char_compare_equal_comptime_1(_SECRET, "s3cr3t-token-valuX"));
    test_expect_false(test, "equal_1 length differ", char_compare_equal_comptime_1(_SECRET, "s3cr3t"));

    test_expect_true(test, "equal_2 sized match", char_compare_equal_comptime_2("abc", 3, "abc", 3));
    test_expect_false(test, "equal_2 sized differ", char_compare_equal_comptime_2("abc", 3, "abd", 3));
    test_expect_true(test, "equal_2 empty match", char_compare_equal_comptime_2("", 0, "", 0));

    test_expect_true(test, "iequal_1 case-fold match", char_compare_iequal_comptime_1("AbC-Token", "abc-token"));
    test_expect_false(test, "iequal_1 differ", char_compare_iequal_comptime_1("AbC", "abd"));
    test_expect_true(test, "iequal_2 sized case-fold", char_compare_iequal_comptime_2("ABC", 3, "abc", 3));

    test_case_end(test);
}

static void _test_str_comptime(Test *const test) {
    test_case_begin(test, "str comptime equal / iequal");

    Str const value = str_init_2((char*) _SECRET);
    Str const other = str_init_2((char*) "ABC");

    test_expect_true(test, "equal_1 (char*)", str_compare_equal_comptime_1(&value, _SECRET));
    test_expect_false(test, "equal_1 differ", str_compare_equal_comptime_1(&value, "nope"));
    test_expect_true(test, "equal_2 sized", str_compare_equal_comptime_2(&value, _SECRET, char_length(_SECRET)));
    test_expect_true(test, "equal_3 (Str)", str_compare_equal_comptime_3(&value, &value));
    test_expect_true(test, "iequal_1 case-fold", str_compare_iequal_comptime_1(&other, "abc"));

    test_case_end(test);
}

static void _test_string_comptime(Test *const test) {
    test_case_begin(test, "string comptime equal / iequal");

    String value = string_init_1();
    string_add_last_1(&value, (char*) _SECRET);

    Str const value_str = str_init_2((char*) _SECRET);

    test_expect_true(test, "equal_1 (char*)", string_compare_equal_comptime_1(&value, _SECRET));
    test_expect_false(test, "equal_1 differ", string_compare_equal_comptime_1(&value, "nope"));
    test_expect_true(test, "equal_2 sized", string_compare_equal_comptime_2(&value, _SECRET, char_length(_SECRET)));
    test_expect_true(test, "equal_3 (Str)", string_compare_equal_comptime_3(&value, &value_str));
    test_expect_true(test, "equal_4 (String)", string_compare_equal_comptime_4(&value, &value));
    test_expect_true(test, "iequal_1 case-fold", string_compare_iequal_comptime_1(&value, "S3CR3T-TOKEN-VALUE"));

    string_uninit(&value);

    test_case_end(test);
}

static void _test_char_try_to_bool(Test *const test) {
    test_case_begin(test, "char try-to-bool shared dialect");

    bool value = false;

    test_expect_true(test, "1 parses", char_try_to_bool("1", &value));
    test_expect_true(test, "1 is true", value);
    test_expect_true(test, "TRUE parses case-insensitive", char_try_to_bool("TRUE", &value));
    test_expect_true(test, "TRUE is true", value);
    test_expect_true(test, "on parses", char_try_to_bool("on", &value));
    test_expect_true(test, "on is true", value);

    test_expect_true(test, "0 parses", char_try_to_bool("0", &value));
    test_expect_false(test, "0 is false", value);
    test_expect_true(test, "No parses case-insensitive", char_try_to_bool("No", &value));
    test_expect_false(test, "No is false", value);
    test_expect_true(test, "OFF parses", char_try_to_bool("OFF", &value));
    test_expect_false(test, "OFF is false", value);

    value = true;

    test_expect_false(test, "garbage rejected", char_try_to_bool("banana", &value));
    test_expect_true(test, "out untouched on failure", value);
    test_expect_false(test, "empty rejected", char_try_to_bool("", &value));

    test_case_end(test);
}

static void _test_char_try_to_number(Test *const test) {
    test_case_begin(test, "char try-to-number strict parse");

    ISize i = 0;
    USize u = 0;
    FSize f = 0.0;

    test_expect_true(test, "i valid", char_try_to_number_i("42", &i));
    test_expect_i(test, "i value", 42, i);
    test_expect_false(test, "i trailing junk", char_try_to_number_i("42x", &i));
    test_expect_false(test, "i overflow", char_try_to_number_i("99999999999999999999", &i));

    test_expect_true(test, "u valid", char_try_to_number_u("100", &u));
    test_expect_u(test, "u value", 100, u);
    test_expect_false(test, "u negative", char_try_to_number_u("-5", &u));

    test_expect_true(test, "f valid", char_try_to_number_f("3.5", &f));
    test_expect_f(test, "f value", 3.5, f, 0.0001);
    test_expect_false(test, "f empty", char_try_to_number_f("", &f));

    test_case_end(test);
}

static void _test_char_copy_3_terminator(Test *const test) {
    test_case_begin(test, "char_copy_3 terminator behavior (incident: cfw-char-copy3-no-terminator)");

    /* REGRESSION PIN (incident: char_copy_3 never terminated, which crashed a
     * live server when a short copy left a long previous value's tail visible
     * past it). Fixed 2026-08-22: char_copy_3 now writes self[data_size] =
     * '\0' in every case, so no stale tail can survive a copy. data_size <
     * self_capacity is enforced by the function's own bound check, so the
     * terminator is always in range. */
    char buffer[8];
    char_fill(buffer, sizeof(buffer), 'Z');

    char_copy_3(buffer, sizeof(buffer), "ab", 2);

    test_expect_true(test, "copied bytes land", buffer[0] == 'a' && buffer[1] == 'b');
    test_expect_true(test, "terminator written at data_size (no stale tail)", buffer[2] == '\0');
    test_expect_true(test, "bytes past the terminator are untouched", buffer[3] == 'Z');

    /* The exact incident shape: a long value, then a short one over it. */
    char reused[32];
    char_fill(reused, sizeof(reused), 'Z');
    char_copy_3(reused, sizeof(reused), "LONGLONGLONGSTRING", 18);
    char_copy_3(reused, sizeof(reused), "short", 5);

    test_expect_string(test, "short copy over a long one reads back clean", "short", reused);

    char buffer_zero[4];
    char_fill(buffer_zero, sizeof(buffer_zero), 'Z');

    char_copy_3(buffer_zero, sizeof(buffer_zero), "x", 0);

    test_expect_true(test, "data_size==0 IS terminated at self[0]", buffer_zero[0] == '\0');

    test_case_end(test);
}

static void _test_char_find_slice_5_needle_boundary(Test *const test) {
    test_case_begin(test, "char_find_slice_5 over-long needle (incident: cfw-char-find-slice5-abort)");

    /* Pins both sides of the boundary: a needle exactly filling the remaining
     * haystack matches, and anything longer is NOT FOUND rather than fatal. */
    char const *const haystack = "hello";
    USize const haystack_size  = char_length(haystack);

    char const *const exact_fit = char_find_slice_5(haystack, haystack_size, 2, "llo", 3);
    test_expect_true(test, "needle exactly fills remaining haystack", exact_fit == haystack + 2);

    char const *const no_match = char_find_slice_5(haystack, haystack_size, 0, "xyz", 3);
    test_expect_null(test, "no match within bounds returns null", (void*) no_match);

    /* The incident case, now EXECUTABLE: a needle longer than the remaining
     * haystack used to abort the process (scanning a 1-byte request path for
     * ".." was a whole-server DoS). Fixed 2026-08-22 - it answers "not found". */
    char const *const over_long = char_find_slice_5(haystack, haystack_size, 3, "lo!", 3);
    test_expect_null(test, "needle longer than the remaining haystack returns null", (void*) over_long);

    char const *const tiny = char_find_slice_5("a", 1, 0, "..", 2);
    test_expect_null(test, "the GET /a shape: 2-byte needle in a 1-byte haystack", (void*) tiny);

    test_case_end(test);
}

static void _test_add_fixed_aliased_source(Test *const test) {
    test_case_begin(test, "char_add_fixed_2 with data pointing INTO self: the insert path reads the source from its"
        " snapshot (it used to read bytes the replace had just overwritten); a source running past size or in the"
        " spare capacity is REFUSED");

    char a[16] = "abcdef";

    char_add_fixed_2(a, 16, a + 3, 3, 0);

    test_expect_string(test, "A: own [3,6) inserted at 0", "defabcdef", a);

    char b[16] = "abcdef";

    char_add_fixed_2(b, 16, b + 1, 3, 2);

    test_expect_string(test, "B: own [1,4) inserted at 2 (straddles the hole)", "abbcdcdef", b);

    char c[16] = "abcdef";

    char_add_fixed_2(c, 16, c, 2, 4);

    test_expect_string(test, "C: own [0,2) inserted at 4", "abcdabef", c);

    char f[16] = "abcd";

    char_add_fixed_2(f, 16, f + 3, 8, 0);

    test_expect_string(test, "F: a source running past size is REFUSED", "abcd", f);

    char g[16] = "abcdef";

    char_add_fixed_2(g, 16, g + 6, 2, 0);

    test_expect_string(test, "G: a source in the spare capacity is REFUSED", "abcdef", g);

    test_case_end(test);
}

static void _test_refused_arena_never_aborts(Test *const test) {
    test_case_begin(test, "char_alloc_*: REFUSED arena - producers answer nullptr, allocating mutators leave *self unchanged, add_fixed's non-allocating paths still succeed; never an abort");

    /* Degenerate geometry (byte_size 0) leaves the handler null: the documented refused-arena
     * state whose allocator_borrow answers null. */
    Arena refused = arena_init_2(0, 8, ARENA_TYPE_LINEAR);

    test_expect_null(test, "degenerate init left a null handler (the refusal actually happened)", refused.handler);

    char const *const parts[2] = { "a", "b" };

    test_expect_null(test, "char_alloc_join_1 refuses to nullptr", char_alloc_join_1(parts, 2, ",", &refused));
    test_expect_null(test, "char_alloc_join_1 of zero parts (the \"\" sentinel) refuses to nullptr", char_alloc_join_1(parts, 0, ",", &refused));
    test_expect_null(test, "char_alloc_new_2 refuses to nullptr", char_alloc_new_2("copy me", &refused));
    test_expect_null(test, "char_alloc_from_numbers_int_1 refuses to nullptr", char_alloc_from_numbers_int_1(-42, &refused));
    test_expect_null(test, "char_alloc_from_numbers_uint_1 refuses to nullptr", char_alloc_from_numbers_uint_1(42, &refused));
    test_expect_null(test, "char_alloc_new_replace_1 refuses to nullptr", char_alloc_new_replace_1("a-b", "-", "+", &refused));
    test_expect_null(test, "char_alloc_new_slice_range_1 refuses to nullptr", char_alloc_new_slice_range_1("abcdef", 1, 3, &refused));
    test_expect_null(test, "char_alloc_repeat_1 refuses to nullptr", char_alloc_repeat_1("ab", 3, &refused));
    test_expect_null(test, "char_alloc_repeat_1 with count 0 (the \"\" sentinel) refuses to nullptr", char_alloc_repeat_1("ab", 0, &refused));
    test_expect_null(test, "char_alloc_from_trim_2 refuses to nullptr", char_alloc_from_trim_2("  x  ", 5, &refused));

    /* The mutator subject lives on the heap: a mutator's SUCCESS path releases the old buffer, so a
     * stack address here would turn a regression into a free of stack memory instead of a red
     * assertion. */
    char *self = char_new_2("  abc  ");
    char *const original = self;

    char_alloc_add_last_1(&self, "d", &refused);

    test_expect_true(test, "char_alloc_add_last_1 leaves *self pointing at the original buffer", self == original);
    test_expect_string(test, "and its content unchanged", "  abc  ", self);

    char_alloc_remove_1(&self, 1, &refused);

    test_expect_true(test, "char_alloc_remove_1 leaves *self pointing at the original buffer", self == original);
    test_expect_string(test, "and its content unchanged", "  abc  ", self);

    char_alloc_trim_1(&self, &refused);

    test_expect_true(test, "char_alloc_trim_1 leaves *self pointing at the original buffer (it used to store the refused nullptr)", self == original);
    test_expect_string(test, "and its content untrimmed", "  abc  ", self);

    char_delete(self);

    char fixed[8] = "abc";

    char_alloc_add_fixed_1(fixed, 8, "X", 1, &refused);

    test_expect_string(test, "char_alloc_add_fixed_1 (insert needing a snapshot) leaves self unchanged", "abc", fixed);

    /* Index 3 == size is the append (index 2, the old size-1 append, is an insert now). */
    char_alloc_add_fixed_1(fixed, 8, "X", 3, &refused);

    test_expect_string(test, "the append path needs no allocation and still succeeds on the refused arena", "abcX", fixed);

    char empty_target[8] = "";

    char_alloc_add_fixed_1(empty_target, 8, "Y", 0, &refused);

    test_expect_string(test, "the empty-target path needs no allocation and still succeeds", "Y", empty_target);

    test_case_end(test);
}

static void _test_char_numbers_by_content(Test *const test) {
    test_case_begin(test, "char numbers by CONTENT: the lossy parsers saturate, the float parser neither wraps nor overflows its"
        " digit counter, the formatters refuse wholly and round with a carry");

    /* Parsers: a wrap would turn a huge value into a small PLAUSIBLE one (ruling 942). The
     * fuzzer proves these do not crash; only a content pin proves the value. */
    test_expect_u(test, "uint_1 saturates at USIZE_MAX on a 21-digit input", USIZE_MAX, char_to_numbers_uint_1("184467440737095516160"));
    test_expect_i(test, "int_1 saturates at ISIZE_MAX", ISIZE_MAX, char_to_numbers_int_1("99999999999999999999"));
    test_expect_i(test, "int_1 saturates at ISIZE_MIN under a sign", ISIZE_MIN, char_to_numbers_int_1("-99999999999999999999"));
    test_expect_true(test, "float_1 of a 25-digit integer part is >= 1e24 (the USize accumulator wrapped it)", char_to_numbers_float_1("1000000000000000000000000") >= 1e24);

    /* 0.5 followed by 300 zeros: the U8 digit counter used to wrap at 256 digits. */
    char long_fraction[304] = DEFAULT_INITIALIZATION;

    char_copy_3(long_fraction, sizeof(long_fraction), "0.5", 3);
    char_fill(long_fraction + 3, 300, '0');

    test_expect_f(test, "float_1 with 300 fraction digits still parses 0.5", 0.5, char_to_numbers_float_1(long_fraction), 1e-12);
    test_expect_f(test, "float_1 keeps its lenient dialect: \"1.2.3\" -> 1.23", 1.23, char_to_numbers_float_1("1.2.3"), 1e-12);
    test_expect_f(test, "float_1 keeps its lenient dialect: \"1-2\" -> -12", -12.0, char_to_numbers_float_1("1-2"), 1e-12);
    test_expect_f(test, "float_1 stops at the first foreign byte: \"3.25x9\" -> 3.25", 3.25, char_to_numbers_float_1("3.25x9"), 1e-12);

    /* Formatters: a too-small buffer answers the empty sentinel, never a truncated number
     * (the family's whole "empty means refused" premise). */
    char buffer[16] = DEFAULT_INITIALIZATION;

    char_from_numbers_uint_1(buffer, 4, 12345);

    test_expect_string(test, "from_numbers_uint_1 into a 4-byte buffer refuses WHOLLY to \"\"", "", buffer);

    char_from_numbers_float_2(buffer, sizeof(buffer), 0.99999, 4);

    test_expect_string(test, "float_2 carries a fraction that rounds up: 0.99999 @ 4 -> \"1.0000\"", "1.0000", buffer);

    char_from_numbers_float_2(buffer, sizeof(buffer), 9.99, 1);

    test_expect_string(test, "float_2 carries across a digit boundary: 9.99 @ 1 -> \"10.0\"", "10.0", buffer);

    char_from_numbers_float_2(buffer, sizeof(buffer), -0.99999, 4);

    test_expect_string(test, "float_2 carries under a sign: -0.99999 @ 4 -> \"-1.0000\"", "-1.0000", buffer);

    char_from_numbers_float_2(buffer, sizeof(buffer), 1.25, 1);

    test_expect_string(test, "float_2 rounds half up without a carry: 1.25 @ 1 -> \"1.3\"", "1.3", buffer);

    test_case_end(test);
}

static void _test_char_round_2_pins(Test *const test) {
    test_case_begin(test, "char design round 2: precision 19 renders exactly, precision 0 emits no point, copy_3 and split_next REFUSE"
        " in this checked build (their error_checks are gone), try_to_number_f accepts an underflow");

    char buffer[32] = DEFAULT_INITIALIZATION;

    char_from_numbers_float_2(buffer, sizeof(buffer), 3.0, 19);

    test_expect_string(test, "3.0 @ 19 (the largest USize scale) is exact, not value + 1", "3.0000000000000000000", buffer);

    char_from_numbers_float_2(buffer, sizeof(buffer), 2.5, 0);

    test_expect_string(test, "2.5 @ 0 rounds half up with no dangling point", "3", buffer);

    char_from_numbers_float_2(buffer, sizeof(buffer), 2.4, 0);

    test_expect_string(test, "2.4 @ 0", "2", buffer);

    char small[4] = DEFAULT_INITIALIZATION;

    char_copy_3(small, sizeof(small), "abcd", 4);

    test_expect_string(test, "char_copy_3 of 4 bytes into a 4-byte buffer REFUSES to \"\" (it aborted in checked builds)", "", small);

    char_copy_3(small, sizeof(small), "abc", 3);

    test_expect_string(test, "char_copy_3 of 3 bytes into a 4-byte buffer fits", "abc", small);

    USize index = 10;
    USize token_from = 0;
    USize token_size = 0;

    test_expect_false(test, "char_split_next with a cursor past the end answers false (it aborted in checked builds)", char_split_next("a,b", 3, ",", 1, &index, &token_from, &token_size));

    FSize tiny = 1.0;

    test_expect_true(test, "char_try_to_number_f accepts an underflowing value (\"1e-310\", a denormal)", char_try_to_number_f("1e-310", &tiny));
    test_expect_true(test, "and the value is tiny, not the untouched 1.0", tiny > 0.0 && tiny < 1e-300);
    test_expect_false(test, "overflow (\"1e999\") still fails", char_try_to_number_f("1e999", &tiny));

    test_case_end(test);
}

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/char/test_all.c");

    test_suite_begin(&test, "char_comptime");
    _test_char_comptime(&test);
    _test_str_comptime(&test);
    _test_string_comptime(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "char_convert");
    _test_char_try_to_bool(&test);
    _test_char_try_to_number(&test);
    _test_char_numbers_by_content(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "char_design_round_2");
    _test_char_round_2_pins(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "char_copy_and_find_gotchas");
    _test_char_copy_3_terminator(&test);
    _test_char_find_slice_5_needle_boundary(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "char_refused_arena");
    _test_refused_arena_never_aborts(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "char_add_fixed_alias");
    _test_add_fixed_aliased_source(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}