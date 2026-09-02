#include <stdio.h>
#include <string.h>

#include <test/test.h>

#include <char/char.h>

/* Correctness coverage for the reverse-search family char_find_reverse_1/2/3,
 * which previously had ZERO assertion coverage (the fuzz harness checks only a
 * bounds invariant). The backbone is a differential sweep against an
 * INDEPENDENT brute-force reference - not a transcription of the shipping
 * loops - exhaustive over every haystack up to length 6 on a 2-symbol
 * alphabet, every needle up to length 3, and every self_index from 0 to one
 * PAST self_size. Named boundary assertions and non-terminated-buffer cases
 * guard the spots a bulk loop can silently stop exercising. */

static USize _compare_count = 0;
static USize _found_count   = 0;

/* Independent brute force: largest match START in [self_index, ...] whose match
 * fits inside [0, self_size). Enumerates candidate ENDS from the top and checks
 * each with memcmp; shares no loop structure with the shipping implementation. */
static USize _reference_find_reverse(char const *const self, USize const self_size,
                                     USize const self_index, char const *const data, USize const data_size) {
    if (data_size == 0) {
        return self_index <= self_size ? self_size : CHAR_NPOS;
    }

    if (self_size == 0) {
        return CHAR_NPOS;
    }

    for (USize i = self_size; i > self_index; ) {
        i -= 1;

        if (i + 1 < data_size) {
            continue;
        }

        USize const start = i + 1 - data_size;

        if (start < self_index) {
            continue;
        }

        if (memcmp(self + start, data, data_size) == 0) {
            return start;
        }
    }

    return CHAR_NPOS;
}

/* Differential probe: shipping vs reference, plus _2 wrapper agreement.
 * Every caller must pass a NUL-terminated self (char_find_reverse_2 measures
 * self with char_length). On mismatch the assertion name carries the actual
 * haystack, needle, and index so the failing input is reproducible. */
static void _expect_reverse(Test *const test, char const *const self, USize const self_size,
                            USize const self_index, char const *const data, USize const data_size) {
    USize const got  = char_find_reverse_3(self, self_size, self_index, data, data_size);
    USize const want = _reference_find_reverse(self, self_size, self_index, data, data_size);

    _compare_count += 1;

    if (want != CHAR_NPOS) {
        _found_count += 1;
    }

    if (got == want) {
        test_expect_u(test, "shipping == reference", want, got);
    }
    else {
        char message[160] = DEFAULT_INITIALIZATION;

        snprintf(message, sizeof(message),
            "MISMATCH self=\"%.*s\"(%zu) index=%zu data=\"%.*s\"(%zu)",
            (int) self_size, self, self_size, self_index, (int) data_size, data, data_size);
        test_expect_u(test, message, want, got);
    }

    test_expect_u(test, "reverse_2 agrees with reverse_3", got, char_find_reverse_2(self, self_index, data, data_size));
}

static void _test_differential(Test *const test) {
    test_case_begin(test, "differential vs independent brute force");

    char const alphabet[] = { 'a', 'b' };

    char haystack[8] = DEFAULT_INITIALIZATION;
    char needle[8]   = DEFAULT_INITIALIZATION;

    for (USize haystack_size = 0; haystack_size <= 6; haystack_size += 1) {
        USize const haystack_count = (USize) 1 << haystack_size;

        for (USize haystack_bits = 0; haystack_bits < haystack_count; haystack_bits += 1) {
            for (USize bit = 0; bit < haystack_size; bit += 1) {
                haystack[bit] = alphabet[(haystack_bits >> bit) & 1u];
            }

            haystack[haystack_size] = '\0';

            for (USize needle_size = 0; needle_size <= 3; needle_size += 1) {
                USize const needle_count = (USize) 1 << needle_size;

                for (USize needle_bits = 0; needle_bits < needle_count; needle_bits += 1) {
                    for (USize bit = 0; bit < needle_size; bit += 1) {
                        needle[bit] = alphabet[(needle_bits >> bit) & 1u];
                    }

                    needle[needle_size] = '\0';

                    /* Every legal self_index plus one PAST self_size. */
                    for (USize index = 0; index <= haystack_size + 1; index += 1) {
                        _expect_reverse(test, haystack, haystack_size, index, needle, needle_size);
                    }
                }
            }
        }
    }

    /* Distinct-alphabet spot checks: a bug visible only with more than two
     * symbols must not hide behind the dense repetition above. */
    _expect_reverse(test, "abcabcabc", 9, 0, "abc", 3);
    _expect_reverse(test, "abcabcabc", 9, 4, "abc", 3);
    _expect_reverse(test, "abcabcabc", 9, 7, "abc", 3);
    _expect_reverse(test, "abcabcabc", 9, 0, "cab", 3);
    _expect_reverse(test, "aaaa", 4, 0, "aa", 2);
    _expect_reverse(test, "aaaa", 4, 3, "aa", 2);

    /* Anti-vacuity: the sweep above has a closed-form comparison count
     * (15 * sum over hs of 2^hs * (hs + 2) = 13440, plus the 6 spot checks).
     * A loop that silently stops iterating fails here, and the positive and
     * negative anchors prove both outcomes actually occurred. */
    test_expect_u(test, "differential comparison count", 13446, _compare_count);
    test_expect_true(test, "positive anchor: some comparisons matched", _found_count > 0);
    test_expect_true(test, "negative anchor: some comparisons did not match", _found_count < _compare_count);

    test_case_end(test);
}

static void _test_boundaries_empty(Test *const test) {
    test_case_begin(test, "boundaries: empty needle and empty haystack");

    test_expect_u(test, "empty needle matches at the far end", 3, char_find_reverse_3("abc", 3, 0, "", 0));
    test_expect_u(test, "empty needle at a middle index still returns self_size", 3, char_find_reverse_3("abc", 3, 2, "", 0));
    test_expect_u(test, "empty needle at self_index == self_size", 3, char_find_reverse_3("abc", 3, 3, "", 0));
    test_expect_u(test, "empty needle past self_size returns CHAR_NPOS", CHAR_NPOS, char_find_reverse_3("abc", 3, 4, "", 0));
    test_expect_u(test, "empty haystack returns CHAR_NPOS", CHAR_NPOS, char_find_reverse_3("", 0, 0, "a", 1));
    test_expect_u(test, "empty haystack, empty needle returns 0 (the far end is 0)", 0, char_find_reverse_3("", 0, 0, "", 0));

    test_case_end(test);
}

static void _test_boundaries_position(Test *const test) {
    test_case_begin(test, "boundaries: sizes and match positions");

    test_expect_u(test, "needle longer than haystack", CHAR_NPOS, char_find_reverse_3("ab", 2, 0, "abc", 3));
    test_expect_u(test, "needle equals haystack exactly", 0, char_find_reverse_3("abc", 3, 0, "abc", 3));
    test_expect_u(test, "match at the very start", 0, char_find_reverse_3("abzz", 4, 0, "ab", 2));
    test_expect_u(test, "match at the very end", 2, char_find_reverse_3("zzab", 4, 0, "ab", 2));
    test_expect_u(test, "overlapping candidates return the last start", 2, char_find_reverse_3("aaaa", 4, 0, "aa", 2));
    test_expect_u(test, "overlapping with self_index at the last start", 2, char_find_reverse_3("aaaa", 4, 2, "aa", 2));
    test_expect_u(test, "overlapping with self_index past the last start", CHAR_NPOS, char_find_reverse_3("aaaa", 4, 3, "aa", 2));
    test_expect_u(test, "single-character needle finds the last occurrence", 3, char_find_reverse_3("abcba", 5, 0, "b", 1));

    test_case_end(test);
}

static void _test_boundaries_index_window(Test *const test) {
    test_case_begin(test, "boundaries: the self_index window");

    test_expect_u(test, "self_index 0 sees the last match", 2, char_find_reverse_3("abab", 4, 0, "ab", 2));
    test_expect_u(test, "self_index 1 (middle) still sees it", 2, char_find_reverse_3("abab", 4, 1, "ab", 2));
    test_expect_u(test, "self_index at the match start still sees it", 2, char_find_reverse_3("abab", 4, 2, "ab", 2));
    test_expect_u(test, "self_index past the last match start", CHAR_NPOS, char_find_reverse_3("abab", 4, 3, "ab", 2));
    test_expect_u(test, "self_index == self_size finds nothing", CHAR_NPOS, char_find_reverse_3("abab", 4, 4, "ab", 2));
    test_expect_u(test, "self_index past self_size finds nothing", CHAR_NPOS, char_find_reverse_3("abab", 4, 9, "ab", 2));
    test_expect_u(test, "a match starting before self_index is not returned", CHAR_NPOS, char_find_reverse_3("abzzz", 5, 1, "ab", 2));
    test_expect_u(test, "a match starting at self_index is returned", 0, char_find_reverse_3("abzzz", 5, 0, "ab", 2));
    test_expect_u(test, "a single-char match ending exactly at self_index is found", 2, char_find_reverse_3("zab", 3, 2, "b", 1));

    test_case_end(test);
}

static void _test_overloads(Test *const test) {
    test_case_begin(test, "char_find_reverse_1/_2 agree with _3");

    char const *const text      = "hello world hello";
    USize const       text_size = char_length(text);

    test_expect_u(test, "_1 finds the last occurrence", 12, char_find_reverse_1(text, 0, "hello"));
    test_expect_u(test, "_1 honors self_index at the match start", 12, char_find_reverse_1(text, 12, "hello"));
    test_expect_u(test, "_1 refuses a match before self_index", CHAR_NPOS, char_find_reverse_1(text, 13, "hello"));
    test_expect_u(test, "_1 empty needle returns self_size", text_size, char_find_reverse_1(text, 0, ""));

    test_expect_u(test, "_2 matches _1 on the same input", char_find_reverse_1(text, 0, "hello"), char_find_reverse_2(text, 0, "hello", 5));
    test_expect_u(test, "_2 matches _3 with an explicit size", char_find_reverse_3(text, text_size, 4, "lo", 2), char_find_reverse_2(text, 4, "lo", 2));
    test_expect_u(test, "_2 with a sized prefix of a longer literal", 3, char_find_reverse_2("abcabc", 0, "abX", 2));

    test_case_end(test);
}

static void _test_unterminated_buffers(Test *const test) {
    test_case_begin(test, "sized overloads ignore bytes past the given size");

    /* Needle built in a larger buffer with live garbage after data_size and NO
     * terminator anywhere. The haystack "zzab" contains "ab" at 2 but not
     * "abz", so an implementation that discards data_size (the char_find_2
     * defect shape that survived 12.8M NUL-terminated fuzz executions) flips
     * the result to CHAR_NPOS instead of merely agreeing by accident. */
    char needle_buffer[6] = DEFAULT_INITIALIZATION;

    needle_buffer[0] = 'a';
    needle_buffer[1] = 'b';
    needle_buffer[2] = 'z';
    needle_buffer[3] = 'q';
    needle_buffer[4] = 'q';
    needle_buffer[5] = 'q';

    test_expect_u(test, "_3 ignores needle garbage past data_size", 2, char_find_reverse_3("zzab", 4, 0, needle_buffer, 2));
    test_expect_u(test, "_2 ignores needle garbage past data_size", 2, char_find_reverse_2("zzab", 0, needle_buffer, 2));

    /* Haystack with live bytes past self_size and no terminator: inside the
     * size-3 window "zab" the only match starts at 1; the "ab" at 3 lies past
     * the window and must be invisible to the sized overload. */
    char haystack_buffer[5] = DEFAULT_INITIALIZATION;

    haystack_buffer[0] = 'z';
    haystack_buffer[1] = 'a';
    haystack_buffer[2] = 'b';
    haystack_buffer[3] = 'a';
    haystack_buffer[4] = 'b';

    test_expect_u(test, "_3 ignores haystack bytes past self_size", 1, char_find_reverse_3(haystack_buffer, 3, 0, "ab", 2));

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

    Test test = test_init("tests/char/test_char_reverse.c");

    test_suite_begin(&test, "char_find_reverse");
    _test_differential(&test);
    _test_boundaries_empty(&test);
    _test_boundaries_position(&test);
    _test_boundaries_index_window(&test);
    _test_overloads(&test);
    _test_unterminated_buffers(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}