/*
 * test_unchecked.c - Behavioral tests for include/bits/bits.c built WITHOUT
 * ERROR_CHECK_ENABLED.
 *
 * Pins the inert-degradation half of the contract bits.h documents: every
 * input that aborts under the checked build (test_all.c) must instead leave
 * data unchanged, answer a safe sentinel value, or render nothing - never
 * read or write past a buffer. Built by the makefile's `unchecked` target
 * into a separate binary/object namespace (.bitsunchecked.o) so it never
 * shares objects with the checked build.
 */
#include <stdio.h>

#include <bits/bits.h>
#include <char/char.h>
#include <test/test.h>

#ifdef OS_WINDOWS
#include <io.h>
#endif // OS_WINDOWS

int main(void) {
    Test test = test_init("tests/bits/test_unchecked.c");

    test_suite_begin(&test, "bits (unchecked)");
    test_case_begin(&test, "single-word functions: inert past index 63");

    /* A zero word cannot catch a lost "index <= 63" guard: x86 shifts by a count
     * masked to 6 bits, so index 64 shifts by 0 and reads bit 0 of the word - which
     * is 0 either way for a zero word, so the pin would pass whether or not the
     * guard survives. ~0ULL has bit 0 set, so a lost guard reads bit 0 as 1 and this
     * pin catches it; the correct unchecked contract stays false for any index > 63. */
    test_expect_false(&test, "bits_at(~0ULL, 64) is false", bits_at((USize) ~0ULL, (U8) 64));

    USize value = (USize) 0x1234;
    USize const before_flip = value;

    bits_flip(&value, (U8) 64);
    test_expect_u(&test, "bits_flip(&v, 64) leaves the value unchanged", before_flip, value);

    bits_write(&value, (U8) 64, true);
    test_expect_u(&test, "bits_write(&v, 64, true) leaves the value unchanged", before_flip, value);

    test_case_end(&test);

    test_case_begin(&test, "bits_print_2 past 64 bits prints nothing");

    char const *const path = "fixture_bits_unchecked_print.txt";

    /* Opened before stdout is touched, so a failure leaves nothing to restore;
     * and asserted, so an unwritable cwd cannot pass "prints nothing" with an
     * empty capture for the wrong reason. */
    FILE *const capture = fopen(path, "wb");

    test_expect_true(&test, "capture fixture opened", capture != nullptr);

    char captured[64] = DEFAULT_INITIALIZATION;

    if (capture != nullptr) {
        fflush(stdout);

#ifdef OS_WINDOWS
        I32 const saved_fd = _dup(_fileno(stdout));

        _dup2(_fileno(capture), _fileno(stdout));
#else
        I32 const saved_fd = dup(fileno(stdout));

        dup2(fileno(capture), fileno(stdout));
#endif // OS_WINDOWS

        bits_print_2((USize) 0xFF, (U8) 65, ' ');

        fflush(stdout);
        fclose(capture);

#ifdef OS_WINDOWS
        _dup2(saved_fd, _fileno(stdout));
        _close(saved_fd);
#else
        dup2(saved_fd, fileno(stdout));
        close(saved_fd);
#endif // OS_WINDOWS

        FILE *const readback = fopen(path, "rb");

        if (readback != nullptr) {
            USize const read_size = fread(captured, sizeof(char), sizeof(captured) - 1, readback);

            captured[read_size] = '\0';

            fclose(readback);
        }

        remove(path);

        test_expect_string(&test, "self_size 65 prints nothing", "", captured);
    }

    test_case_end(&test);

    test_case_begin(&test, "bits_format with a too-small buffer");

    char buffer[4] = DEFAULT_INITIALIZATION;

    USize const written = bits_format((USize) 0xFF, (U8) 8, ' ', 0, buffer, sizeof(buffer));

    test_expect_u(&test, "return is 0 for a too-small buffer", 0, written);
    test_expect_string(&test, "buffer holds the empty string", "", buffer);

    /* A null buffer: nothing can be written, but the return is still 0, not a crash. */
    USize const null_written = bits_format((USize) 5, (U8) 8, ' ', 8, nullptr, (USize) 128);

    test_expect_u(&test, "bits_format(nullptr) returns 0", 0, null_written);

    test_case_end(&test);

    test_case_begin(&test, "bits_array_* past the array");

    U64 words[3] = { (U64) 0x1111, (U64) 0x2222, (U64) 0x3333 };

    bits_array_set(words, (USize) 2, (USize) 128);
    test_expect_u(&test, "set past self_size leaves word 0 unchanged", 0x1111, words[0]);
    test_expect_u(&test, "set past self_size leaves word 1 unchanged", 0x2222, words[1]);
    test_expect_u(&test, "set past self_size leaves word 2 unchanged", 0x3333, words[2]);

    bits_array_clear(words, (USize) 2, (USize) 128);
    test_expect_u(&test, "clear past self_size leaves word 0 unchanged", 0x1111, words[0]);
    test_expect_u(&test, "clear past self_size leaves word 1 unchanged", 0x2222, words[1]);
    test_expect_u(&test, "clear past self_size leaves word 2 unchanged", 0x3333, words[2]);

    test_expect_false(&test, "test past self_size answers false", bits_array_test(words, (USize) 2, (USize) 128));

    test_case_end(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}