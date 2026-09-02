/*
 * windows.h - Canonical entry point for the Windows system headers
 *
 * Features:
 *   - The single correct way to reach <windows.h> from CFW: pulls
 *     <winsock2.h> before <windows.h> so the sockets API is Winsock 2, never
 *     the Winsock 1.1 subset <windows.h> would otherwise supply
 *   - Detects the poisoned include order (a <windows.h> that already ran
 *     without WIN32_LEAN_AND_MEAN) and turns it into a compile error instead
 *     of the silent API downgrade MinGW only warns about
 *   - Sets the configuration macros that must precede the system headers:
 *     WIN32_LEAN_AND_MEAN (~75k preprocessed lines instead of ~109k on this
 *     toolchain) and NOMINMAX (drops the function-like min/max in minwindef.h)
 *   - Asserts the _WIN32_WINNT API baseline the framework is written against,
 *     and rejects UNICODE builds, which would silently retarget every
 *     generic-name Win32 call in the tree to its -W variant
 *   - Compiles to nothing on non-Windows targets, so a caller needs only its
 *     own platform branch, not a nested one
 *
 * Usage Examples:
 *   @code
 *   // In a consumer header, after <types.h> has defined OS_WINDOWS:
 *   #ifdef OS_WINDOWS
 *   #include <platform/windows/windows.h>
 *   #include <shlwapi.h>              // extras come AFTER, never before
 *   #endif
 *   @endcode
 *
 * Error Handling:
 *   - Diagnostics are compile-time #error only; there is no runtime surface
 *
 * Thread Safety:
 *   - Not applicable (no code, no state)
 *
 * Memory Management:
 *   - Not applicable
 *
 * Performance Characteristics:
 *   - Preprocessing cost only; WIN32_LEAN_AND_MEAN keeps it at ~68% of a
 *     full <windows.h>
 *
 * Dependencies:
 *   - System headers only (<winsock2.h>, <windows.h>, <ws2tcpip.h>).
 *     Deliberately none from CFW: this is a layer-0 leaf, which is what lets
 *     it sit below <types.h> and travel drag-free in a `gcc -MM` closure when
 *     a tool is packaged standalone. Do not add a CFW include here.
 *
 * Notes:
 *   - Guard vocabulary: this header tests raw _WIN32 because it cannot see
 *     <types.h>. Consumers above that layer branch on OS_WINDOWS instead
 *     (include/result.h is the canonical example). Including it
 *     unconditionally is equally correct - the header self-guards on _WIN32
 *     and expands to nothing elsewhere - so all three forms in the tree are
 *     fine and none needs "fixing" to match the others
 *   - Include it FIRST among the Windows headers of a translation unit.
 *     Anything with no ordering constraint (<shlwapi.h>, <io.h>,
 *     <commdlg.h>, <winerror.h>) belongs after it - and <winerror.h> arrives
 *     free, even under WIN32_LEAN_AND_MEAN
 *   - <windows.h> unconditionally defines `interface` as `struct`, and its
 *     legacy `near`/`far`/`ERROR` macros survive here. They are NOT #undef'd:
 *     the extras-after headers (<commdlg.h>) still use them. Avoid those
 *     identifiers in CFW code instead (see math's near_z / far_z)
 *   - Boundary of the NOMINMAX guarantee: a translation unit that sets
 *     WIN32_LEAN_AND_MEAN itself and includes <windows.h> before this header
 *     is safe for sockets (the order guard deliberately allows it - a later
 *     <winsock2.h> still lands its declarations) but has already taken
 *     minwindef.h's function-like min/max, which a later #define cannot
 *     retract. Reaching this header first is what makes NOMINMAX hold; no CFW
 *     translation unit does otherwise, and the tree defines no min/max helper
 *     for such a macro to capture
 *   - _WIN32_WINNT is pinned by the makefiles (CDEFINES); only the floor
 *     assert lives here. Do not "simplify" that into an #ifndef pin in this
 *     header: <_mingw.h> is reached by any libc header and usually sets the
 *     macro before we run, so the #ifndef would bind only in a translation
 *     unit that includes this header first - giving different TUs different
 *     API baselines depending on their include order. A -D applies to all of
 *     them equally, which is the only consistent answer
 *   - Every CFW translation unit reaches the Windows headers through this
 *     file; `tools/cfw_lint.py`'s windows-raw-include rule enforces that (and
 *     matches case-insensitively, because the one bypass that escaped an
 *     earlier sweep spelled it <Windows.h>). The single allowed exception is
 *     the vendored include/csv/csvfr.c, kept pristine against upstream: its
 *     TU includes no CFW header afterwards, so nothing can be downgraded
 *   - If an MSVC target is ever added, this is where
 *     `#pragma comment(lib, "ws2_32")` belongs, gated on _MSC_VER
 *
 * This is a header-only module; there is no windows.c.
 */

#ifndef PLATFORM_WINDOWS_WINDOWS_H
#define PLATFORM_WINDOWS_WINDOWS_H

#ifdef _WIN32

/*==============================================================================
 * MARK: - Include-Order Guard
 * <windows.h> defines _WINSOCKAPI_ when it supplies Winsock 1.1 itself; a
 * later <winsock2.h> then finds that guard already set, skips its own
 * declarations, and leaves the translation unit on the 1.1 API. Winsock2-first
 * paths (this header, libcurl, libwebsockets) define _WINSOCK2API_ and pass.
 *============================================================================*/
#if defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#error "<windows.h> was included before <platform/windows/windows.h>, downgrading sockets to Winsock 1.1 - include this header first"
#endif

/*==============================================================================
 * MARK: - Configuration Macros
 * Every macro here must be set before the system headers below; setting one
 * afterwards has no effect.
 *============================================================================*/
#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN

// CFW calls the generic-name Win32 entry points with narrow (LPCSTR)
// arguments throughout, so a UNICODE build would break every such call site.
// Moving the framework to the wide API is a deliberate tree-wide change, not
// something a stray -DUNICODE should decide.
#if defined(UNICODE) || defined(_UNICODE)
#error "CFW targets the ANSI Win32 API - remove -DUNICODE / -D_UNICODE"
#endif

/*==============================================================================
 * MARK: - Includes
 * The order is load-bearing: <winsock2.h> must precede <windows.h>.
 * <ws2tcpip.h> belongs to the same ordering-sensitive sockets family and is
 * supplied here so no consumer has to hand-roll the sequence.
 *============================================================================*/
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

/*==============================================================================
 * MARK: - API Baseline
 * Checked after the includes on purpose: <_mingw.h> has run by this point, so
 * a toolchain that supplies a default has already applied it. A standalone
 * build that does not inherit the makefile's CDEFINES lands here. An undefined
 * _WIN32_WINNT is also a failure - a toolchain that never sets one leaves the
 * API surface unspecified, which is exactly what this assert exists to reject.
 *============================================================================*/
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#error "CFW requires a Windows 10 API baseline - build with -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00"
#endif

#endif // _WIN32

#endif // PLATFORM_WINDOWS_WINDOWS_H