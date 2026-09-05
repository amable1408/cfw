#include <stdio.h>

#include <log/log.h>
#include <net/net.h>
#include <test/test.h>

/* Built WITHOUT ERROR_CHECK_ENABLED: every error_check_* call in net.c
 * compiles to nothing, so a contract violation (null pointer, bad
 * Net_Family/Net_Type enum, zero size) is undefined behaviour here and is
 * deliberately never exercised by this suite - net.h says so plainly.
 *
 * What MUST still hold with checks off is the VALUE-dependent refusals: a
 * bad address literal is data the caller does not control the shape of, so
 * net_socket_address_init_2 must keep returning RESULT_CATEGORY_ARGUMENT
 * (never a silent wildcard) regardless of this build flag. The same goes for
 * every OS-outcome Result (address-in-use, connection refused) - those are
 * runtime branches on what the OS returned, not error_check_* guards. */

static void _test_literal_refusals_hold(Test *const test) {
    test_case_begin(test, "address literal refusals hold without ERROR_CHECK_ENABLED");

    Net_Socket_Address out = DEFAULT_INITIALIZATION;

    Result const empty_result = net_socket_address_init_2(NET_FAMILY_IPV4, 1, "", &out);
    test_expect_true(test, "empty literal is still refused as ARGUMENT", result_category(empty_result) == RESULT_CATEGORY_ARGUMENT);

    Result const bad_octet_result = net_socket_address_init_2(NET_FAMILY_IPV4, 1, "999.1.1.1", &out);
    test_expect_true(test, "out-of-range literal is still refused as ARGUMENT", result_category(bad_octet_result) == RESULT_CATEGORY_ARGUMENT);

    Result const garbage_result = net_socket_address_init_2(NET_FAMILY_IPV4, 1, "not-an-address", &out);
    test_expect_true(test, "garbage literal is still refused as ARGUMENT", result_category(garbage_result) == RESULT_CATEGORY_ARGUMENT);

    test_expect_true(test, "a valid literal still succeeds", result_is_success(net_socket_address_init_2(NET_FAMILY_IPV4, 1, "127.0.0.1", &out)));

    test_case_end(test);
}

static void _test_address_in_use_holds(Test *const test) {
    test_case_begin(test, "address-in-use holds without ERROR_CHECK_ENABLED");

    Net_Socket holder = NET_SOCKET_INVALID;
    Net_Socket_Address wildcard = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "holder init succeeds", result_is_success(net_socket_init(&holder, NET_FAMILY_IPV4, NET_TYPE_TCP)))) {
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV4, 0, "127.0.0.1", &wildcard);
    net_socket_bind(holder, &wildcard);

    Net_Socket_Address bound = DEFAULT_INITIALIZATION;
    net_socket_address_local(holder, &bound);

    Net_Socket contender = NET_SOCKET_INVALID;
    Net_Socket_Address same_address = DEFAULT_INITIALIZATION;

    if (!test_expect_true(test, "contender init succeeds", result_is_success(net_socket_init(&contender, NET_FAMILY_IPV4, NET_TYPE_TCP)))) {
        net_socket_close(holder);
        test_case_end(test);

        return;
    }

    net_socket_address_init_2(NET_FAMILY_IPV4, net_socket_address_port(&bound), "127.0.0.1", &same_address);

    Result const second_bind = net_socket_bind(contender, &same_address);

    test_expect_true(test, "the second bind still fails", result_is_error(second_bind));
    test_expect_true(test, "net_result_is_address_in_use still recognises it", net_result_is_address_in_use(second_bind));

    net_socket_close(contender);
    net_socket_close(holder);

    test_case_end(test);
}

static void _test_address_size_refusals_hold(Test *const test) {
    test_case_begin(test, "bad address-size refusals hold without ERROR_CHECK_ENABLED");

    Net_Socket probe = NET_SOCKET_INVALID;

    if (!test_expect_true(test, "probe init succeeds", result_is_success(net_socket_init(&probe, NET_FAMILY_IPV4, NET_TYPE_TCP)))) {
        test_case_end(test);

        return;
    }

    Net_Socket_Address zero_size = DEFAULT_INITIALIZATION;
    zero_size.size = 0;

    Result const zero_size_result = net_socket_bind(probe, &zero_size);
    test_expect_true(test, "size == 0 is refused as ARGUMENT, not passed to bind()", result_category(zero_size_result) == RESULT_CATEGORY_ARGUMENT);
    test_expect_u(test, "size == 0 refusal uses NET_ARGUMENT_BAD_ADDRESS_SIZE", (USize) NET_ARGUMENT_BAD_ADDRESS_SIZE, (USize) result_code(zero_size_result));

    Net_Socket_Address oversize = DEFAULT_INITIALIZATION;
    oversize.size = (Net_Socket_Len) (sizeof(oversize.storage) + 1);

    Result const oversize_result = net_socket_connect(probe, &oversize);
    test_expect_true(test, "size > sizeof(storage) is refused as ARGUMENT, not passed to connect()", result_category(oversize_result) == RESULT_CATEGORY_ARGUMENT);
    test_expect_u(test, "oversize refusal uses NET_ARGUMENT_BAD_ADDRESS_SIZE", (USize) NET_ARGUMENT_BAD_ADDRESS_SIZE, (USize) result_code(oversize_result));

    net_socket_close(probe);

    test_case_end(test);
}

static void _test_send_zero_size_holds(Test *const test) {
    test_case_begin(test, "zero-size send stays a legal no-op VALUE, not a contract abort, without ERROR_CHECK_ENABLED");

    Net_Socket sender   = NET_SOCKET_INVALID;
    Net_Socket receiver = NET_SOCKET_INVALID;

    if (!test_expect_true(test, "sender init succeeds", result_is_success(net_socket_init(&sender, NET_FAMILY_IPV4, NET_TYPE_UDP)))
        || !test_expect_true(test, "receiver init succeeds", result_is_success(net_socket_init(&receiver, NET_FAMILY_IPV4, NET_TYPE_UDP)))) {
        net_socket_close(sender);
        net_socket_close(receiver);
        test_case_end(test);

        return;
    }

    Net_Socket_Address wildcard = net_socket_address_init_1(NET_FAMILY_IPV4, 0);

    net_socket_bind(receiver, &wildcard);

    Net_Socket_Address bound = DEFAULT_INITIALIZATION;
    net_socket_address_local(receiver, &bound);

    Net_Socket_Address target = DEFAULT_INITIALIZATION;
    net_socket_address_init_2(NET_FAMILY_IPV4, net_socket_address_port(&bound), "127.0.0.1", &target);
    net_socket_connect(sender, &target);

    USize sent = 123;
    Result const send_result = net_socket_send_1(sender, "", 0, &sent);

    test_expect_true(test, "size == 0 still succeeds - a VALUE no-op, not error_check_*'s territory", result_is_success(send_result));
    test_expect_u(test, "size == 0 still reports *sent == 0", 0, sent);

    net_socket_close(sender);
    net_socket_close(receiver);

    test_case_end(test);
}

static void _test_resolve_size_holds(Test *const test) {
    test_case_begin(test, "resolve's own address-size validation still runs without ERROR_CHECK_ENABLED");

    Net_Socket_Address out = DEFAULT_INITIALIZATION;
    Result const result    = net_socket_address_resolve(NET_FAMILY_IPV4, "127.0.0.1", 9999, &out);

    if (test_expect_true(test, "loopback resolve succeeds", result_is_success(result))) {
        // net.c's resolve rejects an oversized/zero getaddrinfo answer with a
        // plain `if`, not error_check_* - a checks-off build cannot compile
        // that guard away. A normal answer must still fit within it.
        test_expect_true(test, "resolved size fits within sockaddr_storage", out.size > 0 && out.size <= (Net_Socket_Len) sizeof(out.storage));
    }

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

    Test test = test_init("net-unchecked");

    test_suite_begin(&test, "net (ERROR_CHECK_ENABLED off)");

    _test_literal_refusals_hold(&test);
    _test_address_in_use_holds(&test);
    _test_address_size_refusals_hold(&test);
    _test_send_zero_size_holds(&test);
    _test_resolve_size_holds(&test);

    test_suite_end(&test);

    return test_uninit(&test);
}