// The shim leads, ahead of the usual external-headers-first order, because
// this suite asserts on the preprocessor state it establishes: putting it
// first makes the translation unit an instance of the shim-included-first
// case the API baseline test is about. Everything below is ordinary.
#include <platform/windows/windows.h>

#include <stdio.h>

#include <log/log.h>
#include <test/test.h>

/* Coverage for the Windows header shim. The module is header-only and its
 * whole job is preprocessor state, so the suite checks the state it promises
 * (configuration macros, API baseline, Winsock 2 selection) and then proves
 * each promise functionally — a Winsock 2 handshake and a <ws2tcpip.h> call.
 * The macro assertion is what proves the API baseline; the GetTickCount64
 * call only witnesses that the headers exposed a post-XP surface at all.
 *
 * This makefile deliberately does NOT pass -D_WIN32_WINNT: a standalone build
 * that does not inherit build/windows/makefile's CDEFINES must still land on
 * the required baseline through <_mingw.h>. The baseline case below is what
 * proves that, and it is exactly the situation the shim's floor assert covers.
 *
 * The negative cases (a poisoned include order, -DUNICODE, too low a
 * baseline) cannot be expressed here — a translation unit that fails to
 * compile is not a test that runs. They live in test_guards.ps1, which drives
 * the compiler once per case and asserts the diagnostic. Run both. */

#ifdef NOMINMAX
#define _TEST_NOMINMAX_SET true
#else
#define _TEST_NOMINMAX_SET false
#endif

#ifdef WIN32_LEAN_AND_MEAN
#define _TEST_LEAN_AND_MEAN_SET true
#else
#define _TEST_LEAN_AND_MEAN_SET false
#endif

/* WIN32_LEAN_AND_MEAN is what keeps these out; if either reappears as a
 * function-like macro, a local min/max helper silently changes meaning. */
#if defined(min) || defined(max)
#define _TEST_MIN_MAX_CLEAR false
#else
#define _TEST_MIN_MAX_CLEAR true
#endif

/* <winsock2.h> sets this; plain <windows.h> sets _WINSOCKAPI_ instead. */
#ifdef _WINSOCK2API_
#define _TEST_WINSOCK2_SELECTED true
#else
#define _TEST_WINSOCK2_SELECTED false
#endif

/* Unreachable in a binary that exists - the shim's floor assert would have
 * stopped the compile. It keeps the assertions below well-formed, and those
 * assertions are witnesses of the compile-time gate, not a live defense. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0
#endif

static void _test_configuration_macros(Test *const test) {
    test_case_begin(test, "configuration macros");

    test_expect_true(test, "WIN32_LEAN_AND_MEAN is set", _TEST_LEAN_AND_MEAN_SET);
    test_expect_true(test, "NOMINMAX is set", _TEST_NOMINMAX_SET);
    test_expect_true(test, "min/max are not function-like macros", _TEST_MIN_MAX_CLEAR);

    test_case_end(test);
}

static void _test_api_baseline(Test *const test) {
    test_case_begin(test, "API baseline");

    test_expect_true(test, "_WIN32_WINNT is defined", _WIN32_WINNT != 0);
    test_expect_true(test, "_WIN32_WINNT >= 0x0A00 without an explicit -D", _WIN32_WINNT >= 0x0A00);

    /* GetTickCount64 is the Windows Vista+ replacement for the wrapping
     * GetTickCount; resolving it proves the headers exposed the modern
     * surface rather than a downlevel subset. */
    test_expect_true(test, "GetTickCount64 is available and running", GetTickCount64() > 0);

    test_case_end(test);
}

static void _test_winsock2_selected(Test *const test) {
    test_case_begin(test, "Winsock 2 selection");

    test_expect_true(test, "_WINSOCK2API_ defined (not the 1.1 subset)", _TEST_WINSOCK2_SELECTED);

    WSADATA data = DEFAULT_INITIALIZATION;
    int const started = WSAStartup(MAKEWORD(2, 2), &data);

    test_expect_i(test, "WSAStartup(2.2) succeeds", 0, (ISize) started);

    if (started == 0) {
        test_expect_u(test, "negotiated version is 2.2", (USize) MAKEWORD(2, 2), (USize) data.wVersion);

        SOCKET const handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        test_expect_true(test, "socket() returns a usable handle", handle != INVALID_SOCKET);

        if (handle != INVALID_SOCKET) {
            closesocket(handle);
        }

        /* Pairs this case's own startup. Winsock stays initialized for the
         * cases that follow when main()'s suite-level startup succeeded —
         * which is what the hoist is for; main reports it when it did not. */
        WSACleanup();
    }

    test_case_end(test);
}

static void _test_ws2tcpip_available(Test *const test) {
    test_case_begin(test, "<ws2tcpip.h> comes with the shim");

    /* inet_pton lives in <ws2tcpip.h>, not <winsock2.h> — before the shim
     * absorbed it, net.h had to include that header by hand. */

    /* Named member rather than DEFAULT_INITIALIZATION: it initializes exactly
     * the member the assertion below reads, instead of relying on the reader
     * to know that {0} reaches s_addr by punning through the union's first
     * member. ({0} would in fact zero all four bytes here - the first member
     * spans the whole union - but that is a subtlety worth not depending on.) */
    struct in_addr address = { .s_addr = 0 };

    test_expect_i(test, "inet_pton parses a dotted quad", 1, (ISize) inet_pton(AF_INET, "127.0.0.1", &address));

    /* s_addr is network (big-endian) order read on a little-endian host, so
     * the bytes 7F 00 00 01 read back as 0x0100007F. Not a typo - CFW pins
     * little-endian in types.h. */
    test_expect_u(test, "parsed to network-order 127.0.0.1", 0x0100007FU, (USize) address.s_addr);

    char text[INET_ADDRSTRLEN] = DEFAULT_INITIALIZATION;

    test_expect_not_null(test, "inet_ntop round-trips", (void*) inet_ntop(AF_INET, &address, text, sizeof(text)));
    test_expect_string(test, "round-trip preserves the address", "127.0.0.1", text);

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

    /* Winsock's contract is startup-before-use. Holding one reference around
     * the whole suite keeps every case inside an initialized Winsock, so no
     * assertion can fail for a reason unrelated to the shim. The handshake
     * case makes its own paired call, which is what actually proves 2.2.
     *
     * The result is checked because everything below leans on it: WSAStartup
     * only increments the reference count on success, so a silent failure
     * here would leave the handshake case's own cleanup dropping the count to
     * zero and the <ws2tcpip.h> case failing for an unrelated reason. */
    WSADATA suite_data = DEFAULT_INITIALIZATION;
    int const suite_started = WSAStartup(MAKEWORD(2, 2), &suite_data);

    if (suite_started != 0) {
        printf("WSAStartup failed for the suite (%d) - Winsock cases will not be meaningful\n", suite_started);
    }

    Test test = test_init("platform/windows");

    test_suite_begin(&test, "Windows header shim");

    _test_configuration_macros(&test);
    _test_api_baseline(&test);
    _test_winsock2_selected(&test);
    _test_ws2tcpip_available(&test);

    test_suite_end(&test);

    I32 const status = test_uninit(&test);

    if (suite_started == 0) {
        WSACleanup();
    }

    return status;
}