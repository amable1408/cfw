#include <test/test.h>

#include <http/service/multipart/multipart.h>

/*
 * Allocation-failure sweep for the multipart parser.
 *
 * WHY. http_service_multipart_parse borrows three kinds of block whose size the
 * remote uploader sets - the full boundary (Content-Type header), each part's
 * header block, and each extracted field - and every one of them was converted
 * to allocator_try_borrow with a documented decline: refuse the parse, or report
 * the field as absent. None of those branches ever ran under test_multipart.c,
 * which only counts blocks. A decline branch is unverified until the allocation
 * behind it is actually made to fail, and the branch that matters most here is
 * the one that must RELEASE a block borrowed earlier (full_boundary, when a
 * header block is refused) - the abort-to-leak trap.
 *
 * HOW. The suite already links with -Wl,--wrap=calloc / --wrap=free; this file
 * supplies the wrappers for its own binary (oom.exe - it cannot share
 * test_multipart.c's, which define the same symbols). The injector arms by SIZE
 * (tests/benchmark/test_oom.c's idiom): every fixture string below has a
 * distinct `length + 1`, so an arm size names exactly one borrow in the parse.
 * A ledger of live blocks per size is what proves a release happened.
 *
 * ARENA_IMPLEMENTATION build with the heap constructor (allocator == nullptr),
 * the production shape: allocator_try_borrow then calls memory_try_alloc, which
 * calls calloc directly and returns nullptr regardless of ERROR_CHECK_ENABLED.
 *
 * SINGLE-THREADED BY REQUIREMENT: the counters below are plain statics.
 */

/*==============================================================================
 * MARK: - Injector
 *============================================================================*/

#define _TEST_LEDGER_CAPACITY 4096

typedef struct {
    void    *pointer;
    USize   size;
    bool    freed;
} LedgerEntry;

static LedgerEntry  _ledger[_TEST_LEDGER_CAPACITY];
static USize        _ledger_count        = 0;
static USize        _ledger_overflow     = 0;
static USize        _double_free_count   = 0;
static USize        _live_count          = 0;
static USize        _fail_size           = 0;
static USize        _fail_ordinal        = 0;
static USize        _fail_seen           = 0;

void* __real_calloc(USize const count, USize const size);
void __real_free(void *const pointer);

void* __wrap_calloc(USize const count, USize const size) {
    // Guarded because count * size can wrap, and a wrapped product could
    // false-match _fail_size and inject a failure into an unrelated allocation.
    USize const bytes = (size != 0 && count > USIZE_MAX / size) ? USIZE_MAX : count * size;

    if (_fail_size != 0 && bytes == _fail_size) {
        _fail_seen += 1;

        if (_fail_seen >= _fail_ordinal) {
            return nullptr;
        }
    }

    void *const pointer = __real_calloc(count, size);

    if (pointer == nullptr) {
        return pointer;
    }

    _live_count += 1;

    if (_ledger_count >= _TEST_LEDGER_CAPACITY) {
        _ledger_overflow += 1;

        return pointer;
    }

    _ledger[_ledger_count].pointer  = pointer;
    _ledger[_ledger_count].size     = bytes;
    _ledger[_ledger_count].freed    = false;
    _ledger_count                  += 1;

    return pointer;
}

/* Newest-first, because the allocator recycles addresses: a forward scan matches
 * the stale record of a block freed earlier and reads a legal free as a double
 * free. */
static USize _ledger_find(void const *const pointer) {
    for (USize i = _ledger_count; i > 0; i -= 1) {
        if (_ledger[i - 1].pointer == pointer) {
            return i - 1;
        }
    }

    return _ledger_count;
}

void __wrap_free(void *const pointer) {
    if (pointer == nullptr) {
        __real_free(pointer);

        return;
    }

    _live_count -= 1;

    USize const found = _ledger_find(pointer);

    if (found != _ledger_count) {
        if (_ledger[found].freed) {
            _double_free_count += 1;
        }

        _ledger[found].freed = true;
    }

    __real_free(pointer);
}

/** @brief Blocks of exactly this size allocated and not yet released. */
static USize _live_of_size(USize const bytes) {
    USize live = 0;

    for (USize i = 0; i < _ledger_count; i += 1) {
        if (!_ledger[i].freed && _ledger[i].size == bytes) {
            live += 1;
        }
    }

    return live;
}

/** @brief Arm the injector: fail the ordinal-th calloc of exactly this size. */
static void _test_fail_arm(USize const size, USize const ordinal) {
    _fail_size    = size;
    _fail_ordinal = ordinal;
    _fail_seen    = 0;
}

static void _test_fail_disarm(void) {
    _fail_size    = 0;
    _fail_ordinal = 0;
    _fail_seen    = 0;
}

/*==============================================================================
 * MARK: - Fixture
 *============================================================================*/

/* Two parts. Every borrowed string has its own `length + 1`, so each arm size
 * below names exactly one borrow:
 *   boundary "BOUND"                 -> 5 + 3 = 8   (full_boundary: "--" + name + NUL)
 *   alpha name                        -> 6
 *   alpha content type "text/plain"   -> 11
 *   beta name                         -> 5
 *   beta filename "beta.bin"          -> 9
 *   beta content type (octet-stream)  -> 25
 *   the two header blocks             -> 71 and 105 (_TEST_HEADERS_*_BYTES) */
#define _TEST_BOUNDARY          "BOUND"
#define _TEST_BOUNDARY_BYTES    (2 + 5 + CHAR_END_CHARACTER)
#define _TEST_ALPHA_NAME_BYTES  (5 + CHAR_END_CHARACTER)
#define _TEST_HEADERS_ALPHA     "Content-Disposition: form-data; name=\"alpha\"\r\nContent-Type: text/plain"
#define _TEST_HEADERS_BETA      "Content-Disposition: form-data; name=\"beta\"; filename=\"beta.bin\"\r\nContent-Type: application/octet-stream"
#define _TEST_HEADERS_ALPHA_BYTES (sizeof(_TEST_HEADERS_ALPHA) - 1 + CHAR_END_CHARACTER)
#define _TEST_HEADERS_BETA_BYTES  (sizeof(_TEST_HEADERS_BETA) - 1 + CHAR_END_CHARACTER)
#define _TEST_PAYLOAD                                   \
    "--" _TEST_BOUNDARY "\r\n" _TEST_HEADERS_ALPHA "\r\n\r\none\r\n"  \
    "--" _TEST_BOUNDARY "\r\n" _TEST_HEADERS_BETA  "\r\n\r\ntwo\r\n"  \
    "--" _TEST_BOUNDARY "--\r\n"
#define _TEST_PAYLOAD_SIZE      (sizeof(_TEST_PAYLOAD) - 1)

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_control(Test *const test) {
    test_case_begin(test, "unarmed: both parts parse with every field (anchor)");

    USize const live_before = _live_count;

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    bool const parsed = http_service_multipart_parse(&multipart, _TEST_BOUNDARY, (Byte const*) _TEST_PAYLOAD, _TEST_PAYLOAD_SIZE);

    test_expect_true(test, "payload parsed", parsed);
    test_expect_u(test, "two parts", 2, al_multipart_get_size(&multipart.parts));

    HTTP_Service_Multipart_Node const *const alpha = http_service_multipart_node_get(&multipart, "alpha");
    HTTP_Service_Multipart_Node const *const beta  = http_service_multipart_node_get(&multipart, "beta");

    test_expect_not_null(test, "alpha found", alpha);
    test_expect_not_null(test, "beta found", beta);

    if (alpha != nullptr && beta != nullptr) {
        test_expect_string(test, "alpha content type", "text/plain", alpha->content_type);
        test_expect_null(test, "alpha has no filename", alpha->filename);
        test_expect_u(test, "alpha body size", 3, alpha->size);
        test_expect_string(test, "beta filename", "beta.bin", beta->filename);
        test_expect_string(test, "beta content type", "application/octet-stream", beta->content_type);
    }

    // The arm sizes below are only meaningful if these blocks really exist.
    test_expect_u(test, "alpha's name is one 6-byte block", 1, _live_of_size(_TEST_ALPHA_NAME_BYTES));
    test_expect_u(test, "full_boundary was released by the parse", 0, _live_of_size(_TEST_BOUNDARY_BYTES));
    test_expect_u(test, "beta's header block was released by the parse", 0, _live_of_size(_TEST_HEADERS_BETA_BYTES));

    http_service_multipart_uninit(&multipart);

    test_expect_u(test, "nothing left allocated after uninit", live_before, _live_count);

    test_case_end(test);
}

static void _test_boundary_refused(Test *const test) {
    test_case_begin(test, "armed on full_boundary: the parse is refused before it starts");

    USize const live_before = _live_count;

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    _test_fail_arm(_TEST_BOUNDARY_BYTES, 1);

    bool const parsed = http_service_multipart_parse(&multipart, _TEST_BOUNDARY, (Byte const*) _TEST_PAYLOAD, _TEST_PAYLOAD_SIZE);

    // Read before disarm, which zeroes the counter.
    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_false(test, "parse reports failure", parsed);
    test_expect_u(test, "no part was collected", 0, al_multipart_get_size(&multipart.parts));

    http_service_multipart_uninit(&multipart);

    test_expect_u(test, "nothing left allocated", live_before, _live_count);

    test_case_end(test);
}

static void _test_headers_refused_keeps_earlier_parts(Test *const test) {
    test_case_begin(test, "armed on beta's header block: parse stops, alpha stays, full_boundary released");

    USize const live_before = _live_count;

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    _test_fail_arm(_TEST_HEADERS_BETA_BYTES, 1);

    bool const parsed = http_service_multipart_parse(&multipart, _TEST_BOUNDARY, (Byte const*) _TEST_PAYLOAD, _TEST_PAYLOAD_SIZE);

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_false(test, "parse reports failure", parsed);
    test_expect_u(test, "the part parsed before the refusal is kept", 1, al_multipart_get_size(&multipart.parts));

    HTTP_Service_Multipart_Node const *const alpha = http_service_multipart_node_get(&multipart, "alpha");

    test_expect_not_null(test, "alpha is still addressable", alpha);

    if (alpha != nullptr) {
        test_expect_string(test, "alpha content type intact", "text/plain", alpha->content_type);
    }

    // The block borrowed BEFORE the refusal - the abort-to-leak trap.
    test_expect_u(test, "full_boundary was released on the refusal path", 0, _live_of_size(_TEST_BOUNDARY_BYTES));

    http_service_multipart_uninit(&multipart);

    test_expect_u(test, "nothing left allocated after uninit", live_before, _live_count);

    test_case_end(test);
}

static void _test_field_refused_is_absent(Test *const test) {
    test_case_begin(test, "armed on alpha's name: the field is absent, the part and the rest survive");

    USize const live_before = _live_count;

    HTTP_Service_Multipart multipart = http_service_multipart_init();

    _test_fail_arm(_TEST_ALPHA_NAME_BYTES, 1);

    bool const parsed = http_service_multipart_parse(&multipart, _TEST_BOUNDARY, (Byte const*) _TEST_PAYLOAD, _TEST_PAYLOAD_SIZE);

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_true(test, "parse still succeeds", parsed);
    test_expect_u(test, "both parts collected", 2, al_multipart_get_size(&multipart.parts));

    HTTP_Service_Multipart_Node const *const first = al_multipart_at(&multipart.parts, 0);

    test_expect_null(test, "the refused name reads as absent", first->name);
    test_expect_string(test, "the other fields of that part are intact", "text/plain", first->content_type);
    test_expect_u(test, "its body is intact", 3, first->size);
    test_expect_null(test, "the part is no longer addressable by name", http_service_multipart_node_get(&multipart, "alpha"));
    test_expect_not_null(test, "the next part is unaffected", http_service_multipart_node_get(&multipart, "beta"));

    http_service_multipart_uninit(&multipart);

    test_expect_u(test, "nothing left allocated after uninit", live_before, _live_count);

    test_case_end(test);
}

/* Arena arm: proves the DECLINE path (not the heap-injector's abort-free
 * calloc/free ledger above) under a real too-small arena. multipart.h's Arena
 * Sizing section says only the max_parts reservation at parse entry can
 * ABORT on an exhausted arena; every later per-part borrow (full_boundary,
 * a header block, a field) goes through allocator_try_borrow and DECLINES -
 * parse returns false, or the field reads absent - never an abort. Sizing
 * the arena to cover the reservation and alpha's borrows but not beta's
 * larger header block borrow exercises exactly that later decline. */
#define _TEST_ARENA_BYTES 260

static void _test_arena_headers_refused_keeps_earlier_parts(Test *const test) {
    test_case_begin(test, "linear arena too small for beta's header borrow declines - parse fails, alpha stays, no abort");

    Arena arena = arena_init_1(_TEST_ARENA_BYTES, ARENA_TYPE_LINEAR);

    HTTP_Service_Multipart multipart = http_service_multipart_alloc_init(&arena);

    multipart.max_parts = 2;

    bool const parsed = http_service_multipart_parse(&multipart, _TEST_BOUNDARY, (Byte const*) _TEST_PAYLOAD, _TEST_PAYLOAD_SIZE);

    test_expect_false(test, "parse reports failure, not an abort", parsed);
    test_expect_u(test, "the part parsed before the refusal is kept", 1, al_multipart_get_size(&multipart.parts));

    HTTP_Service_Multipart_Node const *const alpha = http_service_multipart_node_get(&multipart, "alpha");

    test_expect_not_null(test, "alpha is still addressable", alpha);

    if (alpha != nullptr) {
        test_expect_string(test, "alpha content type intact", "text/plain", alpha->content_type);
    }

    test_expect_null(test, "beta was never added", http_service_multipart_node_get(&multipart, "beta"));

    http_service_multipart_uninit(&multipart);
    arena_uninit(&arena, ARENA_TYPE_LINEAR);

    test_case_end(test);
}

static void _test_ledger_sane(Test *const test) {
    test_case_begin(test, "the ledger itself is trustworthy");

    test_expect_u(test, "no double free was observed", 0, _double_free_count);
    test_expect_u(test, "the ledger never overflowed", 0, _ledger_overflow);

    test_case_end(test);
}

/*==============================================================================
 * MARK: - Main
 *============================================================================*/

int main(void) {
    LogConfig const log_config = {
        .level             = LOG_LEVEL_ERROR,
        .stream            = stdout,
        .timestamp_enabled = true,
        .autoflush         = true
    };

    log_init(log_config);

    Test test = test_init("tests/http/service/multipart/test_oom.c");

    test_suite_begin(&test, "http_service_multipart allocation-failure sweep");
    _test_control(&test);
    _test_boundary_refused(&test);
    _test_headers_refused_keeps_earlier_parts(&test);
    _test_field_refused_is_absent(&test);
    _test_arena_headers_refused_keeps_earlier_parts(&test);
    _test_ledger_sane(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}