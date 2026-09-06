/*
 * fixture.h - minimal HTTP/1.1 loopback server for tests/http/client/test_all.c
 * and test_unchecked.c. Not a CFW module: test-only scaffolding, built and
 * linked into both of this suite's binaries.
 *
 * One Fixture_Server accepts up to `max_connections` connections on 127.0.0.1
 * (port chosen by the OS) and, per connection, serves requests according to
 * one scripted behaviour on its own CFW thread. The caller starts it, points
 * the real http_client at fixture_server_port(), drives the scenario, then
 * fixture_server_join()s and reads the output fields. Every request's method,
 * path, header block, and body are captured into the output fields regardless
 * of script, so any script can double as an echo check.
 */
#ifndef TEST_HTTP_CLIENT_FIXTURE_H
#define TEST_HTTP_CLIENT_FIXTURE_H

#include <stdatomic.h>

#include <container/string/string.h>
#include <net/net.h>
#include <thread/thread.h>
#include <types.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/** @brief Scripted server-side response behaviour after each request is parsed. */
typedef enum Fixture_Script : U8 {
    FIXTURE_SCRIPT_OK,             /**< 200 with a body and duplicate/mixed-case/empty-value headers. */
    FIXTURE_SCRIPT_STATUS,         /**< `status_code` (e.g. 404/500) with a small body. */
    FIXTURE_SCRIPT_REDIRECT,       /**< "/" -> 302 Location: /second; "/second" -> 200 (final-hop headers only). */
    FIXTURE_SCRIPT_REDIRECT_LOOP,  /**< Every path -> 302 to itself, forever (for MAXREDIRS). */
    FIXTURE_SCRIPT_REDIRECT_FTP,   /**< 302 Location: ftp://... (must be refused, never connected to). */
    FIXTURE_SCRIPT_CHUNKED,        /**< 200, Transfer-Encoding: chunked body. */
    FIXTURE_SCRIPT_GZIP,           /**< 200, Content-Encoding: gzip body (precomputed bytes). */
    FIXTURE_SCRIPT_OVERSIZED,      /**< 200, Content-Length: `oversized_body_size`, that many body bytes. */
    FIXTURE_SCRIPT_HEADER_FLOOD,   /**< 200, `header_flood_count` small response headers before the body. */
    FIXTURE_SCRIPT_STALL,          /**< Reads the request, then sends nothing until the peer gives up. */
    FIXTURE_SCRIPT_PARTIAL_CLOSE,  /**< Declares `partial_close_content_length`, sends only `partial_close_send_size`, closes. */
    FIXTURE_SCRIPT_KEEP_ALIVE      /**< 200 + Connection: keep-alive, `keep_alive_request_count` times on ONE connection. */
} Fixture_Script;

/** @brief One loopback fixture instance; caller-owned, zero-initialize before fixture_server_start. */
typedef struct Fixture_Server {
    Net_Socket   listen_socket;
    Thread       thread;
    U16          port;
    _Atomic bool stop;          /**< Set by fixture_server_join before it wakes and joins the accept loop. */

    Fixture_Script script;

    /* Script inputs. */
    USize status_code;                     /**< FIXTURE_SCRIPT_STATUS. */
    USize oversized_body_size;             /**< FIXTURE_SCRIPT_OVERSIZED. */
    USize header_flood_count;              /**< FIXTURE_SCRIPT_HEADER_FLOOD. */
    USize header_flood_value_size;         /**< FIXTURE_SCRIPT_HEADER_FLOOD: bytes per header value (0 means 1, "v"). */
    USize partial_close_content_length;    /**< FIXTURE_SCRIPT_PARTIAL_CLOSE: the declared Content-Length. */
    USize partial_close_send_size;         /**< FIXTURE_SCRIPT_PARTIAL_CLOSE: bytes actually written before close. */
    USize keep_alive_request_count;        /**< FIXTURE_SCRIPT_KEEP_ALIVE: requests to serve before closing. */
    USize max_connections;                 /**< Accept-loop bound; 0 means 1. */

    /* Script outputs - valid only after fixture_server_join. Reflect the LAST request parsed. */
    USize  connection_count;               /**< Number of TCP connections actually accepted. */
    USize  request_count;                  /**< Total requests parsed across every connection. */
    char   request_method[16];             /**< NUL-terminated, truncated if longer. */
    char   request_path[256];              /**< NUL-terminated, truncated if longer. */
    char   request_headers[4096];          /**< Raw header block (no request line, no blank terminator), NUL-terminated. */
    String request_body;                   /**< Body bytes (may contain embedded NUL). Caller must string_uninit it after join. */
} Fixture_Server;

/*==============================================================================
 * MARK: - API
 *============================================================================*/

/** @brief Join the fixture thread and close the listen socket. Blocks until the script finishes. */
void fixture_server_join(Fixture_Server *const self);

/** @brief The OS-assigned port the fixture is listening on. Valid after fixture_server_start. */
U16 fixture_server_port(Fixture_Server const *const self);

/**
 * @brief Bind on 127.0.0.1:0, start the accept/script thread. Fill in the
 *        script fields on `self` before calling this.
 * @param self Zero-initialized Fixture_Server; script fields already set.
 * @return true on success (listen socket bound and thread started).
 */
bool fixture_server_start(Fixture_Server *const self);

#endif // TEST_HTTP_CLIENT_FIXTURE_H