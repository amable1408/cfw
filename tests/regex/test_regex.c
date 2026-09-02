#include <string.h>

#include <char/char.h>
#include <container/str/str.h>
#include <container/string/string.h>
#include <log/log.h>
#include <regex/regex.h>
#include <test/test.h>

/*
 * First suite for CFW's regex module (zero coverage before this file - it is the
 * HTTP router's capture mechanism and vestigo's match engine), pinning the
 * Block F fix set:
 *
 *   1. The REFUSING object: a fresh regex_init (and a failed compile_try) answers
 *      false to every match, 0 to every match_all, failure Results from all six
 *      copy accessors and nullptr/empty from all six get accessors - no crash.
 *      The `matched` gate extends this to COMPILED-BUT-NEVER-MATCHED objects
 *      (the virgin-match_data HIGH: pcre2's fresh match_data is uninitialized
 *      heap), to a recompiled object until its next successful match, and to a
 *      compiled object whose only match attempt answered false.
 *   2. match_all returns REAL counts (the bool-truncation HIGH): a subject with
 *      exactly 3 matches reports 3 through all four variants, and the callback's
 *      void *context round-trips.
 *   3. Empty subject / empty pattern are VALUES: "a*" matches "", the empty
 *      pattern compiles and matches everywhere, empty Str/String subjects
 *      (data == nullptr internally) match without crash.
 *   4. Offset contract: past-the-end refuses (false / 0), offset == size is legal
 *      ("a*" matches empty at the very end).
 *   5. Captures: numbered + named groups through all three container forms
 *      (content AND teardown), non-participating group refusals, the
 *      participating-but-empty freeable-empty branch.
 *   6. copy_match_group in/out contract: pre-sized Str receives the substring
 *      with an honest size; a too-small buffer fails with the container
 *      UNCHANGED; the String form bounds by CAPACITY; PCRE2's terminator-space
 *      edge (length + 1 bytes needed) pinned at exactly 4 vs 5.
 *   7. Zero-length match iteration: the PCRE2 anchored non-empty retry - "a*"
 *      over "bab" yields the exact sequence empty@0, "a"@1, empty@2, empty@3
 *      (the old blind +1 bump skipped the "a").
 *   8. Sized compile forms: embedded NUL in a sized pattern, Str/String
 *      patterns, REGEX_COMPILE_CASELESS (the vestigo "(?i)" splice replacement).
 *   9. Error surface: a failed compile_try renders a non-empty message and an
 *      offset inside the pattern; the accessors are safe after success too.
 *  10. Recompile-into-a-live-object: correct results after the second compile
 *      (the leak itself is the memory-hooks/ASan lane's pin).
 *  11. Match state validity: begin + size == end and subject[begin..end) equals
 *      the matched text.
 */

/*==============================================================================
 * MARK: - Anti-Vacuity Counters
 *============================================================================*/
/** Closed-form number of case functions below; the final case asserts every one ran. */
#define _EXPECTED_CASE_COUNT 21

/** A value no accident writes; proves the callback context pointer round-trips. */
#define _CONTEXT_MAGIC 0xC0FFEE

/** Incremented at the top of every case function; a case that silently never runs
 * (or a dispatcher edit that drops one) fails the closed-form check. */
static USize _case_entered_count = 0;

/*==============================================================================
 * MARK: - Callback Plumbing
 *============================================================================*/
/** Match recorder handed through the match_all context pointer. */
typedef struct {
    USize count;        /**< Callback invocations */
    USize begins[8];    /**< match.begin per invocation (first 8) */
    USize sizes[8];     /**< match.size per invocation (first 8) */
    USize magic;        /**< _CONTEXT_MAGIC when the context arrived intact */
} MatchRecord;

static void _record_match(Regex const *const self, char const *const subject, USize const subject_size, void *const context) {
    (void) subject;
    (void) subject_size;

    MatchRecord *const record = context;

    if (record->count < 8) {
        record->begins[record->count] = self->match.begin;
        record->sizes[record->count]  = self->match.size;
    }

    record->count += 1;
    record->magic  = _CONTEXT_MAGIC;
}

/** Refusal battery shared by the fresh-object and failed-compile cases: every
 * match answers false, every match_all answers 0 without firing the callback,
 * every copy accessor fails, every get accessor returns nullptr/empty. */
static void _expect_object_refuses(Test *const test, Regex *const re, char const *const label_match, char const *const label_capture) {
    char subject_buffer[]  = "abc";
    Str str_subject        = str_init_3(subject_buffer, 3);
    String string_subject  = string_init_3(subject_buffer);
    MatchRecord record     = DEFAULT_INITIALIZATION;

    test_expect_false(test, label_match, regex_match_1(re, "abc", 0));
    test_expect_false(test, "match_2 refuses", regex_match_2(re, "abc", 3, 0));
    test_expect_false(test, "match_3 refuses", regex_match_3(re, &str_subject, 0));
    test_expect_false(test, "match_4 refuses", regex_match_4(re, &string_subject, 0));

    test_expect_u(test, "match_all_1 answers 0", 0, regex_match_all_1(re, "abc", 0, _record_match, &record));
    test_expect_u(test, "match_all_2 answers 0", 0, regex_match_all_2(re, "abc", 3, 0, _record_match, &record));
    test_expect_u(test, "match_all_3 answers 0", 0, regex_match_all_3(re, &str_subject, 0, _record_match, &record));
    test_expect_u(test, "match_all_4 answers 0", 0, regex_match_all_4(re, &string_subject, 0, _record_match, &record));
    test_expect_u(test, "the callback never fired on the refusing object", 0, record.count);

    char copy_buffer[16]    = DEFAULT_INITIALIZATION;
    USize copy_size         = sizeof(copy_buffer);
    char str_buffer[16]     = DEFAULT_INITIALIZATION;
    Str str_copy            = str_init_3(str_buffer, sizeof(str_buffer));
    String string_copy      = string_init_2(16);

    test_expect_true(test, label_capture, result_is_error(regex_copy_match_group_name_1(re, "g", copy_buffer, &copy_size)));
    test_expect_true(test, "copy_name_2 fails", result_is_error(regex_copy_match_group_name_2(re, "g", &str_copy)));
    test_expect_true(test, "copy_name_3 fails", result_is_error(regex_copy_match_group_name_3(re, "g", &string_copy)));

    copy_size = sizeof(copy_buffer);

    test_expect_true(test, "copy_number_1 fails", result_is_error(regex_copy_match_group_number_1(re, 0, copy_buffer, &copy_size)));
    test_expect_true(test, "copy_number_2 fails", result_is_error(regex_copy_match_group_number_2(re, 0, &str_copy)));
    test_expect_true(test, "copy_number_3 fails", result_is_error(regex_copy_match_group_number_3(re, 0, &string_copy)));

    test_expect_null(test, "get_name_1 returns nullptr", regex_get_match_group_name_1(re, "g"));
    test_expect_null(test, "get_number_1 returns nullptr", regex_get_match_group_number_1(re, 0));

    Str got_str_name   = regex_get_match_group_name_2(re, "g");
    Str got_str_number = regex_get_match_group_number_2(re, 0);

    test_expect_u(test, "get_name_2 returns the empty Str", 0, str_get_size(&got_str_name));
    test_expect_u(test, "get_number_2 returns the empty Str", 0, str_get_size(&got_str_number));

    String got_string_name   = regex_get_match_group_name_3(re, "g");
    String got_string_number = regex_get_match_group_number_3(re, 0);

    test_expect_u(test, "get_name_3 returns the empty String", 0, string_get_size(&got_string_name));
    test_expect_u(test, "get_number_3 returns the empty String", 0, string_get_size(&got_string_number));

    str_uninit(&got_str_name);
    str_uninit(&got_str_number);
    string_uninit(&got_string_name);
    string_uninit(&got_string_number);
    string_uninit(&string_copy);
    string_uninit(&string_subject);
}

/** Capture-accessor refusal battery for an object whose `matched` gate is closed
 * (compiled-but-never-matched, recompiled, or matched-false): all six copy
 * accessors fail, all six get accessors return nullptr/empty - no crash. This is
 * the pin that would have caught the virgin-match_data HIGH (pcre2_match_data
 * comes back as uninitialized heap; only a successful match initializes it). */
static void _expect_capture_accessors_refuse(Test *const test, Regex *const re, char const *const label) {
    char copy_buffer[16] = DEFAULT_INITIALIZATION;
    USize copy_size      = sizeof(copy_buffer);
    char str_buffer[16]  = DEFAULT_INITIALIZATION;
    Str str_copy         = str_init_3(str_buffer, sizeof(str_buffer));
    String string_copy   = string_init_2(16);

    test_expect_true(test, label, result_is_error(regex_copy_match_group_name_1(re, "g", copy_buffer, &copy_size)));
    test_expect_true(test, "copy_name_2 fails (gate closed)", result_is_error(regex_copy_match_group_name_2(re, "g", &str_copy)));
    test_expect_true(test, "copy_name_3 fails (gate closed)", result_is_error(regex_copy_match_group_name_3(re, "g", &string_copy)));

    copy_size = sizeof(copy_buffer);

    test_expect_true(test, "copy_number_1 fails (gate closed)", result_is_error(regex_copy_match_group_number_1(re, 0, copy_buffer, &copy_size)));
    test_expect_true(test, "copy_number_2 fails (gate closed)", result_is_error(regex_copy_match_group_number_2(re, 0, &str_copy)));
    test_expect_true(test, "copy_number_3 fails (gate closed)", result_is_error(regex_copy_match_group_number_3(re, 0, &string_copy)));

    test_expect_null(test, "get_name_1 returns nullptr (gate closed)", regex_get_match_group_name_1(re, "g"));
    test_expect_null(test, "get_number_1 returns nullptr (gate closed)", regex_get_match_group_number_1(re, 0));

    Str got_str_name   = regex_get_match_group_name_2(re, "g");
    Str got_str_number = regex_get_match_group_number_2(re, 0);

    test_expect_u(test, "get_name_2 returns the empty Str (gate closed)", 0, str_get_size(&got_str_name));
    test_expect_u(test, "get_number_2 returns the empty Str (gate closed)", 0, str_get_size(&got_str_number));

    String got_string_name   = regex_get_match_group_name_3(re, "g");
    String got_string_number = regex_get_match_group_number_3(re, 0);

    test_expect_u(test, "get_name_3 returns the empty String (gate closed)", 0, string_get_size(&got_string_name));
    test_expect_u(test, "get_number_3 returns the empty String (gate closed)", 0, string_get_size(&got_string_number));

    str_uninit(&got_str_name);
    str_uninit(&got_str_number);
    string_uninit(&got_string_name);
    string_uninit(&got_string_number);
    string_uninit(&string_copy);
}

/*==============================================================================
 * MARK: - Cases: The Refusing Object
 *============================================================================*/
static void _test_fresh_object_refuses(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "regex_init: the fresh (never-compiled) object refuses every match, match_all, copy and get accessor - no crash");

    Regex re = regex_init();

    _expect_object_refuses(test, &re, "fresh object: match_1 refuses", "fresh object: copy_name_1 fails");

    /* The refusals above must come from the module's own guard, NOT from PCRE2
     * bouncing a null code (PCRE2_ERROR_NULL) - a refusal must not pollute the
     * error surface. A fresh object's error_code is 0, which renders nothing. */
    char message[REGEX_ERROR_MESSAGE_SIZE] = DEFAULT_INITIALIZATION;

    test_expect_false(test, "the refused matches left the error surface clean (nothing to render)", regex_get_error_message(&re, message, sizeof(message)));

    regex_uninit(&re);

    test_case_end(test);
}

static void _test_failed_compile_refuses(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "regex_compile_try(\"[unclosed\"): false, and the object refuses everything afterwards; regex_uninit is safe");

    Regex re = regex_init();

    test_expect_false(test, "compile_try on the malformed pattern answers false", regex_compile_try(&re, "[unclosed"));

    _expect_object_refuses(test, &re, "failed compile: match_1 refuses", "failed compile: copy_name_1 fails");

    regex_uninit(&re);

    test_case_end(test);
}

static void _test_compiled_unmatched_refuses(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "compiled-but-NEVER-MATCHED refuses: after compile_try(\"(a+)\") every capture accessor fails cleanly (the virgin-match_data HIGH - pcre2_match_data_create does not zero its heap)");

    Regex re = regex_init();

    test_expect_true(test, "\"(a+)\" compiles", regex_compile_try(&re, "(a+)"));

    _expect_capture_accessors_refuse(test, &re, "compiled-unmatched: copy_name_1 fails");

    /* Positive anchor: the SAME object serves captures once a match succeeds. */
    test_expect_true(test, "the same object then matches \"aaa\"", regex_match_1(&re, "aaa", 0));

    char *group_1 = regex_get_match_group_number_1(&re, 1);

    test_expect_string(test, "and group 1 is \"aaa\" (the gate opened)", "aaa", group_1);

    char_delete(group_1);
    regex_uninit(&re);

    test_case_end(test);
}

static void _test_recompile_regates_accessors(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "recompile REOPENS the gate: compile + match serves captures, recompiling into the same object makes the accessors refuse again until the next successful match");

    Regex re = regex_init();

    test_expect_true(test, "\"(a+)\" compiles", regex_compile_try(&re, "(a+)"));
    test_expect_true(test, "it matches \"aaa\"", regex_match_1(&re, "aaa", 0));

    char *before = regex_get_match_group_number_1(&re, 1);

    test_expect_string(test, "group 1 works after the match", "aaa", before);

    char_delete(before);

    test_expect_true(test, "\"(b+)\" recompiles into the same object", regex_compile_try(&re, "(b+)"));

    _expect_capture_accessors_refuse(test, &re, "post-recompile: copy_name_1 refuses again");

    /* Positive anchor: the next successful match re-arms the accessors. */
    test_expect_true(test, "the recompiled object matches \"bb\"", regex_match_1(&re, "bb", 0));

    char *after = regex_get_match_group_number_1(&re, 1);

    test_expect_string(test, "and group 1 is \"bb\"", "bb", after);

    char_delete(after);
    regex_uninit(&re);

    test_case_end(test);
}

static void _test_failed_match_keeps_gate_closed(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "a FAILED match does not arm the gate: compile, match a non-matching subject (false), the accessors still refuse cleanly");

    Regex re = regex_init();

    test_expect_true(test, "\"(a+)\" compiles", regex_compile_try(&re, "(a+)"));
    test_expect_false(test, "\"zzz\" does not match", regex_match_1(&re, "zzz", 0));

    _expect_capture_accessors_refuse(test, &re, "matched-false: copy_name_1 refuses");

    regex_uninit(&re);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: match_all Counts (the bool-truncation HIGH)
 *============================================================================*/
static void _test_match_all_exact_three(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "match_all: \"a+\" over \"aa bb aaa a\" reports EXACTLY 3 through all four variants (not the bool-truncated 1), and the context round-trips");

    Regex re = regex_init();

    test_expect_true(test, "\"a+\" compiles", regex_compile_try(&re, "a+"));

    char subject_buffer[] = "aa bb aaa a";
    Str str_subject       = str_init_3(subject_buffer, 11);
    String string_subject = string_init_3(subject_buffer);

    MatchRecord record_1 = DEFAULT_INITIALIZATION;
    MatchRecord record_2 = DEFAULT_INITIALIZATION;
    MatchRecord record_3 = DEFAULT_INITIALIZATION;
    MatchRecord record_4 = DEFAULT_INITIALIZATION;

    test_expect_u(test, "match_all_1 returns 3", 3, regex_match_all_1(&re, "aa bb aaa a", 0, _record_match, &record_1));
    test_expect_u(test, "match_all_2 returns 3", 3, regex_match_all_2(&re, "aa bb aaa a", 11, 0, _record_match, &record_2));
    test_expect_u(test, "match_all_3 returns 3", 3, regex_match_all_3(&re, &str_subject, 0, _record_match, &record_3));
    test_expect_u(test, "match_all_4 returns 3", 3, regex_match_all_4(&re, &string_subject, 0, _record_match, &record_4));

    test_expect_u(test, "the callback fired 3 times (variant 1)", 3, record_1.count);
    test_expect_u(test, "the callback fired 3 times (variant 4)", 3, record_4.count);
    test_expect_u(test, "the context pointer round-tripped intact", (USize) _CONTEXT_MAGIC, record_1.magic);

    test_expect_u(test, "first match begins at 0 (\"aa\")", 0, record_2.begins[0]);
    test_expect_u(test, "second match begins at 6 (\"aaa\")", 6, record_2.begins[1]);
    test_expect_u(test, "third match begins at 10 (\"a\")", 10, record_2.begins[2]);
    test_expect_u(test, "second match size is 3", 3, record_2.sizes[1]);

    string_uninit(&string_subject);
    regex_uninit(&re);

    test_case_end(test);
}

static void _test_match_all_zero_length_iteration(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "match_all zero-length iteration: \"a*\" over \"bab\" is empty@0, \"a\"@1, empty@2, empty@3 (the anchored non-empty retry - the old +1 bump skipped the \"a\")");

    Regex re = regex_init();

    test_expect_true(test, "\"a*\" compiles", regex_compile_try(&re, "a*"));

    MatchRecord record = DEFAULT_INITIALIZATION;

    test_expect_u(test, "\"a*\" over \"bab\" counts 4 matches", 4, regex_match_all_1(&re, "bab", 0, _record_match, &record));
    test_expect_u(test, "match 0 is the empty at 0", 0, record.begins[0]);
    test_expect_u(test, "match 0 size is 0", 0, record.sizes[0]);
    test_expect_u(test, "match 1 is \"a\" at 1 (NOT skipped)", 1, record.begins[1]);
    test_expect_u(test, "match 1 size is 1 (the non-empty retry won)", 1, record.sizes[1]);
    test_expect_u(test, "match 2 is the empty at 2", 2, record.begins[2]);
    test_expect_u(test, "match 2 size is 0", 0, record.sizes[2]);
    test_expect_u(test, "match 3 is the empty at 3 (end of subject)", 3, record.begins[3]);
    test_expect_u(test, "match 3 size is 0", 0, record.sizes[3]);

    Regex re_never = regex_init();

    test_expect_true(test, "\"x*\" compiles", regex_compile_try(&re_never, "x*"));

    MatchRecord record_empties = DEFAULT_INITIALIZATION;

    test_expect_u(test, "\"x*\" over \"aaa\" is 4 empties and TERMINATES", 4, regex_match_all_1(&re_never, "aaa", 0, _record_match, &record_empties));
    test_expect_u(test, "all 4 are zero-length", 0, record_empties.sizes[0] + record_empties.sizes[1] + record_empties.sizes[2] + record_empties.sizes[3]);

    Regex re_none = regex_init();

    test_expect_true(test, "\"z+\" compiles", regex_compile_try(&re_none, "z+"));

    MatchRecord record_none = DEFAULT_INITIALIZATION;

    test_expect_u(test, "a never-matching pattern reports 0", 0, regex_match_all_1(&re_none, "abc", 0, _record_match, &record_none));
    test_expect_u(test, "and its callback never fired", 0, record_none.count);

    regex_uninit(&re_none);
    regex_uninit(&re_never);
    regex_uninit(&re);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Empty Values
 *============================================================================*/
static void _test_empty_subject(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "empty subject is a VALUE: \"a*\" matches \"\" through all four match variants (empty Str/String carry data == nullptr internally)");

    Regex re = regex_init();

    test_expect_true(test, "\"a*\" compiles", regex_compile_try(&re, "a*"));
    test_expect_true(test, "match_1 over \"\" answers true", regex_match_1(&re, "", 0));
    test_expect_true(test, "match_2 over size 0 answers true", regex_match_2(&re, "", 0, 0));

    Str empty_str       = str_init_1();
    String empty_string = string_init_1();

    test_expect_true(test, "match_3 over the empty Str answers true (no crash on data == nullptr)", regex_match_3(&re, &empty_str, 0));
    test_expect_true(test, "match_4 over the empty String answers true", regex_match_4(&re, &empty_string, 0));
    test_expect_u(test, "the empty match begins at 0", 0, regex_get_match_begin(&re));
    test_expect_u(test, "the empty match has size 0", 0, regex_get_match_size(&re));

    MatchRecord record = DEFAULT_INITIALIZATION;

    test_expect_u(test, "match_all_3 over the empty Str counts the one empty match", 1, regex_match_all_3(&re, &empty_str, 0, _record_match, &record));

    /* Negative anchor: a pattern REQUIRING content refuses the empty subject. */
    Regex re_plus = regex_init();

    test_expect_true(test, "\"a+\" compiles", regex_compile_try(&re_plus, "a+"));
    test_expect_false(test, "\"a+\" does NOT match \"\"", regex_match_1(&re_plus, "", 0));

    string_uninit(&empty_string);
    regex_uninit(&re_plus);
    regex_uninit(&re);

    test_case_end(test);
}

static void _test_empty_pattern(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "empty pattern is a VALUE: compile_try_2(\"\", 0) compiles and matches everywhere");

    Regex re = regex_init();

    test_expect_true(test, "the empty pattern compiles", regex_compile_try_2(&re, "", 0));
    test_expect_true(test, "it matches \"abc\"", regex_match_1(&re, "abc", 0));
    test_expect_true(test, "it matches \"\"", regex_match_1(&re, "", 0));
    test_expect_u(test, "its match has size 0", 0, regex_get_match_size(&re));

    MatchRecord record = DEFAULT_INITIALIZATION;

    test_expect_u(test, "match_all over \"ab\" counts 3 empties (offsets 0, 1, 2)", 3, regex_match_all_1(&re, "ab", 0, _record_match, &record));

    Str empty_str_pattern = str_init_1();

    Regex re_str = regex_init();

    test_expect_true(test, "compile_try_3 accepts the empty Str pattern (data == nullptr)", regex_compile_try_3(&re_str, &empty_str_pattern));
    test_expect_true(test, "and the result matches", regex_match_1(&re_str, "x", 0));

    String empty_string_pattern = string_init_1();

    Regex re_string = regex_init();

    test_expect_true(test, "compile_try_4 accepts the empty String pattern", regex_compile_try_4(&re_string, &empty_string_pattern));
    test_expect_true(test, "and the result matches", regex_match_1(&re_string, "x", 0));

    string_uninit(&empty_string_pattern);
    regex_uninit(&re_string);
    regex_uninit(&re_str);
    regex_uninit(&re);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Offset Contract
 *============================================================================*/
static void _test_offset_bounds(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "offset contract: offset == size is legal (\"a*\" matches empty at the end), past-the-end refuses with false / 0");

    Regex re = regex_init();

    test_expect_true(test, "\"a*\" compiles", regex_compile_try(&re, "a*"));
    test_expect_true(test, "offset == size matches the empty at the end", regex_match_2(&re, "abc", 3, 3));
    test_expect_u(test, "that match begins at 3", 3, regex_get_match_begin(&re));
    test_expect_u(test, "that match ends at 3", 3, regex_get_match_end(&re));

    test_expect_false(test, "match_2 past the end refuses", regex_match_2(&re, "abc", 3, 4));
    test_expect_false(test, "match_1 past the end refuses", regex_match_1(&re, "abc", 4));

    char subject_buffer[] = "abc";
    Str str_subject       = str_init_3(subject_buffer, 3);
    String string_subject = string_init_3(subject_buffer);

    test_expect_false(test, "match_3 past the end refuses", regex_match_3(&re, &str_subject, 4));
    test_expect_false(test, "match_4 past the end refuses", regex_match_4(&re, &string_subject, 4));

    MatchRecord record = DEFAULT_INITIALIZATION;

    test_expect_u(test, "match_all_2 past the end answers 0", 0, regex_match_all_2(&re, "abc", 3, 4, _record_match, &record));
    test_expect_u(test, "and its callback never fired", 0, record.count);

    /* An anchored-at-end pattern through a mid-string offset: "c$" over "abc". */
    Regex re_end = regex_init();

    test_expect_true(test, "\"c$\" compiles", regex_compile_try(&re_end, "c$"));
    test_expect_true(test, "\"c$\" matches \"abc\"", regex_match_1(&re_end, "abc", 0));
    test_expect_u(test, "begin is 2", 2, regex_get_match_begin(&re_end));
    test_expect_u(test, "end is 3 (the subject size)", 3, regex_get_match_end(&re_end));

    string_uninit(&string_subject);
    regex_uninit(&re_end);
    regex_uninit(&re);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Captures
 *============================================================================*/
static void _test_numbered_groups(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "numbered captures: \"(\\\\w+)@(\\\\w+)\" over \"user@host\" - group 0/1/2 through all three get_number container forms, content AND teardown");

    Regex re = regex_init();

    test_expect_true(test, "the two-group pattern compiles", regex_compile_try(&re, "(\\w+)@(\\w+)"));
    test_expect_true(test, "it matches \"user@host\"", regex_match_1(&re, "user@host", 0));

    char *group_0 = regex_get_match_group_number_1(&re, 0);
    char *group_1 = regex_get_match_group_number_1(&re, 1);
    char *group_2 = regex_get_match_group_number_1(&re, 2);

    test_expect_string(test, "group 0 is the whole match", "user@host", group_0);
    test_expect_string(test, "group 1 is \"user\"", "user", group_1);
    test_expect_string(test, "group 2 is \"host\"", "host", group_2);

    char_delete(group_0);
    char_delete(group_1);
    char_delete(group_2);

    Str str_group_1 = regex_get_match_group_number_2(&re, 1);

    test_expect_u(test, "the Str form carries size 4", 4, str_get_size(&str_group_1));
    test_expect_true(test, "the Str form content is \"user\"", str_compare_equal_1(&str_group_1, "user"));

    str_uninit(&str_group_1);

    String string_group_2 = regex_get_match_group_number_3(&re, 2);

    test_expect_u(test, "the String form carries size 4", 4, string_get_size(&string_group_2));
    test_expect_string(test, "the String form content is \"host\"", "host", string_get_data(&string_group_2));

    string_uninit(&string_group_2);
    regex_uninit(&re);

    test_case_end(test);
}

static void _test_named_groups(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "named captures: \"(?<user>\\\\w+)@(?<host>\\\\w+)\" through all three get_name container forms");

    Regex re = regex_init();

    test_expect_true(test, "the named-group pattern compiles", regex_compile_try(&re, "(?<user>\\w+)@(?<host>\\w+)"));
    test_expect_true(test, "it matches \"user@host\"", regex_match_1(&re, "user@host", 0));

    char *by_name = regex_get_match_group_name_1(&re, "user");

    test_expect_string(test, "get_name_1(\"user\") is \"user\"", "user", by_name);

    char_delete(by_name);

    Str str_by_name = regex_get_match_group_name_2(&re, "host");

    test_expect_u(test, "get_name_2(\"host\") carries size 4", 4, str_get_size(&str_by_name));
    test_expect_true(test, "get_name_2(\"host\") content is \"host\"", str_compare_equal_1(&str_by_name, "host"));

    str_uninit(&str_by_name);

    String string_by_name = regex_get_match_group_name_3(&re, "user");

    test_expect_u(test, "get_name_3(\"user\") carries size 4", 4, string_get_size(&string_by_name));
    test_expect_string(test, "get_name_3(\"user\") content is \"user\"", "user", string_get_data(&string_by_name));

    string_uninit(&string_by_name);
    regex_uninit(&re);

    test_case_end(test);
}

static void _test_group_absent_and_empty(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "group refusals and the freeable empty: a NON-participating group returns nullptr/empty/failure; \"(a?)b\" over \"b\" returns the documented size-0 freeable empty");

    /* Non-participating: "(a)|(b)" matching "b" leaves group 1 unset. */
    Regex re = regex_init();

    test_expect_true(test, "\"(a)|(b)\" compiles", regex_compile_try(&re, "(a)|(b)"));
    test_expect_true(test, "it matches \"b\"", regex_match_1(&re, "b", 0));

    test_expect_null(test, "non-participating group 1: get_number_1 returns nullptr", regex_get_match_group_number_1(&re, 1));

    Str absent_str = regex_get_match_group_number_2(&re, 1);

    test_expect_u(test, "non-participating group 1: get_number_2 returns the empty Str", 0, str_get_size(&absent_str));

    String absent_string = regex_get_match_group_number_3(&re, 1);

    test_expect_u(test, "non-participating group 1: get_number_3 returns the empty String", 0, string_get_size(&absent_string));

    char copy_buffer[16] = DEFAULT_INITIALIZATION;
    USize copy_size      = sizeof(copy_buffer);

    test_expect_true(test, "non-participating group 1: copy_number_1 fails cleanly", result_is_error(regex_copy_match_group_number_1(&re, 1, copy_buffer, &copy_size)));

    /* Positive anchor: group 2 DID participate. */
    char *group_2 = regex_get_match_group_number_1(&re, 2);

    test_expect_string(test, "participating group 2 is \"b\"", "b", group_2);

    char_delete(group_2);

    test_expect_null(test, "out-of-range index 5 returns nullptr", regex_get_match_group_number_1(&re, 5));
    test_expect_null(test, "unknown name returns nullptr", regex_get_match_group_name_1(&re, "nope"));

    str_uninit(&absent_str);
    string_uninit(&absent_string);
    regex_uninit(&re);

    /* Participating-but-empty: "(a?)b" over "b" - group 1 matched zero bytes. */
    Regex re_empty = regex_init();

    test_expect_true(test, "\"(a?)b\" compiles", regex_compile_try(&re_empty, "(a?)b"));
    test_expect_true(test, "it matches \"b\"", regex_match_1(&re_empty, "b", 0));

    char *empty_group = regex_get_match_group_number_1(&re_empty, 1);

    test_expect_not_null(test, "the empty-participating group is a real (freeable) pointer", empty_group);
    test_expect_string(test, "and it holds the empty string", "", empty_group);

    char_delete(empty_group);

    Str empty_group_str = regex_get_match_group_number_2(&re_empty, 1);

    test_expect_u(test, "the Str form of the empty group has size 0", 0, str_get_size(&empty_group_str));

    str_uninit(&empty_group_str);
    regex_uninit(&re_empty);

    test_case_end(test);
}

static void _test_copy_contract(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "copy_match_group in/out contract: pre-sized Str receives \"user\" with size 4; a too-small buffer FAILS with the container UNCHANGED; String bounds by CAPACITY; the 4-vs-5 terminator edge");

    Regex re = regex_init();

    test_expect_true(test, "the named two-group pattern compiles", regex_compile_try(&re, "(?<u>\\w+)@(?<h>\\w+)"));
    test_expect_true(test, "it matches \"user@host\"", regex_match_1(&re, "user@host", 0));

    /* copy_number_1: the router shape - char buffer, in/out size. */
    char copy_buffer[32] = DEFAULT_INITIALIZATION;
    USize copy_size      = sizeof(copy_buffer);

    test_expect_true(test, "copy_number_1 into a 32-byte buffer succeeds", result_is_success(regex_copy_match_group_number_1(&re, 1, copy_buffer, &copy_size)));
    test_expect_u(test, "out size is the substring length 4", 4, copy_size);
    test_expect_string(test, "the buffer holds \"user\" (terminated)", "user", copy_buffer);

    /* The router's second-call shape: size REINITIALIZED, buffer reused. */
    copy_size = sizeof(copy_buffer);

    test_expect_true(test, "the reused buffer with a reinitialized size succeeds again", result_is_success(regex_copy_match_group_number_1(&re, 2, copy_buffer, &copy_size)));
    test_expect_string(test, "and now holds \"host\"", "host", copy_buffer);

    /* The terminator edge: PCRE2 needs length + 1 bytes. */
    char exact_4[4] = DEFAULT_INITIALIZATION;
    USize size_4    = 4;

    test_expect_true(test, "a buffer of exactly 4 for \"user\" FAILS (no terminator room)", result_is_error(regex_copy_match_group_number_1(&re, 1, exact_4, &size_4)));

    char exact_5[5] = DEFAULT_INITIALIZATION;
    USize size_5    = 5;

    test_expect_true(test, "a buffer of exactly 5 for \"user\" succeeds", result_is_success(regex_copy_match_group_number_1(&re, 1, exact_5, &size_5)));
    test_expect_u(test, "with out size 4", 4, size_5);
    test_expect_string(test, "and the terminated content", "user", exact_5);

    /* copy_number_2: pre-sized Str, size = capacity in, length out. */
    char str_buffer[32] = DEFAULT_INITIALIZATION;
    Str str_copy        = str_init_3(str_buffer, sizeof(str_buffer));

    test_expect_true(test, "copy_number_2 into a size-32 Str succeeds", result_is_success(regex_copy_match_group_number_2(&re, 1, &str_copy)));
    test_expect_u(test, "the Str size became 4", 4, str_get_size(&str_copy));
    test_expect_true(test, "the Str content is \"user\"", str_compare_equal_1(&str_copy, "user"));

    char small_buffer[2] = DEFAULT_INITIALIZATION;
    Str str_small        = str_init_3(small_buffer, sizeof(small_buffer));

    test_expect_true(test, "a size-2 Str FAILS", result_is_error(regex_copy_match_group_number_2(&re, 1, &str_small)));
    test_expect_u(test, "and its size is UNCHANGED (still 2 - the invariant fix)", 2, str_get_size(&str_small));

    /* copy_name_3 / copy_number_3: the String's CAPACITY is the bound. */
    String string_copy           = string_init_2(32);
    USize const capacity_before  = string_get_capacity(&string_copy);

    test_expect_true(test, "copy_name_3 into a capacity-32 String succeeds", result_is_success(regex_copy_match_group_name_3(&re, "h", &string_copy)));
    test_expect_u(test, "the String size was SET to 4", 4, string_get_size(&string_copy));
    test_expect_string(test, "the String content is \"host\"", "host", string_get_data(&string_copy));
    test_expect_u(test, "its capacity is untouched", capacity_before, string_get_capacity(&string_copy));

    String string_small               = string_init_2(2);
    USize const small_capacity_before = string_get_capacity(&string_small);
    USize const small_size_before     = string_get_size(&string_small);

    test_expect_true(test, "a capacity-2 String FAILS", result_is_error(regex_copy_match_group_number_3(&re, 1, &string_small)));
    test_expect_u(test, "its size is unchanged on failure", small_size_before, string_get_size(&string_small));
    test_expect_u(test, "its capacity is unchanged on failure", small_capacity_before, string_get_capacity(&string_small));

    /* copy_name_1 and copy_name_2 complete the six-accessor sweep. */
    char name_buffer[32] = DEFAULT_INITIALIZATION;
    USize name_size      = sizeof(name_buffer);

    test_expect_true(test, "copy_name_1(\"u\") succeeds", result_is_success(regex_copy_match_group_name_1(&re, "u", name_buffer, &name_size)));
    test_expect_string(test, "and copied \"user\"", "user", name_buffer);

    char name_str_buffer[32] = DEFAULT_INITIALIZATION;
    Str name_str_copy        = str_init_3(name_str_buffer, sizeof(name_str_buffer));

    test_expect_true(test, "copy_name_2(\"h\") succeeds", result_is_success(regex_copy_match_group_name_2(&re, "h", &name_str_copy)));
    test_expect_u(test, "with size 4", 4, str_get_size(&name_str_copy));
    test_expect_true(test, "and content \"host\"", str_compare_equal_1(&name_str_copy, "host"));

    string_uninit(&string_small);
    string_uninit(&string_copy);
    regex_uninit(&re);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Compile Forms
 *============================================================================*/
static void _test_compile_sized_embedded_nul(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "compile_try_2 with an embedded NUL: pattern \"a\\0b\" sized 3 is legal in PCRE2's sized form and matches the same bytes in a sized subject");

    Regex re = regex_init();

    char const pattern_with_nul[3] = { 'a', '\0', 'b' };

    test_expect_true(test, "the sized pattern with an embedded NUL compiles", regex_compile_try_2(&re, pattern_with_nul, 3));

    char const subject_with_nul[5] = { 'x', 'a', '\0', 'b', 'y' };

    test_expect_true(test, "it matches the same bytes in a sized subject", regex_match_2(&re, subject_with_nul, 5, 0));
    test_expect_u(test, "the match begins at 1", 1, regex_get_match_begin(&re));
    test_expect_u(test, "the match size is 3", 3, regex_get_match_size(&re));

    /* Negative anchor: the NUL is a real byte - a subject without it refuses. */
    test_expect_false(test, "\"ab\" (no NUL between) does not match", regex_match_2(&re, "aby", 3, 0));

    regex_uninit(&re);

    test_case_end(test);
}

static void _test_compile_container_forms(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "compile_try_3/_4 (Str/String patterns) and compile_try_options: REGEX_COMPILE_CASELESS makes \"ABC\" match \"abc\" (the vestigo \"(?i)\" splice replacement)");

    char pattern_buffer[] = "a+b";
    Str str_pattern       = str_init_3(pattern_buffer, 3);

    Regex re_str = regex_init();

    test_expect_true(test, "compile_try_3 accepts the Str pattern", regex_compile_try_3(&re_str, &str_pattern));
    test_expect_true(test, "and it matches \"xaab\"", regex_match_1(&re_str, "xaab", 0));
    test_expect_u(test, "at begin 1", 1, regex_get_match_begin(&re_str));

    String string_pattern = string_init_3(pattern_buffer);

    Regex re_string = regex_init();

    test_expect_true(test, "compile_try_4 accepts the String pattern", regex_compile_try_4(&re_string, &string_pattern));
    test_expect_true(test, "and it matches \"aab\"", regex_match_1(&re_string, "aab", 0));

    Regex re_caseless = regex_init();

    test_expect_true(test, "compile_try_options(\"ABC\", CASELESS) compiles", regex_compile_try_options(&re_caseless, "ABC", REGEX_COMPILE_CASELESS));
    test_expect_true(test, "\"ABC\" matches \"abc\" under CASELESS", regex_match_1(&re_caseless, "xabcy", 0));

    /* Negative anchor: without the flag the same pattern refuses "abc". */
    Regex re_cased = regex_init();

    test_expect_true(test, "compile_try_options(\"ABC\", 0) compiles", regex_compile_try_options(&re_cased, "ABC", 0));
    test_expect_false(test, "\"ABC\" does NOT match \"abc\" without CASELESS", regex_match_1(&re_cased, "xabcy", 0));

    string_uninit(&string_pattern);
    regex_uninit(&re_cased);
    regex_uninit(&re_caseless);
    regex_uninit(&re_string);
    regex_uninit(&re_str);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Error Surface
 *============================================================================*/
static void _test_error_surface(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "error surface: a failed compile_try renders a non-empty message and an offset inside the pattern; the accessors are safe after a successful match");

    Regex re = regex_init();

    char const *const bad_pattern = "[unclosed";

    test_expect_false(test, "the malformed pattern refuses to compile", regex_compile_try(&re, bad_pattern));

    char message[REGEX_ERROR_MESSAGE_SIZE] = DEFAULT_INITIALIZATION;

    test_expect_true(test, "regex_get_error_message renders text", regex_get_error_message(&re, message, sizeof(message)));
    test_expect_true(test, "the message is non-empty", strlen(message) > 0);
    test_expect_string_contains(test, "the message names the missing bracket", message, "missing terminating");
    test_expect_true(test, "error_offset points into (or just past) the pattern", regex_get_error_offset(&re) <= strlen(bad_pattern));
    test_expect_true(test, "error_offset is past the opening bracket (a real position, not 0)", regex_get_error_offset(&re) > 0);

    regex_uninit(&re);

    /* After a SUCCESSFUL compile + match neither accessor crashes. */
    Regex re_good = regex_init();

    test_expect_true(test, "\"a+\" compiles", regex_compile_try(&re_good, "a+"));
    test_expect_true(test, "and matches", regex_match_1(&re_good, "aaa", 0));

    char after_message[REGEX_ERROR_MESSAGE_SIZE] = DEFAULT_INITIALIZATION;

    regex_get_error_message(&re_good, after_message, sizeof(after_message));

    test_expect_u(test, "error_offset on the fresh successful object is 0", 0, regex_get_error_offset(&re_good));

    regex_uninit(&re_good);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Recompile and Match State
 *============================================================================*/
static void _test_recompile_live_object(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "recompile into a LIVE object: compile \"a+\", match, compile \"b+\" into the SAME object - correct results, no crash (the leak fix's behavior pin)");

    Regex re = regex_init();

    test_expect_true(test, "\"a+\" compiles", regex_compile_try(&re, "a+"));
    test_expect_true(test, "\"a+\" matches \"aaa\"", regex_match_1(&re, "aaa", 0));
    test_expect_u(test, "with size 3", 3, regex_get_match_size(&re));

    test_expect_true(test, "\"b+\" compiles into the same live object", regex_compile_try(&re, "b+"));
    test_expect_false(test, "the recompiled object no longer matches \"aaa\"", regex_match_1(&re, "aaa", 0));
    test_expect_true(test, "and matches \"bbb\"", regex_match_1(&re, "bbb", 0));
    test_expect_u(test, "with size 3", 3, regex_get_match_size(&re));

    /* A failed recompile leaves the object refusing, not half-alive on "b+". */
    test_expect_false(test, "a malformed recompile refuses", regex_compile_try(&re, "(oops"));
    test_expect_false(test, "and the object no longer matches \"bbb\"", regex_match_1(&re, "bbb", 0));

    regex_uninit(&re);

    test_case_end(test);
}

static void _test_match_state_validity(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "match state validity: begin + size == end, and subject[begin..end) equals the matched text");

    Regex re = regex_init();

    char const *const subject = "say user@host now";

    test_expect_true(test, "\"(\\\\w+)@(\\\\w+)\" compiles", regex_compile_try(&re, "(\\w+)@(\\w+)"));
    test_expect_true(test, "it matches inside the subject", regex_match_1(&re, subject, 0));

    USize const begin = regex_get_match_begin(&re);
    USize const end   = regex_get_match_end(&re);
    USize const size  = regex_get_match_size(&re);

    test_expect_u(test, "begin is 4", 4, begin);
    test_expect_u(test, "end is 13", 13, end);
    test_expect_u(test, "begin + size == end", end, begin + size);
    test_expect_true(test, "subject[begin..end) is \"user@host\"", memcmp(subject + begin, "user@host", size) == 0);

    regex_uninit(&re);

    test_case_end(test);
}

static void _test_sized_subject_poisoned_tail(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "sized, non-terminated subjects: bytes past subject_size are INVISIBLE (the discarded-size fuzz lesson)");

    Regex re = regex_init();

    test_expect_true(test, "\"cZ\" compiles", regex_compile_try(&re, "cZ"));

    /* The poisoned tail: real content "abc", then bytes that WOULD complete the
     * match if the size were ignored. No terminator inside the claimed size. */
    char poisoned[8] = { 'a', 'b', 'c', 'Z', 'Z', 'Z', 'Z', 'Z' };

    test_expect_false(test, "size 3 hides the tail - no match", regex_match_2(&re, poisoned, 3, 0));
    test_expect_true(test, "size 4 exposes the 'Z' - match (positive anchor)", regex_match_2(&re, poisoned, 4, 0));
    test_expect_u(test, "the match begins at 2", 2, regex_get_match_begin(&re));

    MatchRecord record = DEFAULT_INITIALIZATION;

    test_expect_u(test, "match_all_2 respects the size too (0 at size 3)", 0, regex_match_all_2(&re, poisoned, 3, 0, _record_match, &record));
    test_expect_u(test, "and finds the 1 at size 4", 1, regex_match_all_2(&re, poisoned, 4, 0, _record_match, &record));

    regex_uninit(&re);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Cases: Anti-Vacuity
 *============================================================================*/
static void _test_anti_vacuity_closed_form(Test *const test) {
    _case_entered_count += 1;

    test_case_begin(test, "anti-vacuity: every case function above actually ran");

    test_expect_u(test, "closed-form case count", _EXPECTED_CASE_COUNT, _case_entered_count);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Main
 *============================================================================*/
int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/regex/test_regex.c");

    test_suite_begin(&test, "regex_refusing_object");
    _test_fresh_object_refuses(&test);
    _test_failed_compile_refuses(&test);
    _test_compiled_unmatched_refuses(&test);
    _test_recompile_regates_accessors(&test);
    _test_failed_match_keeps_gate_closed(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "regex_match_all");
    _test_match_all_exact_three(&test);
    _test_match_all_zero_length_iteration(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "regex_empty_values");
    _test_empty_subject(&test);
    _test_empty_pattern(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "regex_offsets");
    _test_offset_bounds(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "regex_captures");
    _test_numbered_groups(&test);
    _test_named_groups(&test);
    _test_group_absent_and_empty(&test);
    _test_copy_contract(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "regex_compile_forms");
    _test_compile_sized_embedded_nul(&test);
    _test_compile_container_forms(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "regex_error_surface");
    _test_error_surface(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "regex_state");
    _test_recompile_live_object(&test);
    _test_match_state_validity(&test);
    _test_sized_subject_poisoned_tail(&test);
    test_suite_end(&test);

    test_suite_begin(&test, "regex_anchors");
    _test_anti_vacuity_closed_form(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}