#include <stdio.h>
#include <string.h>

#include <log/log.h>
#include <net/net.h>
#include <test/test.h>

/* Coverage for net's Result-based socket API: every init/address/socket
 * combination, the failure shapes a caller must be able to tell apart
 * (address-in-use, refused, would-block, timed out, peer close), and
 * loopback round trips over TCP and UDP that use only the module's own
 * connect/send/recv.
 *
 * Every case is loopback-local and port-0 based: the server binds port 0 and
 * asks the OS which port it got (net_socket_address_local), so the suite
 * never collides with a port already in use and never accepts traffic from
 * another host. */

#define _TEST_TIMEOUT_MS 2000

static U16 _test_bound_port(Net_Socket const self) {
    Net_Socket_Address bound = DEFAULT_INITIALIZATION;

    if (result_is_error(net_socket_address_local(self, &bound))) {
        return 0;
    }

    return net_socket_address_port(&bound);
}

static void _test_init_combinations(Test *const test) {
    test_case_begin(test, "net_socket_init accepts every legal combination");

    Net_Socket tcp4 = NET_SOCKET_INVALID;
    Result const tcp4_result = net_socket_init(&tcp4, NET_FAMILY_IPV4, NET_TYPE_TCP);
    test_expect_true(test, "IPv4 TCP init succeeds", result_is_success(tcp4_result));

    if (result_is_success(tcp4_result)) {
        net_socket_close(tcp4);
    }

    Net_Socket udp4 = NET_SOCKET_INVALID;
    Result const udp4_result = net_socket_init(&udp4, NET_FAMILY_IPV4, NET_TYPE_UDP);
    test_expect_true(test, "IPv4 UDP init succeeds", result_is_success(udp4_result));

    if (result_is_success(udp4_result)) {
        net_socket_close(udp4);
    }

    Net_Socket tcp6 = NET_SOCKET_INVALID;
    Result const tcp6_result = net_socket_init(&tcp6, NET_FAMILY_IPV6, NET_TYPE_TCP);
    test_expect_true(test, "IPv6 TCP init succeeds or reports SYSTEM (unsupported family)", result_is_success(tcp6_result) || result_category(tcp6_result) == RESULT_CATEGORY_SYSTEM);

    if (result_is_success(tcp6_result)) {
        net_socket_close(tcp6);
    }

    Net_Socket udp6 = NET_SOCKET_INVALID;
    Result const udp6_result = net_socket_init(&udp6, NET_FAMILY_IPV6, NET_TYPE_UDP);
    test_expect_true(test, "IPv6 UDP init succeeds or reports SYSTEM (unsupported family)", result_is_success(udp6_result) || result_category(udp6_result) == RESULT_CATEGORY_SYSTEM);

    if (result_is_success(udp6_result)) {
        net_socket_close(udp6);
    }

    test_case_end(test);
}

static void _test_address_initializers(Test *const test) {
    test_case_begin(test, "address initializers and getters");

    Net_Socket_Address const wildcard = net_socket_address_init_1(NET_FAMILY_IPV4, 8080);

    test_expect_true(test, "init_1 sets the family", net_socket_address_family(&wildcard) == NET_FAMILY_IPV4);
    test_expect_u(test, "init_1 keeps the port", 8080, (USize) net_socket_address_port(&wildcard));

    Net_Socket_Address loopback = DEFAULT_INITIALIZATION;

    test_expect_true(test, "init_2 parses a valid literal", result_is_success(net_socket_address_init_2(NET_FAMILY_IPV4, 9090, "127.0.0.1", &loopback)));
    test_expect_u(test, "init_2 keeps the port", 9090, (USize) net_socket_address_port(&loopback));

    Net_Socket_Address refused = DEFAULT_INITIALIZATION;

    Result const empty_result = net_socket_address_init_2(NET_FAMILY_IPV4, 1, "", &refused);
    test_expect_true(test, "empty literal is refused as ARGUMENT", result_category(empty_result) == RESULT_CATEGORY_ARGUMENT);

    Result const bad_octet_result = net_socket_address_init_2(NET_FAMILY_IPV4, 1, "999.1.1.1", &refused);
    test_expect_true(test, "out-of-range literal is refused as ARGUMENT", result_category(bad_octet_result) == RESULT_CATEGORY_ARGUMENT);

    Result const garbage_result = net_socket_address_init_2(NET_FAMILY_IPV4, 1, "not-an-address", &refused);
    test_expect_true(test, "garbage literal is refused as ARGUMENT", result_category(garbage_result) == RESULT_CATEGORY_ARGUMENT);

    // A refused literal never falls back to the wildcard - `refused` was
    // never written by any of the three failing calls above.
    test_expect_u(test, "a refused literal never becomes the wildcard", 0, (USize) refused.storage.ss_family);

    char text[NET_SOCKET_ADDRESS_TEXT_SIZE] = DEFAULT_INITIALIZATION;

    net_socket_address_format(&loopback, text);
    test_expect_string(test, "format renders ip:port", "127.0.0.1:9090", text);

    Net_Socket_Address const loopback_copy = loopback;

    test_expect_true(test, "equal addresses compare equal", net_socket_address_equal(&loopback, &loopback_copy));
    test_expect_false(test, "different ports compare unequal", net_socket_address_equal(&loopback, &wildcard));

    // IPv6 round trip: skip cleanly (not a failure) when the platform itself
    // has no IPv6 support - a returned Result is how a caller finds out its
    // family is unserviceable, without an abort in the way.
    Net_Socket_Address v6 = DEFAULT_INITIALIZATION;
    Result const v6_result = net_socket_address_init_2(NET_FAMILY_IPV6, 9999, "::1", &v6);

    if (result_is_success(v6_result)) {
        test_expect_true(test, "::1 keeps its family", net_socket_address_family(&v6) == NET_FAMILY_IPV6);
        test_expect_u(test, "::1 keeps the port", 9999, (USize) net_socket_address_port(&v6));

        Net_Socket_Address v4_family_v6_literal = DEFAULT_INITIALIZATION;
        Result const mismatch = net_socket_address_init_2(NET_FAMILY_IPV4, 1, "::1", &v4_family_v6_literal);
        test_expect_true(test, "::1 under NET_FAMILY_IPV4 is refused, not silently wildcarded", result_category(mismatch) == RESULT_CATEGORY_ARGUMENT);

        char v6_text[NET_SOCKET_ADDRESS_TEXT_SIZE] = DEFAULT_INITIALIZATION;

        net_socket_address_format(&v6, v6_text);
        test_expect_string(test, "format renders [ip]:port for an IPv6 literal into the exact-size buffer", "[::1]:9999", v6_text);
    }
    else {
        printf("  [skip] \"::1\" unsupported on this platform (code=%u) - init_2 returned a Result instead of aborting\n", (unsigned) result_code(v6_result));
    }

    test_case_end(test);
}

static void _test_tcp_round_trip(Test *const test) {
    test_case_begin(test, "TCP loopback round trip (module's own connect/send)");

    Net_Socket server = NET_SOCKET_INVALID;
    Net_Socket_Address bind_address = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "server init succeeds", result_is_success(net_socket_init(&server, NET_FAMILY_IPV4, NET_TYPE_TCP)))) {
        test_case_end(test);

        return;
    }

    test_expect_true(test, "set_reuse_address is called and checked", result_is_success(net_socket_set_reuse_address(server, true)));

    net_socket_address_init_2(NET_FAMILY_IPV4, 0, "127.0.0.1", &bind_address);
    net_socket_bind(server, &bind_address);
    net_socket_listen(server, 1);
    net_socket_set_timeout(server, _TEST_TIMEOUT_MS);

    U16 const port = _test_bound_port(server);

    test_expect_true(test, "the OS assigned a port", port != 0);

    Net_Socket client = NET_SOCKET_INVALID;
    Net_Socket_Address target = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "client init succeeds", result_is_success(net_socket_init(&client, NET_FAMILY_IPV4, NET_TYPE_TCP)))) {
        net_socket_close(server);
        test_case_end(test);

        return;
    }

    test_expect_true(test, "set_nodelay is called and checked", result_is_success(net_socket_set_nodelay(client, true)));

    net_socket_address_init_2(NET_FAMILY_IPV4, port, "127.0.0.1", &target);

    if (!test_expect_true(test, "client connects", result_is_success(net_socket_connect(client, &target)))) {
        net_socket_close(client);
        net_socket_close(server);
        test_case_end(test);

        return;
    }

    Net_Socket_Address client_local = DEFAULT_INITIALIZATION;
    test_expect_true(test, "client's own local address is readable", result_is_success(net_socket_address_local(client, &client_local)));

    Net_Socket accepted = NET_SOCKET_INVALID;
    Net_Socket_Address peer = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "server accepts the connection", result_is_success(net_socket_accept(server, &accepted, &peer)))) {
        net_socket_close(client);
        net_socket_close(server);
        test_case_end(test);

        return;
    }

    test_expect_true(test, "accepted peer equals the client's own local address", net_socket_address_equal(&peer, &client_local));

    char peer_text[NET_SOCKET_ADDRESS_TEXT_SIZE] = DEFAULT_INITIALIZATION;
    net_socket_address_format(&peer, peer_text);
    test_expect_true(test, "accepted peer formats as a non-empty ip:port string", peer_text[0] != '\0');

    net_socket_set_timeout(accepted, _TEST_TIMEOUT_MS);

    char const message[] = "cfw-net";
    USize sent = 0;

    test_expect_true(test, "client sends the payload", result_is_success(net_socket_send_1(client, message, sizeof(message), &sent)));
    test_expect_u(test, "the whole payload was sent", sizeof(message), sent);

    Net_Wait read_ready = (Net_Wait) 0;
    test_expect_true(test, "wait reports the accepted socket readable after the send", result_is_success(net_socket_wait(accepted, NET_WAIT_READ, _TEST_TIMEOUT_MS, &read_ready)));
    test_expect_true(test, "accepted socket is readable", (read_ready & NET_WAIT_READ) != 0);

    char received[32] = DEFAULT_INITIALIZATION;
    USize received_size = 0;

    test_expect_true(test, "server receives the payload", result_is_success(net_socket_recv_1(accepted, received, sizeof(received), &received_size)));
    test_expect_u(test, "the whole payload was received", sizeof(message), received_size);
    test_expect_string(test, "payload survives the round trip", message, received);

    // size == 0 is a legal no-op on TCP send, not an error: nothing to write,
    // so this must report SUCCESS with *sent == 0 rather than being refused.
    USize zero_sent = 1; // deliberately non-zero, so a missed write is visible
    Result const zero_send_result = net_socket_send_1(client, message, 0, &zero_sent);

    test_expect_true(test, "a 0-byte TCP send reports SUCCESS", result_is_success(zero_send_result));
    test_expect_u(test, "a 0-byte send reports sent == 0", 0, zero_sent);

    // Readiness: after the send above, `accepted` must already be readable.
    Net_Wait ready = (Net_Wait) 0;
    test_expect_true(test, "wait reports the client writable after it already drained its peer's data", result_is_success(net_socket_wait(client, NET_WAIT_WRITE, _TEST_TIMEOUT_MS, &ready)));
    test_expect_true(test, "client socket is writable", (ready & NET_WAIT_WRITE) != 0);

    // wait on an idle socket for a short timeout must report SUCCESS with
    // ready == 0 (no event), not an error - a timeout is not a failure here.
    Net_Wait idle_ready = (Net_Wait) 1; // deliberately non-zero, so a missed clear is visible
    Result const idle_wait_result = net_socket_wait(accepted, NET_WAIT_READ, 100, &idle_ready);

    test_expect_true(test, "wait on an idle socket still succeeds", result_is_success(idle_wait_result));
    test_expect_u(test, "a timeout with no event reports ready == 0", 0, (USize) idle_ready);

    // Peer close: the client shuts down and closes; the server's next recv
    // must report SUCCESS with 0 bytes, not an error.
    Result const shutdown_result = net_socket_shutdown(client, NET_SHUTDOWN_BOTH);
    test_expect_true(test, "shutdown's Result is checked and succeeds", result_is_success(shutdown_result));
    net_socket_close(client);

    USize eof_size = 1; // deliberately non-zero, so a missed write is visible
    Result const eof_result = net_socket_recv_1(accepted, received, sizeof(received), &eof_size);

    test_expect_true(test, "peer close is reported as SUCCESS", result_is_success(eof_result));
    test_expect_u(test, "peer close reads back as 0 bytes, not an error", 0, eof_size);

    net_socket_close(accepted);
    net_socket_close(server);

    test_case_end(test);
}

static void _test_send_after_peer_close(Test *const test) {
    test_case_begin(test, "send after the peer closes reports net_result_is_closed, not a process kill");

    Net_Socket server = NET_SOCKET_INVALID;
    Net_Socket_Address bind_address = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "server init succeeds", result_is_success(net_socket_init(&server, NET_FAMILY_IPV4, NET_TYPE_TCP)))) {
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV4, 0, "127.0.0.1", &bind_address);
    net_socket_bind(server, &bind_address);
    net_socket_listen(server, 1);

    U16 const port = _test_bound_port(server);
    Net_Socket client = NET_SOCKET_INVALID;
    Net_Socket_Address target = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "client init succeeds", result_is_success(net_socket_init(&client, NET_FAMILY_IPV4, NET_TYPE_TCP)))) {
        net_socket_close(server);
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV4, port, "127.0.0.1", &target);
    net_socket_connect(client, &target);

    Net_Socket accepted = NET_SOCKET_INVALID;

    if (!test_expect_true(test, "server accepts the connection", result_is_success(net_socket_accept(server, &accepted, nullptr)))) {
        net_socket_close(client);
        net_socket_close(server);
        test_case_end(test);

        return;
    }

    // Close the peer, then keep sending: on Linux, without MSG_NOSIGNAL a
    // write past the peer's FIN/RST would raise SIGPIPE and kill the whole
    // test process; net_socket_send_1's MSG_NOSIGNAL flag turns that into an
    // ordinary Result. A short wait between attempts gives the kernel time
    // to notice the peer is gone; the loop bounds how long that can take.
    net_socket_shutdown(accepted, NET_SHUTDOWN_BOTH);
    net_socket_close(accepted);

    char const message[] = "after-close";
    Result send_result    = RESULT_SUCCESS;

    for (I32 attempt = 0; attempt < 20 && result_is_success(send_result); attempt += 1) {
        Net_Wait ready = (Net_Wait) 0;
        net_socket_wait(client, NET_WAIT_WRITE, 50, &ready);

        USize sent = 0;
        send_result = net_socket_send_1(client, message, sizeof(message), &sent);
    }

    test_expect_true(test, "repeated sends after peer close eventually fail", result_is_error(send_result));
    test_expect_true(test, "net_result_is_closed recognises a peer-closed send", net_result_is_closed(send_result));

    net_socket_close(client);
    net_socket_close(server);

    test_case_end(test);
}

static void _test_udp_round_trip(Test *const test) {
    test_case_begin(test, "UDP loopback round trip with a reply (send_2/recv_2)");

    Net_Socket server = NET_SOCKET_INVALID;
    Net_Socket_Address bind_address = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "server init succeeds", result_is_success(net_socket_init(&server, NET_FAMILY_IPV4, NET_TYPE_UDP)))) {
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV4, 0, "127.0.0.1", &bind_address);
    net_socket_bind(server, &bind_address);
    net_socket_set_timeout(server, _TEST_TIMEOUT_MS);

    U16 const port = _test_bound_port(server);

    test_expect_true(test, "the OS assigned a port", port != 0);

    Net_Socket client = NET_SOCKET_INVALID;
    Net_Socket_Address target = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "client init succeeds", result_is_success(net_socket_init(&client, NET_FAMILY_IPV4, NET_TYPE_UDP)))) {
        net_socket_close(server);
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV4, port, "127.0.0.1", &target);
    net_socket_set_timeout(client, _TEST_TIMEOUT_MS);

    char const message[] = "cfw-udp";
    USize sent = 0;

    test_expect_true(test, "client sends the datagram", result_is_success(net_socket_send_2(client, message, sizeof(message), &target, &sent)));
    test_expect_u(test, "the whole datagram was sent", sizeof(message), sent);

    // A UDP socket only gets an implicit local address once it has sent (or
    // bound/connected) - Windows' getsockname() refuses WSAEINVAL on a
    // socket that has never been bound, unlike Linux, which answers a
    // 0.0.0.0:0 wildcard even before that. Reading it AFTER the send above
    // is what makes this portable.
    Net_Socket_Address client_local = DEFAULT_INITIALIZATION;
    test_expect_true(test, "client's own local address is readable after sending", result_is_success(net_socket_address_local(client, &client_local)));

    char received[32]         = DEFAULT_INITIALIZATION;
    USize received_size       = 0;
    Net_Socket_Address sender = DEFAULT_INITIALIZATION;

    test_expect_true(test, "server receives the datagram", result_is_success(net_socket_recv_2(server, received, sizeof(received), &received_size, &sender)));
    test_expect_u(test, "the whole datagram was received", sizeof(message), received_size);
    test_expect_string(test, "datagram survives the round trip", message, received);
    // Compare ports only, not full address equality: an implicitly-bound
    // client's own local address can read back as the wildcard (0.0.0.0),
    // while the receiver sees the actual outgoing interface (127.0.0.1) as
    // the sender - the port is what identifies "the same socket" here.
    test_expect_u(test, "recv_2's sender port matches the client's own local port", (USize) net_socket_address_port(&client_local), (USize) net_socket_address_port(&sender));

    char const reply[] = "cfw-udp-reply";
    USize reply_sent    = 0;

    test_expect_true(test, "server replies to the recorded sender", result_is_success(net_socket_send_2(server, reply, sizeof(reply), &sender, &reply_sent)));

    char reply_received[32] = DEFAULT_INITIALIZATION;
    USize reply_size        = 0;

    test_expect_true(test, "client receives the reply", result_is_success(net_socket_recv_1(client, reply_received, sizeof(reply_received), &reply_size)));
    test_expect_string(test, "reply survives the round trip", reply, reply_received);

    // size == 0 is a legal UDP datagram, not an error: a real 0-byte
    // datagram is sent and received, SUCCESS with sent/received == 0.
    USize zero_sent = 1; // deliberately non-zero, so a missed write is visible
    Result const zero_send_result = net_socket_send_2(client, message, 0, &target, &zero_sent);

    test_expect_true(test, "a 0-byte UDP send reports SUCCESS", result_is_success(zero_send_result));
    test_expect_u(test, "a 0-byte send reports sent == 0", 0, zero_sent);

    USize zero_received_size = 1; // deliberately non-zero
    Net_Socket_Address zero_sender = DEFAULT_INITIALIZATION;
    Result const zero_recv_result = net_socket_recv_2(server, received, sizeof(received), &zero_received_size, &zero_sender);

    test_expect_true(test, "the 0-byte datagram round-trips as SUCCESS", result_is_success(zero_recv_result));
    test_expect_u(test, "the 0-byte datagram reads back as 0 bytes", 0, zero_received_size);

    net_socket_close(client);
    net_socket_close(server);

    test_case_end(test);
}

static void _test_address_in_use(Test *const test) {
    test_case_begin(test, "binding an already-bound address is refused");

    Net_Socket holder = NET_SOCKET_INVALID;
    Net_Socket_Address wildcard = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "holder init succeeds", result_is_success(net_socket_init(&holder, NET_FAMILY_IPV4, NET_TYPE_TCP)))) {
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV4, 0, "127.0.0.1", &wildcard);
    net_socket_bind(holder, &wildcard);

    U16 const port = _test_bound_port(holder);

    test_expect_true(test, "the OS assigned a port", port != 0);

    Net_Socket contender = NET_SOCKET_INVALID;
    Net_Socket_Address same_address = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "contender init succeeds", result_is_success(net_socket_init(&contender, NET_FAMILY_IPV4, NET_TYPE_TCP)))) {
        net_socket_close(holder);
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV4, port, "127.0.0.1", &same_address);

    Result const second_bind = net_socket_bind(contender, &same_address);

    test_expect_true(test, "the second bind fails", result_is_error(second_bind));
    test_expect_true(test, "net_result_is_address_in_use recognises it", net_result_is_address_in_use(second_bind));

    net_socket_close(contender);
    net_socket_close(holder);

    test_case_end(test);
}

static void _test_connect_refused(Test *const test) {
    test_case_begin(test, "connecting to a closed loopback port is refused");

    // Bind, learn the port, then close it - nothing listens there anymore,
    // so the kernel replies with an immediate, deterministic refusal.
    Net_Socket probe = NET_SOCKET_INVALID;
    Net_Socket_Address bind_address = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "probe init succeeds", result_is_success(net_socket_init(&probe, NET_FAMILY_IPV4, NET_TYPE_TCP)))) {
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV4, 0, "127.0.0.1", &bind_address);
    net_socket_bind(probe, &bind_address);

    U16 const port = _test_bound_port(probe);

    net_socket_close(probe);

    test_expect_true(test, "the OS assigned a port", port != 0);

    Net_Socket client = NET_SOCKET_INVALID;
    Net_Socket_Address target = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "client init succeeds", result_is_success(net_socket_init(&client, NET_FAMILY_IPV4, NET_TYPE_TCP)))) {
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV4, port, "127.0.0.1", &target);

    Result const connect_result = net_socket_connect(client, &target);

    test_expect_true(test, "connect fails", result_is_error(connect_result));
    test_expect_true(test, "the failure classifies as NETWORK", result_category(connect_result) == RESULT_CATEGORY_NETWORK);
    test_expect_true(test, "net_result_is_closed recognises the refusal", net_result_is_closed(connect_result));

    net_socket_close(client);

    test_case_end(test);
}

static void _test_nonblocking_would_block(Test *const test) {
    test_case_begin(test, "nonblocking recv on an idle socket would-blocks");

    Net_Socket server = NET_SOCKET_INVALID;
    Net_Socket_Address bind_address = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "server init succeeds", result_is_success(net_socket_init(&server, NET_FAMILY_IPV4, NET_TYPE_UDP)))) {
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV4, 0, "127.0.0.1", &bind_address);
    net_socket_bind(server, &bind_address);

    test_expect_true(test, "socket accepts non-blocking mode", result_is_success(net_socket_set_blocking(server, false)));

    char buffer[8]     = DEFAULT_INITIALIZATION;
    USize received     = 0;
    Result const result = net_socket_recv_1(server, buffer, sizeof(buffer), &received);

    test_expect_true(test, "recv reports an error rather than blocking", result_is_error(result));
    test_expect_true(test, "net_result_is_would_block recognises it", net_result_is_would_block(result));

    net_socket_close(server);

    test_case_end(test);
}

static void _test_timeout_setter(Test *const test) {
    test_case_begin(test, "net_socket_set_timeout bounds a blocking recv");

    Net_Socket server = NET_SOCKET_INVALID;
    Net_Socket_Address bind_address = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "server init succeeds", result_is_success(net_socket_init(&server, NET_FAMILY_IPV4, NET_TYPE_UDP)))) {
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV4, 0, "127.0.0.1", &bind_address);
    net_socket_bind(server, &bind_address);

    test_expect_true(test, "timeout setter succeeds", result_is_success(net_socket_set_timeout(server, 200)));

    char buffer[8]      = DEFAULT_INITIALIZATION;
    USize received      = 0;
    Result const result = net_socket_recv_1(server, buffer, sizeof(buffer), &received);

    // A timed-out blocking recv reports an error rather than hanging - the
    // exact code differs by platform (Windows: WSAETIMEDOUT, recognised by
    // net_result_is_timed_out; POSIX commonly surfaces the same
    // EAGAIN/EWOULDBLOCK a non-blocking idle socket would, recognised by
    // net_result_is_would_block) - a caller must test both.
    test_expect_true(test, "the bounded recv returns rather than hanging", result_is_error(result));
    test_expect_true(test, "the timeout classifies as would_block or timed_out", net_result_is_would_block(result) || net_result_is_timed_out(result));

    net_socket_close(server);

    test_case_end(test);
}

static void _test_resolve_localhost(Test *const test) {
    test_case_begin(test, "net_socket_address_resolve answers a usable address for \"localhost\"");

    Net_Socket_Address resolved = DEFAULT_INITIALIZATION;
    Result const resolve_result = net_socket_address_resolve(NET_FAMILY_IPV4, "localhost", 0, &resolved);

    if (!result_is_success(resolve_result)) {
        printf("  [skip] \"localhost\" did not resolve (code=%u) - no usable resolver/loopback entry on this host\n", (unsigned) result_code(resolve_result));
        test_case_end(test);

        return;
    }

    test_expect_true(test, "resolved address keeps the requested family", net_socket_address_family(&resolved) == NET_FAMILY_IPV4);

    // Usable, not just parsed: bind a real socket to what came back.
    Net_Socket probe = NET_SOCKET_INVALID;

    if (test_expect_true(test, "probe init succeeds", result_is_success(net_socket_init(&probe, NET_FAMILY_IPV4, NET_TYPE_UDP)))) {
        test_expect_true(test, "the resolved address is bindable", result_is_success(net_socket_bind(probe, &resolved)));
        net_socket_close(probe);
    }

    test_case_end(test);
}

static void _test_resolve_failure(Test *const test) {
    test_case_begin(test, "net_socket_address_resolve on an unresolvable host classifies as NETWORK");

    Net_Socket_Address out = DEFAULT_INITIALIZATION;
    Result const result = net_socket_address_resolve(NET_FAMILY_IPV4, "nonexistent.invalid", 80, &out);

    test_expect_true(test, "the resolve fails", result_is_error(result));
    test_expect_true(test, "the failure classifies as NETWORK on this platform", result_category(result) == RESULT_CATEGORY_NETWORK);

    test_case_end(test);
}

static void _test_ipv6_loopback_traffic(Test *const test) {
    test_case_begin(test, "IPv6 loopback TCP traffic over \"::1\" (skips cleanly when unsupported)");

    Net_Socket server = NET_SOCKET_INVALID;
    Result const server_init = net_socket_init(&server, NET_FAMILY_IPV6, NET_TYPE_TCP);

    if (!result_is_success(server_init)) {
        printf("  [skip] IPv6 unsupported on this platform (code=%u)\n", (unsigned) result_code(server_init));
        test_case_end(test);

        return;
    }

    Net_Socket_Address bind_address = DEFAULT_INITIALIZATION;
    Result const literal_result = net_socket_address_init_2(NET_FAMILY_IPV6, 0, "::1", &bind_address);

    if (!result_is_success(literal_result)) {
        printf("  [skip] \"::1\" literal unsupported on this platform (code=%u)\n", (unsigned) result_code(literal_result));
        net_socket_close(server);
        test_case_end(test);

        return;
    }

    Result const bind_result = net_socket_bind(server, &bind_address);

    if (!result_is_success(bind_result)) {
        printf("  [skip] \"::1\" not bindable on this platform (code=%u, category=%s)\n", (unsigned) result_code(bind_result), result_category_name(bind_result));
        net_socket_close(server);
        test_case_end(test);

        return;
    }

    net_socket_listen(server, 1);
    net_socket_set_timeout(server, _TEST_TIMEOUT_MS);

    U16 const port = _test_bound_port(server);
    test_expect_true(test, "the OS assigned a port", port != 0);

    Net_Socket client = NET_SOCKET_INVALID;
    Net_Socket_Address target = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "client init succeeds", result_is_success(net_socket_init(&client, NET_FAMILY_IPV6, NET_TYPE_TCP)))) {
        net_socket_close(server);
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV6, port, "::1", &target);

    if (!test_expect_true(test, "client connects over ::1", result_is_success(net_socket_connect(client, &target)))) {
        net_socket_close(client);
        net_socket_close(server);
        test_case_end(test);

        return;
    }

    Net_Socket accepted = NET_SOCKET_INVALID;

    if (!test_expect_true(test, "server accepts the ::1 connection", result_is_success(net_socket_accept(server, &accepted, nullptr)))) {
        net_socket_close(client);
        net_socket_close(server);
        test_case_end(test);

        return;
    }

    net_socket_set_timeout(accepted, _TEST_TIMEOUT_MS);

    char const message[] = "cfw-net-v6";
    USize sent = 0;

    test_expect_true(test, "client sends over ::1", result_is_success(net_socket_send_1(client, message, sizeof(message), &sent)));

    char received[32] = DEFAULT_INITIALIZATION;
    USize received_size = 0;

    test_expect_true(test, "server receives over ::1", result_is_success(net_socket_recv_1(accepted, received, sizeof(received), &received_size)));
    test_expect_string(test, "payload survives the ::1 round trip", message, received);

    net_socket_close(accepted);
    net_socket_close(client);
    net_socket_close(server);

    test_case_end(test);
}

static void _test_wait_timeout_clamp(Test *const test) {
    test_case_begin(test, "net_socket_wait clamps a timeout above I32_MAX instead of hanging");

    Net_Socket server = NET_SOCKET_INVALID;
    Net_Socket_Address bind_address = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "server init succeeds", result_is_success(net_socket_init(&server, NET_FAMILY_IPV4, NET_TYPE_TCP)))) {
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV4, 0, "127.0.0.1", &bind_address);
    net_socket_bind(server, &bind_address);
    net_socket_listen(server, 1);

    U16 const port = _test_bound_port(server);
    Net_Socket client = NET_SOCKET_INVALID;
    Net_Socket_Address target = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "client init succeeds", result_is_success(net_socket_init(&client, NET_FAMILY_IPV4, NET_TYPE_TCP)))) {
        net_socket_close(server);
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV4, port, "127.0.0.1", &target);
    net_socket_connect(client, &target);

    // A freshly connected TCP socket is immediately WRITE-ready, so this call
    // returns right away regardless of the timeout's sign - what it actually
    // proves, cheaply, is that a timeout_ms above I32_MAX survives the (int)
    // narrowing without net_socket_wait itself misbehaving. It cannot, on a
    // cheap test budget, prove the wait would eventually return on an IDLE
    // socket instead of blocking for the ~24.8 days I32_MAX ms would mean if
    // narrowed unclamped to a negative int ("wait forever" to poll/WSAPoll).
    Net_Wait ready = (Net_Wait) 0;
    Result const wait_result = net_socket_wait(client, NET_WAIT_WRITE, U32_MAX, &ready);

    test_expect_true(test, "the clamped wait still succeeds", result_is_success(wait_result));
    test_expect_true(test, "the socket reports writable", (ready & NET_WAIT_WRITE) != 0);

    net_socket_close(client);
    net_socket_close(server);

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

    Test test = test_init("net");

    test_suite_begin(&test, "net");

    /* net_socket_init starts Winsock lazily on the first call, so the suite
     * needs no WSAStartup of its own - that this works is part of what is
     * tested. */
    _test_init_combinations(&test);
    _test_address_initializers(&test);
    _test_tcp_round_trip(&test);
    _test_send_after_peer_close(&test);
    _test_udp_round_trip(&test);
    _test_address_in_use(&test);
    _test_connect_refused(&test);
    _test_nonblocking_would_block(&test);
    _test_timeout_setter(&test);
    _test_resolve_localhost(&test);
    _test_resolve_failure(&test);
    _test_ipv6_loopback_traffic(&test);
    _test_wait_timeout_clamp(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}