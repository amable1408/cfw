/*
 * net.h - Cross-platform BSD-socket abstraction for the C Libraries Framework
 *
 * Features:
 *   - Cross-platform (Windows and POSIX) socket abstraction: one Net_Socket
 *     handle type, one Net_Socket_Address type wide enough for IPv4 and IPv6
 *   - Full client + server surface: init/bind/listen/accept/connect,
 *     send/recv for both TCP and UDP, shutdown/close
 *   - Setters for the platform-specific option seams: timeout, SO_REUSEADDR,
 *     blocking mode, TCP_NODELAY
 *   - Address helpers: wildcard/literal construction, DNS resolution,
 *     text formatting, equality, and getters - all IPv4/IPv6-agnostic
 *   - net_socket_wait: single-socket readiness over poll/WSAPoll
 *
 * Usage Example (loopback TCP echo - first block Result-checked; the rest
 * elided below for brevity, not because they cannot fail):
 *   @code
 *   Net_Socket server = NET_SOCKET_INVALID;
 *   Net_Socket_Address address = net_socket_address_init_1(NET_FAMILY_IPV4, 0);
 *
 *   if (result_is_error(net_socket_init(&server, NET_FAMILY_IPV4, NET_TYPE_TCP))
 *       || result_is_error(net_socket_bind(server, &address))
 *       || result_is_error(net_socket_listen(server, 8))
 *       || result_is_error(net_socket_address_local(server, &address))) {
 *       return; // any step above may fail under load - abandon the attempt
 *   }
 *
 *   Net_Socket client = NET_SOCKET_INVALID;
 *   Net_Socket_Address target = DEFAULT_INITIALIZATION;
 *
 *   net_socket_init(&client, NET_FAMILY_IPV4, NET_TYPE_TCP);
 *   net_socket_address_init_2(NET_FAMILY_IPV4, net_socket_address_port(&address), "127.0.0.1", &target);
 *   net_socket_connect(client, &target);
 *
 *   Net_Socket accepted = NET_SOCKET_INVALID;
 *   net_socket_accept(server, &accepted, nullptr);
 *
 *   USize sent = 0;
 *   net_socket_send_1(client, "ping", 4, &sent);
 *
 *   char buffer[16] = DEFAULT_INITIALIZATION;
 *   USize received = 0;
 *   net_socket_recv_1(accepted, buffer, sizeof(buffer), &received); // SUCCESS + 0 = peer closed
 *
 *   net_socket_close(accepted);
 *   net_socket_close(client);
 *   net_socket_close(server);
 *   @endcode
 *
 * Error Handling:
 *   Every OS-facing call returns a Result (result.h) - the type IS the error
 *   channel; there is no separate try/abort pair. A failing OS call is
 *   captured via result_from_os() immediately after it returns, so its
 *   category and code reach the caller intact. net_result_is_would_block,
 *   net_result_is_timed_out, net_result_is_address_in_use and
 *   net_result_is_closed classify the common outcomes without a caller ever
 *   comparing a platform errno/WSA code directly.
 *
 *   error_check_* aborts under ERROR_CHECK_ENABLED for CONTRACT VIOLATIONS
 *   only: a null out-param or address pointer, a zero RECEIVE size, or a
 *   Net_Family/Net_Type value outside its two legal enumerators. These are
 *   programming errors, never runtime outcomes - a bad address LITERAL
 *   ("999.1.1.1", "", garbage) is a VALUE, not a contract violation, so
 *   net_socket_address_init_2 returns RESULT_CATEGORY_ARGUMENT for one rather
 *   than aborting or silently falling back to a wildcard. An address whose
 *   `size` field cannot describe real storage is the same kind of VALUE
 *   problem (bind/connect/send_2, and net_socket_address_resolve's own
 *   getaddrinfo result) - refused as RESULT_CATEGORY_ARGUMENT
 *   (NET_ARGUMENT_BAD_ADDRESS_SIZE), never aborted. A zero SEND size is a
 *   legal no-op (see net_socket_send_1/_2 below), not a contract violation
 *   either.
 *
 *   With ERROR_CHECK_ENABLED undefined, every check above compiles to
 *   nothing and its guarded dereference becomes undefined behaviour: a null
 *   out-param is written through, an invalid Net_Family/Net_Type reaches the
 *   OS call unvalidated, a zero-size recv proceeds. This is a shipped
 *   configuration (tests/net/test_unchecked.c exercises it), not a
 *   theoretical build. The address-size and zero-send-size refusals above are
 *   VALUE checks, not error_check_*, so they hold in this build too.
 *
 *   net_socket_recv_1/_2: RESULT_SUCCESS with *received == 0 is an orderly
 *   TCP close by the peer, not an error - stop reading, do not retry. On UDP
 *   the same SUCCESS + 0 is a legal zero-byte datagram; the two are
 *   indistinguishable from the return alone, which is inherent to the
 *   underlying recv()/recvfrom() contract, not something this wrapper adds.
 *
 * Thread Safety:
 *   Not thread-safe: a Net_Socket must not be used concurrently from more
 *   than one thread without external synchronization. The one exception is
 *   the lazy Winsock start inside net_socket_init, which is safe to race.
 *
 * Memory Management:
 *   No heap allocation anywhere in this module. Net_Socket is a plain
 *   integer/handle value and Net_Socket_Address is a plain value type (a
 *   fixed-size struct sockaddr_storage plus a length) - both are copied
 *   freely and need no cleanup call.
 *
 * Platform:
 *   Windows links -lws2_32. Winsock is started lazily, once per process, on
 *   the first net_socket_init call - no explicit init step. There is
 *   deliberately no WSACleanup: the module cannot know when the last user is
 *   finished, and the single reference is released by the OS at process
 *   exit. This coexists with a consumer's own WSAStartup/WSACleanup pair -
 *   Winsock reference-counts them - so linking a library that manages its
 *   own lifecycle alongside this module is safe. A failed WSAStartup returns
 *   RESULT_CATEGORY_SYSTEM and leaves the internal flag false, so the next
 *   net_socket_init call retries rather than being wedged forever.
 *
 *   This module's SIGPIPE suppression (net_socket_send_1/_2) targets Windows
 *   (no such signal) and Linux (MSG_NOSIGNAL) only. macOS/BSD lack
 *   MSG_NOSIGNAL and instead need SO_NOSIGPIPE set at socket-creation time,
 *   which this module does not do - a peer-closed send there can still raise
 *   SIGPIPE and terminate the process.
 *
 * Performance Characteristics:
 *   Every function is a thin wrapper over exactly one syscall (two for
 *   net_socket_set_timeout, which sets SO_RCVTIMEO and SO_SNDTIMEO
 *   separately). net_socket_send_1/_2 and net_socket_recv_1/_2 clamp their
 *   size to I32_MAX per call, since the underlying API takes a signed int
 *   length - the size is never narrowed or truncated, only chunked; a
 *   caller moving more than that in one go loops on the sent/received
 *   out-param, the same shape as a partial write.
 *
 * Dependencies:
 *   - <types.h>, <result.h>, <error/error.h>
 *   - Windows: <platform/windows/windows.h> (winsock2.h, ws2tcpip.h)
 *   - POSIX: <arpa/inet.h>, <fcntl.h>, <netdb.h>, <netinet/in.h>,
 *     <netinet/tcp.h>, <poll.h>, <sys/socket.h>, <unistd.h>
 *
 * See docs/modules/net.html for the full contract and worked examples.
 */
#ifndef NET_H
#define NET_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <error/error.h>
#include <result.h>
#include <types.h>

#ifdef OS_WINDOWS
#include <platform/windows/windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/*==============================================================================
 * MARK: - Types
 *============================================================================*/
#ifdef OS_WINDOWS
/** @brief Socket handle type for Windows (SOCKET). */
typedef SOCKET Net_Socket;
#else
/** @brief Socket handle type for POSIX (int). fd 0 is a VALID socket - never treat it as "unset"; use NET_SOCKET_INVALID for that. */
typedef int Net_Socket;
#endif

/** @brief Address length type carried alongside a Net_Socket_Address. */
typedef socklen_t Net_Socket_Len;

/**
 * @brief Address family. Values equal AF_INET/AF_INET6/AF_UNSPEC on both
 *        platforms (same numeric constants), so no per-platform mapping is
 *        needed. NET_FAMILY_ANY is legal only for net_socket_address_resolve
 *        ("either family") - every other function that takes a Net_Family
 *        requires IPV4 or IPV6 and aborts on ANY under ERROR_CHECK_ENABLED.
 */
typedef enum Net_Family : U8 {
    NET_FAMILY_ANY  = AF_UNSPEC,
    NET_FAMILY_IPV4 = AF_INET,
    NET_FAMILY_IPV6 = AF_INET6
} Net_Family;

/** @brief Socket transport type. Values equal SOCK_STREAM/SOCK_DGRAM on both platforms. */
typedef enum Net_Type : U8 {
    NET_TYPE_TCP = SOCK_STREAM,
    NET_TYPE_UDP = SOCK_DGRAM
} Net_Type;

/**
 * @brief Which half of a full-duplex connection net_socket_shutdown closes.
 *        SHUT_RD/WR/RDWR (POSIX) and SD_RECEIVE/SEND/BOTH (Windows) share the
 *        same numeric values (0/1/2), so one literal definition serves both
 *        platforms - documented for clarity, not derived from either
 *        platform's own macros.
 */
typedef enum Net_Shutdown : U8 {
    NET_SHUTDOWN_READ  = 0,
    NET_SHUTDOWN_WRITE = 1,
    NET_SHUTDOWN_BOTH  = 2
} Net_Shutdown;

/** @brief Readiness flags for net_socket_wait; OR together to wait on both. */
typedef enum Net_Wait : U8 {
    NET_WAIT_READ  = 1U << 0,
    NET_WAIT_WRITE = 1U << 1
} Net_Wait;

/**
 * @brief A socket address wide enough for IPv4 or IPv6, with its real length
 *        carried alongside it. Bind/accept/connect/send_2/recv_2 all read or
 *        write `size` honestly instead of a caller guessing sizeof(Net_Socket_Address).
 */
typedef struct Net_Socket_Address {
    struct sockaddr_storage storage;
    Net_Socket_Len size;
} Net_Socket_Address;

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/
/**
 * @brief Result code (RESULT_CATEGORY_ARGUMENT) for an address whose `size`
 *        cannot describe real storage (zero, or larger than
 *        sockaddr_storage) - a bind/connect/send_2 address, or an
 *        address_resolve() result, with a corrupt or malicious length.
 */
#define NET_ARGUMENT_BAD_ADDRESS_SIZE 2U

/** @brief Result code (RESULT_CATEGORY_ARGUMENT) for a syntactically bad address literal. */
#define NET_ARGUMENT_BAD_LITERAL 1U

/** @brief Buffer size for net_socket_address_format: the platform's own INET6_ADDRSTRLEN (65 on MinGW, 46 on POSIX) + brackets + ':' + 5 port digits + NUL, rounded up - derived, not a literal, so it tracks whichever platform is building. */
#define NET_SOCKET_ADDRESS_TEXT_SIZE (INET6_ADDRSTRLEN + 10)

#ifdef OS_WINDOWS
/** @brief The "no socket" sentinel value. fd/handle 0 is a legitimate socket - never compare against 0 to mean "unset". */
#define NET_SOCKET_INVALID ((Net_Socket) INVALID_SOCKET)
#else
/** @brief The "no socket" sentinel value. fd/handle 0 is a legitimate socket - never compare against 0 to mean "unset". */
#define NET_SOCKET_INVALID ((Net_Socket) -1)
#endif

/*==============================================================================
 * MARK: - Socket API
 *============================================================================*/
/**
 * @brief Accept a pending connection on a listening socket.
 * @param self         Listening socket.
 * @param out          Receives the new connected socket. Untouched on failure.
 * @param peer_or_null When non-null, receives the peer's address on success;
 *                      on failure its `size` field is still written (set to
 *                      sizeof(storage) before the call) but `storage` is
 *                      unspecified - do not read it after a failing Result.
 * @return RESULT_SUCCESS, or a Result classifying accept()'s failure.
 */
Result net_socket_accept(Net_Socket const self, Net_Socket *const out, Net_Socket_Address *const peer_or_null);

/**
 * @brief Get the address a socket is bound to (getsockname).
 * @param self Socket, ideally already bound.
 * @param out  Receives the local address.
 * @return RESULT_SUCCESS, or a Result classifying getsockname()'s failure.
 */
Result net_socket_address_local(Net_Socket const self, Net_Socket_Address *const out);

/**
 * @brief Bind a socket to an address.
 * @param self    Socket.
 * @param address Address to bind to; read-only.
 * @return RESULT_SUCCESS, RESULT_CATEGORY_ARGUMENT (NET_ARGUMENT_BAD_ADDRESS_SIZE)
 *         when address->size is zero or larger than sockaddr_storage, or a
 *         Result classifying bind()'s failure - net_result_is_address_in_use
 *         recognises a port already taken.
 */
Result net_socket_bind(Net_Socket const self, Net_Socket_Address const *const address);

/**
 * @brief Close a socket. Never fails in a way a caller can act on, so this stays void, matching close()/closesocket() being a one-way release.
 * @param self Socket to close.
 */
void net_socket_close(Net_Socket const self);

/**
 * @brief Connect a socket to a remote address.
 * @param self    Socket.
 * @param address Address to connect to; read-only.
 * @return RESULT_SUCCESS, RESULT_CATEGORY_ARGUMENT (NET_ARGUMENT_BAD_ADDRESS_SIZE)
 *         when address->size is zero or larger than sockaddr_storage, or a
 *         Result classifying connect()'s failure - net_result_is_closed
 *         recognises a refused/aborted connection.
 * @note On a UDP socket this sets the default peer rather than opening a
 *       connection: net_socket_send_1/net_socket_recv_1 then work without a
 *       destination address, and a datagram from any other peer is dropped.
 */
Result net_socket_connect(Net_Socket const self, Net_Socket_Address const *const address);

/**
 * @brief Create and initialize a socket.
 * @param out    Receives the new socket. Untouched on failure.
 * @param family NET_FAMILY_IPV4 or NET_FAMILY_IPV6.
 * @param type   NET_TYPE_TCP or NET_TYPE_UDP.
 * @return RESULT_SUCCESS, or a Result classifying socket()'s failure (or, on
 *         Windows, a lazy WSAStartup's failure - see the Platform section).
 */
Result net_socket_init(Net_Socket *const out, Net_Family const family, Net_Type const type);

/**
 * @brief Mark a bound socket as listening for incoming connections.
 * @param self    Socket, already bound.
 * @param backlog Maximum pending connection queue length.
 * @return RESULT_SUCCESS, or a Result classifying listen()'s failure.
 */
Result net_socket_listen(Net_Socket const self, U16 const backlog);

/**
 * @brief Receive data from a connected (TCP) or peer-set (UDP) socket.
 * @param self     Socket.
 * @param buffer   Destination buffer.
 * @param size     Buffer size; must be non-zero (a contract violation, not a
 *                 value outcome) - unlike send, a zero recv has no useful
 *                 meaning to return. Clamped to I32_MAX per call - see Performance.
 * @param received Receives the byte count actually read. RESULT_SUCCESS with
 *                 *received == 0 is an orderly TCP close by the peer; on UDP
 *                 the same outcome is a legal zero-byte datagram. Set to 0 on
 *                 failure (unlike accept's peer_or_null, there is nothing
 *                 partial to preserve).
 * @return RESULT_SUCCESS, or a Result classifying recv()'s failure -
 *         net_result_is_would_block recognises a nonblocking idle socket.
 */
Result net_socket_recv_1(Net_Socket const self, void *const buffer, USize const size, USize *const received);

/**
 * @brief Receive a datagram and its sender address (recvfrom).
 * @param self     Socket.
 * @param buffer   Destination buffer.
 * @param size     Buffer size; same non-zero and I32_MAX-clamp contract as net_socket_recv_1.
 * @param received Receives the byte count actually read; see net_socket_recv_1 for the SUCCESS + 0 cases and the set-to-0-on-failure rule.
 * @param from     Receives the sender's address on success; on failure its
 *                 `size` field is still written but `storage` is unspecified
 *                 - do not read it after a failing Result.
 * @return RESULT_SUCCESS, or a Result classifying recvfrom()'s failure.
 * @note Windows returns WSAEMSGSIZE (datagram discarded) when the sender's
 *       datagram exceeds `size`; POSIX instead truncates it silently with
 *       RESULT_SUCCESS. On Windows a prior send's ICMP port-unreachable can
 *       also surface here later as WSAECONNRESET on a UDP socket.
 */
Result net_socket_recv_2(Net_Socket const self, void *const buffer, USize const size, USize *const received, Net_Socket_Address *const from);

/**
 * @brief Send data on a connected (TCP) or peer-set (UDP) socket.
 * @param self Socket.
 * @param data Buffer to send; read-only.
 * @param size Byte count to send; never an error_check on the value - zero is
 *             passed through to send() unmodified: a legal zero-byte
 *             datagram on UDP, and on TCP send()'s own well-defined no-op,
 *             which can still report a connection error rather than silently
 *             succeeding (the syscall runs either way). Otherwise clamped to
 *             I32_MAX per call - a caller moving more loops on *sent, same
 *             shape as a partial write.
 * @param sent Receives the byte count actually written; may be less than
 *             size, and is set to 0 on failure.
 * @return RESULT_SUCCESS, or a Result classifying send()'s failure -
 *         net_result_is_closed recognises a peer that reset the connection.
 */
Result net_socket_send_1(Net_Socket const self, void const *const data, USize const size, USize *const sent);

/**
 * @brief Send a datagram to an explicit address (sendto).
 * @param self Socket.
 * @param data Buffer to send; read-only.
 * @param size Byte count to send; same pass-through-zero-to-sendto() and I32_MAX-clamp contract as net_socket_send_1.
 * @param to   Destination address; read-only.
 * @param sent Receives the byte count actually written; set to 0 on failure.
 * @return RESULT_SUCCESS, RESULT_CATEGORY_ARGUMENT (NET_ARGUMENT_BAD_ADDRESS_SIZE)
 *         when to->size is zero or larger than sockaddr_storage, or a Result
 *         classifying sendto()'s failure.
 */
Result net_socket_send_2(Net_Socket const self, void const *const data, USize const size, Net_Socket_Address const *const to, USize *const sent);

/**
 * @brief Set a socket's blocking mode.
 * @param self    Socket.
 * @param enabled true for blocking (the default), false for non-blocking (FIONBIO / O_NONBLOCK).
 * @return RESULT_SUCCESS, or a Result classifying the underlying call's failure.
 */
Result net_socket_set_blocking(Net_Socket const self, bool const enabled);

/**
 * @brief Set or clear TCP_NODELAY (disable/enable Nagle's algorithm). No-op semantics on UDP are left to the OS.
 * @param self    Socket.
 * @param enabled true disables Nagle's algorithm (lower latency, more small packets).
 * @return RESULT_SUCCESS, or a Result classifying setsockopt()'s failure.
 */
Result net_socket_set_nodelay(Net_Socket const self, bool const enabled);

/**
 * @brief Set or clear SO_REUSEADDR.
 * @param self    Socket, before binding.
 * @param enabled true allows a quick rebind of a recently-closed address.
 * @return RESULT_SUCCESS, or a Result classifying setsockopt()'s failure.
 */
Result net_socket_set_reuse_address(Net_Socket const self, bool const enabled);

/**
 * @brief Set both the receive and send timeout (SO_RCVTIMEO and SO_SNDTIMEO)
 *        to the same value. The payload differs per platform - a DWORD
 *        millisecond count on Windows, a struct timeval on POSIX - and is
 *        built separately for each; this is exactly the seam the module
 *        exists to hide.
 * @param self       Socket.
 * @param timeout_ms Timeout in milliseconds; 0 restores blocking-forever semantics.
 * @return RESULT_SUCCESS, or a Result classifying setsockopt()'s failure.
 * @note An expired timeout reads differently per platform: Windows reports
 *       net_result_is_timed_out (WSAETIMEDOUT), POSIX commonly reports
 *       net_result_is_would_block (EAGAIN/EWOULDBLOCK) - test both.
 */
Result net_socket_set_timeout(Net_Socket const self, U32 const timeout_ms);

/**
 * @brief Shut down one or both halves of a full-duplex connection.
 * @param self Socket.
 * @param how  Which half(ves) to close.
 * @return RESULT_SUCCESS, or a Result classifying shutdown()'s failure.
 */
Result net_socket_shutdown(Net_Socket const self, Net_Shutdown const how);

/**
 * @brief Wait for one socket to become readable and/or writable (poll/WSAPoll, one descriptor).
 * @param self       Socket.
 * @param flags      NET_WAIT_READ and/or NET_WAIT_WRITE, OR'd together; zero
 *                    is a contract violation (aborts under
 *                    ERROR_CHECK_ENABLED) - there is no explicit "forever"
 *                    spelling other than a large timeout_ms.
 * @param timeout_ms How long to wait; 0 polls once without blocking. Clamped
 *                    to I32_MAX before the underlying int cast (~24.8 days),
 *                    so a huge value can never turn into "wait forever".
 * @param ready      Receives which of the requested flags are actually ready;
 *                    0 on a timeout with no event - that is still RESULT_SUCCESS, not an error.
 *                    POLLERR/POLLHUP/POLLNVAL are folded into NET_WAIT_READ,
 *                    so the caller's next recv() surfaces the real error.
 * @return RESULT_SUCCESS, or a Result classifying poll()'s failure.
 * @note Windows builds before 10 version 2004 have a known WSAPoll defect:
 *       it does not report a failed non-blocking connect() as writable/error.
 */
Result net_socket_wait(Net_Socket const self, Net_Wait const flags, U32 const timeout_ms, Net_Wait *const ready);

/*==============================================================================
 * MARK: - Address API
 *============================================================================*/
/**
 * @brief Compare two addresses for equality (family, IP, and port).
 * @param a First address.
 * @param b Second address.
 * @return true when both addresses have the same family, IP, and port.
 */
bool net_socket_address_equal(Net_Socket_Address const *const a, Net_Socket_Address const *const b);

/**
 * @brief Read an address's family back out.
 * @param address Address.
 * @return NET_FAMILY_IPV4 or NET_FAMILY_IPV6; a default-initialized (zeroed)
 *         Net_Socket_Address reads back as NET_FAMILY_ANY (0), matching
 *         neither - net_socket_address_equal/_format/_port instead treat any
 *         non-IPv6 family (including a zeroed one) as IPv4.
 */
Net_Family net_socket_address_family(Net_Socket_Address const *const address);

/**
 * @brief Format an address as "ip:port" (IPv4) or "[ip]:port" (IPv6) via inet_ntop.
 * @param address Address to format; read-only.
 * @param buffer  Caller buffer of exactly NET_SOCKET_ADDRESS_TEXT_SIZE bytes.
 * @return buffer, for inline use in a log/printf call.
 */
char const* net_socket_address_format(Net_Socket_Address const *const address, char buffer[static NET_SOCKET_ADDRESS_TEXT_SIZE]);

/**
 * @brief Build a wildcard address (INADDR_ANY / in6addr_any) for a given family and port.
 * @param family NET_FAMILY_IPV4 or NET_FAMILY_IPV6.
 * @param port   Port number (host order).
 * @return The wildcard address - always succeeds for a legal family, so this returns by value, not Result.
 */
Net_Socket_Address net_socket_address_init_1(Net_Family const family, U16 const port);

/**
 * @brief Build an address from a numeric IP literal (e.g. "127.0.0.1", "::1").
 * @param family  NET_FAMILY_IPV4 or NET_FAMILY_IPV6 - must match the literal's own form.
 * @param port    Port number (host order).
 * @param literal Numeric IP text; a hostname is not accepted here (see net_socket_address_resolve).
 * @param out     Receives the parsed address. Untouched on failure.
 * @return RESULT_SUCCESS, or RESULT_CATEGORY_ARGUMENT (code NET_ARGUMENT_BAD_LITERAL)
 *         when the literal does not parse for the given family - never a
 *         silent wildcard fallback.
 */
Result net_socket_address_init_2(Net_Family const family, U16 const port, char const *const literal, Net_Socket_Address *const out);

/**
 * @brief Read an address's port back out (host order).
 * @param address Address.
 * @return The port number.
 */
U16 net_socket_address_port(Net_Socket_Address const *const address);

/**
 * @brief Resolve a hostname (or numeric literal) and port to an address via getaddrinfo.
 * @param family NET_FAMILY_IPV4, NET_FAMILY_IPV6, or NET_FAMILY_ANY to accept
 *               either - the only three legal values (a contract violation,
 *               aborts under ERROR_CHECK_ENABLED, on anything else).
 * @param host   Hostname or numeric literal to resolve.
 * @param port   Port number (host order).
 * @param out    Receives the first resolved address. Untouched on failure.
 * @return RESULT_SUCCESS, RESULT_CATEGORY_ARGUMENT (NET_ARGUMENT_BAD_ADDRESS_SIZE)
 *         when the resolver's own answer cannot fit sockaddr_storage, or a
 *         Result classifying getaddrinfo()'s failure, mapped at the call site
 *         (not through the shared errno/WSA classifier, which has no EAI_*
 *         route): RESULT_CATEGORY_NETWORK for "host not found"
 *         (EAI_NONAME / WSAHOST_NOT_FOUND) and for "name exists, no address of
 *         the requested family" (EAI_NODATA where the platform defines it /
 *         WSANO_DATA) and for a non-recoverable server failure
 *         (EAI_FAIL / WSANO_RECOVERY, no flags), NETWORK with
 *         RETRYABLE|TRANSIENT for a transient resolver failure (EAI_AGAIN /
 *         WSATRY_AGAIN), SYSTEM with CRITICAL for a resolver allocation
 *         failure (EAI_MEMORY, POSIX only), and on POSIX EAI_SYSTEM is routed
 *         through result_from_os() (its own message is "see errno") - the
 *         code field always carries the failure's magnitude, never a
 *         truncated negative EAI_* value.
 * @note The lookup hints SOCK_STREAM (ai_socktype), matching this module's
 *       Socket_Addr shape; the resolved address is equally usable for UDP -
 *       getaddrinfo's socktype hint only narrows which of a host's addresses
 *       are returned, it does not bind the address to one transport.
 */
Result net_socket_address_resolve(Net_Family const family, char const *const host, U16 const port, Net_Socket_Address *const out);

/*==============================================================================
 * MARK: - Result Predicates
 *============================================================================*/
/**
 * @brief Whether a Result from this module means "bind failed: address already in use".
 * @param result A Result returned by net_socket_bind.
 * @return true for EADDRINUSE / WSAEADDRINUSE.
 */
bool net_result_is_address_in_use(Result const result);

/**
 * @brief Whether a Result from this module means "the peer reset, refused, or aborted the connection".
 * @param result A Result returned by net_socket_connect, net_socket_send_1/_2, or net_socket_recv_1/_2.
 * @return true for ECONNRESET/ECONNREFUSED/ECONNABORTED/EPIPE (or their WSA equivalents).
 */
bool net_result_is_closed(Result const result);

/**
 * @brief Whether a Result from this module means "the operation timed out".
 * @param result A Result returned by any net_socket_* call on a socket with
 *               net_socket_set_timeout applied.
 * @return true for WSAETIMEDOUT (Windows) or ETIMEDOUT (POSIX).
 * @note An expired SO_RCVTIMEO/SO_SNDTIMEO reads as net_result_is_would_block
 *       (EAGAIN/EWOULDBLOCK) on POSIX and as net_result_is_timed_out
 *       (WSAETIMEDOUT) on Windows - a caller that only checks one predicate
 *       will miss the timeout on the other platform, so net_socket_set_timeout
 *       callers must test both. connect()'s own timeout is unaffected by this
 *       overlap: a timed-out connect leaves the socket dead, not idle, so
 *       folding it into would_block would be wrong. Exception: Linux honours
 *       SO_SNDTIMEO on a blocking connect() and reports its expiry as
 *       EINPROGRESS, which none of these four predicates match - net_socket_wait
 *       is the portable way to bound a connect's duration instead.
 */
bool net_result_is_timed_out(Result const result);

/**
 * @brief Whether a Result from this module means "no data/connection right now - retry later".
 * @param result A Result returned by any net_socket_* call on a non-blocking socket.
 * @return true for EAGAIN/EWOULDBLOCK (or WSAEWOULDBLOCK).
 */
bool net_result_is_would_block(Result const result);

#endif // NET_H