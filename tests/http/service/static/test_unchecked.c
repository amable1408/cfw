#include <stdio.h>

#include <dir/dir.h>
#include <http/service/static/static.h>
#include <log/log.h>
#include <test/test.h>

/*
 * Behavioral tests for include/http/service/static/static.c built WITHOUT ERROR_CHECK_ENABLED.
 *
 * error_check_null in this module guards only null-pointer contracts on the public entry
 * points - deliberately absent here (undefined behavior with the checks compiled out, and this
 * file never exercises them). The path-security and cap refusals below - an oversize path, a
 * ".." traversal segment, a path outside the route prefix - are coded as ordinary runtime
 * branches, never routed through error_check, so this build proves each one refuses identically
 * whether ERROR_CHECK_ENABLED is defined or not, per the value-dependent-refusal standard.
 */

#define _FIXTURE_ROOT "static_unchecked_fixture"

/* http_server.c calls this only when no per-server handler is set; this suite never runs a live
 * server (every case here returns before serve_1 touches `response`, per the file header), but
 * linking the real http_server.c (see makefile) still needs the symbol to resolve. */
void http_server_request_default_callback(void *context, HTTP_Server_Request *request, HTTP_Server_Response *response) {
    (void) context;
    (void) request;

    http_server_response_send_1(response, "default", HTTP_SERVER_CONTENT_TYPE_TEXT_PLAIN, HTTP_SERVER_STATUS_CODE_NOT_FOUND);
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

    http_service_static_uninit(&static_svc);

    test_suite_end(&test);

    /* dir_remove_all_1 refuses any path with a dot segment (dir.h), so the root is spelled bare.
     * A refusal here is worth a warning: the next run would find a stale fixture. */
    if (!dir_remove_all_1(_FIXTURE_ROOT)) {
        log_message_2(LOG_LEVEL_WARN, LOG_METADATA, "teardown: dir_remove_all_1 refused %s", _FIXTURE_ROOT);
    }

    return test_uninit(&test);
}