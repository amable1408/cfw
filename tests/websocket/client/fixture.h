/*
 * fixture.h - minimal RFC 6455 loopback WebSocket server for
 * tests/websocket/client/test_loopback.c. Not a CFW module: test-only
 * scaffolding, built and linked only into this suite's second binary.
 *
 * One Fixture_Server accepts exactly ONE connection on 127.0.0.1 (port
 * chosen by the OS), performs the server-side handshake, then runs one
 * scripted behaviour on its own CFW thread. The caller starts it, connects
 * the real websocket_client to fixture_server_port(), drives the scenario,
 * then fixture_server_join()s and reads the output fields.
 */
#ifndef TEST_WEBSOCKET_CLIENT_FIXTURE_H
#define TEST_WEBSOCKET_CLIENT_FIXTURE_H

#include <net/net.h>
#include <thread/thread.h>
#include <types.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

/** @brief Scripted server-side behaviour after a successful handshake. */
typedef enum Fixture_Script : U8 {
    FIXTURE_SCRIPT_ECHO,              /**< Read one message, write it back with the same opcode. */
    FIXTURE_SCRIPT_FRAGMENT_3,        /**< Send one message split into 3 frames (2 CONT, last FIN). */
    FIXTURE_SCRIPT_PING_MID,          /**< Send 2 fragments of one message with a PING in between. */
    FIXTURE_SCRIPT_PING_FLOOD,        /**< Send ping_count PINGs back-to-back, then nothing. */
    FIXTURE_SCRIPT_OVERSIZED,         /**< Send one binary frame of payload_size bytes. */
    FIXTURE_SCRIPT_CLOSE,             /**< Send a CLOSE frame (close_code / close_reason). */
    FIXTURE_SCRIPT_WRONG_SUBPROTOCOL, /**< Handshake echoes a subprotocol the client never asked for. */
    FIXTURE_SCRIPT_CLIENT_CLOSE,      /**< Read frames until a CLOSE arrives from the client. */
    FIXTURE_SCRIPT_DRAIN_LARGE,       /**< Sleep pre_read_delay_ms without reading, then drain payload_size bytes. */
    FIXTURE_SCRIPT_IDLE_SILENT,       /**< Send nothing until pre_read_delay_ms elapse, then one small message. */
    FIXTURE_SCRIPT_FRAGMENT_DELAY     /**< Send fragment 1, sleep pre_read_delay_ms, send fragment 2 (final). */
} Fixture_Script;

/** @brief One loopback fixture instance; caller-owned, zero-initialize before fixture_server_start. */
typedef struct Fixture_Server {
    Net_Socket listen_socket;
    Thread     thread;
    U16        port;

    Fixture_Script script;

    /* Script inputs. */
    USize ping_count;                 /**< FIXTURE_SCRIPT_PING_FLOOD. */
    U8 const *payload;                /**< FIXTURE_SCRIPT_OVERSIZED / DRAIN_LARGE source bytes. */
    USize payload_size;               /**< Byte count of `payload`, or bytes to drain. */
    U16 close_code;                   /**< FIXTURE_SCRIPT_CLOSE. */
    char const *close_reason;         /**< FIXTURE_SCRIPT_CLOSE; may be nullptr. */
    U32 pre_read_delay_ms;            /**< FIXTURE_SCRIPT_DRAIN_LARGE / IDLE_SILENT. */

    /* Script outputs - valid only after fixture_server_join. */
    bool saw_client_close;            /**< FIXTURE_SCRIPT_CLIENT_CLOSE / CLOSE: a CLOSE frame arrived FROM the client
                                            (CLIENT_CLOSE: the client-initiated close; CLOSE: its RFC 6455 5.5.1 echo). */
    U16 client_close_code;            /**< The close code carried by the frame above. */
    bool drain_ok;                    /**< FIXTURE_SCRIPT_DRAIN_LARGE: bytes matched `payload` exactly. */
    USize drain_received;             /**< FIXTURE_SCRIPT_DRAIN_LARGE: total bytes actually read. */
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

#endif // TEST_WEBSOCKET_CLIENT_FIXTURE_H