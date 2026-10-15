# Changelog

All notable changes to the public CFW export. Versioning follows [SemVer 2.0.0](https://semver.org/).

A module arrives in its own commit, the moment it has cleared all five audit dimensions
upstream; a version is tagged when a family is complete, so entries accumulate here first.

## Unreleased

- `encoding/hex`
- `encoding/base64`
- `crypto/random`
- `crypto/hash`
- `crypto/hmac`
- `crypto/kdf`
- `crypto/password`
- `json`
- sync: result
- `net`
- `websocket/client`
- `http/service/body_parser`
- `http/query`
- sync: websocket/client
- `http/service/multipart`
- `http/service/trace`
- `http/client`
- `http/headers`
- `http/cookie`
- sync: datetime
- `http/server`
- sync: http/server
- `http/service/static`
- `http/service/ip_block`
- `http/service/security`
- sync: http/client, http/server
- sync: container/string, http/headers, http/service/ip_block, net
- sync: http/cookie, http/query
- `http/service/permission`
- sync: http/client
- `http/service/captcha`
- `http/service/traceparent`
- `http/service/auth`
- `encoding/quoted_printable`
- `http/service/session`
- `http/service/rate_limit`
- `http/service/compression`
- `http/service/cors`
- `http/service/csrf`
- sync: process
- `http/service/email`
- `http/service/oauth`
- sync: http/service/csrf
- sync: http/service/oauth
- http/service/oauth 0.3.0 changes the signed state token's format (provider and binding are now length-prefixed, and the _2 tiers bind the state to a caller cookie). A 0.2.x process and a 0.3.0 process sharing a key do not verify each other's states: restart a multi-process deployment all at once, or expect one TTL (600 s) of bad_state redirects during a rolling restart.
- sync: http/service/compression
- sync: http/service/email
- email 0.4.0: a hard header fold never splits a quoted-pair (step-back), closing a From-address spoof through an escaped display name
- sync: encoding/quoted_printable, http/client, http/query, http/service/cors, http/service/oauth, http/service/rate_limit, http/service/static
- http/client 0.4.0: `http_client_header_add` and `http_client_set_user_agent` now return bool and REFUSE a value carrying CR or LF (libcurl forwards header strings verbatim, so caller data could inject header lines); new `http_client_header_add_2(name, value)`; an arena-backed Result's `error` is now an owned copy (it was a view the next request on the same handle overwrote). Source-compatible for callers that ignored the old void return.
- http/service/static 0.4.0: a Range response's window is capped at 1 MiB (was 4 MiB buffered per in-flight request); the ETag is now STRONG (`"size-mtime"`, no `W/`), so clients revalidate once after upgrading; If-None-Match uses the weak comparison (lists, `*`, `W/`); new opt-in `refuse_dotfiles`. On Windows it now refuses NTFS stream names (`:`), components ending in `.` or space, 8.3 short-name aliases (`~` + digit), and reserved device names with spaces or dots before the extension (`CONIN$ .txt`, `AUX .html` previously opened the device and blocked the serving thread), plus `CONIN$`, `CONOUT$`, `CLOCK$` and the superscript COM/LPT forms.
- http/service/cors 0.4.0: `origin_add` refuses a malformed origin (it must be `scheme://host[:port]`), refuses `null` while `allow_credentials` is on (as it already refused `*`), strips a scheme's default port (`:443`, `:80`) and refuses a port that is 0, has a leading zero or exceeds 65535. A configuration that relied on any of these now gets `false` and a WARN at startup.
- http/service/rate_limit 0.3.5: `get_time_left_*` answers the same wait `check_*` reports on a refusal, never 0, so the blocked/time-left pairing no longer emits `Retry-After: 0`.
- encoding/quoted_printable 0.2.0: new `ENCODING_QUOTED_PRINTABLE_ENCODE_SIZE_MAX(byte_count)` for sizing a fixed buffer.
- sync: file, http/client, http/service/static
- file 0.5.0 (first version marker): two new OS-independent predicates for request-chosen names on a Windows-deployable server: `file_name_reserved_windows` (a component Win32 opens as a device - the reserved list with the stem trimmed the way Win32 trims it, `CON:` and `CON::$DATA` included, and COM/LPT followed only by non-ASCII bytes) and `file_path_aliased_windows` (an NTFS stream `:`, a component ending in `.` or space, or an 8.3-shaped alias). Pure byte rules; no file is opened.
- http/service/static 0.4.1: the Windows refusals now come from `file/`'s predicates; the 8.3 rule is narrower (`my-report~2.pdf` is served again, `BACKUP~1.TXT` is still refused).
- http/client 0.4.1: an arena-backed `response` now reserves `max_response_size + 1` from its arena at the start of every request, so an arena too small for the cap aborts at the caller's own cap before any I/O instead of at a size the remote peer chose. The default cap is 16 MiB: lower it with `http_client_set_max_response_size` before the first request on an arena-backed handle (0 or USIZE_MAX means unbounded, nothing reserved).
- sync: container/string, file, http/client, http/headers, http/query, http/server, http/service/body_parser, http/service/cors, http/service/email, http/service/oauth, http/service/security, http/service/static, http/service/traceparent, process
- file 0.6.0: handle-first openers `file_open_nofollow_try_1/2/3` refuse a final symlink or junction (Windows opens through `CreateFile2` and pins the file identity across the placeholder re-open; device names and every UNC path are refused before any open), plus `file_final_path_1/2/3`, `file_get_final_path`, `file_get_modified` and `FILE_PATH_WIDE_MAX`. An allocation failure inside the path-addressability check now reports MEMORY, never `FILE_ARGUMENT_UNADDRESSABLE_PATH`.
- http/server 0.5.0: `http_server_response_send_file_handle` streams an opened, verified `File*` in `HTTP_SERVER_FILE_CHUNK_SIZE` (64 KiB) chunks, bounding in-process memory per slow reader. Readers are held to a minimum read rate (about 510 B/s by the timer's arithmetic); under Linux `tcp_mem` pressure every stream can be dropped at once whatever its rate - documented, fix pending. Limit: no client bytes (pipelining, a stray CRLF) while a body streams. Header hardening: a `content_type` carrying CR, LF or another control byte is refused at every sender, and `header_add_raw` now returns `bool` and refuses (as does send_file's `extra_headers`) a block that is not well-formed `Name: value\r\n` lines. http/server now depends on http/headers.
- http/service/static 0.5.0: serves through the handle seam (one open, verify, serve). **`refuse_symlinks` now defaults to true**: run `find -H <root> -type l` (Linux) or `Get-ChildItem <root> -Recurse -Attributes ReparsePoint` (Windows) on a served tree before upgrading - a link whose target is INSIDE the root is still served; only a final path outside it is refused. static inherits `send_file_handle`'s one limit: no client bytes while a body streams.
- http/client 0.5.0 (breaking): an arena-backed response with no `max_response_size` cap is refused; an arena too small for the cap + 1 now gets a named error before any I/O instead of an abort; the error text falls back to the heap when the arena is full.
- container/string 0.5.0: `string_try_reserve`, the non-aborting reserve.
- http/query 0.4.0: zero-allocation tier `http_query_next`, `http_query_find`, `http_query_decode`; `http_query_alloc_get_*` now match the whole parameter name (byte-identical to 0.3.2 for every key free of `=` and `&`). http/service/body_parser parses urlencoded bodies through it.
- http/service/cors 0.4.1: an unbracketed host carrying a URL-Standard forbidden host byte (`@ \ [ ] ^ | % < >` or space) is refused.
- http/service/oauth 0.4.0 (breaking): the deprecated pre-tier names and the positional `provider_add_2` are removed (call the `_1` tiers); `oauth.h` now includes only what its API names, so include `crypto/random`, `json` and the like yourself.
- http/service/traceparent 0.3.1: `header_create` re-checks a hand-filled context and service - every public array is measured within its capacity, and a bad `header_name` or `tracestate` is dropped, never forwarded.
- http/headers 0.4.1 (first version marker): `http_headers_security_create_2` re-checks policy Strings written after init and omits only a failing field's line. http/service/security checks `header_add_raw`'s answer.
- process: an out-of-memory fault-injection harness (`tests/process/test_oom.c`); no defects found. http/service/email: line-continuation formatting only.
- Build: a suite can recompile its own module with a test-only define (http/service/static's test hooks never reach `libcfw.a`). An OOM harness that refuses the checked build links `libcfw_unchecked.a` with the unchecked flags, and every per-binary OOM override is `private`, so it no longer leaks into the shared harness's prerequisites.
- sync: http/server, net
- http/server 0.5.1: a streamed `send_file_handle` socket now has its kernel send buffer capped at take-over (256 KiB requested; Linux stores up to 512 KiB, bounded by `net.core.wmem_max`). Under Linux TCP memory pressure an uncapped socket's buffer is moderated to half and never signalled writable again for a slow reader, which dropped every stream at the first timeout (measured 8/8 at ~130 s); capped, 0/8 drop and the kernel never enters pressure. The price is per-connection throughput of about cap/RTT (~22 Mbit/s measured at 100 ms RTT), sticky for the connection's life; `send_file` (libwebsockets' own path) is unaffected and still drops readers below ~4-16 KiB/s at ~20 s on Linux. `header_add_raw` and `send_file`'s `extra_headers` now accept RFC 9110 field lines (`Name:value`, optional whitespace, empty value); the relaxation exposed and fixed two latent defects - a `Name:value` line had its value's first byte overwritten by the name terminator, and a `header_add` key carrying `:` split into an injected header (keys are now token-checked, so a key such as `X: Y`, which used to go out as a mangled single header, is refused). Neither was reachable through the old validator. http/server now depends on `net`.
- net 0.4.0 (first version marker): `net_socket_set_send_buffer_size` (SO_SNDBUF; Linux doubles and locks it, Windows takes it literally).
- sync: file, http/server, http/service/static
- file 0.7.0: `file_seek_try`, the non-aborting twin of `file_at` (no size query; a seek past EOF is legal and the next read answers 0; false only when the C runtime refuses the offset). A nofollow open that fails inside `_fdopen` now reports `ERROR_TOO_MANY_OPEN_FILES` (the C runtime's 512-stream table) where it documented `ERROR_NOT_ENOUGH_MEMORY` before - a documented code changed. `file_seek_try` also refuses a `File_Position` outside the enum by value (the UCRT would otherwise end the process). A junction opened with `file_open_nofollow_try_*` answers `ERROR_ACCESS_DENIED`, pinned. Docs: hard links are outside the final-path compare; mapped drive letters are not refused by the UNC rule. The path-addressability check takes one allocation instead of two.
- http/server 0.5.2: `send_file_handle` seeks through `file_seek_try` BEFORE writing headers, so a file that shrank under the reader is refused like any other pre-header refusal (false, the caller's fallback reply still goes out) instead of aborting a checked build. Requires file >= 0.7.0.
- sync: file, http/service/cors, http/service/static
- http/service/static 0.5.1: a `root_dir` that is itself a link is resolved once at construction - re-pointing it afterwards answers 404 on every request with the per-request `link refused` WARN (a silent 404 if the new root does not exist) until the service is reconstructed (documented); `default_file` / `fallback_file` are checked at init and a Windows-reserved or aliased name logs one WARN; the final-path WARN names the `root_dir` it belongs to; hard links, Windows follow-by-path and UNC targets documented.
- http/service/cors 0.4.2: no behaviour change; the space byte is refused by the control-range pass alone.
- sync: container/string, http/client
- http/client 0.5.1: documentation of the pre-I/O refusal (`HTTP_CLIENT_STATUS_ERROR` with `code == CURLE_OK`; no separate REFUSED status will be added) and of the two outcomes for a view-backed `response` (written in place below the cap, promoted into the arena above it); a refused request no longer clears a caller's String first. `error` is now log-safe: every byte below 0x20 and the byte 0x7F in libcurl's message is replaced by `?` before it is stored, so a logger may print it verbatim.
- container/string 0.5.1: `string_try_reserve` is the twin to reach for whenever the allocator may be exhausted, not only for sizes that came from outside the program; the heap failure branch is pinned.
- sync: http/query, http/service/body_parser
- http/query 0.4.1: documentation - the `http_query_next` '?'-skip recipe adjusts the size too; no char*-key twin of `http_query_find` is planned (spell a literal key with `CHAR_STATIC_SIZE`); the zero-allocation tier's `(nullptr, 0)` empty versus the alloc tier's null refusal stated once; the Error Handling text names `alloc_decode_2` / `alloc_form_decode_2`.
- http/service/body_parser 0.3.1 (first version marker): an urlencoded pair with an empty key is skipped before any allocation; dependencies listed in full.

## 0.2.0 - release

The container wave. Four containers join the foundation, each through the same
five-dimension gate as the first export, and the tools that keep their families
honest ship with them.

- `container/arrayList` - thirteen typed dynamic arrays, one per element type,
  with al_u64 as the canonical instantiation; `tools/al_divergence.py` measures
  every other file's divergence from it and `make check` runs it here.
- `container/map` - nine string-keyed maps in three value shapes; four
  hand-maintained pairs and five scalars generated from the u64 pair by
  `tools/map_generate.py`, with `tools/map_divergence.py` proving they match.
- `container/hashset` - a counting string set (borrow or copy the key, sized
  slice forms, a cursor walk) whose every borrow declines instead of aborting.
- `container/slotmap` - generational 32-bit handles over a caller-owned pool,
  with a stated walk-under-removal guarantee and an ABA bound of exactly 65535.
- `env` and `bits` - the two modules that landed between the tags: a dotenv
  loader and the single-word and bit-array primitives behind al_bool.
- Build: `make check` runs the family drift gates (python3) and `make test`
  depends on it; `make test-unchecked` builds every `test_unchecked.c` without
  ERROR_CHECK_ENABLED against its own archive; the Linux CI leg runs in a
  debian:13 container, which packages the cglm the math module pins.
- Fixed: `math`'s `mat4_inv_fast` pins allow the approximation the instruction
  promises rather than one CPU model's exact result; the test loops name the
  suite that fails and its exit code.

## 0.1.0 - first export

The foundation and `math`, audited on all five dimensions with their whole include closure:
`allocator`, `arena`, `char`, `chrono`, `console`, `container/str`, `container/string`, `datetime`, `dir`, `error`, `file`, `log`, `math`, `memory`, `platform/windows`, `process`, `regex`, `result`, `test`, `thread`, `tracelog`, `tuple`, `types`.

- `math` 0.3.1: value-typed facade over cglm 0.9.6 with `cglm_compat.h` supplying the eight
  post-release entry points; refusals over aborts for data-shaped inputs.
- `memory` / `allocator` / `arena`: the allocation layer, with the Linux allocation-failure
  harness (`make test-oom`) proving every documented decline path against a real failed calloc.
- `log` / `tracelog` / `error` / `thread` / `console` / `chrono` / `tuple` / `types` / `result`.
