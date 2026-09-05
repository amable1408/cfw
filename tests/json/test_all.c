#include <json/json.h>
#include <test/test.h>

/* Suite for json/json.c, the yyjson-backed tree wrapper (yyjson.* itself is vendored and out
 * of scope). Covers parse (char/Str/String/file, heap and arena), malformed/empty text,
 * query by family (array/object/number/string/bool) including missing keys and wrong-type
 * refusals, nested access via json_at_*, edit (add/set/remove, static vs owned setters),
 * serialize with indentation 0/n plus round-trip, unicode/escapes, number boundaries, the
 * arena (alloc_*) family against a real Arena, the any-type getters, iteration, and the
 * public-round behavior changes: empty-input refusal before the null check, the deleted
 * per-node string cache, O(1) iteration, json_get_type/json_is_bool, the CFW heap allocator
 * feeding the yyjson writer, json_from_try_2, json_arena_size, remove/array_at/save, and
 * setters returning bool.
 *
 * Deliberately NOT exercised: json_alloc_from_2/json_from_2/json_alloc_from_1/json_from_1 with
 * a null data pointer AND a nonzero size. That combination still ABORTS the process under
 * ERROR_CHECK_ENABLED (this build) - calling it that way would end the run, not fail an
 * assertion. A null pointer WITH size 0 (the empty-Str/String shape) is exercised below and no
 * longer aborts (item 1). */

/*==============================================================================
 * MARK: - Internal
 *============================================================================*/

// cwd-relative (R2 High 4): the suite runs with tests/json as its working directory, and
// a hardcoded per-session scratchpad path aborted the whole run on any other machine,
// user, or session the moment the first file case ran.
static char const *const _TEST_JSON_TEMP_FILE = "json_test_temp.json";

static void _test_write_temp_file(char const *const data) {
    // file_open_try_1, not file_open_1 (R2 High 4): a write helper that can abort the
    // whole suite on a transient open failure (a locked file, a read-only cwd) has no
    // place in a suite meant to run unattended on any machine.
    File *file = file_open_try_1(_TEST_JSON_TEMP_FILE, "wb");

    if (file == nullptr) {
        return;
    }

    file_write_1(file, data, 1, char_length(data));
    file_close(&file);
}

/* Builds a large "[0,1,2,...,N-1]" body (heap String) - used to size json_arena_size
 * realistically rather than against a tiny document where any arena would happen to fit. */
static String _test_build_large_array_body(USize const element_count) {
    String body = string_init_1();

    string_add_last_1(&body, (char*) "[");

    for (USize i = 0; i < element_count; i += 1) {
        if (i > 0) {
            string_add_last_1(&body, (char*) ",");
        }

        Str number = str_from_numbers_uint_1(i);

        string_add_last_3(&body, &number);
        str_uninit(&number);
    }

    string_add_last_1(&body, (char*) "]");

    return body;
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_object_add(Test *const test) {
    test_case_begin(test, "json_object_add_* one-shots");

    Json *root = json_from_1("{}");

    Str const value_str = str_init_2((char*) "world");

    String value_string = string_init_1();
    string_add_last_1(&value_string, (char*) "sg");

    json_object_add_string_1(root, "s1", "hello");
    json_object_add_string_2(root, "s2", "sizedxx", 5);
    json_object_add_string_3(root, "s3", &value_str);
    json_object_add_string_4(root, "s4", &value_string);
    json_object_add_number_int(root, "ns", -42);
    json_object_add_number_uint(root, "nu", 42);
    json_object_add_number_float_precision(root, "nf", 3.14159, 2);
    json_object_add_bool(root, "bt", true);
    json_object_add_null(root, "zz");

    char *const data = json_get_data_1(root, 0);

    test_expect_string_contains(test, "string_1", data, "\"s1\":\"hello\"");
    test_expect_string_contains(test, "string_2 (sized)", data, "\"s2\":\"sized\"");
    test_expect_string_contains(test, "string_3 (Str)", data, "\"s3\":\"world\"");
    test_expect_string_contains(test, "string_4 (String)", data, "\"s4\":\"sg\"");
    test_expect_string_contains(test, "number_int", data, "\"ns\":-42");
    test_expect_string_contains(test, "number_uint", data, "\"nu\":42");
    test_expect_string_contains(test, "number_float_precision", data, "\"nf\":3.14");
    test_expect_string_contains(test, "bool", data, "\"bt\":true");
    test_expect_string_contains(test, "null", data, "\"zz\":null");

    // json_get_data_1's buffer now comes from the CFW heap allocator (item 8) - release
    // with memory_free, never allocator_release/libc free.
    memory_free((void*) data);
    json_delete(&root);
    string_uninit(&value_string);

    test_case_end(test);
}

static void _test_object_one_shots_on_array_root_refuse(Test *const test) {
    test_case_begin(test, "R2 High 1/Mid 7: json_object_add_* one-shots refuse (nullptr) "
        "rather than crash when self is not (and has no) object parent - repro: "
        "json_from_1(\"[1,2]\") then json_object_add_bool(root, \"k\", true) used to "
        "segfault in the unchecked build (memsec HIGH).");

    Json *array_root = json_from_1("[1,2]");

    test_expect_null(test, "add_bool on an array root refuses", json_object_add_bool(array_root, "k", true));
    test_expect_null(test, "add_number_float on an array root refuses", json_object_add_number_float(array_root, "k", 1.5));
    test_expect_null(test, "add_number_float_precision on an array root refuses", json_object_add_number_float_precision(array_root, "k", 1.5, 2));
    test_expect_null(test, "add_number_int on an array root refuses", json_object_add_number_int(array_root, "k", -1));
    test_expect_null(test, "add_number_uint on an array root refuses", json_object_add_number_uint(array_root, "k", 1));
    test_expect_null(test, "add_string_1 on an array root refuses", json_object_add_string_1(array_root, "k", "v"));
    test_expect_null(test, "add_string_2 on an array root refuses", json_object_add_string_2(array_root, "k", "v", 1));

    Str const value_str = str_init_2((char*) "v");

    test_expect_null(test, "add_string_3 on an array root refuses", json_object_add_string_3(array_root, "k", &value_str));

    String value_string = string_init_1();

    string_add_last_1(&value_string, (char*) "v");

    test_expect_null(test, "add_string_4 on an array root refuses", json_object_add_string_4(array_root, "k", &value_string));

    string_uninit(&value_string);
    json_delete(&array_root);

    test_case_end(test);
}

static void _test_delete_edge_cases(Test *const test) {
    test_case_begin(test, "R2 suite gap: json_delete(&nullptr) is a documented no-op, and "
        "deleting via a CHILD handle frees the whole tree (not just the child's own "
        "allocation) and nulls that handle.");

    Json *null_handle = nullptr;

    json_delete(&null_handle);
    test_expect_null(test, "json_delete on an already-null pointer stays null (no-op, no crash)", null_handle);

    Json *root = json_from_1("{\"a\":{\"b\":1}}");
    Json *child = json_get_object_1(root, "a"); // not const: json_delete needs &child

    test_expect_not_null(test, "child handle obtained", child);

    json_delete(&child); // deletes the WHOLE tree via the child; root is now dangling

    test_expect_null(test, "deleting via a child handle nulls that handle", child);

    test_case_end(test);
}

static void _test_parse_from_char(Test *const test) {
    test_case_begin(test, "json_from_1 / json_from_2: null-terminated and sized char parsing");

    Json *root_1 = json_from_1("{\"a\":1}");

    test_expect_not_null(test, "json_from_1 parses a small object", root_1);
    test_expect_not_null(test, "field a is present", json_get_number_uint_1(root_1, "a"));

    json_delete(&root_1);

    Json *root_2 = json_from_2("{\"a\":1}xxxxx", 7);

    test_expect_not_null(test, "json_from_2 honours the explicit size, ignoring trailing bytes", root_2);
    test_expect_u(test, "field a reads 1 from the sized parse", 1, json_get_value_number_uint(json_get_number_uint_1(root_2, "a")));

    json_delete(&root_2);

    Json *malformed = json_from_1("{not valid json");

    test_expect_null(test, "malformed text parses to nullptr, never aborts", malformed);

    Json *unterminated_string = json_from_1("{\"a\":\"oops}");

    test_expect_null(test, "an unterminated string value parses to nullptr", unterminated_string);

    Json *empty = json_from_2("", 0);

    test_expect_null(test, "an empty (size 0) buffer parses to nullptr, no abort", empty);

    test_case_end(test);
}

static void _test_parse_from_str_and_string(Test *const test) {
    test_case_begin(test, "json_from_3 (Str) / json_from_4 (String) parsing");

    Str const text_str = str_init_2((char*) "{\"b\":2}");
    Json *root_str = json_from_3(&text_str);

    test_expect_not_null(test, "json_from_3 parses from Str", root_str);
    test_expect_u(test, "field b reads 2", 2, json_get_value_number_uint(json_get_number_uint_1(root_str, "b")));

    json_delete(&root_str);

    String text_string = string_init_1();

    string_add_last_1(&text_string, (char*) "{\"c\":3}");

    Json *root_string = json_from_4(&text_string);

    test_expect_not_null(test, "json_from_4 parses from String", root_string);
    test_expect_u(test, "field c reads 3", 3, json_get_value_number_uint(json_get_number_uint_1(root_string, "c")));

    json_delete(&root_string);
    string_uninit(&text_string);

    test_case_end(test);
}

static void _test_empty_str_string_parse_refuses(Test *const test) {
    test_case_begin(test, "item 1, PROVING THE FIX: an EMPTY Str/String (null data pointer, "
        "size 0) used to reach error_check_null(data) and ABORT inside json_from_2/json_alloc_"
        "from_2 before this fix moved the data_size == 0 refusal ahead of the null check - now "
        "it answers nullptr like any other empty input");

    Str const empty_str = str_init_1();
    Json *const from_empty_str = json_from_3(&empty_str);

    test_expect_null(test, "json_from_3 on the EMPTY Str is nullptr, not an abort", from_empty_str);

    String empty_string = string_init_1();
    Json *const from_empty_string = json_from_4(&empty_string);

    test_expect_null(test, "json_from_4 on the EMPTY String is nullptr, not an abort", from_empty_string);

    string_uninit(&empty_string);

    test_case_end(test);
}

static void _test_parse_from_file(Test *const test) {
    test_case_begin(test, "json_load_1/3/4: parse from a char/Str/String file path, missing file is nullptr");

    _test_write_temp_file("{\"loaded\":true}");

    Json *root_1 = json_load_1(_TEST_JSON_TEMP_FILE);

    test_expect_not_null(test, "json_load_1 loads the temp file", root_1);

    bool loaded_flag = false;

    test_expect_true(test, "read_bool finds \"loaded\"", json_read_bool(root_1, "loaded", &loaded_flag));
    test_expect_true(test, "\"loaded\" is true", loaded_flag);

    json_delete(&root_1);

    Str const path_str = str_init_2((char*) _TEST_JSON_TEMP_FILE);
    Json *root_2 = json_load_3(&path_str);

    test_expect_not_null(test, "json_load_3 loads via Str path", root_2);
    json_delete(&root_2);

    String path_string = string_init_1();

    string_add_last_1(&path_string, (char*) _TEST_JSON_TEMP_FILE);

    Json *root_3 = json_load_4(&path_string);

    test_expect_not_null(test, "json_load_4 loads via String path", root_3);

    json_delete(&root_3);
    string_uninit(&path_string);

    file_remove_1(_TEST_JSON_TEMP_FILE);

    Json *missing = json_load_1(_TEST_JSON_TEMP_FILE);

    test_expect_null(test, "a missing/just-deleted file loads as nullptr, not an abort", missing);

    test_case_end(test);
}

static void _test_type_reporting_missing_and_wrong_type(Test *const test) {
    test_case_begin(test, "typed getters report the value's JsonType by which accessor succeeds; "
        "missing keys and type mismatches both answer nullptr");

    Json *root = json_from_1("{\"a\":[1,2],\"o\":{\"x\":1},\"f\":false,\"t\":true,"
        "\"si\":-5,\"ui\":5,\"nul\":null,\"re\":1.5,\"s\":\"hi\"}");

    test_expect_not_null(test, "root parses", root);

    test_expect_not_null(test, "array field found as array", json_get_array_1(root, "a"));
    test_expect_null(test, "array field refused as object", json_get_object_1(root, "a"));

    test_expect_not_null(test, "object field found as object", json_get_object_1(root, "o"));
    test_expect_null(test, "object field refused as array", json_get_array_1(root, "o"));

    Json *const signed_field = json_get_number_int_1(root, "si");

    test_expect_not_null(test, "negative literal typed signed", signed_field);
    test_expect_i(test, "si value is -5", -5, json_get_value_number_int(signed_field));
    test_expect_null(test, "signed field refused by the unsigned accessor", json_get_number_uint_1(root, "si"));

    Json *const unsigned_field = json_get_number_uint_1(root, "ui");

    test_expect_not_null(test, "positive literal typed unsigned", unsigned_field);
    test_expect_u(test, "ui value is 5", 5, json_get_value_number_uint(unsigned_field));
    test_expect_null(test, "unsigned field refused by the signed accessor", json_get_number_int_1(root, "ui"));

    Json *const real_field = json_get_number_float_1(root, "re");

    test_expect_not_null(test, "real field found as real", real_field);
    test_expect_f(test, "re value is 1.5", 1.5, json_get_value_number_float(real_field), 0.0001);

    Json *const string_field = json_get_string_1(root, "s");

    test_expect_not_null(test, "string field found as string", string_field);
    test_expect_string(test, "s value is hi", "hi", json_get_value_string_1(string_field));
    test_expect_null(test, "string field refused as number", json_get_number_int_1(root, "s"));

    bool bool_value = false;

    test_expect_true(test, "read_bool finds the true field", json_read_bool(root, "t", &bool_value));
    test_expect_true(test, "t reads true", bool_value);
    test_expect_true(test, "read_bool finds the false field", json_read_bool(root, "f", &bool_value));
    test_expect_false(test, "f reads false", bool_value);

    test_expect_null(test, "null-typed field refused as string", json_get_string_1(root, "nul"));

    test_expect_null(test, "a missing key is nullptr on the array accessor", json_get_array_1(root, "zzz"));
    test_expect_null(test, "a missing key is nullptr on the object accessor", json_get_object_1(root, "zzz"));
    test_expect_null(test, "a missing key is nullptr on the string accessor", json_get_string_1(root, "zzz"));
    test_expect_null(test, "a missing key is nullptr on the int accessor", json_get_number_int_1(root, "zzz"));
    test_expect_false(test, "read_bool on a missing key returns false", json_read_bool(root, "zzz", &bool_value));

    json_delete(&root);

    test_case_end(test);
}

static void _test_type_and_bool_and_any_getters(Test *const test) {
    test_case_begin(test, "item 7/29: json_get_type, json_is_bool, and the any-type getters json_get_1..4");

    Json *root = json_from_1("{\"a\":[1],\"o\":{\"x\":1},\"t\":true,\"f\":false,\"s\":\"hi\",\"n\":null,\"i\":5,\"r\":1.5}");

    test_expect_u(test, "json_get_type: array", (USize) JSON_TYPE_ARRAY, (USize) json_get_type(json_get_array_1(root, "a")));
    test_expect_u(test, "json_get_type: object", (USize) JSON_TYPE_OBJECT, (USize) json_get_type(json_get_object_1(root, "o")));
    test_expect_u(test, "json_get_type: true", (USize) JSON_TYPE_TRUE, (USize) json_get_type(json_get_1(root, "t")));
    test_expect_u(test, "json_get_type: false", (USize) JSON_TYPE_FALSE, (USize) json_get_type(json_get_1(root, "f")));
    test_expect_u(test, "json_get_type: string", (USize) JSON_TYPE_STRING, (USize) json_get_type(json_get_string_1(root, "s")));
    test_expect_u(test, "json_get_type: a genuine JSON null literal", (USize) JSON_TYPE_NULL, (USize) json_get_type(json_get_1(root, "n")));
    test_expect_u(test, "json_get_type: unsigned integer", (USize) JSON_TYPE_INTEGER_U, (USize) json_get_type(json_get_1(root, "i")));
    test_expect_u(test, "json_get_type: real", (USize) JSON_TYPE_REAL, (USize) json_get_type(json_get_1(root, "r")));

    test_expect_true(test, "json_is_bool is true for the true field", json_is_bool(json_get_1(root, "t")));
    test_expect_true(test, "json_is_bool is true for the false field", json_is_bool(json_get_1(root, "f")));
    test_expect_false(test, "json_is_bool is false for a non-boolean field", json_is_bool(json_get_1(root, "i")));

    // json_get_1..4 return a node of ANY type by name, unlike the typed json_get_object_*
    // family family - a string field, an array field, and a number field all succeed.
    test_expect_not_null(test, "json_get_1 finds a string field regardless of type", json_get_1(root, "s"));
    test_expect_not_null(test, "json_get_1 finds an array field regardless of type", json_get_1(root, "a"));

    Str const name_str = str_init_2((char*) "s");
    String name_string = string_init_1();
    string_add_last_1(&name_string, (char*) "s");

    test_expect_not_null(test, "json_get_2 (sized) finds the field", json_get_2(root, "s", 1));
    test_expect_not_null(test, "json_get_3 (Str) finds the field", json_get_3(root, &name_str));
    test_expect_not_null(test, "json_get_4 (String) finds the field", json_get_4(root, &name_string));

    test_expect_null(test, "json_get_1 on a missing key is nullptr", json_get_1(root, "zzz"));

    string_uninit(&name_string);
    json_delete(&root);

    test_case_end(test);
}

static void _test_array_query_and_iteration(Test *const test) {
    test_case_begin(test, "array query: size, indexed access via at-path and json_array_at, "
        "get_next (O(1) per step, item 4), remove, out-of-range is nullptr");

    Json *root = json_from_1("[10,20,30]");

    test_expect_u(test, "array has 3 elements", 3, json_array_get_size(root));

    Json *const first = json_at_1(root, "[0]");

    test_expect_not_null(test, "index 0 found via at-path", first);
    test_expect_u(test, "index 0 is 10", 10, json_get_value_number_uint(first));

    Json *const first_via_public = json_array_at(root, 0);

    test_expect_not_null(test, "json_array_at (item 28) finds the same element", first_via_public);
    test_expect_u(test, "json_array_at index 0 is 10", 10, json_get_value_number_uint(first_via_public));
    test_expect_null(test, "json_array_at out-of-range is nullptr", json_array_at(root, 99));

    Json *const second = json_array_get_next(first);

    test_expect_not_null(test, "get_next from index 0 reaches index 1", second);
    test_expect_u(test, "index 1 is 20", 20, json_get_value_number_uint(second));

    Json *const third = json_array_get_next(second);

    test_expect_not_null(test, "get_next from index 1 reaches index 2", third);
    test_expect_u(test, "index 2 is 30", 30, json_get_value_number_uint(third));

    Json *const past_the_end = json_array_get_next(third);

    test_expect_null(test, "get_next past the last element is nullptr (circular-list wraparound detected)", past_the_end);

    Json *const first_from_container = json_array_get_next(root);

    test_expect_not_null(test, "get_next called on the container itself returns the first child", first_from_container);
    test_expect_u(test, "the container's get_next is index 0", 10, json_get_value_number_uint(first_from_container));

    Json *const out_of_range = json_at_1(root, "[7]");

    test_expect_null(test, "an out-of-range index is nullptr", out_of_range);

    // item 26: json_array_get_size now reports SELF's size, never a parent's.
    test_expect_u(test, "json_array_get_size on a non-array element (self, not parent) is 0", 0, json_array_get_size(first));

    Json *empty_array = json_from_1("[]");

    test_expect_null(test, "get_next on an empty array container is nullptr", json_array_get_next(empty_array));
    json_delete(&empty_array);

    Json *mutable_root = json_from_1("[1,2,3]");

    test_expect_true(test, "json_array_remove (item 28) removes the middle element", json_array_remove(mutable_root, 1));
    test_expect_u(test, "the array now has 2 elements", 2, json_array_get_size(mutable_root));
    test_expect_false(test, "json_array_remove past the end is false", json_array_remove(mutable_root, 99));

    json_delete(&mutable_root);
    json_delete(&root);

    test_case_end(test);
}

static void _test_object_iteration_and_remove(Test *const test) {
    test_case_begin(test, "object iteration (O(1) per step, item 4): get_next order and labels, "
        "empty object is nullptr; json_object_remove_1/_2 (item 28)");

    Json *root = json_from_1("{\"a\":1,\"b\":2,\"c\":3}");

    Json *const first = json_object_get_next(root);

    test_expect_not_null(test, "get_next on the container returns the first key-value pair", first);

    Str const first_label = json_get_label(first);

    test_expect_string(test, "the first key is \"a\"", "a", str_get_data((Str*) &first_label));
    test_expect_u(test, "the first value is 1", 1, json_get_value_number_uint(first));

    Json *const second = json_object_get_next(first);

    test_expect_not_null(test, "get_next reaches the second pair", second);

    Str const second_label = json_get_label(second);

    test_expect_string(test, "the second key is \"b\"", "b", str_get_data((Str*) &second_label));
    test_expect_u(test, "the second value is 2", 2, json_get_value_number_uint(second));

    Json *const third = json_object_get_next(second);

    test_expect_not_null(test, "get_next reaches the third pair", third);

    Str const third_label = json_get_label(third);

    test_expect_string(test, "the third key is \"c\"", "c", str_get_data((Str*) &third_label));

    Json *const past_the_end = json_object_get_next(third);

    test_expect_null(test, "get_next past the last pair is nullptr", past_the_end);

    Json *empty_object = json_from_1("{}");

    test_expect_null(test, "get_next on an empty object container is nullptr", json_object_get_next(empty_object));
    json_delete(&empty_object);

    test_expect_true(test, "json_object_remove_1 removes an existing key", json_object_remove_1(root, "b"));
    test_expect_null(test, "the removed key is gone", json_get_number_uint_1(root, "b"));
    test_expect_false(test, "json_object_remove_1 on a missing key is false", json_object_remove_1(root, "zzz"));
    test_expect_true(test, "json_object_remove_2 (sized) removes another key", json_object_remove_2(root, "a", 1));

    json_delete(&root);

    test_case_end(test);
}

static void _test_nested_at_path(Test *const test) {
    test_case_begin(test, "json_at_1/json_at_2: nested ['key'][index] chains, missing/malformed segments are nullptr");

    Json *root = json_from_1("{\"items\":[{\"name\":\"a\"},{\"name\":\"b\"}]}");

    Json *const name_b = json_at_1(root, "['items'][1]['name']");

    test_expect_not_null(test, "a two-level nested path resolves", name_b);
    test_expect_string(test, "resolves to \"b\"", "b", json_get_value_string_1(name_b));

    Json *const missing_key = json_at_1(root, "['missing']");

    test_expect_null(test, "a missing top-level key in a path is nullptr", missing_key);

    Json *const missing_index = json_at_1(root, "['items'][9]");

    test_expect_null(test, "an out-of-range index in a path is nullptr", missing_index);

    Json *const sized = json_at_2(root, "['items'][0]['name']extra", 20);

    test_expect_not_null(test, "json_at_2 honours the explicit search size, ignoring trailing bytes", sized);
    test_expect_string(test, "sized-path resolves to \"a\"", "a", json_get_value_string_1(sized));

    Json *const no_bracket = json_at_1(root, "no brackets here");

    test_expect_true(test, "item 24: a search with no bracket segment returns self", no_bracket == root);

    Json *const empty_search = json_at_2(root, "", 0);

    test_expect_null(test, "R2 Low 11: json_at_2 with search_size 0 refuses (nullptr), distinct from the no-bracket-segment case above which returns self", empty_search);

    json_delete(&root);

    test_case_end(test);
}

static void _test_at_path_malformed_refuses(Test *const test) {
    test_case_begin(test, "item 24: json_at refuses malformed paths instead of guessing");

    Json *root = json_from_1("[1,2,3]");

    test_expect_null(test, "[abc] (neither a digit nor a quoted key) is nullptr", json_at_1(root, "[abc]"));
    test_expect_null(test, "an unterminated ['x (no closing quote) is nullptr", json_at_1(root, "['x"));
    test_expect_null(test, "a digit index that overflows USize is nullptr", json_at_1(root, "[99999999999999999999999999]"));
    test_expect_null(test, "a digit segment missing its closing bracket is nullptr", json_at_1(root, "[0"));
    test_expect_not_null(test, "a well-formed digit path still resolves", json_at_1(root, "[0]"));

    json_delete(&root);

    test_case_end(test);
}

static void _test_empty_key_refuses(Test *const test) {
    test_case_begin(test, "item 11: typed getters refuse an empty key with nullptr, never an abort");

    Json *root = json_from_1("{\"a\":1}");

    test_expect_null(test, "json_get_string_2 with name_size 0 is nullptr", json_get_string_2(root, "a", 0));
    test_expect_null(test, "json_get_number_uint_2 with name_size 0 is nullptr", json_get_number_uint_2(root, "a", 0));
    test_expect_null(test, "json_get_array_2 with name_size 0 is nullptr", json_get_array_2(root, "a", 0));

    json_delete(&root);

    // R2 Low 10: the asymmetry is deliberate and documented, not a bug - the any-type
    // getters and json_at's ['' ] path segment look "" up like any other key.
    Json *key_variety_root = json_from_1("{\"\":1,\"x\":2}"); // not const: json_delete needs &key_variety_root

    test_expect_not_null(test, "the any-type getter (json_get_1) FINDS an empty-string key, unlike the typed getters above", json_get_1(key_variety_root, ""));
    test_expect_null(test, "the typed getter still refuses the same empty key", json_get_number_uint_1(key_variety_root, ""));

    json_delete(&key_variety_root);

    test_case_end(test);
}

static void _test_edit_add_and_set(Test *const test) {
    test_case_begin(test, "edit: json_object_add_1..4 + json_set_* mutate a value already in the tree");

    Json *root = json_from_1("{}");

    Json *const node = json_object_add_1(root, JSON_TYPE_STRING, "k");

    json_set_string_1(node, "first");

    test_expect_string(test, "set_string_1 wrote the initial value", "first", json_get_value_string_1(node));

    json_set_string_1(node, "second");

    test_expect_string(test, "a later set_string_1 overwrites the same node", "second", json_get_value_string_1(node));

    Json *const number_node = json_object_add_1(root, JSON_TYPE_INTEGER_S, "n");

    json_set_number_int(number_node, -7);

    test_expect_i(test, "set_number_int wrote -7", -7, json_get_value_number_int(number_node));

    json_set_number_uint(number_node, 7);

    test_expect_u(test, "set_number_uint re-typed the same node to unsigned 7", 7, json_get_value_number_uint(number_node));

    Json *const bool_node = json_object_add_1(root, JSON_TYPE_TRUE, "flag");

    json_set_bool(bool_node, false);

    test_expect_false(test, "set_bool flipped the value", json_get_value_bool(bool_node));

    Json *const array_node = json_object_add_1(root, JSON_TYPE_ARRAY, "arr");
    Json *const array_child = json_array_add(array_node, JSON_TYPE_INTEGER_U);

    json_set_number_uint(array_child, 99);

    test_expect_u(test, "array_add + set_number_uint on the child works", 1, json_array_get_size(array_node));
    test_expect_u(test, "the child value is 99", 99, json_get_value_number_uint(json_at_1(array_node, "[0]")));

    json_delete(&root);

    test_case_end(test);
}

static void _test_setters_return_bool(Test *const test) {
    test_case_begin(test, "item 25: json_set_string_* and json_set_label_* return bool");

    Json *root = json_from_1("{}");

    Json *const string_node = json_object_add_1(root, JSON_TYPE_STRING, "s");

    test_expect_true(test, "json_set_string_1 on a valid string node returns true", json_set_string_1(string_node, "value"));

    Json *const other_node = json_object_add_1(root, JSON_TYPE_STRING, "other");

    test_expect_true(test, "json_set_label_1 renaming an existing key returns true", json_set_label_1(other_node, "renamed"));

    Str const renamed_label = json_get_label(other_node);

    test_expect_string(test, "the rename actually took effect", "renamed", str_get_data((Str*) &renamed_label));
    test_expect_not_null(test, "the node is reachable under its new key", json_get_string_1(root, "renamed"));
    test_expect_null(test, "the node is no longer reachable under its old key", json_get_string_1(root, "other"));

    test_expect_false(test, "json_set_label_2 with an empty new key is false, no rename attempted", json_set_label_2(other_node, "", 0));

    // An ARRAY ELEMENT's parent is the array, not an object - set_label must refuse it.
    Json *const array_node = json_object_add_1(root, JSON_TYPE_ARRAY, "arr");
    Json *const array_child = json_array_add(array_node, JSON_TYPE_INTEGER_U);

    test_expect_false(test, "json_set_label_1 on an array element (non-object parent) is false", json_set_label_1(array_child, "x"));

    json_delete(&root);

    test_case_end(test);
}

static void _test_set_label_empty_key_no_desync(Test *const test) {
    test_case_begin(test, "item 35: json_set_label_2 on a child whose CURRENT key is empty "
        "refuses (label_ready false) and does NOT cache a label it never applied");

    Json *root = json_from_1("{\"\":1,\"after\":2}");

    Json *const empty_keyed = json_object_get_next(root);

    test_expect_not_null(test, "the empty-keyed entry is still reachable by iteration", empty_keyed);
    test_expect_u(test, "the empty-keyed entry's value round-trips", 1, json_get_value_number_uint(empty_keyed));

    test_expect_false(test, "set_label_2 on the empty-keyed child refuses", json_set_label_2(empty_keyed, "new", 3));

    Str const label_after = json_get_label(empty_keyed);

    test_expect_u(test, "the label was never cached as \"new\" - it stays empty, matching the real key", 0, str_get_size((Str*) &label_after));

    json_delete(&root);

    test_case_end(test);
}

static void _test_static_setter_copies(Test *const test) {
    test_case_begin(test, "item 3: json_set_string_static now COPIES (it used to borrow the "
        "unterminated caller buffer - the only setter that did); behaves like _2");

    Json *root = json_from_1("{}");

    char string_buffer[16] = DEFAULT_INITIALIZATION;

    char_copy_2(string_buffer, "original", 8);
    string_buffer[8] = '\0';

    Json *const string_node = json_object_add_1(root, JSON_TYPE_STRING, "s");

    test_expect_true(test, "set_string_static reports success", json_set_string_static(string_node, string_buffer, 8));
    test_expect_string(test, "set_string_static reads the caller's buffer at call time", "original", json_get_value_string_1(string_node));

    string_buffer[0] = 'X';

    test_expect_string(test, "set_string_static COPIED - a later mutation of the source buffer is NOT reflected", "original", json_get_value_string_1(string_node));

    json_delete(&root);

    test_case_end(test);
}

static void _test_serialize_indentation_and_roundtrip(Test *const test) {
    test_case_begin(test, "serialize with indentation 0 and n, round-trip equality, get_data_1/3/4 agree");

    Json *root = json_from_1("{\"a\":1,\"b\":[1,2]}");

    char *const compact = json_get_data_1(root, 0);

    test_expect_string(test, "indentation 0 is compact, no inserted whitespace", "{\"a\":1,\"b\":[1,2]}", compact);

    char *const pretty_1 = json_get_data_1(root, 1);
    char *const pretty_7 = json_get_data_1(root, 7);

    test_expect_string(test, "indentation is a bare on/off gate: 1 and 7 write BYTE-IDENTICAL pretty output (sharp edge e)", pretty_1, pretty_7);
    test_expect_string_contains(test, "pretty output actually inserted a newline", pretty_1, "\n");

    Str data_2 = json_get_data_3(root, 0);
    String data_3 = json_get_data_4(root, 0);

    test_expect_string(test, "get_data_3 (Str) agrees with get_data_1 (char*)", compact, str_get_data(&data_2));
    test_expect_string(test, "get_data_4 (String) agrees with get_data_1 (char*)", compact, string_get_data(&data_3));

    Json *reparsed = json_from_1(compact);

    test_expect_not_null(test, "the compact serialization reparses", reparsed);
    test_expect_u(test, "round-trip a survives", 1, json_get_value_number_uint(json_get_number_uint_1(reparsed, "a")));
    test_expect_u(test, "round-trip b[1] survives", 2, json_get_value_number_uint(json_at_1(reparsed, "['b'][1]")));

    // item 8: json_get_data_1's buffer is CFW heap memory now - release with memory_free.
    memory_free((void*) compact);
    memory_free((void*) pretty_1);
    memory_free((void*) pretty_7);
    str_uninit(&data_2);
    string_uninit(&data_3);
    json_delete(&reparsed);
    json_delete(&root);

    test_case_end(test);
}

static void _test_save_1(Test *const test) {
    test_case_begin(test, "item 28: json_save_1 serializes to a file path");

    Json *root = json_from_1("{\"saved\":true,\"n\":7}");

    test_expect_true(test, "json_save_1 reports success", json_save_1(root, _TEST_JSON_TEMP_FILE, false));

    Json *reloaded = json_load_1(_TEST_JSON_TEMP_FILE);

    test_expect_not_null(test, "the saved file reloads", reloaded);

    bool saved_flag = false;

    test_expect_true(test, "the saved field round-trips", json_read_bool(reloaded, "saved", &saved_flag));
    test_expect_true(test, "saved is true", saved_flag);
    test_expect_u(test, "n round-trips", 7, json_get_value_number_uint(json_get_number_uint_1(reloaded, "n")));

    json_delete(&reloaded);
    file_remove_1(_TEST_JSON_TEMP_FILE);
    json_delete(&root);

    test_case_end(test);
}

static void _test_unicode_and_escapes(Test *const test) {
    test_case_begin(test, "unicode escapes and backslash/quote/newline escapes decode correctly");

    Json *root = json_from_1("{\"s\":\"caf\\u00e9\"}");

    test_expect_not_null(test, "unicode-escaped text parses", root);
    test_expect_string(test, "\\u00e9 decodes to the UTF-8 e-acute bytes", "caf\xc3\xa9", json_get_value_string_1(json_get_string_1(root, "s")));

    json_delete(&root);

    Json *escapes = json_from_1("{\"s\":\"a\\\"b\\\\c\\nd\"}");

    test_expect_not_null(test, "backslash-escaped text parses", escapes);
    test_expect_string(test, "quote/backslash/newline escapes decode literally", "a\"b\\c\nd", json_get_value_string_1(json_get_string_1(escapes, "s")));

    json_delete(&escapes);

    test_case_end(test);
}

static void _test_numbers(Test *const test) {
    test_case_begin(test, "numbers: signed, unsigned, real, ISize/USize boundaries, negative zero");

    Json *root = json_from_1("{\"min_signed\":-9223372036854775808,"
        "\"unsigned_only\":18446744073709551615,\"real\":-2.5,\"neg_zero\":-0.0}");

    // A negative literal is the only shape yyjson types as signed; a positive one (even
    // ISIZE_MAX itself) types unsigned - see the "si"/"ui" pair in the type-reporting case.
    Json *const min_signed = json_get_number_int_1(root, "min_signed");

    test_expect_not_null(test, "a negative literal reads as a signed node", min_signed);
    test_expect_i(test, "value equals ISIZE_MIN", ISIZE_MIN, json_get_value_number_int(min_signed));
    test_expect_null(test, "the same negative field is refused by the unsigned accessor", json_get_number_uint_1(root, "min_signed"));

    Json *const unsigned_only = json_get_number_uint_1(root, "unsigned_only");

    test_expect_not_null(test, "a literal past ISIZE_MAX reads as an unsigned-only node", unsigned_only);
    test_expect_u(test, "value equals USIZE_MAX", USIZE_MAX, json_get_value_number_uint(unsigned_only));
    test_expect_null(test, "the same field is refused by the signed accessor", json_get_number_int_1(root, "unsigned_only"));

    ISize read_int_value = 0;

    test_expect_false(test, "json_read_int refuses a value beyond ISIZE_MAX rather than truncating", json_read_int(root, "unsigned_only", &read_int_value));

    Json *const real = json_get_number_float_1(root, "real");

    test_expect_not_null(test, "real field found", real);
    test_expect_f(test, "real value is -2.5", -2.5, json_get_value_number_float(real), 0.0001);

    Json *const neg_zero = json_get_number_float_1(root, "neg_zero");

    test_expect_not_null(test, "negative zero parses as a real node", neg_zero);
    test_expect_f(test, "negative zero compares equal to 0.0", 0.0, json_get_value_number_float(neg_zero), 0.0001);

    json_delete(&root);

    // R2 High/Mid 9: json_get_value_number_int reads ANY integer node (yyjson's own
    // get_sint covers both subtypes) - a small positive literal parses UNSIGNED and still
    // reads correctly through the signed getter, contrary to the old "0 when not signed"
    // header claim.
    Json *uint_small_root = json_from_1("{\"u\":5}");
    Json *const uint_small = json_get_number_uint_1(uint_small_root, "u");

    test_expect_not_null(test, "a small positive literal is typed unsigned", uint_small);
    test_expect_i(test, "get_value_number_int reads a UINT node correctly, not 0", 5, json_get_value_number_int(uint_small));

    json_delete(&uint_small_root);

    // R2 Mid 6: json_set_number_float (no precision suffix) now serializes at shortest
    // round-trip precision instead of always rounding to 4 decimal places.
    Json *tiny_root = json_from_1("[0]");
    Json *const tiny_node = json_array_at(tiny_root, 0);

    json_set_number_float(tiny_node, 0.00001);

    char *const tiny_serialized = json_get_data_1(tiny_root, 0);

    test_expect_string_contains(test, "json_set_number_float's default is shortest round-trip, not fixed to 4 decimals", tiny_serialized, "0.00001");

    memory_free(tiny_serialized);
    json_delete(&tiny_root);

    test_case_end(test);
}

static void _test_sharp_edges(Test *const test) {
    test_case_begin(test, "item 3, PROVING THE FIX: an empty JSON string VALUE now reads as \"\" "
        "(never nullptr, never a per-node cache), and json_get_value_string_1's pointer is "
        "STABLE across two calls on the same node - the old cache freed and replaced it each "
        "call, a documented dangling-pointer trap. json_read_int on a boolean-typed field "
        "still reads as ABSENT.");

    Json *root = json_from_1("{\"empty\":\"\",\"flag\":true}");

    Json *const empty_node = json_get_string_1(root, "empty");

    test_expect_not_null(test, "the empty-string field is still typed STRING and found", empty_node);

    char *const value_first_call = json_get_value_string_1(empty_node);

    test_expect_string(test, "an empty string VALUE now reads as \"\", not nullptr", "", value_first_call);

    char *const value_second_call = json_get_value_string_1(empty_node);

    test_expect_true(test, "the pointer is stable across two calls on the same node", value_first_call == value_second_call);

    Str const empty_str = json_get_value_string_3(empty_node);
    String const empty_string = json_get_value_string_4(empty_node);

    test_expect_u(test, "get_value_string_3 (Str) answers a safe empty size 0", 0, str_get_size((Str*) &empty_str));
    test_expect_u(test, "get_value_string_4 (String) likewise answers a safe empty size 0", 0, string_get_size((String*) &empty_string));

    ISize out_value = 0;

    test_expect_false(test, "json_read_int on a boolean-typed field reads as ABSENT, current behavior", json_read_int(root, "flag", &out_value));

    // R2 suite gap: json_read_string on a present, empty string value reads "", not
    // nullptr (docs page's example claims this).
    char *const read_empty = json_read_string(root, "empty");

    test_expect_string(test, "json_read_string on an empty string value reads \"\", not nullptr", "", read_empty);

    // R2 suite gap: get_value_string_3/_4 on a NON-STRING node also answer EMPTY (not just
    // on an empty string value, covered above).
    Json *const bool_node = json_get_1(root, "flag");
    Str const non_string_str = json_get_value_string_3(bool_node);
    String const non_string_string = json_get_value_string_4(bool_node);

    test_expect_u(test, "get_value_string_3 on a non-string node answers EMPTY (size 0)", 0, str_get_size((Str*) &non_string_str));
    test_expect_u(test, "get_value_string_4 on a non-string node answers EMPTY (size 0)", 0, string_get_size((String*) &non_string_string));

    json_delete(&root);

    test_case_end(test);
}

static void _test_from_try_2(Test *const test) {
    test_case_begin(test, "item 16: json_from_try_2 reports the parse failure via JsonError");

    JsonError ok_error = DEFAULT_INITIALIZATION;
    Json *ok = json_from_try_2("{\"a\":1}", 7, &ok_error);

    test_expect_not_null(test, "a well-formed document still parses", ok);
    json_delete(&ok);

    JsonError malformed_error = DEFAULT_INITIALIZATION;
    Json *const malformed = json_from_try_2("{not valid json", 15, &malformed_error);

    test_expect_null(test, "malformed text is still nullptr", malformed);
    test_expect_not_null(test, "a message is set for the malformed document", (void*) malformed_error.message);
    test_expect_true(test, "R2 suite gap: the reported position is within the 15-byte input, not left untouched/garbage", malformed_error.position <= 15);

    JsonError empty_error = DEFAULT_INITIALIZATION;
    Json *const empty = json_from_try_2("", 0, &empty_error);

    test_expect_null(test, "an empty body is still nullptr", empty);
    test_expect_not_null(test, "a message is set for the empty body too", (void*) empty_error.message);
    test_expect_u(test, "the empty-body position is 0", 0, empty_error.position);

    test_case_end(test);
}

static void _test_r3_label_cache_self_alias(Test *const test) {
    test_case_begin(test, "R3 High 2 / memsec HIGH: json_set_label_3(node, &json_get_label(node)) "
        "- renaming a node's label from a VIEW over its OWN current label - must not read "
        "freed memory. _json_label_cache used to str_uninit the old label THEN copy from "
        "`data`; when `data` aliases the just-freed buffer (this exact shape, since "
        "json_get_label always hands back a view), the copy read already-freed memory on a "
        "HEAP node (arena mode never frees the old label, so the bug is heap-only). The fix "
        "builds the replacement Str first, then releases the old one.");

    Json *root = json_from_1("{\"original_key\":1}");
    Json *const node = json_get_number_uint_1(root, "original_key");

    Str const label = json_get_label(node);

    test_expect_true(test, "the self-aliased rename (same data, same size) succeeds", json_set_label_3(node, &label));

    Str const label_after = json_get_label(node);

    test_expect_string(test, "the label still reads correctly (not freed memory) after the self-aliased rename", "original_key", str_get_data((Str*) &label_after));
    test_expect_not_null(test, "the key is still findable in the object under the same name", json_get_number_uint_1(root, "original_key"));

    json_delete(&root);

    test_case_end(test);
}

static void _test_r3_empty_str_string_entry_points(Test *const test) {
    test_case_begin(test, "R3 High 3: an EMPTY Str/String (null data pointer, size 0) no "
        "longer aborts inside any _2/_3/_4 entry point - lookups/removal refuse (nullptr/"
        "false), and the key/value SETTERS (add, set_string, object_add_string) treat it "
        "as a legal \"\" instead. Every case below used to hit error_check_null(name/data/"
        "value) before the size test ran.");

    Str const empty = str_init_1(); // EMPTY: data == nullptr, size == 0
    String empty_string = string_init_1(); // EMPTY: data == nullptr, size == 0

    Json *root = json_from_1("{\"k\":\"v\"}");

    // Lookups: EMPTY refuses through every tier that forwards it, without aborting.
    test_expect_null(test, "json_get_3 (any-type) with an EMPTY Str key is nullptr, not an abort", json_get_3(root, &empty));
    test_expect_null(test, "json_get_4 (any-type) with an EMPTY String key is nullptr, not an abort", json_get_4(root, &empty_string));
    test_expect_null(test, "json_get_string_3 (typed) with an EMPTY Str key is nullptr, not an abort", json_get_string_3(root, &empty));
    test_expect_null(test, "json_get_2 with the raw (nullptr, 0) shape is nullptr, not an abort", json_get_2(root, nullptr, 0));
    test_expect_null(test, "json_get_array_2 with the raw (nullptr, 0) shape is nullptr, not an abort", json_get_array_2(root, nullptr, 0));

    // Removal: same refusal shape.
    test_expect_false(test, "json_object_remove_2 with the raw (nullptr, 0) shape is false, not an abort", json_object_remove_2(root, nullptr, 0));

    // Add: an EMPTY key is a legal "" key to ADD (R2 decision), not a lookup refusal.
    Json *const added_via_str = json_object_add_3(root, JSON_TYPE_INTEGER_U, &empty);

    test_expect_not_null(test, "json_object_add_3 with an EMPTY Str key adds an \"\" key, not an abort", added_via_str);
    json_set_number_uint(added_via_str, 1);
    test_expect_not_null(test, "the \"\" key added above is findable through the any-type getter", json_get_1(root, ""));
    test_expect_not_null(test, "json_get_2 with a REAL non-null \"\" pointer still finds it (unlike the EMPTY-view refusal above)", json_get_2(root, "", 0));

    // Add-string value: an EMPTY value is a legal "" string value.
    Json *const added_string = json_object_add_string_3(root, "empty_value", &empty);

    test_expect_not_null(test, "json_object_add_string_3 with an EMPTY Str VALUE adds the field, not an abort", added_string);
    test_expect_string(test, "the value reads as \"\", not garbage from a null-length memcpy", "", json_get_value_string_1(added_string));

    // Set string: an EMPTY value sets "".
    Json *const settable = json_object_add_1(root, JSON_TYPE_STRING, "settable");

    json_set_string_1(settable, "before");
    test_expect_true(test, "json_set_string_4 with an EMPTY String value succeeds, not an abort", json_set_string_4(settable, &empty_string));
    test_expect_string(test, "the value now reads as \"\"", "", json_get_value_string_1(settable));

    // Set label: renaming TO an EMPTY key refuses (false), matching a real "" (item 35/R2) -
    // this only moves WHERE that refusal happens, not what it decides.
    test_expect_false(test, "json_set_label_3 with an EMPTY Str refuses (false), not an abort", json_set_label_3(settable, &empty));

    json_delete(&root);
    string_uninit(&empty_string);

    test_case_end(test);
}

#ifdef ARENA_IMPLEMENTATION
static void _test_arena_variants(Test *const test) {
    test_case_begin(test, "arena (alloc_*) family: parse, load, add, serialize against a real Arena, reclaimed as one block");

    // Sized generously (R2 item 8 shrank json_arena_size's per-call figure - this case
    // now runs MORE small parses than a single json_arena_size(4096) hint anticipates,
    // since each call sizes only for its own tiny body, not for the whole case's total).
    Arena arena = arena_init_1(json_arena_size(65536), ARENA_TYPE_LINEAR);

    Json *root_1 = json_alloc_from_1("{\"a\":1}", &arena);

    test_expect_not_null(test, "alloc_from_1 parses against the arena", root_1);
    test_expect_u(test, "field a reads 1", 1, json_get_value_number_uint(json_get_number_uint_1(root_1, "a")));

    Json *root_2 = json_alloc_from_2("{\"a\":1}xxxxx", 7, &arena);

    test_expect_not_null(test, "alloc_from_2 honours the explicit size", root_2);

    Str const text_str = str_init_2((char*) "{\"b\":2}");
    Json *root_3 = json_alloc_from_3(&text_str, &arena);

    test_expect_not_null(test, "alloc_from_3 parses from Str", root_3);

    String text_string = string_init_1();

    string_add_last_1(&text_string, (char*) "{\"c\":3}");

    Json *root_4 = json_alloc_from_4(&text_string, &arena);

    test_expect_not_null(test, "alloc_from_4 parses from String", root_4);

    string_uninit(&text_string);

    // R2 High 2: the arena twin of json_from_try_2 - 107 of 123 in-tree parse call sites
    // are the arena form, so the error-reporting path needs to reach them too.
    JsonError alloc_ok_error = DEFAULT_INITIALIZATION;
    Json *root_try = json_alloc_from_try_2("{\"d\":4}", 7, &alloc_ok_error, &arena);

    test_expect_not_null(test, "alloc_from_try_2 parses against the arena", root_try);

    JsonError alloc_malformed_error = DEFAULT_INITIALIZATION;
    Json *const alloc_try_malformed = json_alloc_from_try_2("{not valid", 10, &alloc_malformed_error, &arena);

    test_expect_null(test, "alloc_from_try_2 on malformed text is nullptr", alloc_try_malformed);
    test_expect_not_null(test, "alloc_from_try_2 sets a message for malformed text", (void*) alloc_malformed_error.message);

    // item 1, PROVING THE FIX on the arena path too: an EMPTY Str has a null data
    // pointer - used to abort inside json_alloc_from_2, now answers nullptr.
    Str const empty_str = str_init_1();
    Json *const from_empty_str = json_alloc_from_3(&empty_str, &arena);

    test_expect_null(test, "alloc_from_3 on the EMPTY Str is nullptr, not an abort", from_empty_str);

    _test_write_temp_file("{\"loaded\":true}");

    Json *root_loaded = json_alloc_load_1(_TEST_JSON_TEMP_FILE, &arena);

    test_expect_not_null(test, "alloc_load_1 loads the temp file against the arena", root_loaded);

    file_remove_1(_TEST_JSON_TEMP_FILE);

    Json *edit_array_root = json_alloc_from_1("[]", &arena);
    Json *const array_child = json_array_add(edit_array_root, JSON_TYPE_INTEGER_U);

    json_set_number_uint(array_child, 5);

    test_expect_u(test, "array_add on an arena-owned tree produced a settable child", 5, json_get_value_number_uint(json_at_1(edit_array_root, "[0]")));

    Json *edit_root = json_alloc_from_1("{}", &arena);
    Json *const obj_1 = json_object_add_1(edit_root, JSON_TYPE_STRING, "k1");

    json_set_string_1(obj_1, "v1");

    Json *const obj_2 = json_object_add_2(edit_root, JSON_TYPE_STRING, "k2sized", 2);

    json_set_string_1(obj_2, "v2");

    Str const key_3 = str_init_2((char*) "k3");
    Json *const obj_3 = json_object_add_3(edit_root, JSON_TYPE_STRING, &key_3);

    json_set_string_1(obj_3, "v3");

    String key_4 = string_init_1();

    string_add_last_1(&key_4, (char*) "k4");

    Json *const obj_4 = json_object_add_4(edit_root, JSON_TYPE_STRING, &key_4);

    json_set_string_1(obj_4, "v4");
    string_uninit(&key_4);

    char *const serialized = json_alloc_get_data_1(edit_root, 0, &arena);

    test_expect_string_contains(test, "object_add_1 key/value present on an arena-owned tree", serialized, "\"k1\":\"v1\"");
    test_expect_string_contains(test, "object_add_2 sized key truncated to \"k2\"", serialized, "\"k2\":\"v2\"");
    test_expect_string_contains(test, "object_add_3 (Str) key present", serialized, "\"k3\":\"v3\"");
    test_expect_string_contains(test, "object_add_4 (String) key present", serialized, "\"k4\":\"v4\"");

    Str const serialized_str = json_alloc_get_data_3(edit_root, 0, &arena);
    String const serialized_string = json_alloc_get_data_4(edit_root, 0, &arena);

    test_expect_string(test, "alloc_get_data_3 (Str) agrees with alloc_get_data_1", serialized, str_get_data((Str*) &serialized_str));
    test_expect_string(test, "alloc_get_data_4 (String) agrees with alloc_get_data_1", serialized, string_get_data((String*) &serialized_string));

    json_delete(&root_1);
    json_delete(&root_2);
    json_delete(&root_3);
    json_delete(&root_4);
    json_delete(&root_try);
    json_delete(&root_loaded);
    json_delete(&edit_array_root);
    json_delete(&edit_root);

    arena_uninit(&arena, ARENA_TYPE_LINEAR); // reclaims every arena-owned allocation above at once

    test_case_end(test);
}

static void _test_arena_size(Test *const test) {
    test_case_begin(test, "item 19/40: json_arena_size sizes an arena large enough to parse a "
        "real body correctly (not just a non-null root - the tree's own element count is "
        "checked). The complementary half-size-fails-without-abort pin lives in "
        "test_unchecked.c: under ERROR_CHECK_ENABLED (this build) arena exhaustion aborts "
        "inside the arena allocator itself, same as every other arena consumer - not a "
        "json-specific contract, so it cannot be asserted as a graceful nullptr here.");

    String body = _test_build_large_array_body(4000); // a realistically large document

    USize const body_size = string_get_size(&body);
    USize const good_size = json_arena_size(body_size);

    Arena good_arena = arena_init_1(good_size, ARENA_TYPE_LINEAR);
    Json *good_root = json_alloc_from_2(string_get_data(&body), body_size, &good_arena);

    test_expect_not_null(test, "json_arena_size(N) is large enough to parse an N-byte body", good_root);
    test_expect_u(test, "the parsed tree actually holds all 4000 elements (not a truncated/failed copy)", 4000, json_array_get_size(good_root));

    json_delete(&good_root);
    arena_uninit(&good_arena, ARENA_TYPE_LINEAR);
    string_uninit(&body);

    test_case_end(test);
}

static void _test_r3_alloc_get_data_arena_parameter(Test *const test) {
    test_case_begin(test, "R3 High 1: json_alloc_get_data_1/_3/_4 write into the PARAMETER "
        "allocator, never self->allocator - a HEAP tree serializes into an arena, and a "
        "tree in one arena serializes into a DIFFERENT arena. The old _json_arena_write "
        "built its yyjson_alc from self->allocator (via _json_arena_alc), so a heap tree "
        "wrote through yyjson's default libc malloc instead (a guaranteed leak, since the "
        "header forbids ever calling memory_free on the result) and a cross-arena request "
        "silently landed in the WRONG arena.");

    // Heap tree -> arena buffer: the old code would have written this through libc malloc.
    Json *heap_root = json_from_1("{\"a\":1,\"b\":[1,2,3]}");
    Arena target_arena = arena_init_1(json_arena_size(64), ARENA_TYPE_LINEAR);

    char *const heap_into_arena = json_alloc_get_data_1(heap_root, 0, &target_arena);

    test_expect_not_null(test, "a HEAP tree serializes through the arena parameter, not libc default", heap_into_arena);
    test_expect_string_contains(test, "the arena-written buffer holds the heap tree's content", heap_into_arena, "\"a\":1");

    json_delete(&heap_root);
    arena_uninit(&target_arena, ARENA_TYPE_LINEAR);

    // Tree in arena A -> serialize into arena B, where A is sized ONLY for its own parse
    // (json_arena_size's per-handle headroom is its only spare margin) - if the fix
    // regressed to self->allocator, this multi-KB write would exhaust arena A and ABORT
    // (allocator_borrow's own contract) instead of succeeding through arena B's capacity.
    String body = _test_build_large_array_body(2000);
    USize const body_size = string_get_size(&body);

    Arena arena_a = arena_init_1(json_arena_size(body_size), ARENA_TYPE_LINEAR);
    Arena arena_b = arena_init_1(json_arena_size(body_size), ARENA_TYPE_LINEAR);

    Json *root_a = json_alloc_from_2(string_get_data(&body), body_size, &arena_a);

    test_expect_not_null(test, "the cross-arena source tree parses into arena A", root_a);

    char *const cross_arena_data = json_alloc_get_data_1(root_a, 0, &arena_b);

    test_expect_not_null(test, "serializing a tree from arena A into arena B succeeds", cross_arena_data);
    test_expect_string_contains(test, "the cross-arena buffer holds the tree's content", cross_arena_data, "0,1,2");

    Str cross_arena_str = json_alloc_get_data_3(root_a, 0, &arena_b);
    String cross_arena_string = json_alloc_get_data_4(root_a, 0, &arena_b);

    test_expect_string(test, "alloc_get_data_3 (view, item 4/Mid 4) agrees with alloc_get_data_1", cross_arena_data, str_get_data(&cross_arena_str));
    test_expect_string(test, "alloc_get_data_4 (view, item 4/Mid 4) agrees with alloc_get_data_1", cross_arena_data, string_get_data(&cross_arena_string));
    test_expect_false(test, "alloc_get_data_3's result is a VIEW, not owned (item 4/Mid 4)", cross_arena_str.owned);
    test_expect_false(test, "alloc_get_data_4's result is a VIEW, not owned (item 4/Mid 4)", cross_arena_string.owned);

    // memsec LOW (R4 Misc 3(e)): the view now records the PARAMETER allocator (arena_b),
    // not self->allocator (arena_a) - str_alloc_init_3/string_alloc_init_4, not the plain
    // str_init_3/string_init_4 that used to leave `allocator` unset for this arena path.
    test_expect_true(test, "alloc_get_data_3's Str carries arena_b, not arena_a", cross_arena_str.allocator == &arena_b);
    test_expect_true(test, "alloc_get_data_4's String carries arena_b, not arena_a", cross_arena_string.allocator == &arena_b);

    // str_uninit/string_uninit are documented no-ops on a view (owned == false); calling
    // them and then re-reading proves nothing was actually released.
    str_uninit(&cross_arena_str);
    string_uninit(&cross_arena_string);

    test_expect_string_contains(test, "the view is still readable after str_uninit/string_uninit (no-op on a view)", cross_arena_data, "0,1,2");

    json_delete(&root_a);
    arena_uninit(&arena_a, ARENA_TYPE_LINEAR);
    arena_uninit(&arena_b, ARENA_TYPE_LINEAR);
    string_uninit(&body);

    test_case_end(test);
}
#endif // ARENA_IMPLEMENTATION

/*==============================================================================
 * MARK: - Entry Point
 *============================================================================*/

int main(void) {
    log_init((LogConfig){ .level = LOG_LEVEL_ERROR, .stream = stdout, .timestamp_enabled = true, .autoflush = true });

    Test test = test_init("tests/json/test_all.c");

    test_suite_begin(&test, "json");
    _test_object_add(&test);
    _test_object_one_shots_on_array_root_refuse(&test);
    _test_delete_edge_cases(&test);
    _test_parse_from_char(&test);
    _test_parse_from_str_and_string(&test);
    _test_empty_str_string_parse_refuses(&test);
    _test_parse_from_file(&test);
    _test_type_reporting_missing_and_wrong_type(&test);
    _test_type_and_bool_and_any_getters(&test);
    _test_array_query_and_iteration(&test);
    _test_object_iteration_and_remove(&test);
    _test_nested_at_path(&test);
    _test_at_path_malformed_refuses(&test);
    _test_empty_key_refuses(&test);
    _test_edit_add_and_set(&test);
    _test_setters_return_bool(&test);
    _test_set_label_empty_key_no_desync(&test);
    _test_static_setter_copies(&test);
    _test_serialize_indentation_and_roundtrip(&test);
    _test_save_1(&test);
    _test_unicode_and_escapes(&test);
    _test_numbers(&test);
    _test_sharp_edges(&test);
    _test_from_try_2(&test);
    _test_r3_label_cache_self_alias(&test);
    _test_r3_empty_str_string_entry_points(&test);
#ifdef ARENA_IMPLEMENTATION
    _test_arena_variants(&test);
    _test_arena_size(&test);
    _test_r3_alloc_get_data_arena_parameter(&test);
#endif // ARENA_IMPLEMENTATION
    test_suite_end(&test);

    return test_uninit(&test);
}