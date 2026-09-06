#include <test/test.h>

#include <http/service/body_parser/body_parser.h>

/*
 * Allocation-failure sweep for the form-body parser.
 *
 * WHY. _http_service_body_parser_parse_form borrows two blocks per pair - the
 * key and the value, both sized by the request body - through
 * allocator_try_borrow, and skips the pair when either is refused. The parser
 * is constructed by http_service_body_parser_init(void) with NO arena, so those
 * borrows are plain heap blocks: a skip that abandoned the one that DID succeed
 * would turn an attacker-triggered abort into an attacker-triggered leak, the
 * same denial of service by a slower route. That release is the property under
 * test, and it never executed before this file - a decline branch is unverified
 * until the allocation behind it is actually made to fail.
 *
 * HOW. memory_try_alloc calls calloc directly, so -Wl,--wrap=calloc,--wrap=free is the
 * seam (tests/benchmark/test_oom.c's idiom, size-armed; free is wrapped too, to verify
 * the half that DID succeed is actually released rather than merely un-tracked). Every
 * key and value in the
 * fixture has its own `length + 1`, so an arm size names exactly one borrow. The
 * ledger counts live blocks per size, which is how "the other half was released"
 * is proven rather than assumed.
 *
 * COVERAGE LIMIT, stated so this file does not overclaim: the third decline in
 * that function - map_char_char_add refusing the adopted pair - is NOT reached
 * here. Forcing it needs the map's own growth allocation to fail, whose size is
 * an implementation detail of al_char; that branch is verified by reading only.
 * The map's OWN allocation-failure paths - not this call site, but the borrows
 * inside map_char_char itself - are pinned separately, by
 * tests/container/map/test_oom.c.
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

    USize const found = _ledger_find(pointer);

    /* Decrement only for a genuine first free of a tracked block: a double free
     * found already-marked freed must not decrement again, or it hides itself
     * by making _live_count agree with the ledger despite the corruption - the
     * very thing _test_ledger_sane exists to catch. */
    if (found != _ledger_count) {
        if (_ledger[found].freed) {
            _double_free_count += 1;
        }
        else {
            _live_count -= 1;
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

/** @brief Arm the injector: fail the ordinal-th calloc of exactly this size, and every matching call after it (the check below is >=, not ==). */
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

/* Three pairs, every key and value with its own `length + 1`:
 *   "alpha" -> 6   "1"   -> 2
 *   "beta"  -> 5   "22"  -> 3
 *   "gamma" -> 6   "333" -> 4
 * "alpha" and "gamma" share a size on purpose - the arm points are beta's, whose
 * key (5) and value (3) match nothing else in the parse. */
#define _TEST_BODY              "alpha=1&beta=22&gamma=333"
#define _TEST_BODY_SIZE         (sizeof(_TEST_BODY) - 1)
#define _TEST_BETA_KEY_BYTES    (4 + CHAR_END_CHARACTER)
#define _TEST_BETA_VALUE_BYTES  (2 + CHAR_END_CHARACTER)

static bool _parse(HTTP_Service_Body_Parser *const parser) {
    return http_service_body_parser_parse(parser, HTTP_SERVICE_BODY_PARSER_CONTENT_TYPE_FORM, (Byte const*) _TEST_BODY, _TEST_BODY_SIZE);
}

/*==============================================================================
 * MARK: - Cases
 *============================================================================*/

static void _test_control(Test *const test) {
    test_case_begin(test, "unarmed: all three pairs land (anchor)");

    USize const live_before = _live_count;

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    test_expect_true(test, "parse succeeds", _parse(&parser));
    test_expect_true(test, "the body is a form", parser.is_form);
    test_expect_string(test, "alpha", "1", http_service_body_parser_form_get(&parser, "alpha"));
    test_expect_string(test, "beta", "22", http_service_body_parser_form_get(&parser, "beta"));
    test_expect_string(test, "gamma", "333", http_service_body_parser_form_get(&parser, "gamma"));

    // The arm sizes below are only meaningful if these blocks really exist.
    test_expect_u(test, "beta's key is one 5-byte block", 1, _live_of_size(_TEST_BETA_KEY_BYTES));
    test_expect_u(test, "beta's value is one 3-byte block", 1, _live_of_size(_TEST_BETA_VALUE_BYTES));

    http_service_body_parser_uninit(&parser);

    test_expect_u(test, "nothing left allocated after uninit", live_before, _live_count);

    test_case_end(test);
}

static void _test_value_refused_releases_key(Test *const test) {
    test_case_begin(test, "armed on beta's value: the pair is skipped and its key released");

    USize const live_before = _live_count;

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    _test_fail_arm(_TEST_BETA_VALUE_BYTES, 1);

    bool const parsed = _parse(&parser);

    // Read before disarm, which zeroes the counter.
    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_true(test, "parse still succeeds", parsed);
    test_expect_null(test, "beta is absent", http_service_body_parser_form_get(&parser, "beta"));
    test_expect_string(test, "alpha, before it, is present", "1", http_service_body_parser_form_get(&parser, "alpha"));
    test_expect_string(test, "gamma, after it, is present", "333", http_service_body_parser_form_get(&parser, "gamma"));

    // The half that DID succeed - the abort-to-leak trap.
    test_expect_u(test, "beta's key was released, not abandoned", 0, _live_of_size(_TEST_BETA_KEY_BYTES));

    http_service_body_parser_uninit(&parser);

    test_expect_u(test, "nothing left allocated after uninit", live_before, _live_count);

    test_case_end(test);
}

static void _test_key_refused_releases_value(Test *const test) {
    test_case_begin(test, "armed on beta's key: the pair is skipped and its value released");

    USize const live_before = _live_count;

    HTTP_Service_Body_Parser parser = http_service_body_parser_init();

    _test_fail_arm(_TEST_BETA_KEY_BYTES, 1);

    bool const parsed = _parse(&parser);

    USize const fired = _fail_seen;

    _test_fail_disarm();

    test_expect_u(test, "the injection fired", 1, fired);
    test_expect_true(test, "parse still succeeds", parsed);
    test_expect_null(test, "beta is absent", http_service_body_parser_form_get(&parser, "beta"));
    test_expect_string(test, "alpha is present", "1", http_service_body_parser_form_get(&parser, "alpha"));
    test_expect_string(test, "gamma is present", "333", http_service_body_parser_form_get(&parser, "gamma"));

    // The value is borrowed AFTER the key, so it was taken and must be given back.
    test_expect_u(test, "beta's value was released, not abandoned", 0, _live_of_size(_TEST_BETA_VALUE_BYTES));

    http_service_body_parser_uninit(&parser);

    test_expect_u(test, "nothing left allocated after uninit", live_before, _live_count);

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

    Test test = test_init("tests/http/service/body_parser/test_oom.c");

    test_suite_begin(&test, "http_service_body_parser allocation-failure sweep");
    _test_control(&test);
    _test_value_refused_releases_key(&test);
    _test_key_refused_releases_value(&test);
    _test_ledger_sane(&test);
    test_suite_end(&test);

    return test_uninit(&test);
}