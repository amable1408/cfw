#include <container/str/str.h>
#include <container/string/string.h>
#include <file/file.h>
#include <test/test.h>

/* Coverage for the Block F handle-based fixes: the five writers now RETURN the
 * count actually written instead of discarding it, file_flush makes buffered
 * bytes visible to a second open before the writer closes, file_get_size
 * answers by handle without moving the stream position (the fstat/GetFileSizeEx
 * rewrite - it used to fseek/rewind on Linux, silently resetting whatever
 * position file_at was about to seek from), and file_read_2/file_read_4 clamp
 * an oversized data_count to the destination's own space instead of writing
 * past it.
 *
 * Neither test_file_meta.c (by-path metadata only) nor test_file_read.c
 * (by-path whole-file readers only) opens a handle for write, flush, seek, or
 * a bounded partial read, so this is a distinct API surface and earns its own
 * suite rather than being wedged into either. */

/*==============================================================================
 * MARK: - Static/Internal Functions
 *============================================================================*/
static bool _write_fixture(char const *const path, char const *const content) {
    FILE *const handle = fopen(path, "wb");

    if (memory_empty((void*) handle)) {
        return false;
    }

    if (content[0] != '\0') {
        fputs(content, handle);
    }

    fclose(handle);

    return true;
}

/*==============================================================================
 * MARK: - Test Cases
 *============================================================================*/
static void _test_write_returns_count(Test *const test) {
    test_case_begin(test, "file: file_write_1..5 return the count actually written");

    char const *const path = "file_io_write.txt";
    char const *const content = "hello world";

    File *file = file_open_try_1(path, "wb");

    test_expect_true(test, "the fixture opens for write", !memory_empty((void*) file));

    test_expect_u(test, "file_write_1 returns 11 for an 11-byte write", 11, file_write_1(file, content, sizeof(char), char_length(content)));

    file_close(&file);
    file = file_open_try_1(path, "wb");

    Str const str_content = str_init_static(content, char_length(content));

    test_expect_u(test, "file_write_2 returns the Str's whole size", char_length(content), file_write_2(file, &str_content));

    file_close(&file);
    file = file_open_try_1(path, "wb");

    test_expect_u(test, "file_write_3 returns the explicit count (5 of 11)", 5, file_write_3(file, &str_content, 5));

    file_close(&file);
    file = file_open_try_1(path, "wb");

    String const string_content = string_init_static(content, char_length(content));

    test_expect_u(test, "file_write_4 returns the String's whole size", char_length(content), file_write_4(file, &string_content));

    file_close(&file);
    file = file_open_try_1(path, "wb");

    test_expect_u(test, "file_write_5 returns the explicit count (5 of 11)", 5, file_write_5(file, &string_content, 5));

    file_close(&file);

    str_uninit((Str*) &str_content);
    string_uninit((String*) &string_content);
    remove(path);

    test_case_end(test);
}

static void _test_write_zero_count_is_legal_noop(Test *const test) {
    test_case_begin(test, "file: file_write_1/_3/_5 treat a zero count as a legal no-op returning 0 (fresh fix - it used to abort on data_count == 0)");

    char const *const path = "file_io_write_zero.txt";
    char const *const content = "nonempty";

    File *file = file_open_try_1(path, "wb");

    test_expect_true(test, "the fixture opens for write", !memory_empty((void*) file));

    // A valid, non-null buffer with an explicit zero count: the count check
    // itself, isolated from any container's own emptiness.
    test_expect_u(test, "file_write_1 with data_count 0 returns 0 without aborting", 0, file_write_1(file, content, sizeof(char), 0));

    Str str_data = str_init_static(content, char_length(content));

    test_expect_u(test, "file_write_3 with an explicit data_count of 0 returns 0", 0, file_write_3(file, &str_data, 0));

    // The container forwards its OWN zero size - str_set_size neither frees nor
    // nulls the buffer, so this is the literal "an empty Str/String forwards a
    // zero count" case the fix's own comment names, not just a zero argument.
    str_set_size(&str_data, 0);

    test_expect_u(test, "file_write_2 on a Str whose reported size is 0 returns 0", 0, file_write_2(file, &str_data));

    str_uninit(&str_data);

    String string_data = string_init_static(content, char_length(content));

    test_expect_u(test, "file_write_5 with an explicit data_count of 0 returns 0", 0, file_write_5(file, &string_data, 0));

    string_set_size(&string_data, 0);

    test_expect_u(test, "file_write_4 on a String whose reported size is 0 returns 0", 0, file_write_4(file, &string_data));

    string_uninit(&string_data);

    // "Empty container" is TWO states, not one: allocated-then-zeroed (above,
    // data != nullptr) and NEVER allocated (data == nullptr). The zero-count
    // fix's own comment names the second explicitly, and it is the one the
    // memsec lane found still aborting - the null check ran before the
    // zero-count return, so the canonical empty Str/String hit it first. The
    // fix now returns 0 before ever looking at the buffer pointer.
    Str const never_allocated_str = str_init_1();

    test_expect_u(test, "file_write_2 on the CANONICAL empty Str (str_init_1, data == nullptr) returns 0, not an abort", 0, file_write_2(file, &never_allocated_str));

    String const never_allocated_string = string_init_1();

    test_expect_u(test, "file_write_4 on the CANONICAL empty String (string_init_1, data == nullptr) returns 0, not an abort", 0, file_write_4(file, &never_allocated_string));

    // NOT tested here (would abort by design): a null buffer with a NON-zero
    // count is still caller error, and error_check_null below the zero-count
    // return still guards that real write.

    str_uninit((Str*) &never_allocated_str);
    string_uninit((String*) &never_allocated_string);

    file_close(&file);
    remove(path);

    test_case_end(test);
}

static void _test_read_zero_bound_zeroes_stale_size(Test *const test) {
    test_case_begin(test, "file: file_read_2/_4 zero the size on a zero-bounded read rather than leaving the PRIOR read's count standing (fresh fix)");

    char const *const path = "file_io_read_zero.txt";
    char const *const content = "0123456789ABCDEFGHIJ";

    test_expect_true(test, "the fixture is written", _write_fixture(path, content));

    Str str_data = str_init_static("aaaa", 4);

    File *file = file_open_try_1(path, "rb");

    test_expect_true(test, "the fixture opens", !memory_empty((void*) file));

    file_read_2(file, &str_data, 4);

    test_expect_u(test, "the first read leaves a real, non-zero size (the 'prior read' this pin is about)", 4, str_get_size(&str_data));

    file_read_2(file, &str_data, 0);

    test_expect_u(test, "a zero-count follow-up read ZEROES the size rather than leaving 4 standing", 0, str_get_size(&str_data));

    str_uninit(&str_data);
    file_close(&file);

    String string_data = string_init_2(8);

    file = file_open_try_1(path, "rb");

    test_expect_true(test, "the fixture opens again", !memory_empty((void*) file));

    file_read_4(file, &string_data, 7);

    test_expect_u(test, "the first read leaves a real, non-zero size", 7, string_get_size(&string_data));

    file_read_4(file, &string_data, 0);

    test_expect_u(test, "a zero-count follow-up read ZEROES the size rather than leaving 7 standing", 0, string_get_size(&string_data));

    string_uninit(&string_data);
    file_close(&file);
    remove(path);

    test_case_end(test);
}

static void _test_flush_makes_bytes_visible(Test *const test) {
    test_case_begin(test, "file: file_flush makes buffered writes visible to a second open before the writer closes");

    char const *const path = "file_io_flush.txt";
    char const *const content = "flushed-before-close";
    USize const content_size = char_length(content);

    File *writer = file_open_try_1(path, "wb");

    test_expect_true(test, "the writer opens", !memory_empty((void*) writer));
    test_expect_u(test, "the write reports the whole content written", content_size, file_write_1(writer, content, sizeof(char), content_size));
    test_expect_true(test, "the flush reports success", file_flush(writer));

    // The writer is deliberately still open here: the point of file_flush is
    // that the bytes reach the OS before fclose, which reports nothing at all.
    File *reader = file_open_try_1(path, "rb");

    test_expect_true(test, "a second handle opens the same path while the writer is still open", !memory_empty((void*) reader));
    test_expect_u(test, "file_get_size on the reader already sees the flushed length", content_size, file_get_size(reader));

    char buffer[64] = DEFAULT_INITIALIZATION;
    USize const read_count = file_read_1(reader, buffer, sizeof(char), content_size);

    test_expect_u(test, "the reader reads back the whole flushed content", content_size, read_count);
    test_expect_true(test, "and the bytes match what was written", char_equal_2(buffer, read_count, content, content_size));

    file_close(&reader);
    file_close(&writer);
    remove(path);

    test_case_end(test);
}

static void _test_get_size_preserves_position(Test *const test) {
    test_case_begin(test, "file: file_get_size never moves the stream position (the fstat/GetFileSizeEx rewrite)");

    char const *const path = "file_io_position.txt";
    char const *const content = "0123456789ABCDEFGHIJ";
    USize const content_size = char_length(content);

    test_expect_true(test, "the fixture is written", _write_fixture(path, content));
    test_expect_u(test, "the fixture really is 20 bytes", 20, content_size);

    File *file = file_open_try_1(path, "rb");

    test_expect_true(test, "the fixture opens", !memory_empty((void*) file));

    // A fresh handle is at position 0. Querying the size must leave it there.
    test_expect_u(test, "file_get_size answers 20 without moving the stream", content_size, file_get_size(file));

    char whole[32] = DEFAULT_INITIALIZATION;
    USize const whole_read = file_read_1(file, whole, sizeof(char), content_size);

    test_expect_u(test, "a read right after still starts at byte 0", content_size, whole_read);
    test_expect_true(test, "and returns the whole content", char_equal_2(whole, whole_read, content, content_size));

    file_close(&file);

    // Now the regression case: seek to an interior offset, ask for the size,
    // and confirm the read that follows CONTINUES from that offset rather than
    // from 0 - which is exactly what the old Linux fseek/ftell/rewind body
    // would have broken (rewind is unconditional in its name).
    file = file_open_try_1(path, "rb");

    file_at(file, 5, FILE_POSITION_BEGIN);

    test_expect_u(test, "file_get_size still answers 20 from an interior position", content_size, file_get_size(file));

    char tail[32] = DEFAULT_INITIALIZATION;
    USize const tail_read = file_read_1(file, tail, sizeof(char), 5);

    test_expect_u(test, "the read after the size query still starts at byte 5", 5, tail_read);
    test_expect_true(test, "and the bytes are the ones at offset 5, not the start of the file", char_equal_2(tail, tail_read, content + 5, 5));

    file_close(&file);
    remove(path);

    test_case_end(test);
}

static void _test_file_at_seek_contract(Test *const test) {
    test_case_begin(test, "file: file_at's documented seek contract - forward from CURRENT/END, absolute BEGIN for backward");

    char const *const path = "file_io_seek.txt";
    char const *const content = "0123456789ABCDEFGHIJ";

    test_expect_true(test, "the fixture is written", _write_fixture(path, content));

    File *file = file_open_try_1(path, "rb");

    test_expect_true(test, "the fixture opens", !memory_empty((void*) file));

    // Forward from BEGIN.
    file_at(file, 10, FILE_POSITION_BEGIN);

    char at_ten[8] = DEFAULT_INITIALIZATION;

    test_expect_u(test, "a forward BEGIN seek to 10 reads what starts there", 4, file_read_1(file, at_ten, sizeof(char), 4));
    test_expect_true(test, "and it is the expected slice", char_equal_2(at_ten, 4, content + 10, 4));

    // Forward from CURRENT: the position is now 14 (10 + the 4 just read).
    file_at(file, 2, FILE_POSITION_CURRENT);

    char at_sixteen[4] = DEFAULT_INITIALIZATION;

    test_expect_u(test, "a forward CURRENT seek of +2 lands at 16", 2, file_read_1(file, at_sixteen, sizeof(char), 2));
    test_expect_true(test, "and reads the expected slice", char_equal_2(at_sixteen, 2, content + 16, 2));

    // index is UNSIGNED (file.h), so there is no negative offset to express -
    // a backward seek is only reachable through FILE_POSITION_BEGIN with an
    // absolute index. This is the documented contract, not a workaround: seek
    // back to byte 3 the only expressible way and confirm it actually lands
    // there rather than staying wherever CURRENT left it.
    file_at(file, 3, FILE_POSITION_BEGIN);

    char at_three[4] = DEFAULT_INITIALIZATION;

    test_expect_u(test, "BEGIN + absolute index is how a backward seek is expressed", 4, file_read_1(file, at_three, sizeof(char), 4));
    test_expect_true(test, "and it reads the slice at 3, proving the seek actually went backward", char_equal_2(at_three, 4, content + 3, 4));

    file_close(&file);
    remove(path);

    test_case_end(test);
}

static void _test_read_2_clamps_to_str_size(Test *const test) {
    test_case_begin(test, "file: file_read_2 clamps an oversized data_count to the Str's own size rather than writing past it");

    char const *const path = "file_io_clamp_str.txt";
    char const *const content = "0123456789ABCDEFGHIJ";

    test_expect_true(test, "the fixture is written", _write_fixture(path, content));

    // A real 4-byte owned buffer (str_init_static copies exactly data_size
    // bytes), so a clamp failure here is not "reads 4 anyway" by allocator
    // luck - the destination genuinely has no room past its own size.
    Str data = str_init_static("aaaa", 4);

    // A second, independent owned buffer allocated right after: if the clamp
    // were gone, file_read_2 would ask fread for 100 bytes into a 4-byte
    // target, and this is what a heap allocator placed nearby would show the
    // damage on. The size/content checks on `data` below are the authoritative
    // proof either way; this is a best-effort second witness.
    Str canary = str_init_static("CANARY-SHOULD-STAY-INTACT!!", 28);

    File *file = file_open_try_1(path, "rb");

    test_expect_true(test, "the fixture opens", !memory_empty((void*) file));

    file_read_2(file, &data, 100);

    test_expect_u(test, "the read is clamped to the Str's original 4 bytes, not 100", 4, str_get_size(&data));
    test_expect_true(test, "and the 4 bytes read are the file's first 4", char_equal_2(str_get_data(&data), str_get_size(&data), content, 4));
    test_expect_u(test, "the canary's size is untouched", 28, str_get_size(&canary));
    test_expect_true(test, "and the canary's content is untouched", char_equal_1(str_get_data(&canary), "CANARY-SHOULD-STAY-INTACT!!"));

    file_close(&file);
    str_uninit(&data);
    str_uninit(&canary);
    remove(path);

    test_case_end(test);
}

static void _test_read_4_clamps_to_capacity_and_stays_a_c_string(Test *const test) {
    test_case_begin(test, "file: file_read_4 clamps to capacity - 1 and the result is still a valid C string (strlen == size)");

    char const *const path = "file_io_clamp_string.txt";
    char const *const content = "0123456789ABCDEFGHIJ";

    test_expect_true(test, "the fixture is written", _write_fixture(path, content));

    // capacity 8: at most 7 bytes may land, the 8th slot is the reserved
    // terminator this clamp exists to protect.
    String data = string_init_2(8);

    // A second, independently allocated buffer to witness an out-of-bounds
    // write the same way the Str case does above.
    String canary = string_init_2(8);

    memory_set(string_get_data(&canary), 8, (Byte) 0xAA);

    File *file = file_open_try_1(path, "rb");

    test_expect_true(test, "the fixture opens", !memory_empty((void*) file));

    file_read_4(file, &data, 100);

    test_expect_u(test, "the read is clamped to capacity - 1 (7), not 100", 7, string_get_size(&data));
    test_expect_true(test, "and the 7 bytes read are the file's first 7", char_equal_2(string_get_data(&data), string_get_size(&data), content, 7));
    test_expect_u(test, "string_get_data is still a valid C string: strlen equals size", string_get_size(&data), char_length(string_get_data(&data)));

    bool canary_intact = true;

    for (USize byte = 0; byte < 8; byte += 1) {
        if ((Byte) string_get_data(&canary)[byte] != (Byte) 0xAA) {
            canary_intact = false;
        }
    }

    test_expect_true(test, "the neighbouring allocation's content is untouched", canary_intact);

    file_close(&file);
    string_uninit(&data);
    string_uninit(&canary);
    remove(path);

    test_case_end(test);
}

static void _test_get_size_zero_byte_file(Test *const test) {
    test_case_begin(test, "file: file_get_size on a 0-byte file returns 0 and the by-handle readers stay empty and clean");

    char const *const path = "file_io_zero.txt";

    test_expect_true(test, "the empty fixture is written", _write_fixture(path, ""));

    File *file = file_open_try_1(path, "rb");

    test_expect_true(test, "the empty fixture opens", !memory_empty((void*) file));
    test_expect_u(test, "file_get_size reports 0", 0, file_get_size(file));

    String const document = file_read_all_3(file);

    test_expect_u(test, "and the by-handle String reader agrees", 0, string_get_size(&document));

    string_uninit((String*) &document);
    file_close(&file);
    remove(path);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Entry Point
 *============================================================================*/
int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = LOG_STREAM_STDOUT,
        .timestamp_enabled = false,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/file/test_file_io.c");

    test_suite_begin(&test, "file io");

    _test_write_returns_count(&test);
    _test_write_zero_count_is_legal_noop(&test);
    _test_read_zero_bound_zeroes_stale_size(&test);
    _test_flush_makes_bytes_visible(&test);
    _test_get_size_preserves_position(&test);
    _test_file_at_seek_contract(&test);
    _test_read_2_clamps_to_str_size(&test);
    _test_read_4_clamps_to_capacity_and_stays_a_c_string(&test);
    _test_get_size_zero_byte_file(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}