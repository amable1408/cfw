#include <stdio.h>

/* Gated on the compiler-defined _WIN32 (see test_all.c): CreateSymbolicLinkA and the junction
 * check come from <platform/windows/windows.h>, symlink/unlink from <unistd.h>. */
#ifdef _WIN32
#include <platform/windows/windows.h>
#else
#include <unistd.h>
#endif // _WIN32

#include <char/char.h>
#include <dir/dir.h>
#include <file/file.h>
#include <http/service/static/static.h>
#include <log/log.h>
#include <test/test.h>
#include <thread/thread.h>

/*
 * Behavioral tests for include/http/service/static/static.c built WITHOUT ERROR_CHECK_ENABLED.
 *
 * error_check_null in this module guards only null-pointer contracts on the public entry
 * points - deliberately absent here (undefined behavior with the checks compiled out, and this
 * file never exercises them). The path-security and cap refusals below - an oversize path, a
 * ".." traversal segment, a path outside the route prefix - are coded as ordinary runtime
 * branches, never routed through error_check, so this build proves each one refuses identically
 * whether ERROR_CHECK_ENABLED is defined or not, per the value-dependent-refusal standard.
 *
 * The TOCTOU twins (T1/T2 of test_all.c) do the same for the link refusals: a link swapped in
 * between the lexical resolve and the open - by static.c's test-only after-resolve hook - is
 * refused by the open (a final-component link) or by the final-path compare (a linked ancestor),
 * both by value. serve_1 opens and verifies BEFORE it reads any header, so a refusal returns
 * without touching the response; a refusal that failed would go on to read headers through the
 * fake response below and crash the run - loudly, never a silent pass.
 */

#define _FIXTURE_OUTSIDE        "static_unchecked_outside"
#define _FIXTURE_ROOT           "static_unchecked_fixture"
#define _SECRET_CONTENT         "TOP-SECRET-OUTSIDE-THE-ROOT"
#define _SWAP_RETRY_COUNT       40
#define _SWAP_RETRY_DELAY_MS    50

#ifdef _WIN32
#define _COMMAND_MAX            1024
#define _OUTSIDE_FROM_ROOT      "..\\" _FIXTURE_OUTSIDE
#define _SECRET_FROM_ROOT       "..\\" _FIXTURE_OUTSIDE "\\secret.txt"
#else
#define _OUTSIDE_FROM_ROOT      "../" _FIXTURE_OUTSIDE
#define _SECRET_FROM_ROOT       "../" _FIXTURE_OUTSIDE "/secret.txt"
#endif // _WIN32

/* http_server.c calls this only when no per-server handler is set; this suite never runs a live
 * server (every case here returns before serve_1 touches `response`, per the file header), but
 * linking the real http_server.c (see makefile) still needs the symbol to resolve. */
void http_server_request_default_callback(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response) {
    (void) context;
    (void) request;

    http_server_response_send_1(response, "default", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
}

/*==============================================================================
 * MARK: - Fixture
 *============================================================================*/
static void _fixture_write(char const *const path, char const *const content) {
    FILE *const file = fopen(path, "wb");

    if (memory_empty(file)) {
        log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "_fixture_write: fopen failed for %s", path);

        return;
    }

    fwrite(content, 1, char_length(content), file);
    fclose(file);
}

/* rename with a short retry - the fixture lives under a Dropbox-synced tree (see test_all.c). */
static bool _fixture_rename(char const *const from, char const *const to) {
    for (USize attempt = 0; attempt < _SWAP_RETRY_COUNT; attempt += 1) {
        if (rename(from, to) == 0) {
            return true;
        }

        thread_sleep(_SWAP_RETRY_DELAY_MS);
    }

    log_message_2(LOG_LEVEL_ERROR, LOG_METADATA, "_fixture_rename: %s -> %s failed", from, to);

    return false;
}

/* A symbolic link to a FILE; false on Windows without SeCreateSymbolicLinkPrivilege. */
static bool _fixture_link_file(char const *const link, char const *const target_from_link) {
#ifdef _WIN32
    return CreateSymbolicLinkA(link, target_from_link, SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
#else
    return symlink(target_from_link, link) == 0;
#endif // _WIN32
}

/* A link to a DIRECTORY: a directory symlink where it can be created, else (Windows, no
 * privilege) a junction through mklink /J, which needs none. */
static bool _fixture_link_directory(char const *const link, char const *const target_from_link, char const *const target_from_cwd) {
#ifdef _WIN32
    if (CreateSymbolicLinkA(link, target_from_link, SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0) {
        return true;
    }

    char link_native[_COMMAND_MAX]      = DEFAULT_INITIALIZATION;
    char target_native[_COMMAND_MAX]    = DEFAULT_INITIALIZATION;
    char command[_COMMAND_MAX]          = DEFAULT_INITIALIZATION;

    char_copy_truncate(link_native, sizeof(link_native), link);
    char_copy_truncate(target_native, sizeof(target_native), target_from_cwd);

    for (USize index = 0; link_native[index] != '\0'; index += 1) {
        if (link_native[index] == '/') {
            link_native[index] = '\\';
        }
    }

    snprintf(command, sizeof(command), "mklink /J \"%s\" \"%s\" >NUL", link_native, target_native);
    system(command);

    DWORD const attributes = GetFileAttributesA(link);

    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    (void) target_from_cwd;

    return symlink(target_from_link, link) == 0;
#endif // _WIN32
}

/* Remove a link itself, never what it points at. */
static void _fixture_unlink(char const *const link, bool const directory) {
#ifdef _WIN32
    if (directory) {
        RemoveDirectoryA(link);
    }
    else {
        DeleteFileA(link);
    }
#else
    (void) directory;

    unlink(link);
#endif // _WIN32
}

/*==============================================================================
 * MARK: - Swap hook
 *============================================================================*/
/* One arming, one swap: park `original`, put a link to the outside where it was. */
static char const   *_swap_original = nullptr;
static char const   *_swap_parked = nullptr;
static char const   *_swap_target_from_link = nullptr;
static bool         _swap_directory = false;
static bool         _swap_armed = false;
static bool         _swap_done = false;

static void _swap_hook_after_resolve(char const *const file_name) {
    (void) file_name;

    if (!_swap_armed) {
        return;
    }

    _swap_armed = false;

    if (!_fixture_rename(_swap_original, _swap_parked)) {
        return;
    }

    _swap_done = _swap_directory ? _fixture_link_directory(_swap_original, _swap_target_from_link, _FIXTURE_OUTSIDE) : _fixture_link_file(_swap_original, _swap_target_from_link);

    if (!_swap_done) {
        _fixture_rename(_swap_parked, _swap_original);
    }
}

static void _swap_restore(void) {
    _swap_armed = false;

    if (!_swap_done) {
        return;
    }

    _fixture_unlink(_swap_original, _swap_directory);
    _fixture_rename(_swap_parked, _swap_original);

    _swap_done = false;
}

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    dir_create_all_1(_FIXTURE_ROOT);
    dir_create_all_1(_FIXTURE_ROOT "/swapdir");
    dir_create_all_1(_FIXTURE_OUTSIDE);
    _fixture_write(_FIXTURE_OUTSIDE "/secret.txt", _SECRET_CONTENT);
    _fixture_write(_FIXTURE_ROOT "/swap_a.txt", "innocent-a");
    _fixture_write(_FIXTURE_ROOT "/swapdir/secret.txt", "innocent-dir");

    http_service_static_test_hook_after_resolve_set(_swap_hook_after_resolve);

    Test test = test_init("tests/http/service/static/test_unchecked.c");

    test_suite_begin(&test, "http_service_static (unchecked)");

    HTTP_Service_Static static_svc = http_service_static_init_1(_FIXTURE_ROOT, "/static");

    /* Never dereferenced: every refusal below returns before serve_1 touches `response`. */
    HTTP_Server_Response *const fake_response = (HTTP_Server_Response*) (void*) 1;

    test_case_begin(&test, "path security refusals hold identically without ERROR_CHECK_ENABLED");

    char oversize_path[HTTP_SERVER_PATH_MAX_LENGTH + 8] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < HTTP_SERVER_PATH_MAX_LENGTH + 4; i += 1) {
        oversize_path[i] = 'a';
    }

    test_expect_false(&test, "an oversize path is still refused", http_service_static_serve_1(&static_svc, fake_response, oversize_path, char_length(oversize_path)));

    char const *const traversal = "/static/../secret.txt";

    test_expect_false(&test, "a '..' segment is still refused", http_service_static_serve_1(&static_svc, fake_response, traversal, char_length(traversal)));

    char const *const other_prefix = "/other/app.js";

    test_expect_false(&test, "a path outside the route prefix is still refused", http_service_static_serve_1(&static_svc, fake_response, other_prefix, char_length(other_prefix)));

    test_case_end(&test);

    test_case_begin(&test, "TOCTOU twins: a link swapped in between resolve and open is refused without ERROR_CHECK_ENABLED");

    test_expect_true(&test, "refuse_symlinks is on by default", static_svc.refuse_symlinks);

    /* T1 twin: the final component becomes a FILE symlink - refused by the nofollow open. */
    bool const file_links = _fixture_link_file(_FIXTURE_ROOT "/probe_link.txt", "swap_a.txt");

    if (file_links) {
        _fixture_unlink(_FIXTURE_ROOT "/probe_link.txt", false);

        char const *const swap_file = "/static/swap_a.txt";

        _swap_original          = _FIXTURE_ROOT "/swap_a.txt";
        _swap_parked            = _FIXTURE_ROOT "/swap_a.parked";
        _swap_target_from_link  = _SECRET_FROM_ROOT;
        _swap_directory         = false;
        _swap_armed             = true;

        test_expect_false(&test, "T1 twin: a final-component link swapped in before the open is refused",
                          http_service_static_serve_1(&static_svc, fake_response, swap_file, char_length(swap_file)));
        test_expect_true(&test, "T1 twin: the hook did swap the link in", _swap_done);

        _swap_restore();
    }
    else {
        printf("    SKIPPED: T1 twin - this account cannot create a FILE symlink (no SeCreateSymbolicLinkPrivilege); the T2 twin pins the swap through a junction.\n");
    }

    /* T2 twin: an ANCESTOR becomes a link to the outside directory - the open follows it, the
     * final-path compare refuses it. */
    char const *const swap_ancestor = "/static/swapdir/secret.txt";

    _swap_original          = _FIXTURE_ROOT "/swapdir";
    _swap_parked            = _FIXTURE_ROOT "/swapdir_parked";
    _swap_target_from_link  = _OUTSIDE_FROM_ROOT;
    _swap_directory         = true;
    _swap_armed             = true;

    test_expect_false(&test, "T2 twin: a linked ancestor swapped in before the open is refused",
                      http_service_static_serve_1(&static_svc, fake_response, swap_ancestor, char_length(swap_ancestor)));
    test_expect_true(&test, "T2 twin: the hook did swap the link in", _swap_done);
    test_expect_true(&test, "T2 twin: ... and the secret WAS reachable through it (the refusal is not a miss)", file_exists_1(_FIXTURE_ROOT "/swapdir/secret.txt"));

    _swap_restore();

    test_case_end(&test);

    http_service_static_uninit(&static_svc);

    test_suite_end(&test);

    /* dir_remove_all_1 refuses any path with a dot segment (dir.h), so the roots are spelled
     * bare. A refusal here is worth a warning: the next run would find a stale fixture. */
    if (!dir_remove_all_1(_FIXTURE_ROOT)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "teardown: dir_remove_all_1 refused %s", _FIXTURE_ROOT);
    }

    if (!dir_remove_all_1(_FIXTURE_OUTSIDE)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "teardown: dir_remove_all_1 refused %s", _FIXTURE_OUTSIDE);
    }

    return test_uninit(&test);
}