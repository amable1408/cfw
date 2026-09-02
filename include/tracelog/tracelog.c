/*
 * tracelog.c - Implementation of manual call-stack tracing for the C Libraries Framework
 *
 * Each pushed frame RECORDS its source location; the text is built later, by
 * trace_log_print. Recording is self-contained (push/pop never touch the log
 * module); printing routes through log_print_raw, reaching the configured
 * stream and the log buffer alike, with a bare stderr fallback when log_init
 * was never called.
 *
 * See tracelog.h for API documentation and usage examples.
 */

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <tracelog/tracelog.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
#define _TRACELOG_FRAME_CAPACITY 64
#define _TRACELOG_FRAME_FORMAT \
    "[" CONSOLE_COLOR_BRIGHT_BLUE "TRACE" CONSOLE_FORMAT_RESET "] " \
    CONSOLE_COLOR_YELLOW       "%s" CONSOLE_FORMAT_RESET " " \
    CONSOLE_COLOR_GREEN        "%s" CONSOLE_FORMAT_RESET ":" \
    CONSOLE_COLOR_BRIGHT_WHITE "%d" CONSOLE_FORMAT_RESET "\n"
#define _TRACELOG_TRUNCATED_FORMAT \
    "[" CONSOLE_COLOR_RED "TRACE TRUNCATED" CONSOLE_FORMAT_RESET "] %llu deeper frame(s) not recorded\n"

/*==============================================================================
 * MARK: - Type Definitions
 *============================================================================*/
/*
 * One recorded frame: three values copied from the call site, no text.
 *
 * The pointers are safe to keep indefinitely because the only things ever passed
 * are __FILE__ and __func__, which are string literals with static storage
 * duration. Nothing here owns memory and nothing needs to be copied.
 */
typedef struct {
    char const *file;
    char const *function;
    I32         line;
} TraceLogFrame;

typedef struct {
    TraceLogFrame frames[_TRACELOG_FRAME_CAPACITY];
    USize         overflow;
    USize         size;
} TraceLog;

/*==============================================================================
 * MARK: - Static/Internal Variables
 *============================================================================*/
#ifdef LOG_THREAD_IMPLEMENTATION
static thread_local TraceLog _trace_log = DEFAULT_INITIALIZATION;
#else
static TraceLog _trace_log = DEFAULT_INITIALIZATION;
#endif // LOG_THREAD_IMPLEMENTATION

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
void trace_log_push(char const *const file, I32 const line, char const *const function) {
#ifdef TRACELOG_ENABLED
    // RECORDED, not formatted. This runs on entry to every traced function in the
    // framework - tens of thousands of times per rendered frame - while the text
    // is read only when something aborts. Building it here formatted a coloured
    // string with snprintf on every call, which measured about 2.2 microseconds
    // per traced call and cost the Keystone client 14 to 64 ms of every frame;
    // the same scene with the formatting moved to print time runs at 2 ms.
    if (_trace_log.size >= _TRACELOG_FRAME_CAPACITY) {
        // COUNTED, not flagged, and this is what keeps push and pop balanced. A
        // dropped push still gets a matching pop, so the pop has to know to
        // discard it - the previous version returned here without recording and
        // let the pop decrement anyway, which ate a real frame off the bottom of
        // the stack for every push past the limit.
        _trace_log.overflow += 1;

        return;
    }

    _trace_log.frames[_trace_log.size].file = file;
    _trace_log.frames[_trace_log.size].function = function;
    _trace_log.frames[_trace_log.size].line = line;

    _trace_log.size += 1;
#endif // TRACELOG_ENABLED
}

void trace_log_pop(void) {
#ifdef TRACELOG_ENABLED
    // The unrecorded frames unwind first, in the order they were dropped.
    if (_trace_log.overflow > 0) {
        _trace_log.overflow -= 1;

        return;
    }

    if (_trace_log.size == 0) {
        return;
    }

    // No clearing. The slot is dead the moment size drops past it, and nothing
    // reads above size - the memset that used to run here wiped 256 bytes on
    // every function exit to hide data that was already unreachable.
    _trace_log.size -= 1;
#endif // TRACELOG_ENABLED
}

void trace_log_print(void) {
#ifdef TRACELOG_ENABLED
    // The abort path must not die inside its own diagnostics: log's gated
    // entry points exit(1) when log_init was never called, which swallowed the
    // trace AND the abort (error.c prints the trace first). Uninitialized
    // programs fall back to a bare stderr write; an initialized log routes
    // every frame through log_print_raw, which honors a deliberately disabled
    // stream AND mirrors into the log buffer - so an operator capturing
    // diagnostics buffer-only no longer loses the trace.
    bool const routed = log_is_initialized();

    for (USize index = 0; index < _trace_log.size; index += 1) {
        if (routed) {
            log_print_raw(_TRACELOG_FRAME_FORMAT, _trace_log.frames[index].function, _trace_log.frames[index].file, _trace_log.frames[index].line);
        }
        else {
            fprintf(stderr, _TRACELOG_FRAME_FORMAT, _trace_log.frames[index].function, _trace_log.frames[index].file, _trace_log.frames[index].line);
        }
    }

    // Reported as an EXTRA line rather than by overwriting the deepest frame,
    // which is what the previous version did - so a truncated stack used to lose
    // the innermost call it recorded, the one nearest whatever went wrong.
    if (_trace_log.overflow > 0) {
        if (routed) {
            log_print_raw(_TRACELOG_TRUNCATED_FORMAT, (unsigned long long) _trace_log.overflow);
        }
        else {
            fprintf(stderr, _TRACELOG_TRUNCATED_FORMAT, (unsigned long long) _trace_log.overflow);
        }
    }
#endif // TRACELOG_ENABLED
}

USize trace_log_depth(void) {
    return _trace_log.size;
}

void trace_log_clear(void) {
#ifdef TRACELOG_ENABLED
    // Two stores. The frames above size are unreachable, so zeroing 16 KB of them
    // bought nothing but the time it took.
    _trace_log.overflow = 0;
    _trace_log.size = 0;
#endif // TRACELOG_ENABLED
}