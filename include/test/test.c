#include <test/test.h>

/*==============================================================================
 * MARK: - Helpers
 *============================================================================*/
static char* _test_label(char const *const name);
static USize _test_now(void);
static void _test_print_count_line(char const *const label, USize const passed_count, USize const failure_count, USize const total_count);
static void _test_print_detail(char const *const detail);
static void _test_print_result(U8 const depth, bool const passed, char const *const name, FSize const elapsed_ms);
static FILE* _test_stream(void);

static USize _test_api_effective_failure_count(Test const *const self) {
    USize const covered_count = self->api_pass_count + self->api_failure_count;

    if (self->api_total_count <= covered_count) {
        return self->api_failure_count;
    }

    return self->api_failure_count + self->api_total_count - covered_count;
}

static FSize _test_elapsed_ms(USize const started_at) {
    USize const elapsed = _test_now() - started_at;

    return ((FSize) elapsed * 1000.0) / (FSize) CLOCKS_PER_SEC;
}

static FSize _test_elapsed_s(USize const started_at) {
    return _test_elapsed_ms(started_at) / 1000.0;
}

static char* _test_label(char const *const name) {
    return (char*) (name == nullptr ? "(unnamed)" : name);
}

static USize _test_now(void) {
    clock_t const now = clock();

    // clock() reports failure as (clock_t) -1; feeding that through the USize
    // cast would make every elapsed display absurd. The value is display-only.
    return now == (clock_t) -1 ? 0 : (USize) now;
}

static void _test_print_count_line(char const *const label, USize const passed_count, USize const failure_count, USize const total_count) {
    FILE *const stream = _test_stream();

    if (stream == nullptr) {
        return;
    }

    fprintf(stream, "%-12s %llu passed, %llu failed, %llu total\n",
        label,
        (unsigned long long) passed_count,
        (unsigned long long) failure_count,
        (unsigned long long) total_count);
}

static void _test_print_detail(char const *const detail) {
    FILE *const stream = _test_stream();

    if (detail == nullptr || stream == nullptr) {
        return;
    }

    fprintf(stream, "        %s\n", detail);
}

static void _test_print_result(U8 const depth, bool const passed, char const *const name, FSize const elapsed_ms) {
    FILE *const stream = _test_stream();

    if (stream == nullptr) {
        return;
    }

    for (U8 i = 0; i < depth; i += 1) {
        fprintf(stream, "  ");
    }

    fprintf(stream, "%s %s (%.0f ms)\n",
        passed ? "+" : "x",
        _test_label(name),
        elapsed_ms);
}

static bool _test_record(Test *const self, char const *const name, bool const passed, char const *const detail) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->assertion_count += 1;

    if (passed) {
        self->assertion_pass_count += 1;
    } else {
        self->assertion_failure_count       += 1;
        self->current_suite_failure_count   += 1;
        self->current_case_failure_count    += 1;
        self->current_section_failure_count += 1;
    }

    if (self->verbose || !passed) {
        _test_print_result(3, passed, name, 0.0);

        if (!passed) {
            _test_print_detail(detail);
        }
    }

    trace_log_pop();

    return passed;
}

static FILE* _test_stream(void) {
    // log_get_stream exit(1)s when log_init was never called; the harness must
    // keep reporting (and produce its exit code) regardless, so uninitialized
    // programs fall back to stdout. An initialized log's nullptr stream means
    // output is DISABLED - honored by returning nullptr, which every print
    // site skips. Counters and the exit code are unaffected either way.
    if (!log_is_initialized()) {
        return stdout;
    }

    return log_get_stream();
}

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
void test_api_begin(Test *const self, char const *const name, USize const total_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // Accumulate across blocks: the old per-block reset erased an earlier
    // block's recorded test_api_fail from the exit code - a false green.
    self->api_count        = 0;
    self->api_total_count += total_count;

    if (self->verbose) {
        FILE *const stream = _test_stream();

        if (stream != nullptr) {
            fprintf(stream, "  api: %s (%llu functions)\n",
                _test_label(name),
                (unsigned long long) total_count);
        }
    }

    trace_log_pop();
}

void test_api_end(Test *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->verbose) {
        FILE    *const  stream            = _test_stream();
        USize   const   api_failure_count = _test_api_effective_failure_count(self);

        if (stream != nullptr) {
            fprintf(stream, "  api %s: %llu passed, %llu failed, %llu total\n",
                api_failure_count == 0 ? "passed" : "failed",
                (unsigned long long) self->api_pass_count,
                (unsigned long long) api_failure_count,
                (unsigned long long) self->api_total_count);
        }
    }

    trace_log_pop();
}

bool test_api_fail(Test *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->api_count         += 1;
    self->api_failure_count += 1;

    _test_print_result(2, false, name, 0.0);

    trace_log_pop();

    return false;
}

bool test_api_pass(Test *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->api_count      += 1;
    self->api_pass_count += 1;

    if (self->verbose) {
        _test_print_result(2, true, name, 0.0);
    }

    trace_log_pop();

    return true;
}

void test_case_begin(Test *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->case_count                 += 1;
    self->case_name                  = name;
    self->case_started_at            = _test_now();
    self->current_case_failure_count = 0;
    self->case_open                  = true;

    trace_log_pop();
}

void test_case_end(Test *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // An end without a matching begin must not inflate the pass counters past
    // their totals (and would time against a never-set started_at).
    if (!self->case_open) {
        trace_log_pop();

        return;
    }

    bool const passed = self->current_case_failure_count == 0;

    if (passed) {
        self->case_pass_count += 1;
    } else {
        self->case_failure_count += 1;
    }

    _test_print_result(2, passed, self->case_name, _test_elapsed_ms(self->case_started_at));

    self->case_name = nullptr;
    self->case_open = false;

    trace_log_pop();
}

bool test_expect_bool(Test *const self, char const *const name, bool const expected, bool const actual) {
    bool const  passed      = expected == actual;
    char        detail[64]  = DEFAULT_INITIALIZATION;

    if (!passed) {
        snprintf(detail, sizeof(detail),
            "expected=%s actual=%s",
            expected ? "true" : "false",
            actual ? "true" : "false");
    }

    return _test_record(self, name, passed, passed ? nullptr : detail);
}

bool test_expect_f(Test *const self, char const *const name, FSize const expected, FSize const actual, FSize const tolerance) {
    FSize   const   diff        = expected > actual ? expected - actual : actual - expected;
    bool    const   passed      = diff <= tolerance;
    char            detail[128] = DEFAULT_INITIALIZATION;

    if (!passed) {
        snprintf(detail, sizeof(detail),
            "expected=%f actual=%f tolerance=%f",
            (FSize) expected,
            (FSize) actual,
            (FSize) tolerance);
    }

    return _test_record(self, name, passed, passed ? nullptr : detail);
}

bool test_expect_false(Test *const self, char const *const name, bool const actual) {
    return test_expect_bool(self, name, false, actual);
}

bool test_expect_i(Test *const self, char const *const name, ISize const expected, ISize const actual) {
    bool const  passed      = expected == actual;
    char        detail[96]  = DEFAULT_INITIALIZATION;

    if (!passed) {
        snprintf(detail, sizeof(detail),
            "expected=%lli actual=%lli",
            (long long) expected,
            (long long) actual);
    }

    return _test_record(self, name, passed, passed ? nullptr : detail);
}

bool test_expect_not_null(Test *const self, char const *const name, void const *const actual) {
    return _test_record(self, name, actual != nullptr, actual == nullptr ? "expected non-null" : nullptr);
}

bool test_expect_null(Test *const self, char const *const name, void const *const actual) {
    return _test_record(self, name, actual == nullptr, actual == nullptr ? nullptr : "expected null");
}

bool test_expect_string(Test *const self, char const *const name, char const *const expected, char const *const actual) {
    bool const  passed       = expected != nullptr && actual != nullptr && strcmp(expected, actual) == 0;
    char        detail[256]  = DEFAULT_INITIALIZATION;

    if (!passed) {
        snprintf(detail, sizeof(detail),
            "expected=%s actual=%s",
            _test_label(expected),
            _test_label(actual));
    }

    return _test_record(self, name, passed, passed ? nullptr : detail);
}

bool test_expect_string_contains(Test *const self, char const *const name, char const *const text, char const *const search) {
    bool const passed = text != nullptr && search != nullptr && strstr(text, search) != nullptr;

    return _test_record(self, name, passed, passed ? nullptr : "substring not found");
}

bool test_expect_true(Test *const self, char const *const name, bool const actual) {
    return test_expect_bool(self, name, true, actual);
}

bool test_expect_u(Test *const self, char const *const name, USize const expected, USize const actual) {
    bool const  passed      = expected == actual;
    char        detail[96]  = DEFAULT_INITIALIZATION;

    if (!passed) {
        snprintf(detail, sizeof(detail),
            "expected=%llu actual=%llu",
            (unsigned long long) expected,
            (unsigned long long) actual);
    }

    return _test_record(self, name, passed, passed ? nullptr : detail);
}

Test test_init(char const *const name) {
    Test self = DEFAULT_INITIALIZATION;

    self.name       = name;
    self.started_at = _test_now();

    return self;
}

void test_section_begin(Test *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->current_section_failure_count = 0;
    self->section_count                 += 1;
    self->section_name                  = name;
    self->section_started_at            = _test_now();
    self->section_open                  = true;

    trace_log_pop();
}

void test_section_end(Test *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!self->section_open) {
        trace_log_pop();

        return;
    }

    bool const passed = self->current_section_failure_count == 0;

    if (passed) {
        self->section_pass_count += 1;
    } else {
        self->section_failure_count += 1;
    }

    _test_print_result(3, passed, self->section_name, _test_elapsed_ms(self->section_started_at));

    self->section_name = nullptr;
    self->section_open = false;

    trace_log_pop();
}

void test_suite_begin(Test *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->current_suite_failure_count = 0;
    self->suite_count                 += 1;
    self->suite_name                  = name;
    self->suite_started_at            = _test_now();
    self->suite_open                  = true;

    FILE *const stream = _test_stream();

    if (stream != nullptr) {
        fprintf(stream, "  %s\n", _test_label(name));
    }

    trace_log_pop();
}

void test_suite_end(Test *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!self->suite_open) {
        trace_log_pop();

        return;
    }

    bool const passed = self->current_suite_failure_count == 0;

    if (passed) {
        self->suite_pass_count += 1;
    } else {
        self->suite_failure_count += 1;
    }

    self->suite_name = nullptr;
    self->suite_open = false;

    trace_log_pop();
}

I32 test_uninit(Test *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize   const   api_failure_count   = _test_api_effective_failure_count(self);
    USize   const   total_failure_count = self->assertion_failure_count + api_failure_count;

    // A run that recorded NOTHING must not report green: a suite whose
    // assertions were silently compiled out, skipped, or never wired up would
    // otherwise pass forever (the vacuous-pass trap this framework has hit
    // repeatedly). Zero failures only counts when something actually ran.
    bool    const   vacuous = self->assertion_count == 0 && self->api_total_count == 0;

    // A structurally defective run must not report green either: a scope still
    // open at uninit means an end call never ran (early return, crash-adjacent
    // flow), so the pass bookkeeping above it is incomplete.
    bool    const   unbalanced = self->case_open || self->section_open || self->suite_open;
    I32     const   result     = (total_failure_count == 0 && !vacuous && !unbalanced) ? 0 : 1;

    FILE *const stream = _test_stream();

    if (stream != nullptr) {
        fprintf(stream, "\n%s  %s\n\n",
            result == 0 ? "PASS" : "FAIL",
            _test_label(self->name));

        if (vacuous) {
            fprintf(stream, "VACUOUS: no assertions and no API coverage were recorded\n\n");
        }

        if (unbalanced) {
            fprintf(stream, "UNBALANCED: a suite, case, or section was never ended\n\n");
        }
    }

    _test_print_count_line("Test Suites:",
        self->suite_pass_count,
        self->suite_failure_count,
        self->suite_count);

    _test_print_count_line("Tests:",
        self->case_pass_count,
        self->case_failure_count,
        self->case_count);

    if (self->section_count > 0) {
        _test_print_count_line("Sections:",
            self->section_pass_count,
            self->section_failure_count,
            self->section_count);
    }

    _test_print_count_line("Assertions:",
        self->assertion_pass_count,
        self->assertion_failure_count,
        self->assertion_count);

    if (self->api_total_count > 0) {
        _test_print_count_line("API:",
            self->api_pass_count,
            api_failure_count,
            self->api_total_count);
    }

    if (stream != nullptr) {
        fprintf(stream, "Time:        %.3f s\n", _test_elapsed_s(self->started_at));
    }

    *self = (Test) DEFAULT_INITIALIZATION;

    trace_log_pop();

    return result;
}

void test_verbose_set(Test *const self, bool const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    self->verbose = value;

    trace_log_pop();
}