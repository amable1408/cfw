#include <net/net.h>

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* Linux (and other MSG_NOSIGNAL platforms) raise SIGPIPE by default on a
 * write to a peer-closed socket, which terminates the process unless
 * suppressed - Windows has no such signal, so this is exactly the platform
 * seam the module exists to hide: a peer RST/FIN is a VALUE outcome
 * (net_result_is_closed), never a process kill. macOS/BSD lack MSG_NOSIGNAL
 * and would instead need SO_NOSIGPIPE set at socket-creation time; this
 * module targets Windows and Linux only, so that seam is not covered here. */
#ifdef MSG_NOSIGNAL
#define _NET_SEND_FLAGS MSG_NOSIGNAL
#else
#define _NET_SEND_FLAGS 0
#endif

#ifdef OS_WINDOWS
/* Atomic because two threads can reach net_socket_init concurrently. The flag is
 * set only after WSAStartup returns, so no thread can see true while startup
 * is in flight; atomicity is what keeps the unsynchronized read/write from
 * being a data race in the C memory model. */
static atomic_bool _winsock_started = false;

/**
 * @brief Start Winsock once per process, on first use.
 *
 * Doing this lazily keeps the lifecycle out of the public API - no caller
 * has to remember an init step. A race between two threads would at worst
 * call WSAStartup twice, which is reference counted and harmless. On
 * failure the flag is left false, so the next net_socket_init call retries
 * rather than being wedged forever.
 */
static Result _winsock_ensure_started(void) {
    trace_log_push(LOG_METADATA);

    if (atomic_load(&_winsock_started)) {
        trace_log_pop();

        return RESULT_SUCCESS;
    }

    /* Local, not file-static: nothing ever reads the negotiated details, so
     * keeping it on the stack leaves this module with no shared mutable state
     * beyond the atomic flag - two racing threads then share nothing at all. */
    WSADATA data = DEFAULT_INITIALIZATION;
    I32 const startup = WSAStartup(MAKEWORD(2, 2), &data);

    if (startup != 0) {
        trace_log_pop();

        // WSAStartup's own return value IS the error code (GetLastError is
        // not guaranteed set on this failure), so it is classified directly
        // rather than through result_from_os().
        return result_from_os_code((U32) startup);
    }

    atomic_store(&_winsock_started, true);

    trace_log_pop();

    return RESULT_SUCCESS;
}
#endif // OS_WINDOWS

/**
 * @brief Answer whether a 16-byte IPv6 address is the IPv4-mapped form: ten
 * zero bytes, then 0xff 0xff, then the 4 IPv4 bytes. Both key tiers unwrap
 * that form so "1.2.3.4" and "::ffff:1.2.3.4" are one client, not two.
 */
static bool _net_address_key_mapped(Byte const *const bytes) {
    bool mapped = bytes[10] == 0xff && bytes[11] == 0xff;

    for (USize i = 0; i < 10 && mapped; i += 1) {
        mapped = bytes[i] == 0;
    }

    return mapped;
}

/**
 * @brief Parse the literal both key tiers are handed - text NOT required to
 * be NUL-terminated at `size` - into an address, answering false when it is
 * not a numeric literal at all.
 *
 * Every refusal here is a VALUE answer, not an error_check abort: this text is
 * untrusted (a proxy header, a peer literal), and a data-dependent decision is
 * never an abort primitive. `text` null or empty, and a length no address
 * literal could have (INET6_ADDRSTRLEN is the honest bound), answer false
 * before any parse; so does an embedded NUL within `size` (it would truncate
 * the literal inet_pton sees to something shorter than the caller intended);
 * a bracketed, zone-id, port-suffixed or simply malformed literal answers
 * false through inet_pton itself. The caller then falls back to comparing
 * the raw text.
 */
static bool _net_address_key_parse(char const *const text, USize const size, Net_Socket_Address *const address) {
    if (text == nullptr || size == 0 || size >= INET6_ADDRSTRLEN) {
        return false;
    }

    /* An embedded NUL truncates the literal inet_pton actually sees below `size` - a caller
     * relying on the (text, size) contract could be silently matched against a shorter,
     * different literal. Refuse rather than let the copy hide the mismatch. */
    if (memchr(text, '\0', size) != nullptr) {
        return false;
    }

    /* A NUL-terminated copy, because inet_pton (inside net_socket_address_init_2) reads to a
     * terminator and `text` is explicitly not required to carry one at `size`. */
    char literal[INET6_ADDRSTRLEN] = DEFAULT_INITIALIZATION;

    memcpy(literal, text, size);

    /* A colon never appears in an IPv4 literal, so its presence picks the family to try -
     * net_socket_address_init_2 requires the family to match the literal's own form and does
     * not auto-detect. */
    Net_Family const family = memchr(literal, ':', size) != nullptr ? NET_FAMILY_IPV6 : NET_FAMILY_IPV4;

    return !result_is_error(net_socket_address_init_2(family, 0, literal, address));
}

/**
 * @brief Refuse an address whose `size` cannot possibly describe real
 * storage: zero, or larger than the storage this module ever allocates.
 * Value-dependent, not a caller contract - the size can come from
 * network-adjacent data (net_socket_address_resolve, or memory a caller
 * composed by hand), so it is refused rather than asserted against.
 */
static bool _net_address_valid(Net_Socket_Address const *const address) {
    return address->size != 0 && address->size <= (Net_Socket_Len) sizeof(address->storage);
}

/**
 * @brief Classify a getaddrinfo() failure code at the call site rather than
 * through the shared result_from_os_code() classifier, which is
 * errno/GetLastError shaped and has no route for EAI_* codes - the same
 * call-site-mapping shape as thread.c's pthread trylock codes (2026-08-22
 * ruling): byte-identical categories across platforms for the outcomes a
 * caller actually needs to tell apart, with the raw magnitude preserved in
 * the code field either way.
 */
static Result _net_result_from_resolver(I32 const eai) {
#ifdef OS_WINDOWS
    // Windows: getaddrinfo's return value IS a WSA error code. WSANO_DATA
    // ("name exists, no address of the requested family") and WSANO_RECOVERY
    // (non-recoverable server failure) are both NETWORK, no flags - retrying
    // the same query cannot help either one.
    switch (eai) {
        case WSAHOST_NOT_FOUND:
        case WSANO_DATA:
        case WSANO_RECOVERY:
            return result_make(RESULT_CATEGORY_NETWORK, (U32) eai, 0);
        case WSATRY_AGAIN:
            return result_make(RESULT_CATEGORY_NETWORK, (U32) eai, RESULT_FLAG_RETRYABLE | RESULT_FLAG_TRANSIENT);
        default:
            return result_from_os_code((U32) eai);
    }
#else
    // POSIX: EAI_* codes are a separate, negative space from errno - never
    // fed through result_from_os_code, which would misclassify them. The
    // code field is U16, so the magnitude (not the raw negative value) is
    // what survives - a bare cast of a negative EAI_* code would instead
    // wrap into a garbage code near 65535. EAI_NODATA is guarded: musl and
    // glibc without _GNU_SOURCE fold it into EAI_NONAME already, so the
    // constant may not exist. EAI_SYSTEM means "see errno" - its own eai
    // value carries no information, so it is routed through result_from_os()
    // (thread-local errno), not the magnitude cast the other cases use.
    switch (eai) {
        case EAI_NONAME:
#ifdef EAI_NODATA
        case EAI_NODATA:
#endif
        case EAI_FAIL:
            return result_make(RESULT_CATEGORY_NETWORK, (U32) -eai, 0);
        case EAI_AGAIN:
            return result_make(RESULT_CATEGORY_NETWORK, (U32) -eai, RESULT_FLAG_RETRYABLE | RESULT_FLAG_TRANSIENT);
        case EAI_MEMORY:
            return result_make(RESULT_CATEGORY_SYSTEM, (U32) -eai, RESULT_FLAG_CRITICAL);
        case EAI_SYSTEM:
            return result_from_os();
        default:
            return result_make(RESULT_CATEGORY_ARGUMENT, (U32) -eai, 0);
    }
#endif // OS_WINDOWS
}

Result net_socket_accept(Net_Socket const self, Net_Socket *const out, Net_Socket_Address *const peer_or_null) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out", (void*) out);

    struct sockaddr_storage discarded = DEFAULT_INITIALIZATION;
    Net_Socket_Len discarded_size     = (Net_Socket_Len) sizeof(discarded);
    struct sockaddr *destination      = (struct sockaddr*) &discarded;
    Net_Socket_Len *destination_size  = &discarded_size;

    if (peer_or_null != nullptr) {
        peer_or_null->size = (Net_Socket_Len) sizeof(peer_or_null->storage);
        destination        = (struct sockaddr*) &peer_or_null->storage;
        destination_size   = &peer_or_null->size;
    }

    Net_Socket const accepted = accept(self, destination, destination_size);

    if (accepted == NET_SOCKET_INVALID) {
        trace_log_pop();

        return result_from_os();
    }

    *out = accepted;

    trace_log_pop();

    return RESULT_SUCCESS;
}

Result net_socket_address_local(Net_Socket const self, Net_Socket_Address *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out", (void*) out);

    out->size = (Net_Socket_Len) sizeof(out->storage);

    I32 const result = getsockname(self, (struct sockaddr*) &out->storage, &out->size);

    trace_log_pop();

    return result == 0 ? RESULT_SUCCESS : result_from_os();
}

Result net_socket_bind(Net_Socket const self, Net_Socket_Address const *const address) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "address", (void*) address);

    if (!_net_address_valid(address)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, NET_ARGUMENT_BAD_ADDRESS_SIZE, 0);
    }

    I32 const result = bind(self, (struct sockaddr const*) &address->storage, address->size);

    trace_log_pop();

    return result == 0 ? RESULT_SUCCESS : result_from_os();
}

void net_socket_close(Net_Socket const self) {
    trace_log_push(LOG_METADATA);

#ifdef OS_WINDOWS
    closesocket(self);
#else
    close(self);
#endif // OS_WINDOWS

    trace_log_pop();
}

Result net_socket_connect(Net_Socket const self, Net_Socket_Address const *const address) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "address", (void*) address);

    if (!_net_address_valid(address)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, NET_ARGUMENT_BAD_ADDRESS_SIZE, 0);
    }

    I32 const result = connect(self, (struct sockaddr const*) &address->storage, address->size);

    trace_log_pop();

    return result == 0 ? RESULT_SUCCESS : result_from_os();
}

Result net_socket_init(Net_Socket *const out, Net_Family const family, Net_Type const type) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out", (void*) out);
    // One check per argument: the value must be one of its two legal
    // constants. Testing each constant separately would reject every input,
    // because no value can equal both.
    error_check_wrong_value(LOG_METADATA, "family must be NET_FAMILY_IPV4 or NET_FAMILY_IPV6", family != NET_FAMILY_IPV4 && family != NET_FAMILY_IPV6);
    error_check_wrong_value(LOG_METADATA, "type must be NET_TYPE_TCP or NET_TYPE_UDP", type != NET_TYPE_TCP && type != NET_TYPE_UDP);

#ifdef OS_WINDOWS
    Result const startup = _winsock_ensure_started();

    if (result_is_error(startup)) {
        trace_log_pop();

        return startup;
    }
#endif // OS_WINDOWS

    Net_Socket const created = socket((int) family, (int) type, 0);

    if (created == NET_SOCKET_INVALID) {
        trace_log_pop();

        return result_from_os();
    }

    *out = created;

    trace_log_pop();

    return RESULT_SUCCESS;
}

Result net_socket_listen(Net_Socket const self, U16 const backlog) {
    trace_log_push(LOG_METADATA);

    I32 const result = listen(self, (int) backlog);

    trace_log_pop();

    return result == 0 ? RESULT_SUCCESS : result_from_os();
}

Result net_socket_recv_1(Net_Socket const self, void *const buffer, USize const size, USize *const received) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "buffer", (void*) buffer);
    error_check_null(LOG_METADATA, "received", (void*) received);
    error_check_non_value_uint(LOG_METADATA, "size", size);

    // recv takes a signed int length; a request above I32_MAX is filled in
    // I32_MAX-sized chunks by the CALLER (this wraps one syscall, and reads
    // "up to" size anyway), never narrowed or truncated - see random.c's
    // identical INT_MAX chunking for RAND_bytes.
    USize const chunk = size > (USize) I32_MAX ? (USize) I32_MAX : size;
    I32 const result  = (I32) recv(self, (char*) buffer, (int) chunk, 0);

    if (result < 0) {
        *received = 0;

        trace_log_pop();

        return result_from_os();
    }

    *received = (USize) result;

    trace_log_pop();

    return RESULT_SUCCESS;
}

Result net_socket_recv_2(Net_Socket const self, void *const buffer, USize const size, USize *const received, Net_Socket_Address *const from) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "buffer", (void*) buffer);
    error_check_null(LOG_METADATA, "received", (void*) received);
    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_non_value_uint(LOG_METADATA, "size", size);

    USize const chunk = size > (USize) I32_MAX ? (USize) I32_MAX : size;

    from->size = (Net_Socket_Len) sizeof(from->storage);

    I32 const result = (I32) recvfrom(self, (char*) buffer, (int) chunk, 0, (struct sockaddr*) &from->storage, &from->size);

    if (result < 0) {
        *received = 0;

        trace_log_pop();

        return result_from_os();
    }

    *received = (USize) result;

    trace_log_pop();

    return RESULT_SUCCESS;
}

Result net_socket_send_1(Net_Socket const self, void const *const data, USize const size, USize *const sent) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "sent", (void*) sent);

    // size == 0 is a legal VALUE, not a contract violation: a UDP peer may
    // legitimately send a zero-byte datagram, and on TCP send() with a
    // length of 0 is a well-defined no-op that returns 0 - so this is never
    // an error_check on a length, and the real syscall below handles it
    // correctly on its own without a special case. _NET_SEND_FLAGS
    // (MSG_NOSIGNAL where available) keeps a peer-closed socket's write from
    // raising SIGPIPE and killing the process on Linux - the failure still
    // arrives here as EPIPE/ECONNRESET, a Result, never a signal.
    USize const chunk = size > (USize) I32_MAX ? (USize) I32_MAX : size;
    I32 const result  = (I32) send(self, (char const*) data, (int) chunk, _NET_SEND_FLAGS);

    if (result < 0) {
        *sent = 0;

        trace_log_pop();

        return result_from_os();
    }

    *sent = (USize) result;

    trace_log_pop();

    return RESULT_SUCCESS;
}

Result net_socket_send_2(Net_Socket const self, void const *const data, USize const size, Net_Socket_Address const *const to, USize *const sent) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "to", (void*) to);
    error_check_null(LOG_METADATA, "sent", (void*) sent);

    if (!_net_address_valid(to)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, NET_ARGUMENT_BAD_ADDRESS_SIZE, 0);
    }

    // size == 0 is a legal VALUE (see net_socket_send_1): on UDP, sendto()
    // with a length of 0 puts a real, legal zero-byte datagram on the wire.
    // _NET_SEND_FLAGS: see net_socket_send_1's comment on SIGPIPE.
    USize const chunk = size > (USize) I32_MAX ? (USize) I32_MAX : size;
    I32 const result  = (I32) sendto(self, (char const*) data, (int) chunk, _NET_SEND_FLAGS, (struct sockaddr const*) &to->storage, to->size);

    if (result < 0) {
        *sent = 0;

        trace_log_pop();

        return result_from_os();
    }

    *sent = (USize) result;

    trace_log_pop();

    return RESULT_SUCCESS;
}

Result net_socket_set_blocking(Net_Socket const self, bool const enabled) {
    trace_log_push(LOG_METADATA);

#ifdef OS_WINDOWS
    u_long mode      = enabled ? 0UL : 1UL;
    I32 const result = ioctlsocket(self, FIONBIO, &mode);
#else
    I32 flags = fcntl(self, F_GETFL, 0);

    if (flags == -1) {
        trace_log_pop();

        return result_from_os();
    }

    flags = enabled ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

    I32 const result = fcntl(self, F_SETFL, flags);
#endif // OS_WINDOWS

    trace_log_pop();

    return result == 0 ? RESULT_SUCCESS : result_from_os();
}

Result net_socket_set_nodelay(Net_Socket const self, bool const enabled) {
    trace_log_push(LOG_METADATA);

    I32 const value   = enabled ? 1 : 0;
    I32 const result  = setsockopt(self, IPPROTO_TCP, TCP_NODELAY, (char const*) &value, sizeof(value));

    trace_log_pop();

    return result == 0 ? RESULT_SUCCESS : result_from_os();
}

Result net_socket_set_reuse_address(Net_Socket const self, bool const enabled) {
    trace_log_push(LOG_METADATA);

    I32 const value  = enabled ? 1 : 0;
    I32 const result = setsockopt(self, SOL_SOCKET, SO_REUSEADDR, (char const*) &value, sizeof(value));

    trace_log_pop();

    return result == 0 ? RESULT_SUCCESS : result_from_os();
}

Result net_socket_set_timeout(Net_Socket const self, U32 const timeout_ms) {
    trace_log_push(LOG_METADATA);

    // SO_RCVTIMEO/SO_SNDTIMEO take a DIFFERENT payload per platform, not just
    // a different type name: Winsock wants a DWORD count of milliseconds,
    // POSIX wants a struct timeval - passing a millisecond DWORD to Linux
    // would compile once cast and then set a nonsense timeout.
#ifdef OS_WINDOWS
    DWORD const timeout = (DWORD) timeout_ms;
#else
    struct timeval const timeout = {
        .tv_sec  = (long) (timeout_ms / 1000),
        .tv_usec = (long) ((timeout_ms % 1000) * 1000)
    };
#endif // OS_WINDOWS

    // Short-circuit on the first failure: result_from_os() reads the
    // thread-local OS error state, so calling both setsockopt()s
    // unconditionally would let a successful SNDTIMEO overwrite RCVTIMEO's
    // failure before it could be captured.
    I32 const recv_result = setsockopt(self, SOL_SOCKET, SO_RCVTIMEO, (char const*) &timeout, sizeof(timeout));

    if (recv_result != 0) {
        trace_log_pop();

        return result_from_os();
    }

    I32 const send_result = setsockopt(self, SOL_SOCKET, SO_SNDTIMEO, (char const*) &timeout, sizeof(timeout));

    trace_log_pop();

    return send_result == 0 ? RESULT_SUCCESS : result_from_os();
}

Result net_socket_shutdown(Net_Socket const self, Net_Shutdown const how) {
    trace_log_push(LOG_METADATA);

    I32 const result = shutdown(self, (int) how);

    trace_log_pop();

    return result == 0 ? RESULT_SUCCESS : result_from_os();
}

Result net_socket_wait(Net_Socket const self, Net_Wait const flags, U32 const timeout_ms, Net_Wait *const ready) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "ready", (void*) ready);
    error_check_wrong_value(LOG_METADATA, "flags must include NET_WAIT_READ and/or NET_WAIT_WRITE", flags == 0);

    *ready = (Net_Wait) 0;

    // A caller-supplied timeout above I32_MAX must never become "wait
    // forever" once narrowed to poll()/WSAPoll()'s signed int parameter -
    // clamp before the cast rather than let a huge value wrap negative.
    U32 const timeout_clamped = timeout_ms > (U32) I32_MAX ? (U32) I32_MAX : timeout_ms;

#ifdef OS_WINDOWS
    WSAPOLLFD descriptor = { .fd = self, .events = 0, .revents = 0 };
#else
    struct pollfd descriptor = { .fd = self, .events = 0, .revents = 0 };
#endif // OS_WINDOWS

    if ((flags & NET_WAIT_READ) != 0) {
        descriptor.events |= POLLIN;
    }

    if ((flags & NET_WAIT_WRITE) != 0) {
        descriptor.events |= POLLOUT;
    }

#ifdef OS_WINDOWS
    I32 const result = WSAPoll(&descriptor, 1, (int) timeout_clamped);
#else
    I32 const result = poll(&descriptor, 1, (int) timeout_clamped);
#endif // OS_WINDOWS

    if (result < 0) {
        trace_log_pop();

        return result_from_os();
    }

    if (result > 0) {
        // POLLERR/POLLHUP/POLLNVAL signal a socket-level error condition, not
        // ordinary read readiness - folding them into READ means the
        // caller's next recv() is what surfaces the real error, rather than
        // this wrapper inventing a third readiness class.
        if ((descriptor.revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0) {
            *ready = (Net_Wait) (*ready | NET_WAIT_READ);
        }

        if ((descriptor.revents & POLLOUT) != 0) {
            *ready = (Net_Wait) (*ready | NET_WAIT_WRITE);
        }
    }

    trace_log_pop();

    return RESULT_SUCCESS;
}

bool net_socket_address_equal(Net_Socket_Address const *const a, Net_Socket_Address const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    bool equal = false;

    if (a->storage.ss_family == b->storage.ss_family) {
        if (a->storage.ss_family == AF_INET6) {
            struct sockaddr_in6 const *const address_a = (struct sockaddr_in6 const*) &a->storage;
            struct sockaddr_in6 const *const address_b = (struct sockaddr_in6 const*) &b->storage;

            equal = address_a->sin6_port == address_b->sin6_port
                && memcmp(&address_a->sin6_addr, &address_b->sin6_addr, sizeof(address_a->sin6_addr)) == 0;
        }
        else {
            struct sockaddr_in const *const address_a = (struct sockaddr_in const*) &a->storage;
            struct sockaddr_in const *const address_b = (struct sockaddr_in const*) &b->storage;

            equal = address_a->sin_port == address_b->sin_port
                && address_a->sin_addr.s_addr == address_b->sin_addr.s_addr;
        }
    }

    trace_log_pop();

    return equal;
}

Net_Family net_socket_address_family(Net_Socket_Address const *const address) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "address", (void*) address);

    Net_Family const family = (Net_Family) address->storage.ss_family;

    trace_log_pop();

    return family;
}

char const* net_socket_address_format(Net_Socket_Address const *const address, char buffer[static NET_SOCKET_ADDRESS_TEXT_SIZE]) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "address", (void*) address);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

#ifdef OS_WINDOWS
    // inet_ntop lives in ws2_32.dll like every other Winsock call - a caller
    // who reaches format() without ever calling init_2/resolve first (e.g.
    // formatting an address received from elsewhere) still needs the lazy
    // start done here. The Result is discarded deliberately: format() itself
    // returns no Result, and a failed start degrades to the zero-initialized
    // `host` below - a well-defined, if empty, ":port"/"[]:port" string.
    (void) _winsock_ensure_started();
#endif // OS_WINDOWS

    // inet_ntop's own return is unchecked - on the (practically unreachable,
    // since ss_family is always one this module itself wrote) failure path,
    // `host` stays as the zero-initialization below, so the result is a
    // well-defined ":port"/"[]:port" string, never garbage.
    char host[INET6_ADDRSTRLEN] = DEFAULT_INITIALIZATION;

    if (address->storage.ss_family == AF_INET6) {
        struct sockaddr_in6 const *const v6 = (struct sockaddr_in6 const*) &address->storage;

        inet_ntop(AF_INET6, (void*) &v6->sin6_addr, host, sizeof(host));
        snprintf(buffer, NET_SOCKET_ADDRESS_TEXT_SIZE, "[%s]:%u", host, (unsigned) ntohs(v6->sin6_port));
    }
    else {
        struct sockaddr_in const *const v4 = (struct sockaddr_in const*) &address->storage;

        inet_ntop(AF_INET, (void*) &v4->sin_addr, host, sizeof(host));
        snprintf(buffer, NET_SOCKET_ADDRESS_TEXT_SIZE, "%s:%u", host, (unsigned) ntohs(v4->sin_port));
    }

    trace_log_pop();

    return buffer;
}

Net_Socket_Address net_socket_address_init_1(Net_Family const family, U16 const port) {
    trace_log_push(LOG_METADATA);

    error_check_wrong_value(LOG_METADATA, "family must be NET_FAMILY_IPV4 or NET_FAMILY_IPV6", family != NET_FAMILY_IPV4 && family != NET_FAMILY_IPV6);

    Net_Socket_Address address = DEFAULT_INITIALIZATION;

    if (family == NET_FAMILY_IPV6) {
        struct sockaddr_in6 *const v6 = (struct sockaddr_in6*) &address.storage;

        v6->sin6_family = AF_INET6;
        v6->sin6_addr   = in6addr_any;
        v6->sin6_port   = htons(port);
        address.size    = (Net_Socket_Len) sizeof(struct sockaddr_in6);
    }
    else {
        struct sockaddr_in *const v4 = (struct sockaddr_in*) &address.storage;

        v4->sin_family      = AF_INET;
        v4->sin_addr.s_addr = INADDR_ANY;
        v4->sin_port        = htons(port);
        address.size        = (Net_Socket_Len) sizeof(struct sockaddr_in);
    }

    trace_log_pop();

    return address;
}

Result net_socket_address_init_2(Net_Family const family, U16 const port, char const *const literal, Net_Socket_Address *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "literal", (void*) literal);
    error_check_null(LOG_METADATA, "out", (void*) out);
    error_check_wrong_value(LOG_METADATA, "family must be NET_FAMILY_IPV4 or NET_FAMILY_IPV6", family != NET_FAMILY_IPV4 && family != NET_FAMILY_IPV6);

#ifdef OS_WINDOWS
    Result const startup = _winsock_ensure_started();

    if (result_is_error(startup)) {
        trace_log_pop();

        return startup;
    }
#endif // OS_WINDOWS

    Net_Socket_Address address = DEFAULT_INITIALIZATION;
    void *destination = nullptr;

    if (family == NET_FAMILY_IPV6) {
        struct sockaddr_in6 *const v6 = (struct sockaddr_in6*) &address.storage;

        v6->sin6_family = AF_INET6;
        v6->sin6_port   = htons(port);
        destination     = &v6->sin6_addr;
        address.size    = (Net_Socket_Len) sizeof(struct sockaddr_in6);
    }
    else {
        struct sockaddr_in *const v4 = (struct sockaddr_in*) &address.storage;

        v4->sin_family = AF_INET;
        v4->sin_port   = htons(port);
        destination    = &v4->sin_addr;
        address.size   = (Net_Socket_Len) sizeof(struct sockaddr_in);
    }

    // inet_pton returns 0 for a syntactically bad literal and -1 for an
    // unsupported family (already excluded above) - either way the literal
    // is a VALUE the caller supplied, so this refuses rather than falling
    // back to a wildcard, which would silently turn a typo into a bind-to-any.
    I32 const parsed = inet_pton((int) family, literal, destination);

    if (parsed != 1) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, NET_ARGUMENT_BAD_LITERAL, 0);
    }

    *out = address;

    trace_log_pop();

    return RESULT_SUCCESS;
}

USize net_socket_address_key_1(char const *const text, USize const size, Byte *const out, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out", (void*) out);

    Net_Socket_Address address = DEFAULT_INITIALIZATION;

    /* A caller buffer below the widest key is a VALUE refusal like every other unusable input -
     * see _net_address_key_parse for why nothing on this path may end the process. */
    if (capacity < NET_SOCKET_ADDRESS_KEY_SIZE || !_net_address_key_parse(text, size, &address)) {
        trace_log_pop();

        return 0;
    }

    if (address.storage.ss_family != AF_INET6) {
        struct sockaddr_in const *const in4 = (struct sockaddr_in const*) &address.storage;

        memcpy(out, &in4->sin_addr, sizeof(in4->sin_addr));

        trace_log_pop();

        return sizeof(in4->sin_addr);
    }

    struct sockaddr_in6 const *const    in6     = (struct sockaddr_in6 const*) &address.storage;
    Byte const *const                   bytes   = (Byte const*) &in6->sin6_addr;

    if (_net_address_key_mapped(bytes)) {
        memcpy(out, bytes + 12, 4);

        trace_log_pop();

        return 4;
    }

    // The /64 network prefix: the high 8 bytes, host part dropped. Policy, not arithmetic - see
    // the header's collapse note before widening or narrowing it.
    memcpy(out, bytes, NET_SOCKET_ADDRESS_KEY_SIZE);

    trace_log_pop();

    return NET_SOCKET_ADDRESS_KEY_SIZE;
}

USize net_socket_address_key_2(char const *const text, USize const size, char *const out, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "out", (void*) out);

    Net_Socket_Address  address = DEFAULT_INITIALIZATION;
    I32                 written = 0;

    /* The empty answer is written FIRST, so every refusal below leaves `out` a valid empty C
     * string rather than whatever the caller's buffer held - a caller that keys on the text and
     * ignores the length can then never read a stale key. */
    if (capacity != 0) {
        out[0] = '\0';
    }

    if (capacity < NET_SOCKET_ADDRESS_KEY_TEXT_SIZE || !_net_address_key_parse(text, size, &address)) {
        trace_log_pop();

        return 0;
    }

    /* inet_ntop lives in ws2_32.dll on Windows, but the parse above only succeeds once
     * net_socket_address_init_2 has started Winsock, so no lazy start is needed here. */
    char host[INET6_ADDRSTRLEN] = DEFAULT_INITIALIZATION;

    if (address.storage.ss_family != AF_INET6) {
        struct sockaddr_in const *const in4 = (struct sockaddr_in const*) &address.storage;

        if (inet_ntop(AF_INET, (void*) &in4->sin_addr, host, sizeof(host)) != nullptr) {
            written = snprintf(out, capacity, "%s", host);
        }
    }
    else {
        struct sockaddr_in6 const *const    in6     = (struct sockaddr_in6 const*) &address.storage;
        Byte const *const                   bytes   = (Byte const*) &in6->sin6_addr;

        if (_net_address_key_mapped(bytes)) {
            struct in_addr unwrapped = DEFAULT_INITIALIZATION;

            memcpy(&unwrapped, bytes + 12, 4);

            if (inet_ntop(AF_INET, (void*) &unwrapped, host, sizeof(host)) != nullptr) {
                written = snprintf(out, capacity, "%s", host);
            }
        }
        else {
            // The /64 network: the host half zeroed, then rendered compressed by inet_ntop, so
            // "2001:db8::1" and "2001:db8:0:0:dead:beef:0:1" write the one key "2001:db8::/64".
            struct in6_addr network = DEFAULT_INITIALIZATION;

            memcpy(&network, bytes, NET_SOCKET_ADDRESS_KEY_SIZE);

            if (inet_ntop(AF_INET6, (void*) &network, host, sizeof(host)) != nullptr) {
                written = snprintf(out, capacity, "%s/64", host);
            }
        }
    }

    /* A failed inet_ntop or a truncating snprintf is practically unreachable - the family is one
     * this module itself parsed and NET_SOCKET_ADDRESS_KEY_TEXT_SIZE bounds the widest answer -
     * but it degrades to the same empty VALUE answer rather than a half-written key. */
    if (written <= 0 || (USize) written >= capacity) {
        out[0] = '\0';

        trace_log_pop();

        return 0;
    }

    trace_log_pop();

    return (USize) written;
}

U16 net_socket_address_port(Net_Socket_Address const *const address) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "address", (void*) address);

    U16 const port = address->storage.ss_family == AF_INET6
        ? ntohs(((struct sockaddr_in6 const*) &address->storage)->sin6_port)
        : ntohs(((struct sockaddr_in const*) &address->storage)->sin_port);

    trace_log_pop();

    return port;
}

Result net_socket_address_resolve(Net_Family const family, char const *const host, U16 const port, Net_Socket_Address *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "host", (void*) host);
    error_check_null(LOG_METADATA, "out", (void*) out);
    error_check_wrong_value(LOG_METADATA, "family must be NET_FAMILY_ANY, NET_FAMILY_IPV4, or NET_FAMILY_IPV6",
        family != NET_FAMILY_ANY && family != NET_FAMILY_IPV4 && family != NET_FAMILY_IPV6);

#ifdef OS_WINDOWS
    Result const startup = _winsock_ensure_started();

    if (result_is_error(startup)) {
        trace_log_pop();

        return startup;
    }
#endif // OS_WINDOWS

    char port_text[6] = DEFAULT_INITIALIZATION;

    snprintf(port_text, sizeof(port_text), "%u", (unsigned) port);

    struct addrinfo hints = DEFAULT_INITIALIZATION;

    hints.ai_family   = (int) family;
    hints.ai_socktype = SOCK_STREAM;
    // port_text is always numeric (built by the snprintf above), so this
    // skips getaddrinfo's own services-database lookup for it.
    hints.ai_flags    = AI_NUMERICSERV;

    struct addrinfo *info  = nullptr;
    I32 const gai_result   = getaddrinfo(host, port_text, &hints, &info);

    if (gai_result != 0) {
        trace_log_pop();

        return _net_result_from_resolver(gai_result);
    }

    if (info->ai_addrlen == 0 || info->ai_addrlen > sizeof(out->storage)) {
        freeaddrinfo(info);
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, NET_ARGUMENT_BAD_ADDRESS_SIZE, 0);
    }

    memcpy(&out->storage, info->ai_addr, info->ai_addrlen);
    out->size = (Net_Socket_Len) info->ai_addrlen;

    freeaddrinfo(info);

    trace_log_pop();

    return RESULT_SUCCESS;
}

bool net_result_is_address_in_use(Result const result) {
    if (!result_is_error(result)) {
        return false;
    }

#ifdef OS_WINDOWS
    return result_code(result) == (U16) WSAEADDRINUSE;
#else
    return result_code(result) == (U16) EADDRINUSE;
#endif // OS_WINDOWS
}

bool net_result_is_closed(Result const result) {
    if (!result_is_error(result)) {
        return false;
    }

#ifdef OS_WINDOWS
    return result_code(result) == (U16) WSAECONNRESET
        || result_code(result) == (U16) WSAECONNREFUSED
        || result_code(result) == (U16) WSAECONNABORTED;
#else
    return result_code(result) == (U16) ECONNRESET
        || result_code(result) == (U16) ECONNREFUSED
        || result_code(result) == (U16) ECONNABORTED
        || result_code(result) == (U16) EPIPE;
#endif // OS_WINDOWS
}

bool net_result_is_timed_out(Result const result) {
    if (!result_is_error(result)) {
        return false;
    }

#ifdef OS_WINDOWS
    return result_code(result) == (U16) WSAETIMEDOUT;
#else
    return result_code(result) == (U16) ETIMEDOUT;
#endif // OS_WINDOWS
}

bool net_result_is_would_block(Result const result) {
    if (!result_is_error(result)) {
        return false;
    }

#ifdef OS_WINDOWS
    return result_code(result) == (U16) WSAEWOULDBLOCK;
#else
    return result_code(result) == (U16) EAGAIN || result_code(result) == (U16) EWOULDBLOCK;
#endif // OS_WINDOWS
}