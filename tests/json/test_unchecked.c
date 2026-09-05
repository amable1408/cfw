#include <json/json.h>
#include <test/test.h>

/*
 * Behavioral tests for include/json/json.c built WITHOUT ERROR_CHECK_ENABLED.
 *
 * error_check_null in this module guards only NULL-POINTER arguments (self/data/name/...);
 * a violated contract there is undefined behavior with the checks compiled out, and this file
 * never passes a null pointer where the checked suite would have aborted. What survives the
 * define is the OTHER class of condition - plain runtime `if` refusals that never went through
 * error_check at all: malformed JSON text, a missing object key, a type mismatch on a typed
 * getter, an out-of-range array index, a missing json_at_* path segment, and an empty JSON
 * string value's raw char* pointer. All of these are coded as ordinary comparisons in json.c,
 * so they refuse identically whether ERROR_CHECK_ENABLED is defined or not - this build proves
 * that the refusal is not an artifact of the check macro being compiled in.
 */

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/json/test_unchecked.c");

    test_suite_begin(&test, "json (unchecked)");
    test_case_begin(&test, "value refusals hold identically without ERROR_CHECK_ENABLED");

    Json *malformed = json_from_1("{not valid json");

    test_expect_null(&test, "malformed text still parses to nullptr", malformed);

    Json *empty = json_from_2("", 0);

    test_expect_null(&test, "an empty (size 0) buffer still parses to nullptr", empty);

    Json *root = json_from_1("{\"a\":[1,2],\"o\":{\"x\":1},\"s\":\"hi\",\"empty\":\"\"}");

    test_expect_not_null(&test, "a well-formed document still parses", root);
    test_expect_null(&test, "a missing key is still nullptr", json_get_string_1(root, "zzz"));
    test_expect_null(&test, "an array field is still refused by the object accessor", json_get_object_1(root, "a"));
    test_expect_null(&test, "an object field is still refused by the array accessor", json_get_array_1(root, "o"));
    test_expect_null(&test, "a string field is still refused by the number accessor", json_get_number_int_1(root, "s"));

    Json *const out_of_range = json_at_1(root, "['a'][7]");

    test_expect_null(&test, "an out-of-range array index in a path is still nullptr", out_of_range);

    Json *const missing_path = json_at_1(root, "['missing']['deeper']");

    test_expect_null(&test, "a missing path segment is still nullptr", missing_path);

    Json *const empty_node = json_get_string_1(root, "empty");

    test_expect_not_null(&test, "the empty-string field is still typed STRING and found", empty_node);
    // item 3: the per-node string cache is gone; an empty string VALUE now reads as "",
    // never nullptr - this holds identically whether ERROR_CHECK is compiled in or not.
    test_expect_string(&test, "an empty JSON string value now reads as \"\", not nullptr", "", json_get_value_string_1(empty_node));

    bool bool_value = false;

    test_expect_false(&test, "read_bool on a missing key still answers false", json_read_bool(root, "zzz", &bool_value));

    ISize int_value = 0;

    test_expect_false(&test, "read_int on a missing key still answers false", json_read_int(root, "zzz", &int_value));

    json_delete(&root);

    test_case_end(&test);

    test_case_begin(&test, "R2 High 1/Mid 7 repro: the nine json_object_add_* one-shot "
        "wrappers (bool/number_float/number_float_precision/number_int/number_uint/"
        "string_1..4) used to segfault in this unchecked build when self was not (and had "
        "no) object parent - json_object_add_1 already answers nullptr there, but the "
        "wrapper dereferenced it unconditionally to call the matching json_set_*.");

    Json *array_root = json_from_1("[1,2]");

    test_expect_null(&test, "add_bool on an array root refuses instead of crashing", json_object_add_bool(array_root, "k", true));
    test_expect_null(&test, "add_number_int on an array root refuses instead of crashing", json_object_add_number_int(array_root, "k", -1));
    test_expect_null(&test, "add_string_1 on an array root refuses instead of crashing", json_object_add_string_1(array_root, "k", "v"));

    json_delete(&array_root);

    test_case_end(&test);

    test_case_begin(&test, "item 19/40: json_arena_size - a too-small arena degrades to a "
        "graceful parse failure ONLY without ERROR_CHECK; the checked build instead aborts "
        "inside the arena allocator on exhaustion (arena_linear_alloc's own contract, not "
        "specific to json), which is why this pin lives here and not in test_all.c's "
        "_test_arena_size. Body sized to ~100 KB (measured separately: at that scale "
        "json_arena_size/2 sits below the real minimum, ~5.65 MB vs the ~5.2 MB half-size, "
        "so the shortfall is not a rounding artifact). Success is checked by the tree's own "
        "element count, not a bare non-null root: on a too-small arena the READ pool can "
        "silently fall back to a plain malloc (a separate, pre-existing gap outside this "
        "phase's scope), which alone would make the parse LOOK like it succeeded.");

    String body = string_init_1();

    string_add_last_1(&body, (char*) "[");

    USize element_count = 0;

    while (string_get_size(&body) < 100000) {
        if (element_count > 0) {
            string_add_last_1(&body, (char*) ",");
        }

        Str number = str_from_numbers_uint_1(element_count);

        string_add_last_3(&body, &number);
        str_uninit(&number);
        element_count += 1;
    }

    string_add_last_1(&body, (char*) "]");

    USize const body_size = string_get_size(&body);
    USize const good_size = json_arena_size(body_size);

    Arena good_arena = arena_init_1(good_size, ARENA_TYPE_LINEAR);
    Json *good_root = json_alloc_from_2(string_get_data(&body), body_size, &good_arena);

    test_expect_not_null(&test, "json_arena_size(N) parses an N-byte body", good_root);
    test_expect_u(&test, "the parsed tree holds every element (not a truncated/failed copy)", element_count, json_array_get_size(good_root));

    json_delete(&good_root);
    arena_uninit(&good_arena, ARENA_TYPE_LINEAR);

    Arena small_arena = arena_init_1(good_size / 2, ARENA_TYPE_LINEAR);
    Json *small_root = json_alloc_from_2(string_get_data(&body), body_size, &small_arena);
    bool const small_arena_produced_a_complete_tree = small_root != nullptr && json_array_get_size(small_root) == element_count;

    test_expect_false(&test, "half of json_arena_size(N) is too small for the same body - no abort, and no complete tree either", small_arena_produced_a_complete_tree);

    if (small_root != nullptr) {
        json_delete(&small_root);
    }

    arena_uninit(&small_arena, ARENA_TYPE_LINEAR);

    // R2 High 3: an arena too small to satisfy even the READ POOL borrow (not just the
    // mutable-copy step above) used to fall through to yyjson_read_opts's own default
    // (libc malloc/free) allocator and silently succeed off-arena, leaking the whole read
    // doc (~13x body bytes) per request. 4096 bytes covers one Json struct comfortably but
    // is nowhere near the read-pool hint (over 1 MB for this body), so _json_alc's pool
    // borrow fails and the fix must refuse (nullptr) rather than parse anyway.
    Arena pool_starved_arena = arena_init_1(4096, ARENA_TYPE_LINEAR);
    Json *pool_starved_root = json_alloc_from_2(string_get_data(&body), body_size, &pool_starved_arena);

    test_expect_null(&test, "a read-pool-starved arena refuses (nullptr), never a libc-backed parse", pool_starved_root);

    if (pool_starved_root != nullptr) {
        json_delete(&pool_starved_root);
    }

    arena_uninit(&pool_starved_arena, ARENA_TYPE_LINEAR);
    string_uninit(&body);

    test_case_end(&test);

    test_case_begin(&test, "code fix: yyjson_val_mut_copy returning nullptr (read pool sized "
        "OK, mutable-copy pool starved) is null-checked in _json_from - the parse fails "
        "cleanly instead of handing back a root with a null val. json_arena_size(N) sums "
        "the immutable read pool (~yyjson's own max-usage figure for N, borrowed once up "
        "front) and the mutable copy that follows it (13xN as of R3 High 3 - stale comment "
        "fixed, R3 Misc 14); the two terms are now roughly EQUAL in size, so half of "
        "json_arena_size(N) leaves just enough headroom for the read pool's single "
        "up-front borrow to succeed, but starves the copy that borrows from the SAME "
        "arena afterwards.");

    String big_body = string_init_1();

    string_add_last_1(&big_body, (char*) "[");

    for (USize i = 0; i < 20000; i += 1) {
        if (i > 0) {
            string_add_last_1(&big_body, (char*) ",");
        }

        Str number = str_from_numbers_uint_1(i);

        string_add_last_3(&big_body, &number);
        str_uninit(&number);
    }

    string_add_last_1(&big_body, (char*) "]");

    USize const big_body_size = string_get_size(&big_body);
    USize const full_size = json_arena_size(big_body_size);
    USize const starved_size = full_size / 2 + 512; // read pool fits; copy pool starved

    Arena starved_arena = arena_init_1(starved_size, ARENA_TYPE_LINEAR);
    Json *starved_root = json_alloc_from_2(string_get_data(&big_body), big_body_size, &starved_arena);

    test_expect_null(&test, "read succeeds but the mutable-copy pool is starved -> nullptr, never a half-built root with a null val", starved_root);

    if (starved_root != nullptr) {
        json_delete(&starved_root);
    }

    arena_uninit(&starved_arena, ARENA_TYPE_LINEAR);
    string_uninit(&big_body);

    test_case_end(&test);

    test_case_begin(&test, "R3 High 3: json_arena_size's copy term is 13xN (yyjson's own "
        "worst-case value count, N/2+1, times its 24-byte yyjson_mut_val chunk), not the "
        "old 6x measured off one ascending-integer body - an all-scalar body ('[0,0,0,...]') "
        "is the worst case that shape under-sized: arena_init_1(json_arena_size(N)) used to "
        "hand back a too-small arena for exactly this shape (nullptr in this unchecked "
        "lane; the checked build would abort instead), now it parses.");

    String scalars_body = string_init_1();

    string_add_last_1(&scalars_body, (char*) "[");

    USize scalar_count = 0;

    while (string_get_size(&scalars_body) < 100000) {
        if (scalar_count > 0) {
            string_add_last_1(&scalars_body, (char*) ",");
        }

        string_add_last_1(&scalars_body, (char*) "0");
        scalar_count += 1;
    }

    string_add_last_1(&scalars_body, (char*) "]");

    USize const scalars_size      = string_get_size(&scalars_body);
    USize const scalars_arena_size = json_arena_size(scalars_size);

    printf("R3 High 3: json_arena_size(%zu) = %zu bytes (%zu scalar elements)\n", (size_t) scalars_size, (size_t) scalars_arena_size, (size_t) scalar_count);

    Arena scalars_arena = arena_init_1(scalars_arena_size, ARENA_TYPE_LINEAR);
    Json *scalars_root = json_alloc_from_2(string_get_data(&scalars_body), scalars_size, &scalars_arena);

    test_expect_not_null(&test, "json_arena_size(N) now parses a 100 KB all-scalar body (nullptr before the 13x fix)", scalars_root);

    if (scalars_root != nullptr) {
        test_expect_u(&test, "the parsed tree holds every scalar element", scalar_count, json_array_get_size(scalars_root));
        json_delete(&scalars_root);
    }

    arena_uninit(&scalars_arena, ARENA_TYPE_LINEAR);
    string_uninit(&scalars_body);

    // Overflow guard (R3 High 3): a byte_count large enough to overflow USize during the
    // internal 13x multiply must refuse (0), never wrap around to a small, wrong size.
    USize const overflow_size = json_arena_size(USIZE_MAX);

    test_expect_u(&test, "json_arena_size(USIZE_MAX) returns 0 (too large to arena), not a wrapped-around small size", 0, overflow_size);

    test_case_end(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}