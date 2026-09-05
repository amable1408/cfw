/* ============================================================================
 *  Crypto Password Implementation
 *  --------------------------------------------------------------------------
 *  @file    password.c
 *  @brief   Stored-record format, salt, constant-time verify, rehash policy.
 * ============================================================================
 */
#include <crypto/password/password.h>

/*==============================================================================
 * MARK: - Internal Constants
 *============================================================================*/

/** @brief Field separator between scheme, iterations, salt, and hash. */
#define _CRYPTO_PASSWORD_FIELD_SEPARATOR '$'

/*==============================================================================
 * MARK: - Internal Implementations
 *============================================================================*/

/** @brief Constant-time byte-buffer equality: folds every byte, no early
 *  exit, so a mismatch position cannot leak through timing. Kept U8-typed by
 *  design for the derived-key compare; for text secrets use the canonical
 *  char_compare_equal_comptime_* family instead. */
static bool _crypto_password_bytes_equal(U8 const *const left, U8 const *const right, USize const size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "left", (void*) left);
    error_check_null(LOG_METADATA, "right", (void*) right);

    U8 value = 0;

    for (USize i = 0; i < size; i += 1) {
        value |= (U8) (left[i] ^ right[i]);
    }

    trace_log_pop();

    return value == 0;
}

/** @brief Advance to the next separator-delimited field of a stored record.
 *  Returns false once offset has passed data_size. */
static bool _crypto_password_field_next(char const *const data, USize const data_size, USize *const offset, char const **const field, USize *const field_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "offset", (void*) offset);
    error_check_null(LOG_METADATA, "field", (void*) field);
    error_check_null(LOG_METADATA, "field_size", (void*) field_size);

    if (*offset > data_size) {
        trace_log_pop();

        return false;
    }

    USize index = *offset;

    while (index < data_size && data[index] != _CRYPTO_PASSWORD_FIELD_SEPARATOR) {
        index += 1;
    }

    *field      = data + *offset;
    *field_size = index - *offset;
    *offset     = index < data_size ? index + 1 : index;

    trace_log_pop();

    return true;
}

/** @brief Parse a decimal iteration field, rejecting non-digits, empty
 *  fields, and any value of zero or above CRYPTO_PASSWORD_ITERATIONS_MAX —
 *  the cap keeps a tampered record from collapsing the KDF cost (via int
 *  wrap-around downstream) or turning a login into a CPU-burning request. */
static bool _crypto_password_iterations_parse(char const *const data, USize const data_size, USize *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "value", (void*) value);

    if (data_size == 0) {
        trace_log_pop();

        return false;
    }

    USize temp = 0;

    for (USize i = 0; i < data_size; i += 1) {
        if (data[i] < '0' || data[i] > '9') {
            trace_log_pop();

            return false;
        }

        temp = (temp * 10) + (USize) (data[i] - '0');

        if (temp > CRYPTO_PASSWORD_ITERATIONS_MAX) {
            trace_log_pop();

            return false;
        }
    }

    if (temp == 0) {
        trace_log_pop();

        return false;
    }

    *value = temp;

    trace_log_pop();

    return true;
}

/** @brief Parse a full stored record into its iteration count and raw salt
 *  and hash bytes. Strict: exactly four fields, the exact scheme tag, exact
 *  salt/hash hex widths. Any deviation returns false. */
static bool _crypto_password_parse(char const *const stored, USize *const iterations, U8 *const salt, U8 *const hash) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "stored", (void*) stored);
    error_check_null(LOG_METADATA, "iterations", (void*) iterations);
    error_check_null(LOG_METADATA, "salt", (void*) salt);
    error_check_null(LOG_METADATA, "hash", (void*) hash);

    USize const stored_size = char_length(stored);

    char const *scheme          = nullptr;
    char const *iterations_text = nullptr;
    char const *salt_text       = nullptr;
    char const *hash_text       = nullptr;
    USize scheme_size           = 0;
    USize iterations_size       = 0;
    USize salt_size             = 0;
    USize hash_size             = 0;
    USize offset                = 0;

    bool const parsed = _crypto_password_field_next(stored, stored_size, &offset, &scheme, &scheme_size)  &&
        _crypto_password_field_next(stored, stored_size, &offset, &iterations_text, &iterations_size)     &&
        _crypto_password_field_next(stored, stored_size, &offset, &salt_text, &salt_size)                 &&
        _crypto_password_field_next(stored, stored_size, &offset, &hash_text, &hash_size);

    if (!parsed || offset != stored_size) {
        trace_log_pop();

        return false;
    }

    // A trailing separator would otherwise parse identically to the canonical
    // record (the 4th scan stops at it and offset still lands on stored_size);
    // reject it so no two distinct byte strings verify the same, matching the
    // canonical-decode stance of the encoding modules.
    if (stored_size > 0 && stored[stored_size - 1] == _CRYPTO_PASSWORD_FIELD_SEPARATOR) {
        trace_log_pop();

        return false;
    }

    bool const success = char_compare_equal_2((char*) scheme, scheme_size, CRYPTO_PASSWORD_SCHEME, CHAR_STATIC_SIZE(CRYPTO_PASSWORD_SCHEME)) &&
        _crypto_password_iterations_parse(iterations_text, iterations_size, iterations)                                                      &&
        result_is_success(encoding_hex_decode_1(salt_text, salt_size, salt, CRYPTO_PASSWORD_SALT_BYTE_COUNT))                                &&
        result_is_success(encoding_hex_decode_1(hash_text, hash_size, hash, CRYPTO_PASSWORD_HASH_BYTE_COUNT));

    trace_log_pop();

    return success;
}

/** @brief Shared record builder: salt, derive, and format. Returns an empty
 *  String (from the given allocator context) on any failure — fail closed. */
#ifdef ARENA_IMPLEMENTATION
static String _crypto_password_hash(char const *const password, USize const password_size, USize const iterations, Arena *const allocator)
#else
static String _crypto_password_hash(char const *const password, USize const password_size, USize const iterations)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "password", (void*) password);

#ifndef ARENA_IMPLEMENTATION
    Arena *const allocator = nullptr;
#endif // ARENA_IMPLEMENTATION

#ifdef ARENA_IMPLEMENTATION
    String string = !memory_empty(allocator) ? string_alloc_init_1(allocator) : string_init_1();
#else
    String string = string_init_1();

    (void) allocator;
#endif // ARENA_IMPLEMENTATION

    if (iterations == 0 || iterations > CRYPTO_PASSWORD_ITERATIONS_MAX) {
        trace_log_pop();

        return string;
    }

    U8 salt[CRYPTO_PASSWORD_SALT_BYTE_COUNT] = DEFAULT_INITIALIZATION;
    U8 hash[CRYPTO_PASSWORD_HASH_BYTE_COUNT] = DEFAULT_INITIALIZATION;

    if (result_is_error(crypto_random_bytes(salt, CRYPTO_PASSWORD_SALT_BYTE_COUNT))) {
        trace_log_pop();

        return string;
    }

    if (result_is_error(crypto_kdf_pbkdf2_sha256(password, password_size, salt, CRYPTO_PASSWORD_SALT_BYTE_COUNT, iterations, hash, CRYPTO_PASSWORD_HASH_BYTE_COUNT))) {
        trace_log_pop();

        return string;
    }

    char salt_hex[ENCODING_HEX_SIZE(CRYPTO_PASSWORD_SALT_BYTE_COUNT) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;
    char hash_hex[ENCODING_HEX_SIZE(CRYPTO_PASSWORD_HASH_BYTE_COUNT) + CHAR_END_CHARACTER] = DEFAULT_INITIALIZATION;
    char separator                                                                         = _CRYPTO_PASSWORD_FIELD_SEPARATOR;
    char *const iterations_text                                                            = char_new_from_numbers_uint_1(iterations);

    encoding_hex_encode_1(salt, CRYPTO_PASSWORD_SALT_BYTE_COUNT, salt_hex);
    encoding_hex_encode_1(hash, CRYPTO_PASSWORD_HASH_BYTE_COUNT, hash_hex);

    string_add_last_2(&string, (char*) CRYPTO_PASSWORD_SCHEME, CHAR_STATIC_SIZE(CRYPTO_PASSWORD_SCHEME));
    string_add_last_2(&string, &separator, 1);
    string_add_last_2(&string, iterations_text, char_length(iterations_text));
    string_add_last_2(&string, &separator, 1);
    string_add_last_2(&string, salt_hex, ENCODING_HEX_SIZE(CRYPTO_PASSWORD_SALT_BYTE_COUNT));
    string_add_last_2(&string, &separator, 1);
    string_add_last_2(&string, hash_hex, ENCODING_HEX_SIZE(CRYPTO_PASSWORD_HASH_BYTE_COUNT));

    char_delete(iterations_text);

    trace_log_pop();

    return string;
}

/** @brief Shared verifier: parse, re-derive, constant-time compare. */
static bool _crypto_password_verify(char const *const password, USize const password_size, char const *const stored) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "password", (void*) password);
    error_check_null(LOG_METADATA, "stored", (void*) stored);

    USize iterations                                = 0;
    U8 salt[CRYPTO_PASSWORD_SALT_BYTE_COUNT]        = DEFAULT_INITIALIZATION;
    U8 stored_hash[CRYPTO_PASSWORD_HASH_BYTE_COUNT] = DEFAULT_INITIALIZATION;
    U8 candidate[CRYPTO_PASSWORD_HASH_BYTE_COUNT]   = DEFAULT_INITIALIZATION;

    bool const success = _crypto_password_parse(stored, &iterations, salt, stored_hash)                                                                                     &&
        result_is_success(crypto_kdf_pbkdf2_sha256(password, password_size, salt, CRYPTO_PASSWORD_SALT_BYTE_COUNT, iterations, candidate, CRYPTO_PASSWORD_HASH_BYTE_COUNT)) &&
        _crypto_password_bytes_equal(stored_hash, candidate, CRYPTO_PASSWORD_HASH_BYTE_COUNT);

    trace_log_pop();

    return success;
}

/*==============================================================================
 * MARK: - Public Implementations
 *============================================================================*/

#ifdef ARENA_IMPLEMENTATION
String crypto_password_alloc_hash_1(char const *const password, USize const iterations, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "password", (void*) password);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String const string = _crypto_password_hash(password, char_length(password), iterations, allocator);

    trace_log_pop();

    return string;
}

String crypto_password_alloc_hash_2(Str const *const password, USize const iterations, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "password", (void*) password);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    // An empty Str carries null data; empty is a legal password value, so map
    // it to "" instead of letting the worker's null check abort.
    char const *const data = str_empty(password) ? "" : str_get_data((Str*) password);

    String const string = _crypto_password_hash(data, str_get_size((Str*) password), iterations, allocator);

    trace_log_pop();

    return string;
}

String crypto_password_alloc_hash_3(String const *const password, USize const iterations, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "password", (void*) password);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    // An empty String carries null data; empty is a legal password value, so
    // map it to "" instead of letting the worker's null check abort.
    char const *const data = string_empty(password) ? "" : string_get_data((String*) password);

    String const string = _crypto_password_hash(data, string_get_size((String*) password), iterations, allocator);

    trace_log_pop();

    return string;
}
#endif // ARENA_IMPLEMENTATION

String crypto_password_hash_1(char const *const password, USize const iterations) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "password", (void*) password);

#ifdef ARENA_IMPLEMENTATION
    String const string = _crypto_password_hash(password, char_length(password), iterations, nullptr);
#else
    String const string = _crypto_password_hash(password, char_length(password), iterations);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return string;
}

String crypto_password_hash_2(Str const *const password, USize const iterations) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "password", (void*) password);

    // An empty Str carries null data; empty is a legal password value, so map
    // it to "" instead of letting the worker's null check abort.
    char const *const data = str_empty(password) ? "" : str_get_data((Str*) password);

#ifdef ARENA_IMPLEMENTATION
    String const string = _crypto_password_hash(data, str_get_size((Str*) password), iterations, nullptr);
#else
    String const string = _crypto_password_hash(data, str_get_size((Str*) password), iterations);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return string;
}

String crypto_password_hash_3(String const *const password, USize const iterations) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "password", (void*) password);

    // An empty String carries null data; empty is a legal password value, so
    // map it to "" instead of letting the worker's null check abort.
    char const *const data = string_empty(password) ? "" : string_get_data((String*) password);

#ifdef ARENA_IMPLEMENTATION
    String const string = _crypto_password_hash(data, string_get_size((String*) password), iterations, nullptr);
#else
    String const string = _crypto_password_hash(data, string_get_size((String*) password), iterations);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return string;
}

bool crypto_password_needs_rehash(char const *const stored, USize const iterations) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "stored", (void*) stored);

    USize stored_iterations                  = 0;
    U8 salt[CRYPTO_PASSWORD_SALT_BYTE_COUNT] = DEFAULT_INITIALIZATION;
    U8 hash[CRYPTO_PASSWORD_HASH_BYTE_COUNT] = DEFAULT_INITIALIZATION;

    bool const success = _crypto_password_parse(stored, &stored_iterations, salt, hash) && stored_iterations != iterations;

    trace_log_pop();

    return success;
}

bool crypto_password_verify_1(char const *const password, char const *const stored) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "password", (void*) password);

    bool const success = _crypto_password_verify(password, char_length(password), stored);

    trace_log_pop();

    return success;
}

bool crypto_password_verify_2(Str const *const password, char const *const stored) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "password", (void*) password);

    // An empty Str carries null data; empty is a legal password value, so map
    // it to "" instead of letting the worker's null check abort.
    char const *const data = str_empty(password) ? "" : str_get_data((Str*) password);

    bool const success = _crypto_password_verify(data, str_get_size((Str*) password), stored);

    trace_log_pop();

    return success;
}

bool crypto_password_verify_3(String const *const password, char const *const stored) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "password", (void*) password);

    // An empty String carries null data; empty is a legal password value, so
    // map it to "" instead of letting the worker's null check abort.
    char const *const data = string_empty(password) ? "" : string_get_data((String*) password);

    bool const success = _crypto_password_verify(data, string_get_size((String*) password), stored);

    trace_log_pop();

    return success;
}