/*
 * types.h - Standard type definitions and platform macros for cross-platform C
 * development
 *
 * Features:
 *   - Short, consistent aliases for signed/unsigned integers and floating-point
 *     types (I8..I64, U8..U64, F32/F64, Byte)
 *   - Signed/unsigned size types (ISize/USize) and a floating size type (FSize)
 *   - Numeric limits and decimal-string buffer sizes for the numeric types
 *   - Platform-detection macros (OS_WINDOWS, OS_LINUX, ...) and an endianness
 *     flag (CFW_LITTLE_ENDIAN) for conditional compilation
 *   - Compiler-attribute macros (CFW_ATTR_*) abstracted across MSVC/GCC/Clang
 *   - A front-door C23 requirement check for one actionable diagnostic
 *
 * Usage Examples:
 *   @code
 *   #include <types.h>
 *   I32 value = 42;
 *   USize size = strlen(text);
 *
 *   #ifdef OS_WINDOWS
 *   // Windows-only path
 *   #endif
 *   @endcode
 *
 * Error Handling:
 *   - Not applicable (type and macro definitions only)
 *
 * Thread Safety:
 *   - Not applicable (type and macro definitions only)
 *
 * Memory Management:
 *   - Not applicable (no allocation; definitions only)
 *
 * Performance Characteristics:
 *   - Not applicable (compile-time definitions only)
 *
 * Dependencies:
 *   - <float.h>, <stddef.h>, <stdint.h>
 *
 * This is a header-only module; there is no types.c.
 */

#ifndef TYPES_H
#define TYPES_H

// CFW is a C23 codebase ([[...]] attributes, bare static_assert). Fail here
// with one actionable message instead of a cascade of confusing syntax errors.
// MSVC is exempt: its __STDC_VERSION__ reporting lags its actual C23 support.
// The floor is the provisional C2x value 202000L, not the final 202311L:
// Debian's gcc 14.2 (the Linux server/bench toolchain) still reports 202000L
// under -std=c23 while supporting every C23 feature CFW uses.
#if !defined(_MSC_VER) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202000L)
#error "CFW requires C23 (the canonical build/windows/makefile sets it)"
#endif

// === Includes ===
#include <float.h>
#include <stddef.h>
#include <stdint.h>

// === Macros and Constants (alphabetized) ===
// CFW_ATTR_*: compiler-attribute abstraction. Sanctioned utility set, not yet
// applied across CFW; use on new APIs (e.g. CFW_ATTR_NODISCARD on allocators).
// The five C23 standard attributes below are portable across MSVC/GCC/Clang in
// C23 mode; the rest have no portable form and are defined per-compiler, in two
// degrade classes:
//   - HINT attributes (ALLOC_SIZE, ALWAYS_INLINE, COLD, CONST, LIKELY/UNLIKELY,
//     MALLOC, NOINLINE, PRINTF, PURE) expand to nothing (or plain `(x)`) where
//     unsupported — slower or less checked there, never wrong.
//   - SEMANTIC attributes (PACKED changes struct layout, WEAK changes linkage)
//     are defined ONLY on GCC/Clang. On MSVC/unknown compilers they are left
//     UNDEFINED so any use fails the compile instead of silently miscompiling;
//     on those compilers use #pragma pack and explicit linkage instead.
// CFW_ATTR_ALWAYS_INLINE supplies the `inline` keyword itself (MSVC
// __forceinline implies it; GCC always_inline does not) — write
// `CFW_ATTR_ALWAYS_INLINE void f(...)`, never a second `inline`.
// CFW_ATTR_LIKELY/CFW_ATTR_UNLIKELY wrap a branch CONDITION (expression), not a
// declaration: `if (CFW_ATTR_UNLIKELY(size == 0)) { ... }`.
// CFW_ATTR_MALLOC contract: ONLY for allocators returning RAW uninitialized or
// zeroed memory (arena/heap block allocators). NEVER on *_alloc_init
// constructors whose returned struct contains pointers (e.g. a String's buffer
// pointer) — GCC's `malloc` attribute promises the result holds no pointers to
// live objects, and a false promise miscompiles under -O3 alias analysis. The
// guarantee is per-compiler: MSVC __declspec(restrict) asserts no-alias only;
// GCC/Clang `malloc` additionally asserts the no-pointers rule.
#define CFW_ATTR_DEPRECATED [[deprecated]]
#define CFW_ATTR_FALLTHROUGH [[fallthrough]]
#define CFW_ATTR_NODISCARD [[nodiscard]]
#define CFW_ATTR_NORETURN [[noreturn]]
#define CFW_ATTR_UNUSED [[maybe_unused]]

#if defined(_MSC_VER)
#define CFW_ATTR_ALLOC_SIZE(size_arg_idx)
#define CFW_ATTR_ALWAYS_INLINE __forceinline
#define CFW_ATTR_COLD
#define CFW_ATTR_CONST
#define CFW_ATTR_LIKELY(x) (x)
#define CFW_ATTR_MALLOC __declspec(restrict)
#define CFW_ATTR_NOINLINE __declspec(noinline)
#define CFW_ATTR_PRINTF(fmt_idx, args_idx)
#define CFW_ATTR_PURE
#define CFW_ATTR_UNLIKELY(x) (x)
// CFW_ATTR_PACKED / CFW_ATTR_WEAK: intentionally NOT defined (semantic class).
#elif defined(__GNUC__) || defined(__clang__)
#define CFW_ATTR_ALLOC_SIZE(size_arg_idx) __attribute__((alloc_size(size_arg_idx)))
#define CFW_ATTR_ALWAYS_INLINE inline __attribute__((always_inline))
#define CFW_ATTR_COLD __attribute__((cold))
#define CFW_ATTR_CONST __attribute__((const))
#define CFW_ATTR_LIKELY(x) __builtin_expect(!!(x), 1)
#define CFW_ATTR_MALLOC __attribute__((malloc))
#define CFW_ATTR_NOINLINE __attribute__((noinline))
#define CFW_ATTR_PACKED __attribute__((packed))
#if defined(__clang__)
#define CFW_ATTR_PRINTF(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else // MinGW/GCC: gnu_printf archetype avoids false %zu/%lld warnings
#define CFW_ATTR_PRINTF(fmt_idx, args_idx) __attribute__((format(gnu_printf, fmt_idx, args_idx)))
#endif
#define CFW_ATTR_PURE __attribute__((pure))
#define CFW_ATTR_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define CFW_ATTR_WEAK __attribute__((weak))
#else
#define CFW_ATTR_ALLOC_SIZE(size_arg_idx)
#define CFW_ATTR_ALWAYS_INLINE inline
#define CFW_ATTR_COLD
#define CFW_ATTR_CONST
#define CFW_ATTR_LIKELY(x) (x)
#define CFW_ATTR_MALLOC
#define CFW_ATTR_NOINLINE
#define CFW_ATTR_PRINTF(fmt_idx, args_idx)
#define CFW_ATTR_PURE
#define CFW_ATTR_UNLIKELY(x) (x)
// CFW_ATTR_PACKED / CFW_ATTR_WEAK: intentionally NOT defined (semantic class).
#endif

/**
 * @def CFW_LITTLE_ENDIAN
 * @brief 1 on little-endian targets, 0 otherwise. Serialization and SIMD
 *        assume little-endian; the static_assert below pins it so a big-endian
 *        port fails the build loudly instead of corrupting data silently.
 */
#if defined(__BYTE_ORDER__)
#define CFW_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#elif defined(_MSC_VER)
#define CFW_LITTLE_ENDIAN 1 // every MSVC target is little-endian
#else
#define CFW_LITTLE_ENDIAN 0 // unknown toolchain: fail the pin below, don't guess
#endif

/**
 * @def DEFAULT_INITIALIZATION
 * @brief Sanctioned zero-initializer brace (`{0}`) for declaring any aggregate
 *        or scalar in a known-zero state.
 *
 * UNION CAVEAT: `{0}` zeroes only the FIRST union member (plus padding); for an
 * automatic object the trailing bytes of larger members stay unspecified — set
 * every field a reader consumes explicitly. C23 `{}` has the same union
 * semantics, so switching braces is not a fix.
 */
#define DEFAULT_INITIALIZATION {0}
/**
 * @def DEFAULT_INITIALIZATION_TYPE
 * @brief Sanctioned zeroed compound literal of @p type, e.g. for passing a
 *        zero-valued temporary as an argument. Same union caveat as
 *        @ref DEFAULT_INITIALIZATION.
 */
#define DEFAULT_INITIALIZATION_TYPE(type) (type) {0}

#if !defined(OS_ANDROID) && defined(__ANDROID__)
#define OS_ANDROID 1
#endif // OS_ANDROID

#if !defined(OS_APPLE) && defined(__APPLE__)
#define OS_APPLE 1
#endif // OS_APPLE

#if !defined(OS_EMSCRIPTEN) && defined(__EMSCRIPTEN__)
#define OS_EMSCRIPTEN 1
#endif // OS_EMSCRIPTEN

#if !defined(OS_LINUX) && defined(__linux__)
#define OS_LINUX 1
#endif // OS_LINUX

#if !defined(OS_UNIX) && (defined(__unix__) || defined(OS_LINUX) || defined(OS_APPLE))
#define OS_UNIX 1
#endif // OS_UNIX

#if !defined(OS_WINDOWS) && defined(_WIN32)
#define OS_WINDOWS 1
#endif // OS_WINDOWS

// === Typedefs and Enums ===
/**
 * @typedef Byte
 * @brief Unsigned 8-bit byte type.
 *
 * Use Byte for raw memory / byte-buffer access: as `unsigned char` it is the
 * aliasing-safe spelling allowed to alias any object. Use @ref U8 instead when
 * the value is a small unsigned integer used for arithmetic.
 */
typedef unsigned char Byte;

/**
 * @typedef I8
 * @brief Signed 8-bit integer.
 */
typedef int8_t I8;
/**
 * @typedef I16
 * @brief Signed 16-bit integer.
 */
typedef int16_t I16;
/**
 * @typedef I32
 * @brief Signed 32-bit integer.
 */
typedef int32_t I32;
/**
 * @typedef I64
 * @brief Signed 64-bit integer.
 */
typedef int64_t I64;

/**
 * @typedef F32
 * @brief 32-bit floating-point type.
 */
typedef float F32;
/**
 * @typedef F64
 * @brief 64-bit floating-point type.
 */
typedef double F64;

/**
 * @typedef U8
 * @brief Unsigned 8-bit integer.
 *
 * Use U8 for small unsigned integer arithmetic. Use @ref Byte instead for raw
 * memory / byte-buffer access where aliasing-safety matters.
 */
typedef uint8_t U8;
/**
 * @typedef U16
 * @brief Unsigned 16-bit integer.
 */
typedef uint16_t U16;
/**
 * @typedef U32
 * @brief Unsigned 32-bit integer.
 */
typedef uint32_t U32;
/**
 * @typedef U64
 * @brief Unsigned 64-bit integer.
 */
typedef uint64_t U64;

/**
 * @typedef FSize
 * @brief Floating-point size type (matches double).
 */
typedef F64 FSize;

/**
 * @typedef ISize
 * @brief Signed size type (matches ptrdiff_t).
 */
typedef ptrdiff_t ISize;
/**
 * @typedef USize
 * @brief Unsigned size type (matches size_t).
 */
typedef size_t USize;

// === Limits and Buffer Sizes (alphabetized) ===
// Contract for the *_DIGITS_MAX / *_STRING_SIZE pairs:
//   *_DIGITS_MAX  = decimal-digit count of the type's largest-magnitude value
//                   (e.g. I64_MAX has 19 digits, U64_MAX has 20). It is a COUNT,
//                   used only to SIZE a buffer; it is not a loop bound.
//   *_STRING_SIZE = full buffer to hold the decimal string, sign and NUL incl.
// HOW TO FORMAT (mandatory idiom): write digits BACK-TO-FRONT from the end of the
// buffer, dividing by 10:
//     char *p = buf + size; *--p = '\0';
//     do { *--p = (char) ('0' + n % 10); n /= 10; } while (n);
// This has no 10^i threshold, no top-decade special case, and cannot overflow.
// DO NOT drive a forward `for (i = 0; i < *_DIGITS_MAX; ++i) if (n < 10^i)` loop
// off *_DIGITS_MAX: it only tests up to 10^(*_DIGITS_MAX - 1), so a top-decade
// value (exactly *_DIGITS_MAX digits) never matches and the loop exits with the
// output UNSET. Extending the bound is not a fix either, since 10^*_DIGITS_MAX
// overflows U64. The forward-loop bug is currently present in the consumers
// (char.c, str.c, string.c) and should be migrated to the back-to-front idiom.
/** @def F32_EPSILON @brief Smallest F32 x with 1.0f + x != 1.0f (FLT_EPSILON). */
#define F32_EPSILON FLT_EPSILON
/** @def F32_LOWEST @brief Most negative finite F32; pairs with @ref F32_MAX. */
#define F32_LOWEST (-F32_MAX)
/** @def F32_MAX @brief Largest finite F32 (FLT_MAX). */
#define F32_MAX FLT_MAX
/**
 * @def F32_SMALLEST
 * @brief Smallest POSITIVE NORMAL F32 (FLT_MIN) — a value near zero, NOT the
 *        most negative F32 (that is @ref F32_LOWEST). Named to dodge the
 *        classic FLT_MIN misread.
 */
#define F32_SMALLEST FLT_MIN
/** @def F64_EPSILON @brief Smallest F64 x with 1.0 + x != 1.0 (DBL_EPSILON). */
#define F64_EPSILON DBL_EPSILON
/** @def F64_LOWEST @brief Most negative finite F64; pairs with @ref F64_MAX. */
#define F64_LOWEST (-F64_MAX)
/** @def F64_MAX @brief Largest finite F64 (DBL_MAX). */
#define F64_MAX DBL_MAX
/**
 * @def F64_PRINTF_STRING_SIZE
 * @brief Buffer for a general printf "%f" of an F64: DBL_MAX has 309 integer
 *        digits, so 309 + sign + '.' + 6 default-precision decimals + NUL =
 *        318. The extra bytes up to 330 are arbitrary headroom, not covering a
 *        specific case — do not "fix" the number downward. Use
 *        FSIZE_FIXED_STRING_SIZE only for the custom fixed formatter (below).
 */
#define F64_PRINTF_STRING_SIZE 330
/**
 * @def F64_SMALLEST
 * @brief Smallest POSITIVE NORMAL F64 (DBL_MIN) — a value near zero, NOT the
 *        most negative F64 (that is @ref F64_LOWEST).
 */
#define F64_SMALLEST DBL_MIN

// FSIZE_DIGITS_MAX / FSIZE_FIXED_STRING_SIZE budget the custom fixed-precision
// formatter (char_from_numbers_float_*: integer part + '.' + a few decimals),
// NOT printf "%f". A "%f" of a large-magnitude double overflows this buffer; use
// "%g"/"%e", or F64_PRINTF_STRING_SIZE above, for general printf.
/** @def FSIZE_DIGITS_MAX @brief Digit budget of the custom fixed formatter. */
#define FSIZE_DIGITS_MAX 24
/** @def FSIZE_FIXED_STRING_SIZE @brief Fixed-formatter buffer: digits + sign + NUL. */
#define FSIZE_FIXED_STRING_SIZE (FSIZE_DIGITS_MAX + 2) // sign + NUL

/** @def I8_MAX @brief Maximum I8 value (INT8_MAX). */
#define I8_MAX  INT8_MAX
/** @def I8_MIN @brief Minimum I8 value (INT8_MIN). */
#define I8_MIN  INT8_MIN
/** @def I16_MAX @brief Maximum I16 value (INT16_MAX). */
#define I16_MAX INT16_MAX
/** @def I16_MIN @brief Minimum I16 value (INT16_MIN). */
#define I16_MIN INT16_MIN
/** @def I32_MAX @brief Maximum I32 value (INT32_MAX). */
#define I32_MAX INT32_MAX
/** @def I32_MIN @brief Minimum I32 value (INT32_MIN). */
#define I32_MIN INT32_MIN
/** @def I64_MAX @brief Maximum I64 value (INT64_MAX). */
#define I64_MAX INT64_MAX
/** @def I64_MIN @brief Minimum I64 value (INT64_MIN). */
#define I64_MIN INT64_MIN

/**
 * @def ISIZE_DIGITS_MAX
 * @brief Decimal-digit count of the largest-magnitude ISize value,
 *        width-adaptive so a 32-bit target (e.g. wasm32) sizes its buffers
 *        correctly instead of failing a hard 64-bit pin. The _CHECK_DIV twin
 *        (10^(DIGITS_MAX - 1)) feeds the cross-check static_assert below and
 *        is #undef'd after it.
 */
#if PTRDIFF_MAX == INT64_MAX
#define ISIZE_DIGITS_MAX 19
#define _ISIZE_DIGITS_CHECK_DIV 1000000000000000000 // 10^(19 - 1)
#elif PTRDIFF_MAX == INT32_MAX
#define ISIZE_DIGITS_MAX 10
#define _ISIZE_DIGITS_CHECK_DIV 1000000000 // 10^(10 - 1)
#else
#error "unsupported ptrdiff_t width: add an ISIZE_DIGITS_MAX branch"
#endif
/** @def ISIZE_MAX @brief Maximum ISize value (PTRDIFF_MAX). */
#define ISIZE_MAX PTRDIFF_MAX // ISize is ptrdiff_t; track its width, not INT64
/** @def ISIZE_MIN @brief Minimum ISize value (PTRDIFF_MIN). */
#define ISIZE_MIN PTRDIFF_MIN
/** @def ISIZE_STRING_SIZE @brief ISize decimal-string buffer: digits + sign + NUL. */
#define ISIZE_STRING_SIZE (ISIZE_DIGITS_MAX + 2) // sign + NUL

/** @def U8_MAX @brief Maximum U8 value (UINT8_MAX). */
#define U8_MAX  UINT8_MAX
/** @def U16_MAX @brief Maximum U16 value (UINT16_MAX). */
#define U16_MAX UINT16_MAX
/** @def U32_MAX @brief Maximum U32 value (UINT32_MAX). */
#define U32_MAX UINT32_MAX
/** @def U64_MAX @brief Maximum U64 value (UINT64_MAX). */
#define U64_MAX UINT64_MAX

/**
 * @def USIZE_DIGITS_MAX
 * @brief Decimal-digit count of the largest USize value, width-adaptive like
 *        @ref ISIZE_DIGITS_MAX (with its own _CHECK_DIV twin for the
 *        cross-check static_assert).
 */
#if SIZE_MAX == UINT64_MAX
#define USIZE_DIGITS_MAX 20
#define _USIZE_DIGITS_CHECK_DIV 10000000000000000000ull // 10^(20 - 1)
#elif SIZE_MAX == UINT32_MAX
#define USIZE_DIGITS_MAX 10
#define _USIZE_DIGITS_CHECK_DIV 1000000000u // 10^(10 - 1)
#else
#error "unsupported size_t width: add a USIZE_DIGITS_MAX branch"
#endif
/** @def USIZE_MAX @brief Maximum USize value (SIZE_MAX). */
#define USIZE_MAX SIZE_MAX
/**
 * @def USIZE_STRING_SIZE
 * @brief USize decimal-string buffer. +2 (sign slack + NUL), not +1: unsigned
 *        never needs the sign byte, but the uniform width lets a buffer sized
 *        by USIZE_STRING_SIZE be reused on a signed formatting path without a
 *        one-byte overflow. The extra byte is deliberate.
 */
#define USIZE_STRING_SIZE (USIZE_DIGITS_MAX + 2)

// === Static Assertions ===
// Pin the alias widths and the byte order the rest of CFW (SIMD, serialization)
// relies on, so a broken toolchain fails to compile rather than miscomputing at
// runtime. ISize/USize widths are pinned by the #if/#error digit-count
// selection above (not by sizeof asserts), so a supported 32-bit target stays
// buildable.
static_assert(sizeof(Byte) == 1, "Byte must be exactly 1 byte");
static_assert(sizeof(F32) == 4, "F32 must be exactly 4 bytes");
static_assert(sizeof(F64) == 8, "F64 must be exactly 8 bytes");
static_assert(CFW_LITTLE_ENDIAN, "CFW (serialization, SIMD) assumes a little-endian target");
// Cross-check the hand-coded digit counts against the actual type width: dividing
// MAX by 10^(DIGITS_MAX - 1) yields a leading-digit value in [1,9] iff MAX has
// exactly DIGITS_MAX digits. The divisor comes from the same width branch that
// picked the digit count, so a mismatched branch fails here instead of silently
// mis-sizing *_STRING_SIZE buffers.
static_assert(USIZE_MAX / _USIZE_DIGITS_CHECK_DIV >= 1 && USIZE_MAX / _USIZE_DIGITS_CHECK_DIV <= 9,
    "USIZE_DIGITS_MAX no longer matches the USize width");
static_assert(ISIZE_MAX / _ISIZE_DIGITS_CHECK_DIV >= 1 && ISIZE_MAX / _ISIZE_DIGITS_CHECK_DIV <= 9,
    "ISIZE_DIGITS_MAX no longer matches the ISize width");
#undef _ISIZE_DIGITS_CHECK_DIV
#undef _USIZE_DIGITS_CHECK_DIV

#endif // TYPES_H