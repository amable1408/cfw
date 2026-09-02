#include <arena/arena.h>
#include <char/char.h>
#include <test/test.h>

/*
 * Coverage for CFW char's ADD family (~48 functions): char_add_*, char_add_first_*,
 * char_add_last_*, their _fixed twins, and every one's char_alloc_add_* arena twin.
 *
 * REGRESSION PIN (incident: _char_add_fixed splice tail restore, fixed 2026-08-22):
 * the internal splice kernel used to snapshot only the PREFIX of self
 * (char_copy_2(buffer, self, self_index)) before overwriting self in place, then
 * restored the "tail" from buffer + self_index - a region the snapshot never
 * touched, so it read back as the allocator's zero fill. Every insert with a
 * non-empty tail replaced that tail with zeros. Fixed by snapshotting the WHOLE
 * string instead (char_copy_2(buffer, self, self_size)). The result stayed in
 * bounds throughout, which is why no sanitizer or fuzzer could see it - this suite
 * pins it by CONTENT, not by crash. _test_regression_fixed_splice below is the
 * exact given shape (char_add_first_fixed_1 of "hello " onto "world" must give
 * "hello world", not "hello ").
 *
 * REGRESSION PIN (design HIGH 1, fixed 2026-09-01): both kernels used to special-case
 * self_index == self_size - 1 as an APPEND rather than an insert before the last
 * character, so char_add_1 of "X" at index 2 into "abc" gave "abcX", and every
 * char_add_first_* onto a ONE-character string appended. The append now fires only at
 * self_index == self_size. _test_regression_index_append_flip pins the flip point by
 * content, and every ladder carries a one-character-base case.
 *
 * REFUSAL PIN (design HIGH 2, same date): the kernels' bounds - index past the end,
 * size overflow, and the fixed forms' capacity fit - were error_checks, which compile
 * away without ERROR_CHECK_ENABLED and left an unbounded write. They are real control
 * flow now: a no-op with self unchanged, in every build, so the old abort-probe child
 * is gone and _test_fixed_capacity_one_over_refuses observes the refusal in-process.
 */

/*==============================================================================
 * MARK: - Shared Helpers
 *============================================================================*/
#define _FIXED_BUFFER_SIZE 128

static void _expect_string_labeled(Test *const test, char const *const prefix, char const *const suffix, char const *const expected, char const *const actual) {
    char message[192] = DEFAULT_INITIALIZATION;

    char_format(message, sizeof(message), "%s: %s", prefix, suffix);

    test_expect_string(test, message, expected, actual);
}

static void _expect_true_labeled(Test *const test, char const *const prefix, char const *const suffix, bool const actual) {
    char message[192] = DEFAULT_INITIALIZATION;

    char_format(message, sizeof(message), "%s: %s", prefix, suffix);

    test_expect_true(test, message, actual);
}

/*==============================================================================
 * MARK: - Heap Arity-Ladder Helpers (char_add_* / char_add_first_* / char_add_last_*)
 *============================================================================*/
/**
 * @brief Run char_add_1..4 with logically identical arguments and assert every arity
 * agrees with the others and with expected. Cheapest way to cover all four call
 * shapes, and catches a discarded-parameter defect (an arity silently ignoring one
 * of its own arguments), which bit this module three times already.
 */
static void _heap_ladder_add(Test *const test, char const *const base, char const *const data, USize const index, char const *const expected, char const *const label) {
    USize const base_size = char_length(base);
    USize const data_size = char_length(data);

    char *s1 = char_new_2(base);
    char *s2 = char_new_2(base);
    char *s3 = char_new_2(base);
    char *s4 = char_new_2(base);

    char_add_1(&s1, data, index);
    char_add_2(&s2, data, data_size, index);
    char_add_3(&s3, base_size, data, index);
    char_add_4(&s4, base_size, data, data_size, index);

    _expect_string_labeled(test, label, "char_add_1", expected, s1);
    _expect_string_labeled(test, label, "char_add_2", expected, s2);
    _expect_string_labeled(test, label, "char_add_3", expected, s3);
    _expect_string_labeled(test, label, "char_add_4", expected, s4);
    _expect_true_labeled(test, label, "arity ladder agrees (_1.._4 identical)", char_equal_1(s1, s2) && char_equal_1(s2, s3) && char_equal_1(s3, s4));

    char_delete(s1);
    char_delete(s2);
    char_delete(s3);
    char_delete(s4);
}

static void _heap_ladder_add_first(Test *const test, char const *const base, char const *const data, char const *const expected, char const *const label) {
    USize const base_size = char_length(base);
    USize const data_size = char_length(data);

    char *s1 = char_new_2(base);
    char *s2 = char_new_2(base);
    char *s3 = char_new_2(base);
    char *s4 = char_new_2(base);

    char_add_first_1(&s1, data);
    char_add_first_2(&s2, data, data_size);
    char_add_first_3(&s3, base_size, data);
    char_add_first_4(&s4, base_size, data, data_size);

    _expect_string_labeled(test, label, "char_add_first_1", expected, s1);
    _expect_string_labeled(test, label, "char_add_first_2", expected, s2);
    _expect_string_labeled(test, label, "char_add_first_3", expected, s3);
    _expect_string_labeled(test, label, "char_add_first_4", expected, s4);
    _expect_true_labeled(test, label, "arity ladder agrees (_1.._4 identical)", char_equal_1(s1, s2) && char_equal_1(s2, s3) && char_equal_1(s3, s4));

    char_delete(s1);
    char_delete(s2);
    char_delete(s3);
    char_delete(s4);
}

static void _heap_ladder_add_last(Test *const test, char const *const base, char const *const data, char const *const expected, char const *const label) {
    USize const base_size = char_length(base);
    USize const data_size = char_length(data);

    char *s1 = char_new_2(base);
    char *s2 = char_new_2(base);
    char *s3 = char_new_2(base);
    char *s4 = char_new_2(base);

    char_add_last_1(&s1, data);
    char_add_last_2(&s2, data, data_size);
    char_add_last_3(&s3, base_size, data);
    char_add_last_4(&s4, base_size, data, data_size);

    _expect_string_labeled(test, label, "char_add_last_1", expected, s1);
    _expect_string_labeled(test, label, "char_add_last_2", expected, s2);
    _expect_string_labeled(test, label, "char_add_last_3", expected, s3);
    _expect_string_labeled(test, label, "char_add_last_4", expected, s4);
    _expect_true_labeled(test, label, "arity ladder agrees (_1.._4 identical)", char_equal_1(s1, s2) && char_equal_1(s2, s3) && char_equal_1(s3, s4));

    char_delete(s1);
    char_delete(s2);
    char_delete(s3);
    char_delete(s4);
}

/*==============================================================================
 * MARK: - Fixed Arity-Ladder Helpers (char_add_fixed_* / _first_fixed_* / _last_fixed_*)
 *============================================================================*/
static void _fixed_ladder_add(Test *const test, char const *const base, USize const capacity, char const *const data, USize const index, char const *const expected, char const *const label) {
    char b1[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b2[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b3[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b4[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    USize const base_size = char_length(base);
    USize const data_size = char_length(data);

    char_copy_3(b1, capacity, base, base_size);
    char_copy_3(b2, capacity, base, base_size);
    char_copy_3(b3, capacity, base, base_size);
    char_copy_3(b4, capacity, base, base_size);

    char_add_fixed_1(b1, capacity, data, index);
    char_add_fixed_2(b2, capacity, data, data_size, index);
    char_add_fixed_3(b3, capacity, base_size, data, index);
    char_add_fixed_4(b4, capacity, base_size, data, data_size, index);

    _expect_string_labeled(test, label, "char_add_fixed_1", expected, b1);
    _expect_string_labeled(test, label, "char_add_fixed_2", expected, b2);
    _expect_string_labeled(test, label, "char_add_fixed_3", expected, b3);
    _expect_string_labeled(test, label, "char_add_fixed_4", expected, b4);
    _expect_true_labeled(test, label, "arity ladder agrees (_1.._4 identical)", char_equal_1(b1, b2) && char_equal_1(b2, b3) && char_equal_1(b3, b4));
}

static void _fixed_ladder_add_first(Test *const test, char const *const base, USize const capacity, char const *const data, char const *const expected, char const *const label) {
    char b1[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b2[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b3[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b4[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    USize const base_size = char_length(base);
    USize const data_size = char_length(data);

    char_copy_3(b1, capacity, base, base_size);
    char_copy_3(b2, capacity, base, base_size);
    char_copy_3(b3, capacity, base, base_size);
    char_copy_3(b4, capacity, base, base_size);

    char_add_first_fixed_1(b1, capacity, data);
    char_add_first_fixed_2(b2, capacity, data, data_size);
    char_add_first_fixed_3(b3, capacity, base_size, data);
    char_add_first_fixed_4(b4, capacity, base_size, data, data_size);

    _expect_string_labeled(test, label, "char_add_first_fixed_1", expected, b1);
    _expect_string_labeled(test, label, "char_add_first_fixed_2", expected, b2);
    _expect_string_labeled(test, label, "char_add_first_fixed_3", expected, b3);
    _expect_string_labeled(test, label, "char_add_first_fixed_4", expected, b4);
    _expect_true_labeled(test, label, "arity ladder agrees (_1.._4 identical)", char_equal_1(b1, b2) && char_equal_1(b2, b3) && char_equal_1(b3, b4));
}

static void _fixed_ladder_add_last(Test *const test, char const *const base, USize const capacity, char const *const data, char const *const expected, char const *const label) {
    char b1[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b2[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b3[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b4[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    USize const base_size = char_length(base);
    USize const data_size = char_length(data);

    char_copy_3(b1, capacity, base, base_size);
    char_copy_3(b2, capacity, base, base_size);
    char_copy_3(b3, capacity, base, base_size);
    char_copy_3(b4, capacity, base, base_size);

    char_add_last_fixed_1(b1, capacity, data);
    char_add_last_fixed_2(b2, capacity, data, data_size);
    char_add_last_fixed_3(b3, capacity, base_size, data);
    char_add_last_fixed_4(b4, capacity, base_size, data, data_size);

    _expect_string_labeled(test, label, "char_add_last_fixed_1", expected, b1);
    _expect_string_labeled(test, label, "char_add_last_fixed_2", expected, b2);
    _expect_string_labeled(test, label, "char_add_last_fixed_3", expected, b3);
    _expect_string_labeled(test, label, "char_add_last_fixed_4", expected, b4);
    _expect_true_labeled(test, label, "arity ladder agrees (_1.._4 identical)", char_equal_1(b1, b2) && char_equal_1(b2, b3) && char_equal_1(b3, b4));
}

/*==============================================================================
 * MARK: - Arena Arity-Ladder Helpers (char_alloc_add_* twins)
 *============================================================================*/
static void _arena_ladder_add(
    Test *const test, Arena *const allocator, char const *const base, char const *const data, USize const index, char const *const expected, char const *const label) {
    USize const base_size = char_length(base);
    USize const data_size = char_length(data);

    char *s1 = char_alloc_new_2(base, allocator);
    char *s2 = char_alloc_new_2(base, allocator);
    char *s3 = char_alloc_new_2(base, allocator);
    char *s4 = char_alloc_new_2(base, allocator);

    char_alloc_add_1(&s1, data, index, allocator);
    char_alloc_add_2(&s2, data, data_size, index, allocator);
    char_alloc_add_3(&s3, base_size, data, index, allocator);
    char_alloc_add_4(&s4, base_size, data, data_size, index, allocator);

    _expect_string_labeled(test, label, "char_alloc_add_1", expected, s1);
    _expect_string_labeled(test, label, "char_alloc_add_2", expected, s2);
    _expect_string_labeled(test, label, "char_alloc_add_3", expected, s3);
    _expect_string_labeled(test, label, "char_alloc_add_4", expected, s4);
    _expect_true_labeled(test, label, "arity ladder agrees (_1.._4 identical)", char_equal_1(s1, s2) && char_equal_1(s2, s3) && char_equal_1(s3, s4));
}

static void _arena_ladder_add_first(Test *const test, Arena *const allocator, char const *const base, char const *const data, char const *const expected, char const *const label) {
    USize const base_size = char_length(base);
    USize const data_size = char_length(data);

    char *s1 = char_alloc_new_2(base, allocator);
    char *s2 = char_alloc_new_2(base, allocator);
    char *s3 = char_alloc_new_2(base, allocator);
    char *s4 = char_alloc_new_2(base, allocator);

    char_alloc_add_first_1(&s1, data, allocator);
    char_alloc_add_first_2(&s2, data, data_size, allocator);
    char_alloc_add_first_3(&s3, base_size, data, allocator);
    char_alloc_add_first_4(&s4, base_size, data, data_size, allocator);

    _expect_string_labeled(test, label, "char_alloc_add_first_1", expected, s1);
    _expect_string_labeled(test, label, "char_alloc_add_first_2", expected, s2);
    _expect_string_labeled(test, label, "char_alloc_add_first_3", expected, s3);
    _expect_string_labeled(test, label, "char_alloc_add_first_4", expected, s4);
    _expect_true_labeled(test, label, "arity ladder agrees (_1.._4 identical)", char_equal_1(s1, s2) && char_equal_1(s2, s3) && char_equal_1(s3, s4));
}

static void _arena_ladder_add_last(Test *const test, Arena *const allocator, char const *const base, char const *const data, char const *const expected, char const *const label) {
    USize const base_size = char_length(base);
    USize const data_size = char_length(data);

    char *s1 = char_alloc_new_2(base, allocator);
    char *s2 = char_alloc_new_2(base, allocator);
    char *s3 = char_alloc_new_2(base, allocator);
    char *s4 = char_alloc_new_2(base, allocator);

    char_alloc_add_last_1(&s1, data, allocator);
    char_alloc_add_last_2(&s2, data, data_size, allocator);
    char_alloc_add_last_3(&s3, base_size, data, allocator);
    char_alloc_add_last_4(&s4, base_size, data, data_size, allocator);

    _expect_string_labeled(test, label, "char_alloc_add_last_1", expected, s1);
    _expect_string_labeled(test, label, "char_alloc_add_last_2", expected, s2);
    _expect_string_labeled(test, label, "char_alloc_add_last_3", expected, s3);
    _expect_string_labeled(test, label, "char_alloc_add_last_4", expected, s4);
    _expect_true_labeled(test, label, "arity ladder agrees (_1.._4 identical)", char_equal_1(s1, s2) && char_equal_1(s2, s3) && char_equal_1(s3, s4));
}

static void _arena_ladder_add_fixed(
    Test *const test, Arena *const allocator, char const *const base, USize const capacity, char const *const data, USize const index, char const *const expected, char const *const label) {
    char b1[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b2[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b3[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b4[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    USize const base_size = char_length(base);
    USize const data_size = char_length(data);

    char_copy_3(b1, capacity, base, base_size);
    char_copy_3(b2, capacity, base, base_size);
    char_copy_3(b3, capacity, base, base_size);
    char_copy_3(b4, capacity, base, base_size);

    char_alloc_add_fixed_1(b1, capacity, data, index, allocator);
    char_alloc_add_fixed_2(b2, capacity, data, data_size, index, allocator);
    char_alloc_add_fixed_3(b3, capacity, base_size, data, index, allocator);
    char_alloc_add_fixed_4(b4, capacity, base_size, data, data_size, index, allocator);

    _expect_string_labeled(test, label, "char_alloc_add_fixed_1", expected, b1);
    _expect_string_labeled(test, label, "char_alloc_add_fixed_2", expected, b2);
    _expect_string_labeled(test, label, "char_alloc_add_fixed_3", expected, b3);
    _expect_string_labeled(test, label, "char_alloc_add_fixed_4", expected, b4);
    _expect_true_labeled(test, label, "arity ladder agrees (_1.._4 identical)", char_equal_1(b1, b2) && char_equal_1(b2, b3) && char_equal_1(b3, b4));
}

static void _arena_ladder_add_first_fixed(
    Test *const test, Arena *const allocator, char const *const base, USize const capacity, char const *const data, char const *const expected, char const *const label) {
    char b1[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b2[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b3[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b4[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    USize const base_size = char_length(base);
    USize const data_size = char_length(data);

    char_copy_3(b1, capacity, base, base_size);
    char_copy_3(b2, capacity, base, base_size);
    char_copy_3(b3, capacity, base, base_size);
    char_copy_3(b4, capacity, base, base_size);

    char_alloc_add_first_fixed_1(b1, capacity, data, allocator);
    char_alloc_add_first_fixed_2(b2, capacity, data, data_size, allocator);
    char_alloc_add_first_fixed_3(b3, capacity, base_size, data, allocator);
    char_alloc_add_first_fixed_4(b4, capacity, base_size, data, data_size, allocator);

    _expect_string_labeled(test, label, "char_alloc_add_first_fixed_1", expected, b1);
    _expect_string_labeled(test, label, "char_alloc_add_first_fixed_2", expected, b2);
    _expect_string_labeled(test, label, "char_alloc_add_first_fixed_3", expected, b3);
    _expect_string_labeled(test, label, "char_alloc_add_first_fixed_4", expected, b4);
    _expect_true_labeled(test, label, "arity ladder agrees (_1.._4 identical)", char_equal_1(b1, b2) && char_equal_1(b2, b3) && char_equal_1(b3, b4));
}

static void _arena_ladder_add_last_fixed(
    Test *const test, Arena *const allocator, char const *const base, USize const capacity, char const *const data, char const *const expected, char const *const label) {
    char b1[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b2[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b3[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;
    char b4[_FIXED_BUFFER_SIZE] = DEFAULT_INITIALIZATION;

    USize const base_size = char_length(base);
    USize const data_size = char_length(data);

    char_copy_3(b1, capacity, base, base_size);
    char_copy_3(b2, capacity, base, base_size);
    char_copy_3(b3, capacity, base, base_size);
    char_copy_3(b4, capacity, base, base_size);

    char_alloc_add_last_fixed_1(b1, capacity, data, allocator);
    char_alloc_add_last_fixed_2(b2, capacity, data, data_size, allocator);
    char_alloc_add_last_fixed_3(b3, capacity, base_size, data, allocator);
    char_alloc_add_last_fixed_4(b4, capacity, base_size, data, data_size, allocator);

    _expect_string_labeled(test, label, "char_alloc_add_last_fixed_1", expected, b1);
    _expect_string_labeled(test, label, "char_alloc_add_last_fixed_2", expected, b2);
    _expect_string_labeled(test, label, "char_alloc_add_last_fixed_3", expected, b3);
    _expect_string_labeled(test, label, "char_alloc_add_last_fixed_4", expected, b4);
    _expect_true_labeled(test, label, "arity ladder agrees (_1.._4 identical)", char_equal_1(b1, b2) && char_equal_1(b2, b3) && char_equal_1(b3, b4));
}

/*==============================================================================
 * MARK: - Cases: Regression Pin
 *============================================================================*/
static void _test_regression_fixed_splice(Test *const test) {
    test_case_begin(test, "REGRESSION PIN: _char_add_fixed splice tail restore (incident 2026-08-22)");

    /* The exact given shape: char_add_first_fixed_1 of "hello " onto "world" used
     * to give back "hello " (the tail "world" replaced by zeros); it must now give
     * "hello world". */
    char buffer[32] = DEFAULT_INITIALIZATION;

    char_copy_3(buffer, sizeof(buffer), "world", char_length("world"));
    char_add_first_fixed_1(buffer, sizeof(buffer), "hello ");

    test_expect_string(test, "char_add_first_fixed_1(\"hello \" onto \"world\") == \"hello world\"", "hello world", buffer);

    /* A second, independent shape: inserting in the MIDDLE (not index 0), so the
     * splice restores a non-trivial tail on both sides of the insertion point. */
    char middle[32] = DEFAULT_INITIALIZATION;

    char_copy_3(middle, sizeof(middle), "abcXYZ", char_length("abcXYZ"));
    char_add_fixed_1(middle, sizeof(middle), "123", 1);

    test_expect_string(test, "char_add_fixed_1 middle-insert preserves the tail (not zeroed)", "a123bcXYZ", middle);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Regression Pin - the size-1 append flip point (design HIGH 1, fixed 2026-09-01)
 *============================================================================*/
static void _test_regression_index_append_flip(Test *const test) {
    test_case_begin(test, "REGRESSION PIN: self_index == self_size - 1 is an INSERT before the last character, not an append (design HIGH 1)");

    /* Both kernels used to special-case self_index == self_size - 1 as an APPEND; the
     * append fires only at self_index == self_size now, so index 2 into "abc" is the
     * genuine insert "abXc". */
    char *heap_value = char_new_2("abc");

    char_add_1(&heap_value, "X", 2); // self_size=3, index == size-1 == 2 -> the old append branch

    test_expect_string(test, "heap: char_add_1(\"abc\", \"X\", index=2) inserts before the last character -> \"abXc\"", "abXc", heap_value);

    char_delete(heap_value);

    /* Contrast at index 1: the general splice, which the fix left alone. */
    char *heap_contrast = char_new_2("abc");

    char_add_1(&heap_contrast, "X", 1);

    test_expect_string(test, "heap: char_add_1(\"abc\", \"X\", index=1) genuinely inserts -> \"aXbc\"", "aXbc", heap_contrast);

    char_delete(heap_contrast);

    char fixed_value[32] = DEFAULT_INITIALIZATION;

    char_copy_3(fixed_value, sizeof(fixed_value), "abc", 3);
    char_add_fixed_1(fixed_value, sizeof(fixed_value), "X", 2);

    test_expect_string(test, "fixed: char_add_fixed_1(\"abc\", \"X\", index=2) inserts before the last character -> \"abXc\"", "abXc", fixed_value);

    char fixed_contrast[32] = DEFAULT_INITIALIZATION;

    char_copy_3(fixed_contrast, sizeof(fixed_contrast), "abc", 3);
    char_add_fixed_1(fixed_contrast, sizeof(fixed_contrast), "X", 1);

    test_expect_string(test, "fixed: char_add_fixed_1(\"abc\", \"X\", index=1) genuinely inserts -> \"aXbc\"", "aXbc", fixed_contrast);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Heap Family
 *============================================================================*/
static void _test_heap_add_family(Test *const test) {
    test_case_begin(test, "char_add_1..4 arity ladder: insert at 0 / middle / end, empty data no-op");

    _heap_ladder_add(test, "world", "hello ", 0, "hello world", "insert at 0");
    _heap_ladder_add(test, "abcdef", "XY", 2, "abXYcdef", "insert at middle");
    _heap_ladder_add(test, "abc", "DEF", 3, "abcDEF", "insert at end (index == size)");
    _heap_ladder_add(test, "hello", "", 2, "hello", "empty data is a no-op");
    _heap_ladder_add(test, "a", "X", 0, "Xa", "insert at 0 into a ONE-character string (the size-1 flip point)");

    test_case_end(test);
}

static void _test_heap_add_first_family(Test *const test) {
    test_case_begin(test, "char_add_first_1..4 arity ladder: prepend, empty destination, empty data");

    _heap_ladder_add_first(test, "world", "hello ", "hello world", "prepend onto non-empty");
    _heap_ladder_add_first(test, "", "abc", "abc", "prepend onto empty destination");
    _heap_ladder_add_first(test, "xyz", "", "xyz", "empty data is a no-op");
    _heap_ladder_add_first(test, "a", "X", "Xa", "prepend onto a ONE-character string (used to append)");

    test_case_end(test);
}

static void _test_heap_add_last_family(Test *const test) {
    test_case_begin(test, "char_add_last_1..4 arity ladder: append, empty destination, empty data");

    _heap_ladder_add_last(test, "hello", " world", "hello world", "append onto non-empty");
    _heap_ladder_add_last(test, "", "abc", "abc", "append onto empty destination");
    _heap_ladder_add_last(test, "xyz", "", "xyz", "empty data is a no-op");

    test_case_end(test);
}

static void _test_heap_reallocation_contract(Test *const test) {
    test_case_begin(test, "heap add replaces the pointer and preserves content (reallocation contract)");

    char *value = char_new_2("hello");
    char *const original_pointer = value;

    char_add_last_1(&value, " world");

    test_expect_true(test, "the old pointer is replaced by a new allocation", value != original_pointer);
    test_expect_string(test, "content is preserved across the reallocation", "hello world", value);

    char_delete(value);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Fixed Family
 *============================================================================*/
static void _test_fixed_add_family(Test *const test) {
    test_case_begin(test, "char_add_fixed_1..4 arity ladder: insert at 0 / middle / end, empty data, empty destination, exact-capacity fit");

    _fixed_ladder_add(test, "world", 32, "hello ", 0, "hello world", "insert at 0");
    _fixed_ladder_add(test, "abcdef", 32, "XY", 2, "abXYcdef", "insert at middle");
    _fixed_ladder_add(test, "abc", 32, "DEF", 3, "abcDEF", "insert at end (index == size)");
    _fixed_ladder_add(test, "hello", 32, "", 2, "hello", "empty data is a no-op");
    _fixed_ladder_add(test, "", 16, "xyz", 0, "xyz", "empty destination (self_size == 0)");
    _fixed_ladder_add(test, "abc", 5, "Y", 0, "Yabc", "exact-capacity fit (self_size + data_size + terminator == capacity)");

    test_case_end(test);
}

static void _test_fixed_add_first_family(Test *const test) {
    test_case_begin(test, "char_add_first_fixed_1..4 arity ladder: prepend, empty destination, empty data");

    _fixed_ladder_add_first(test, "world", 32, "hello ", "hello world", "prepend onto non-empty (the regression shape)");
    _fixed_ladder_add_first(test, "", 16, "abc", "abc", "prepend onto empty destination");
    _fixed_ladder_add_first(test, "xyz", 16, "", "xyz", "empty data is a no-op");
    _fixed_ladder_add_first(test, "a", 16, "X", "Xa", "prepend onto a ONE-character string (used to append)");

    test_case_end(test);
}

static void _test_fixed_add_last_family(Test *const test) {
    test_case_begin(test, "char_add_last_fixed_1..4 arity ladder: append, empty destination, empty data");

    _fixed_ladder_add_last(test, "hello", 32, " world", "hello world", "append onto non-empty");
    _fixed_ladder_add_last(test, "", 16, "abc", "abc", "append onto empty destination");
    _fixed_ladder_add_last(test, "xyz", 16, "", "xyz", "empty data is a no-op");

    test_case_end(test);
}

static void _test_fixed_termination(Test *const test) {
    test_case_begin(test, "fixed add terminates and leaves bytes past the terminator untouched");

    char buffer[16];

    char_fill(buffer, sizeof(buffer), 'Z');
    char_copy_3(buffer, sizeof(buffer), "ab", 2);
    char_add_last_fixed_1(buffer, sizeof(buffer), "cd");

    test_expect_string(test, "content is \"abcd\"", "abcd", buffer);
    test_expect_true(test, "terminator written right after the content", buffer[4] == '\0');
    test_expect_true(test, "bytes past the terminator are untouched filler, not garbage", buffer[5] == 'Z');

    test_case_end(test);
}

static void _test_fixed_capacity_one_over_refuses(Test *const test) {
    test_case_begin(test, "bounds are REFUSED as a no-op in every build (design HIGH 2): one byte over capacity, an index past the end"
        " - self unchanged, never truncated, overflowed or aborted");

    char self[4] = DEFAULT_INITIALIZATION;

    char_copy_3(self, sizeof(self), "abc", 3);

    /* self_size(3) + data_size(1) + terminator(1) = 5 > self_capacity(4). This was an
     * error_check abort, which compiles away without ERROR_CHECK_ENABLED and left an
     * unbounded write; the fit is real control flow now. */
    char_add_first_fixed_1(self, sizeof(self), "Y");

    test_expect_string(test, "fixed: prepend one byte over capacity leaves self unchanged", "abc", self);

    char_add_last_fixed_1(self, sizeof(self), "Y");

    test_expect_string(test, "fixed: append one byte over capacity leaves self unchanged", "abc", self);

    char roomy[8] = DEFAULT_INITIALIZATION;

    char_copy_3(roomy, sizeof(roomy), "abc", 3);
    char_add_fixed_1(roomy, sizeof(roomy), "Y", 4);

    test_expect_string(test, "fixed: an index past the end is refused, not written", "abc", roomy);

    char *heap = char_new_2("abc");
    char *const original = heap;

    char_add_1(&heap, "Y", 4);

    test_expect_true(test, "heap: an index past the end is refused - *self keeps the original buffer", heap == original);
    test_expect_string(test, "heap: and its content", "abc", heap);

    char_delete(heap);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Arena Family
 *============================================================================*/
static void _test_arena_add_family(Test *const test) {
    test_case_begin(test, "char_alloc_add_1..4 / add_first / add_last arena twins (heap-shaped)");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    _arena_ladder_add(test, &arena, "abcdef", "XY", 2, "abXYcdef", "arena insert at middle");
    _arena_ladder_add_first(test, &arena, "world", "hello ", "hello world", "arena prepend");
    _arena_ladder_add_last(test, &arena, "hello", " world", "hello world", "arena append");
    _arena_ladder_add(test, &arena, "hello", "", 2, "hello", "arena empty data is a no-op");
    _arena_ladder_add_first(test, &arena, "a", "X", "Xa", "arena prepend onto a ONE-character string (used to append)");

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_arena_fixed_add_family(Test *const test) {
    test_case_begin(test, "char_alloc_add_fixed_1..4 / add_first_fixed / add_last_fixed arena twins (fixed-shaped)");

    Arena arena = arena_init_1(4096, ARENA_TYPE_LINEAR);

    _arena_ladder_add_fixed(test, &arena, "abcdef", 32, "XY", 2, "abXYcdef", "arena fixed insert at middle");
    _arena_ladder_add_first_fixed(test, &arena, "world", 32, "hello ", "hello world", "arena fixed prepend (same shape the regression fix covers)");
    _arena_ladder_add_last_fixed(test, &arena, "hello", 32, " world", "hello world", "arena fixed append");
    _arena_ladder_add_fixed(test, &arena, "hello", 32, "", 2, "hello", "arena fixed empty data is a no-op");
    _arena_ladder_add_first_fixed(test, &arena, "a", 32, "X", "Xa", "arena fixed prepend onto a ONE-character string (used to append)");

    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry Point
 *============================================================================*/
int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/char/test_char_add.c");

    test_suite_begin(&test, "char_add_regression");
    _test_regression_fixed_splice(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "char_add_index_flip");
    _test_regression_index_append_flip(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "char_add_heap");
    _test_heap_add_family(&test);
    _test_heap_add_first_family(&test);
    _test_heap_add_last_family(&test);
    _test_heap_reallocation_contract(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "char_add_fixed");
    _test_fixed_add_family(&test);
    _test_fixed_add_first_family(&test);
    _test_fixed_add_last_family(&test);
    _test_fixed_termination(&test);
    _test_fixed_capacity_one_over_refuses(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "char_add_arena");
    _test_arena_add_family(&test);
    _test_arena_fixed_add_family(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}