#include <file/file.h>

// Both platforms: open's O_* flags on Linux, _open_osfhandle's _O_* flags on Windows.
#include <fcntl.h>

#ifdef __linux__
#include <limits.h>
#include <utime.h>
#endif // __linux__

#include <char/char.h>

/*
 * Can the ANSI Win32 / CRT entry points below actually ADDRESS this path?
 *
 * THE PROBLEM THIS EXISTS FOR, which is a mis-target and not a mere failure. Everywhere else
 * in this tree a path is UTF-8 (dir.c and trash.c both convert with CP_UTF8), but the calls
 * in this family hand those bytes to the A-APIs, which decode them in the process code page.
 * For "cafe" with an acute e the UTF-8 bytes are 63 61 66 C3 A9; cp1252 reads that as
 * "cafA©.txt" - a DIFFERENT name. When a mojibake twin of the file exists, which is the
 * ordinary result of a bad unzip or a cross-locale copy, the call succeeds against the twin
 * while the file the caller named survives. Demonstrated: purgo reported "deleted cafe.txt"
 * and cafe.txt was still there afterwards; the twin was the file destroyed.
 *
 * FAIL CLOSED is therefore the rule until the module is ported to the W-APIs: a path whose
 * UTF-8 reading and ACP reading are not the same string is REFUSED, because acting on it can
 * only mean acting on something other than what was asked for. On a non-UTF-8 code page
 * exactly the pure-ASCII names read identically both ways: a non-ASCII UTF-8 sequence is two
 * or more code units under a single-byte page and one under UTF-8, and a code-page byte is
 * invalid UTF-8 - so every non-ASCII name is refused there, in both encodings, and nothing is
 * refused when the process code page IS UTF-8.
 *
 * Linux has no such split: paths there are bytes, and the byte string IS the name.
 *
 * THREE ANSWERS, not two, and the third is why this returns a Result rather than a bool:
 * RESULT_SUCCESS when the path can be acted on; ARGUMENT with FILE_ARGUMENT_UNADDRESSABLE_PATH
 * when the two readings genuinely differ; MEMORY when the two conversion buffers could not be
 * taken. The bool this used to return folded the last into the second, so a transient
 * out-of-memory reached the caller as the one ARGUMENT refusal file.h gives a remedy for - and a
 * delete tool would have sent a perfectly addressable file to the trash path because the
 * process was briefly short of memory. Every answer but SUCCESS still fails closed: nothing is
 * acted on.
 */
static Result _file_path_addressable(char const *const path) {
#ifdef _WIN32
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    // Every ANSI code page maps 0x00-0x7F to itself, so a pure-ASCII path reads identically
    // both ways by construction - a byte scan proves it, with no conversion and no allocation.
    // file_replace_1 runs this up to four times per call, so the common case has to be free.
    bool ascii = true;

    for (USize i = 0; ascii && path[i] != '\0'; i += 1) {
        ascii = (unsigned char) path[i] < 0x80;
    }

    if (ascii) {
        trace_log_pop();

        return RESULT_SUCCESS;
    }

    Result const unaddressable = result_make(RESULT_CATEGORY_ARGUMENT, FILE_ARGUMENT_UNADDRESSABLE_PATH, 0);

    // Invalid UTF-8 is refused outright: there is no reading of it this module can honour.
    int const utf8_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);

    if (utf8_size <= 0) {
        trace_log_pop();

        return unaddressable;
    }

    int const acp_size = MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0);

    if (acp_size != utf8_size) {
        // Different lengths already prove the two readings disagree.
        trace_log_pop();

        return unaddressable;
    }

    WCHAR *const as_utf8 = (WCHAR*) memory_try_alloc((USize) utf8_size * sizeof(WCHAR));
    WCHAR *const as_acp  = (WCHAR*) memory_try_alloc((USize) acp_size * sizeof(WCHAR));

    if (memory_empty(as_utf8) || memory_empty(as_acp)) {
        if (!memory_empty(as_utf8)) {
            memory_free(as_utf8);
        }

        if (!memory_empty(as_acp)) {
            memory_free(as_acp);
        }

        /* Refused rather than assumed safe - but as MEMORY, not as unaddressable: nothing is
         * known about this path yet, and a retry with memory to spare may well act on it. */
        trace_log_pop();

        return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
    }

    bool addressable = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, as_utf8, utf8_size) > 0
        && MultiByteToWideChar(CP_ACP, 0, path, -1, as_acp, acp_size) > 0;

    for (int i = 0; addressable && i < utf8_size; i += 1) {
        addressable = as_utf8[i] == as_acp[i];
    }

    memory_free(as_utf8);
    memory_free(as_acp);

    trace_log_pop();

    return addressable ? RESULT_SUCCESS : unaddressable;
#else
    (void) path;

    return RESULT_SUCCESS;
#endif
}

/*
 * Whether an open handle is a regular file.
 *
 * glibc's fopen SUCCEEDS on a directory. file_get_size then seeks to the end of
 * it and ftell reports LONG_MAX, so the readers below would ask the allocator
 * for 2^63 bytes and die there - a config path pointing at a folder became a
 * silent process death on Linux, while the same path merely failed to open on
 * Windows. Dropping the old file_exists_1 pre-check was right (it was racy), but
 * it also removed the only S_ISREG filter, so the test belongs here instead.
 */
static bool _file_is_regular(File *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool regular = true;

    #ifdef __linux__
    struct stat info = DEFAULT_INITIALIZATION;

    regular = fstat(fileno(self), &info) == 0 && S_ISREG(info.st_mode);
    #endif
    // Windows needs no test: its fopen already refuses a directory, so a handle
    // in hand is a real file.

    trace_log_pop();

    return regular;
}

static String _file_align_read_to(File *const self, USize const padding) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "padding", padding);

    // Same contract as the plain readers: an unopenable path, a directory, and a
    // file with nothing in it all report rather than end the process. This was
    // the one reader not collapsed onto the shared helpers, and it was the one
    // that still aborted - file_read_4 rejects a zero count one frame lower.
    USize file_size = memory_empty((void*) self) || !_file_is_regular(self) ? 0 : file_get_size(self);

    /* Over the cap is refused as if the file were empty, so this reader keeps the
     * same no-abort promise as the other two (string_init_2 below allocates
     * through the aborting memory_alloc). */
    if (file_size > FILE_READ_BYTES_MAX) {
        log_message_try_1(LOG_LEVEL_WARN, "file: refusing to read %llu bytes, over the FILE_READ_BYTES_MAX limit\n", (unsigned long long) file_size);

        file_size = 0;
    }

    if (file_size == 0) {
        // Still a padded, zeroed buffer rather than an empty String: the whole
        // promise of this reader is that a vector load past the content is in
        // bounds, and a caller scanning an empty document still issues one.
        String empty_buffer = string_init_2(padding * 2);

        memory_set(string_get_data(&empty_buffer), padding * 2, 0);

        trace_log_pop();

        return empty_buffer;
    }

    // Rounded up to a padding multiple AND given one more whole block. The
    // round-up alone is not enough for the caller this exists for: a SIMD
    // scanner that starts at a header offset runs its last load up to
    // file_size + padding, and a file whose size is already a multiple of the
    // padding would get only a single spare byte. The extra block is what makes
    // "read one vector past the last byte" safe by construction.
    USize const file_buffer_size = file_size + (padding - (file_size % padding)) + padding;

    String file_buffer = string_init_2(file_buffer_size);

    // Not file_read_3: its count is the capacity, and asking for the padding
    // back from the file is how the size ends up wrong on a short read.
    file_read_4(self, &file_buffer, file_size);

    // The slack is zeroed rather than left as whatever the allocator held, so a
    // vector load past the content sees delimiters that are not there instead
    // of stale bytes that may look like one.
    memory_set(string_get_data(&file_buffer) + string_get_size(&file_buffer), file_buffer_size - string_get_size(&file_buffer), 0);

    trace_log_pop();

    return file_buffer;
}

/*
 * Close a handle only if there is one.
 *
 * file_close treats a null handle as a programming error and ends the process.
 * The by-path readers now accept that an open can simply fail, so they need a
 * close that accepts it too.
 */
static void _file_close_try(File **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!memory_empty((void*) *self)) {
        file_close(self);
    }

    trace_log_pop();
}

static String _file_read_to(File *const self) {
    trace_log_push(LOG_METADATA);

    // A handle that could not be opened, a handle that is not a regular file,
    // and a file with nothing in it all read as an empty String rather than
    // ending the process. Callers detect failure by testing the returned size,
    // and that test only becomes reachable because of these guards.
    if (memory_empty((void*) self) || !_file_is_regular(self)) {
        trace_log_pop();

        return string_init_1();
    }

    USize const file_size = file_get_size(self);

    // string_init_2 rejects a zero capacity, and there is nothing to read.
    if (file_size == 0) {
        trace_log_pop();

        return string_init_1();
    }

    /* Refused like an unreadable file, and for the same reason the raw reader
     * refuses it: string_init_2 allocates through memory_alloc, which aborts
     * instead of failing, so an outsized file would end the process rather than
     * report. This reader is the one with real callers, so the cap has to live
     * here too - not only in _file_read_to_raw. */
    if (file_size > FILE_READ_BYTES_MAX) {
        log_message_try_1(LOG_LEVEL_WARN, "file: refusing to read %llu bytes, over the FILE_READ_BYTES_MAX limit\n", (unsigned long long) file_size);

        trace_log_pop();

        return string_init_1();
    }

    // One byte past the content, so the buffer is a valid C string. Callers
    // hand string_get_data straight to strlen-based APIs, and a read that
    // filled the allocation exactly would leave them running off the end.
    String file_buffer = string_init_2(file_size + CHAR_END_CHARACTER);

    // file_read_4 rather than file_read_3: the latter reads the capacity, which
    // is one byte more than the file holds.
    file_read_4(self, &file_buffer, file_size);

    // Unconditional on purpose (see the style guide's terminator rule): a
    // text-mode fread compacts CRLF in place and leaves stale bytes past the count
    // it reports, so this slot is dirty despite the always-zeroed allocation.
    // The count actually read - not file_size - is where the content ends.
    string_get_data(&file_buffer)[string_get_size(&file_buffer)] = '\0';

    trace_log_pop();

    return file_buffer;
}

/*
 * A whole file as an owned, NUL-terminated raw buffer.
 *
 * Returns a buffer the caller frees whether or not the file could be opened, so
 * that a failed read is reported by an empty string rather than by a null the
 * caller has no reason to expect.
 */
static char* _file_read_to_raw(File *const self) {
    trace_log_push(LOG_METADATA);

    USize file_size = memory_empty((void*) self) || !_file_is_regular(self) ? 0 : file_get_size(self);

    /* This function's whole contract is that a read it cannot perform is reported
     * as an empty buffer, never as a crash - but memory_alloc aborts when malloc
     * fails, so sizing it straight from the file made that promise false for any
     * file larger than free memory. Refuse the outsized file the same way a
     * missing one is refused, and take the allocation non-abortively. */
    if (file_size > FILE_READ_BYTES_MAX) {
        log_message_try_1(LOG_LEVEL_WARN, "file: refusing to read %llu bytes, over the FILE_READ_BYTES_MAX limit\n", (unsigned long long) file_size);

        file_size = 0;
    }

    char *file_buffer = (char*) memory_try_alloc(file_size + CHAR_END_CHARACTER);

    /* Out of memory for the content still owes the caller the empty buffer this
     * function promises - callers are documented never to receive null - so fall
     * back to the terminator alone. That one is taken with memory_alloc on
     * purpose: a process that cannot spare a single byte is beyond anything this
     * function could report, and aborting there matches the framework's policy. */
    if (memory_empty(file_buffer)) {
        log_message_try_1(LOG_LEVEL_WARN, "file: not enough memory to read %llu bytes\n", (unsigned long long) file_size);

        file_size   = 0;
        file_buffer = (char*) memory_alloc(CHAR_END_CHARACTER);
    }

    // file_read_1 rejects a zero count, and an empty file has nothing to put in
    // the buffer anyway - the terminator below is then the whole content.
    USize const read_size = file_size > 0 ? file_read_1(self, (void*) file_buffer, sizeof(Byte), file_size) : 0;

    // Unconditional on purpose (see the style guide's terminator rule): a
    // text-mode fread compacts CRLF in place and leaves stale bytes past the count
    // it reports, so this slot is dirty despite the always-zeroed allocation.
    file_buffer[read_size] = '\0';

    trace_log_pop();

    return file_buffer;
}

/*
 * A whole file as a Str.
 */
static Str _file_read_to_str(File *const self) {
    trace_log_push(LOG_METADATA);

    String file_buffer = _file_read_to(self);

    // An empty read has to release the String here: there is no buffer to hand
    // over, and the zero-size Str carries nothing that could free it.
    if (string_get_size(&file_buffer) == 0) {
        string_uninit(&file_buffer);

        trace_log_pop();

        return str_init_1();
    }

    /* ADOPT the String's buffer rather than viewing it. str_init_3 builds a VIEW
     * (str.h's documented contract), so every call used to strand the whole file:
     * the local String went out of scope still owning the block and the Str never
     * released it. The claim is transferred explicitly - the Str becomes the owner
     * and the String's own claim is cleared, so exactly one object frees it. Both
     * are heap-allocated through the same allocator, so the release pairs. */
    Str file_str = str_init_3(string_get_data(&file_buffer), string_get_size(&file_buffer));

    file_str.owned    = true;
    file_buffer.owned = false;

    trace_log_pop();

    return file_str;
}

String file_align_read_to_string(char const *const file_name, U16 const padding) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    File *file = file_open_try_1(file_name, "r");

    String const file_buffer = _file_align_read_to(file, padding);

    _file_close_try(&file);

    trace_log_pop();

    return (String) file_buffer;
}

void file_at(File *const self, USize const index, File_Position const position) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
#ifdef ERROR_CHECK_ENABLED
    USize const file_size = file_get_size(self);

    error_check_out_of_bound_uint(LOG_METADATA, "index", index, "file_size", file_size, "index > file_size", index > file_size);
#endif
    /* 64-bit seek: fseek's long offset truncates 'index' for files over 2 GiB. */
#ifdef _WIN32
    _fseeki64(self, (long long) index, position);
#else
    fseeko(self, (off_t) index, position);
#endif

    trace_log_pop();
}

char* file_basename_1(char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    char const *result = path;

    for (char const *scan = path; *scan != '\0'; scan += 1) {
        if (*scan == '/' || *scan == '\\') {
            result = scan + 1;
        }
    }

    trace_log_pop();

    return (char*) result;
}

void file_close(File **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    fclose(*self);

    *self = nullptr;

    trace_log_pop();
}

// Streaming copy buffer size: large enough to amortize the fread/fwrite call overhead
// without holding a whole large file in memory the way the module's whole-file readers do.
#define _FILE_COPY_BUFFER_BYTES ((USize) 64 * 1024)

// FILETIME counts 100ns ticks: ten million of them make one second.
#define _FILE_FILETIME_TICKS_PER_SECOND 10000000ULL

// Text of a USize: at most 20 digits plus the terminator, rounded up.
#define _FILE_NUMBER_TEXT_MAX 32

// How many temp names file_replace_1 will try before giving up. Only a stale temp from a
// killed run makes attempt 0 fail, so this is generous by orders of magnitude.
#define _FILE_REPLACE_ATTEMPTS_MAX 64

#ifdef _WIN32
// Length of the "X:" drive spec dropped from a final component before the device-name check.
#define _FILE_DRIVE_SPEC_SIZE CHAR_STATIC_SIZE("X:")

// The NT object-namespace prefix Win32 passes through verbatim, and its length.
#define _FILE_NT_PREFIX "\\??\\"
#define _FILE_NT_PREFIX_SIZE CHAR_STATIC_SIZE(_FILE_NT_PREFIX)

// Length of the prefix every path the nofollow opener refuses as UNC begins with: two separators,
// '/' and '\' alike in either place - \\server, //server, \/server, and the namespaces \\. and \\?.
#define _FILE_UNC_PREFIX_SIZE CHAR_STATIC_SIZE("\\\\")

// The long-path prefix GetFinalPathNameByHandleW puts in front of a drive path, and its length.
#define _FILE_WIDE_PREFIX L"\\\\?\\"
// In WCHAR units, less one terminator unit (CHAR_END_CHARACTER).
#define _FILE_WIDE_PREFIX_SIZE (sizeof(_FILE_WIDE_PREFIX) / sizeof(WCHAR) - CHAR_END_CHARACTER)

// Its UNC form, and how many of its units go when `\\?\UNC\server` becomes `\\server`: all of
// them but the `\\` a UNC path begins with.
#define _FILE_WIDE_PREFIX_UNC L"\\\\?\\UNC\\"
#define _FILE_WIDE_PREFIX_UNC_DROPPED (sizeof(_FILE_WIDE_PREFIX_UNC) / sizeof(WCHAR) - sizeof(L"\\\\") / sizeof(WCHAR))
#endif // _WIN32

/*
 * Stream one open handle into another, reporting a read failure as a failure.
 *
 * Split out so file_replace_1 can create its temporary with a NARROW MODE of its own before
 * any content exists. Patching the mode afterwards - which is what the first attempt did -
 * leaves the file world-readable under a fully predictable name for the whole duration of the
 * copy, which is the entire window that mattered for a secret.
 */
static Result _file_copy_stream(File *const reader, File *const writer) {
    trace_log_push(LOG_METADATA);

    U8 *const buffer = (U8*) memory_try_alloc(_FILE_COPY_BUFFER_BYTES);

    if (memory_empty(buffer)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
    }

    Result result = RESULT_SUCCESS;
    USize read_count;

    while ((read_count = file_read_1(reader, buffer, 1, _FILE_COPY_BUFFER_BYTES)) > 0) {
        USize const written_count = file_write_1(writer, buffer, 1, read_count);

        if (written_count != read_count) {
            Result const write_failure = result_from_os();

            result = result_is_error(write_failure) ? write_failure : result_make(RESULT_CATEGORY_IO, 0, RESULT_FLAG_PARTIAL);

            break;
        }
    }

    /* THE CHECK THIS LOOP USED TO LACK. fread reports EOF and a read error identically - it
     * returns 0 for both - so without ferror a failed read simply ended the loop and the copy
     * reported SUCCESS with a truncated destination. It also travelled: file_replace_1 is
     * built on this, so the ATOMIC path could atomically install a truncated file over good
     * content, which is precisely what its contract says cannot happen. */
    if (result_is_success(result) && file_error(reader)) {
        Result const read_failure = result_from_os();

        result = result_is_error(read_failure) ? read_failure : result_make(RESULT_CATEGORY_IO, 0, RESULT_FLAG_PARTIAL);
    }

    /* file_error(writer) is deliberately not consulted: a writer failure surfaces as the short
     * fwrite count above or the flush failure below, and those two cover every write path. */
    memory_free(buffer);

    if (result_is_success(result) && !file_flush(writer)) {
        /* Classified from the OS like its two neighbours, rather than a hardcoded 0. This is
         * where the TAIL of a copy fails - the last sub-buffer chunk, which for a small file
         * is the entire file - so "the disk is full" reaches the caller here more often than
         * through the write loop above, and used to arrive with no code at all. */
        Result const flush_failure = result_from_os();

        result = result_is_error(flush_failure)
            ? result_with_flag(flush_failure, RESULT_FLAG_PARTIAL)
            : result_make(RESULT_CATEGORY_IO, 0, RESULT_FLAG_PARTIAL);
    }

    trace_log_pop();

    return result;
}

/*
 * Create `destination` exclusively, at `mode` where the platform has one, and copy into it.
 *
 * `mode` is applied AT CREATION on Linux (open + fdopen), so there is never an instant where
 * the new file is broader than intended. Windows takes the CRT path: its access control is
 * inherited from the directory and is not expressible here.
 *
 * The source is verified to be a REGULAR FILE. Both public headers state that as a
 * precondition and neither enforced it: the copy relied on glibc setting ferror when read(2)
 * on a directory descriptor returns EISDIR. That works, but it made a documented contract an
 * emergent property of one libc - and Windows fopen refuses a directory outright, so the two
 * platforms did not even fail the same way.
 */
static Result _file_copy_exclusive(char const *const source, char const *const destination, U32 const mode) {
    trace_log_push(LOG_METADATA);

    /* Refused rather than mis-targeted - see _file_path_addressable. ARGUMENT because the
     * path is one this build cannot name, which is a fact about the call, not about the disk.
     * BOTH ends are checked: a readable source copied onto an unaddressable destination would
     * write over whatever the code page happens to name. The check's own MEMORY answer is
     * passed through as it is, never relabelled as this refusal. */
    Result const source_addressable = _file_path_addressable(source);

    if (result_is_error(source_addressable)) {
        trace_log_pop();

        return source_addressable;
    }

    Result const destination_addressable = _file_path_addressable(destination);

    if (result_is_error(destination_addressable)) {
        trace_log_pop();

        return destination_addressable;
    }

    File *reader = file_open_try_1(source, "rb");

    if (memory_empty((void*) reader)) {
        /* fopen is a CRT call, and its code is captured before anything else runs. On Windows
         * the CRT keeps the real Win32 code beside errno in _doserrno (fopen "wbx" over an
         * existing file: errno 17, _doserrno 80) - and result_from_os_code is Win32-shaped, so
         * filing errno into it mapped EACCES to ERROR_INVALID_DATA and EMFILE to
         * ERROR_BAD_LENGTH. The rest of the family files Win32 codes; the copy path does too. */
#ifdef _WIN32
        Result const open_failure = result_from_os_code((U32) _doserrno);
#else
        Result const open_failure = result_from_os_code((U32) errno);
#endif

        trace_log_pop();

        return result_is_error(open_failure) ? open_failure : result_make(RESULT_CATEGORY_IO, 0, 0);
    }

    /* Asked of the OPEN HANDLE rather than the path, so there is no window between the test
     * and the first read. ARGUMENT and not an I/O code: the caller passed the wrong KIND of
     * thing, which is a mistake in the call, not a condition of the disk. */
    if (!file_regular(reader)) {
        _file_close_try(&reader);

        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, FILE_ARGUMENT_NOT_REGULAR, 0);
    }

    /* EXCLUSIVE create, not a stat-then-open. The previous form asked file_exists_1 and then
     * opened with "wb", which was wrong twice: a file appearing between the two was silently
     * truncated, and - needing no race at all - file_exists_1 is stat+S_ISREG, so a DANGLING
     * SYMLINK at the destination reported "absent" and the "wb" open then followed it and
     * created whatever it pointed at. O_EXCL cannot follow a symlink, so the refusal and the
     * creation are one atomic step. */
#ifdef __linux__
    int const descriptor = open(destination, O_WRONLY | O_CREAT | O_EXCL, (mode_t) mode);
    File *writer         = descriptor < 0 ? nullptr : fdopen(descriptor, "wb");

    /* CAPTURED AT THE FAILING CALL, before the close below.
     *
     * That close runs on exactly one path - open succeeded, fdopen did not - which is the
     * path ADDED to fix the zero-byte-destination hole. close() reports deferred writeback
     * errors, so on a full disk or an NFS mount it overwrites fdopen's ENOMEM with EIO and
     * the Result then says I/O where it should say memory. The previous capture sat at the
     * branch join and its comment claimed to precede "the close below" - true of the reader
     * close further down, false of this one ten lines up. */
    int const open_errno = errno;
    U32 const open_code  = (U32) open_errno;

    if (descriptor >= 0 && memory_empty((void*) writer)) {
        close(descriptor);
    }
#else
    (void) mode;

    File *writer         = file_open_try_1(destination, "wbx");
    int const open_errno = errno;
    U32 const open_code  = (U32) _doserrno;  // the Win32 code the CRT keeps beside errno
#endif

    if (memory_empty((void*) writer)) {
        Result const open_failure = result_from_os_code(open_code);

        /* REMOVES ONLY WHAT THIS CALL CREATED, which is the fdopen-failed path and nothing
         * else: the descriptor is valid there, so a file exists and it is ours.
         *
         * The previous form keyed on `open_errno != EEXIST`, reasoning that any non-EEXIST
         * failure meant we had made the file. That is wrong, and destructively so. open(2)
         * allocates the descriptor BEFORE it resolves the path, so with the descriptor table
         * full an EXISTING destination reports EMFILE rather than EEXIST - verified on the
         * Linux bench: rlim_cur=3, O_CREAT|O_EXCL over an existing file, errno 24. The
         * cleanup then unlinked a file this function had just promised to refuse. ENFILE and
         * ENOMEM share that ordering.
         *
         * Windows takes no cleanup here at all: its branch has no descriptor to test, fopen
         * "wbx" never leaves a partial behind on failure, and the mid-copy cleanup further
         * down already covers the only case where one can exist. */
#ifdef __linux__
        if (descriptor >= 0) {
            remove(destination);
        }
#endif

        _file_close_try(&reader);

        trace_log_pop();

        /* An existing destination is this family's most-hit non-OS error, so it carries a
         * code a caller can test (file_result_is_exists) rather than being one more
         * indistinguishable bare STATE. */
        if (open_errno == EEXIST) {
            return result_make(RESULT_CATEGORY_STATE, (U32) EEXIST, 0);
        }

        return result_is_error(open_failure) ? open_failure : result_make(RESULT_CATEGORY_IO, 0, 0);
    }

    Result const result = _file_copy_stream(reader, writer);

    _file_close_try(&reader);
    _file_close_try(&writer);

    /* The partial file is this function's own litter - it always CREATES the destination -
     * and leaving it behind made the failure non-idempotent: a retry hit the exists-refusal
     * above and failed forever. Its sibling file_replace_1 already cleans its temp up. */
    if (result_is_error(result)) {
        remove(destination);
    }

    trace_log_pop();

    return result;
}

Result file_copy_1(char const *const source, char const *const destination) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "source", (void*) source);
    error_check_null(LOG_METADATA, "destination", (void*) destination);

    // 0666 is the ordinary create mode; the umask narrows it, as it does for any new file.
    Result const result = _file_copy_exclusive(source, destination, 0666);

    trace_log_pop();

    return result;
}

Result file_copy_2(Str const *const source, Str const *const destination) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "source", (void*) source);
    error_check_null(LOG_METADATA, "destination", (void*) destination);

    if (str_get_size(source) == 0 || str_get_size(destination) == 0) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, FILE_ARGUMENT_EMPTY_PATH, 0);
    }

    char *const source_text      = char_new_3(str_get_data(source), str_get_size(source));
    char *const destination_text = char_new_3(str_get_data(destination), str_get_size(destination));

    /* The _1 below opens with error_check_null, which ABORTS - inside a family whose header
     * promises it never aborts on an outcome. Each delete is guarded because the other
     * allocation may have succeeded, and CFW's free aborts on nullptr. */
    if (memory_empty(source_text) || memory_empty(destination_text)) {
        if (!memory_empty(source_text)) {
            char_delete(source_text);
        }

        if (!memory_empty(destination_text)) {
            char_delete(destination_text);
        }

        trace_log_pop();

        return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
    }

    Result const result = file_copy_1(source_text, destination_text);

    char_delete(source_text);
    char_delete(destination_text);

    trace_log_pop();

    return result;
}

Result file_copy_3(String const *const source, String const *const destination) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "source", (void*) source);
    error_check_null(LOG_METADATA, "destination", (void*) destination);

    if (string_get_size(source) == 0 || string_get_size(destination) == 0) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, FILE_ARGUMENT_EMPTY_PATH, 0);
    }

    char *const source_text      = char_new_3(string_get_data(source), string_get_size(source));
    char *const destination_text = char_new_3(string_get_data(destination), string_get_size(destination));

    /* The _1 below opens with error_check_null, which ABORTS - inside a family whose header
     * promises it never aborts on an outcome. Each delete is guarded because the other
     * allocation may have succeeded, and CFW's free aborts on nullptr. */
    if (memory_empty(source_text) || memory_empty(destination_text)) {
        if (!memory_empty(source_text)) {
            char_delete(source_text);
        }

        if (!memory_empty(destination_text)) {
            char_delete(destination_text);
        }

        trace_log_pop();

        return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
    }

    Result const result = file_copy_1(source_text, destination_text);

    char_delete(source_text);
    char_delete(destination_text);

    trace_log_pop();

    return result;
}

bool file_empty(File *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return file_get_size(self) == 0;
}

bool file_error(File *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const failed = ferror(self) != 0;

    trace_log_pop();

    return failed;
}

bool file_exists_1(char const *const file_name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    bool exists = false;

    #ifdef __linux__
    // Was `exists = true;` unconditionally, which reported every path as present
    // - including paths that are not. Every caller guarding a read with this
    // function was therefore unguarded on Linux.
    struct stat info = DEFAULT_INITIALIZATION;

    exists = stat(file_name, &info) == 0 && S_ISREG(info.st_mode);
    #elif defined(_WIN32)
    DWORD dwAttrib = GetFileAttributes((LPCSTR) file_name);

    exists = (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
    #endif

    trace_log_pop();

    return exists;
}

bool file_exists_2(Str const *const file_name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    // An empty Str carries a nullptr buffer (str.h's EMPTY value), and char_new_3 aborts on a
    // nullptr source - the abort primitive file_modified_2/_size_2 already refuse instead of.
    if (str_get_size(file_name) == 0) {
        trace_log_pop();

        return false;
    }

    char *const path_text = char_new_3(str_get_data(file_name), str_get_size(file_name));

    bool exists = file_exists_1(path_text);

    char_delete(path_text);

    trace_log_pop();

    return exists;
}

bool file_exists_3(String const *const file_name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    // See file_exists_2: an empty String's buffer is nullptr, which char_new_3 would abort on.
    if (string_get_size(file_name) == 0) {
        trace_log_pop();

        return false;
    }

    char *const path_text = char_new_3(string_get_data(file_name), string_get_size(file_name));

    bool exists = file_exists_1(path_text);

    char_delete(path_text);

    trace_log_pop();

    return exists;
}

char* file_extension_1(char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    char const *const base = file_basename_1(path);
    char const *dot        = nullptr;

    for (USize i = 0; base[i] != '\0'; i += 1) {
        if (base[i] == '.' && i != 0) {
            dot = &base[i + 1];
        }
    }

    trace_log_pop();

    return (char*) (!memory_empty((void*) dot) ? dot : "");
}

#ifdef _WIN32
/*
 * Whether the first units of a wide string spell PREFIX (terminated). Used to strip the `\\?\`
 * and `\\?\UNC\` forms GetFinalPathNameByHandleW prepends.
 */
static bool _file_wide_has_prefix(WCHAR const *const wide, USize const wide_size, WCHAR const *const prefix) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "wide", (void*) wide);
    error_check_null(LOG_METADATA, "prefix", (void*) prefix);

    bool matched = true;
    USize i      = 0;

    for (; matched && prefix[i] != L'\0'; i += 1) {
        matched = i < wide_size && wide[i] == prefix[i];
    }

    trace_log_pop();

    return matched;
}

/*
 * Where an open Win32 HANDLE physically lives, as UTF-8 in the caller's buffer, in the shape
 * file_final_path_1 documents: GetFinalPathNameByHandleW's `\\?\` stripped, `\\?\UNC\server`
 * rewritten to `\\server`. Answers false - leaving the caller to empty the buffer - when the
 * query fails, the wide answer does not fit FILE_PATH_WIDE_MAX, or the UTF-8 answer and its
 * terminator do not fit CAPACITY. Never a truncated path: the size is asked first and compared,
 * so the second conversion only runs when the whole answer fits.
 */
static bool _file_final_path_wide(HANDLE const handle, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    WCHAR wide[FILE_PATH_WIDE_MAX] = DEFAULT_INITIALIZATION;

    // A return of FILE_PATH_WIDE_MAX or more is the REQUIRED size, terminator included: too long.
    DWORD const units = GetFinalPathNameByHandleW(handle, wide, FILE_PATH_WIDE_MAX, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);

    if (units == 0 || units >= FILE_PATH_WIDE_MAX) {
        trace_log_pop();

        return false;
    }

    WCHAR const *start = wide;
    USize length       = (USize) units;

    if (_file_wide_has_prefix(wide, length, _FILE_WIDE_PREFIX_UNC)) {
        // `\\?\UNC\server\share` -> `\\server\share`: keep the last two units of the prefix and
        // turn the 'C' in front of them into the second backslash.
        wide[_FILE_WIDE_PREFIX_UNC_DROPPED] = L'\\';
        start                               = &wide[_FILE_WIDE_PREFIX_UNC_DROPPED];
        length                             -= _FILE_WIDE_PREFIX_UNC_DROPPED;
    }
    else if (_file_wide_has_prefix(wide, length, _FILE_WIDE_PREFIX)) {
        start   = &wide[_FILE_WIDE_PREFIX_SIZE];
        length -= _FILE_WIDE_PREFIX_SIZE;
    }

    int const bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, start, (int) length, nullptr, 0, nullptr, nullptr);

    if (bytes <= 0 || (USize) bytes >= capacity) {
        trace_log_pop();

        return false;
    }

    bool const converted = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, start, (int) length, buffer, bytes, nullptr, nullptr) == bytes;

    if (converted) {
        buffer[bytes] = '\0';
    }

    trace_log_pop();

    return converted;
}

/*
 * UTF-8 bytes -> a terminated UTF-16 path in the caller's FILE_PATH_WIDE_MAX buffer, for the wide
 * functions below. Takes a SIZE rather than a terminator so a Str need not be copied first.
 * Fails closed and leaves the error state the callers document:
 *   - an embedded NUL -> ERROR_INVALID_NAME: the W-API would stop at it and open the SHORTER
 *     name in front of it, a name the caller never passed;
 *   - invalid UTF-8 -> ERROR_NO_UNICODE_TRANSLATION, set by the conversion itself;
 *   - a path that does not fit -> ERROR_FILENAME_EXCED_RANGE.
 * A zero-size path converts to an empty wide string; the open that follows reports it as the
 * OS reports any missing path.
 */
static bool _file_path_wide(char const *const path, USize const path_size, WCHAR *const wide) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "wide", (void*) wide);

    for (USize i = 0; i < path_size; i += 1) {
        if (path[i] == '\0') {
            SetLastError(ERROR_INVALID_NAME);

            trace_log_pop();

            return false;
        }
    }

    if (path_size > (USize) I32_MAX) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);

        trace_log_pop();

        return false;
    }

    if (path_size == 0) {
        wide[0] = L'\0';

        trace_log_pop();

        return true;
    }

    // One unit short of the buffer, so the terminator always has a slot.
    int const units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, (int) path_size, wide, FILE_PATH_WIDE_MAX - 1);

    if (units <= 0) {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
        }

        trace_log_pop();

        return false;
    }

    wide[units] = L'\0';

    trace_log_pop();

    return true;
}

/*
 * Open an EXISTING path with the caller's ACCESS and FLAGS, every share mode granted
 * (FILE_SHARE_DELETE: a file replaced while it is being read keeps reading from this handle), and
 * an identification-only security QoS: a path naming a named pipe - `\\.\pipe\x`, or a UNC path a
 * peer controls - cannot let the pipe's server impersonate this process, only learn who asked.
 *
 * CreateFile2, not CreateFileW: CreateFileW packs flags and QoS into one DWORD, where
 * SECURITY_SQOS_PRESENT and FILE_FLAG_OPEN_NO_RECALL are the same bit (0x00100000) - asking for the
 * QoS there also tells a cloud placeholder's filter driver NOT to recall the data the nofollow
 * re-open exists to read. CreateFile2 carries the two in separate fields. The OS error state is
 * CreateFile2's own on INVALID_HANDLE_VALUE.
 */
static HANDLE _file_open_wide(WCHAR const *const wide, DWORD const access, DWORD const flags) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "wide", (void*) wide);

    CREATEFILE2_EXTENDED_PARAMETERS parameters = DEFAULT_INITIALIZATION;

    parameters.dwSize             = sizeof(parameters);
    parameters.dwFileAttributes   = FILE_ATTRIBUTE_NORMAL;
    parameters.dwFileFlags        = flags;
    parameters.dwSecurityQosFlags = SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION;

    HANDLE const handle = CreateFile2(wide, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, &parameters);

    trace_log_pop();

    return handle;
}
#else
// The symlink naming an open descriptor, followed by the descriptor's decimal text.
#define _FILE_PROC_FD_PREFIX "/proc/self/fd/"

/*
 * Where an open descriptor physically lives, by readlink on its /proc/self/fd entry. Answers
 * false - leaving the caller to empty the buffer - when /proc is not mounted, the descriptor is
 * not open, or the answer and its terminator do not fit CAPACITY. readlink never terminates and
 * silently truncates to the size it is given, so an answer that FILLS the buffer is treated as
 * truncated: it might be.
 */
static bool _file_final_path_descriptor(int const descriptor, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    if (descriptor < 0) {
        trace_log_pop();

        return false;
    }

    char link[sizeof(_FILE_PROC_FD_PREFIX) + _FILE_NUMBER_TEXT_MAX] = _FILE_PROC_FD_PREFIX;
    USize const prefix_size                                         = CHAR_STATIC_SIZE(_FILE_PROC_FD_PREFIX);

    char_from_numbers_uint_1(&link[prefix_size], sizeof(link) - prefix_size, (USize) descriptor);

    ssize_t const written = readlink(link, buffer, capacity);

    if (written < 0 || (USize) written >= capacity) {
        trace_log_pop();

        return false;
    }

    buffer[written] = '\0';

    trace_log_pop();

    return true;
}

/*
 * PATH_SIZE bytes -> a terminated path in the caller's PATH_MAX buffer, so a Str need not carry
 * a terminator. Fails closed, setting errno: an embedded NUL -> EINVAL (the kernel would stop at
 * it and open the SHORTER name in front of it), a path that does not fit -> ENAMETOOLONG.
 */
static bool _file_path_narrow(char const *const path, USize const path_size, char *const narrow) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "narrow", (void*) narrow);

    if (path_size >= (USize) PATH_MAX) {
        errno = ENAMETOOLONG;

        trace_log_pop();

        return false;
    }

    for (USize i = 0; i < path_size; i += 1) {
        if (path[i] == '\0') {
            errno = EINVAL;

            trace_log_pop();

            return false;
        }

        narrow[i] = path[i];
    }

    narrow[path_size] = '\0';

    trace_log_pop();

    return true;
}
#endif // _WIN32

/*
 * The by-path final path, shared by file_final_path_1/2/3: a size rather than a terminator, so
 * the Str form needs no copy. CAPACITY 0 answers false without touching the buffer (there is no
 * byte to empty); otherwise the buffer is empty on every false.
 */
static bool _file_final_path(char const *const path, USize const path_size, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    if (capacity == 0) {
        trace_log_pop();

        return false;
    }

    bool found = false;
#ifdef _WIN32
    WCHAR wide[FILE_PATH_WIDE_MAX] = DEFAULT_INITIALIZATION;

    if (_file_path_wide(path, path_size, wide)) {
        // Zero access and FILE_FLAG_BACKUP_SEMANTICS: a DIRECTORY opens too (a root is asked
        // here), and every link on the way is followed - the target is the answer.
        HANDLE const handle = _file_open_wide(wide, 0, FILE_FLAG_BACKUP_SEMANTICS);

        if (handle != INVALID_HANDLE_VALUE) {
            found = _file_final_path_wide(handle, buffer, capacity);

            CloseHandle(handle);
        }
    }
#else
    char narrow[PATH_MAX]   = DEFAULT_INITIALIZATION;
    char resolved[PATH_MAX] = DEFAULT_INITIALIZATION;

    if (_file_path_narrow(path, path_size, narrow)) {
        // Named rather than tested inline, so the null check reads through memory_empty.
        char const *const canonical_path = realpath(narrow, resolved);

        USize const length = !memory_empty((void*) canonical_path) ? char_length(resolved) : 0;

        if (length > 0 && length < capacity) {
            memory_copy_1(buffer, resolved, length + CHAR_END_CHARACTER);

            found = true;
        }
    }
#endif // _WIN32

    if (!found) {
        buffer[0] = '\0';
    }

    trace_log_pop();

    return found;
}

bool file_final_path_1(char const *const path, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    bool const found = _file_final_path(path, char_length(path), buffer, capacity);

    trace_log_pop();

    return found;
}

bool file_final_path_2(Str const *const path, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    // An empty Str carries a nullptr buffer: refused by value before it reaches the core's own
    // pointer check, with no syscall.
    if (str_get_size(path) == 0) {
        if (capacity > 0) {
            buffer[0] = '\0';
        }

        trace_log_pop();

        return false;
    }

    bool const found = _file_final_path(str_get_data(path), str_get_size(path), buffer, capacity);

    trace_log_pop();

    return found;
}

bool file_final_path_3(String const *const path, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    // See file_final_path_2.
    if (string_get_size(path) == 0) {
        if (capacity > 0) {
            buffer[0] = '\0';
        }

        trace_log_pop();

        return false;
    }

    bool const found = _file_final_path(string_get_data(path), string_get_size(path), buffer, capacity);

    trace_log_pop();

    return found;
}

bool file_flush(File *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* fflush is otherwise unreachable in-framework: callers that must know the
     * bytes reached the OS (an upload endpoint answering 200) had to rely on
     * fclose, which reports nothing. */
    bool const flushed = fflush(self) == 0;

    trace_log_pop();

    return flushed;
}

bool file_get_final_path(File *const self, char *const buffer, USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "buffer", (void*) buffer);

    if (capacity == 0) {
        trace_log_pop();

        return false;
    }

    bool found = false;
#ifdef _WIN32
    // The handle under the stream, asked directly: nothing is re-resolved by path.
    HANDLE const handle = (HANDLE) _get_osfhandle(fileno(self));

    if (handle != INVALID_HANDLE_VALUE) {
        found = _file_final_path_wide(handle, buffer, capacity);
    }
#else
    found = _file_final_path_descriptor(fileno(self), buffer, capacity);
#endif // _WIN32

    if (!found) {
        buffer[0] = '\0';
    }

    trace_log_pop();

    return found;
}

#ifdef _WIN32
/*
 * FILETIME ticks -> Unix epoch seconds, shared by the by-handle and by-path mtime getters so the
 * two cannot drift apart. FILETIME counts 100ns ticks since 1601-01-01; 116444736000000000 of
 * them reach the Unix epoch. Truncates toward zero on both sides of the epoch.
 */
static I64 _file_epoch_from_filetime(FILETIME const filetime) {
    trace_log_push(LOG_METADATA);

    ULARGE_INTEGER ticks = DEFAULT_INITIALIZATION;

    ticks.LowPart  = filetime.dwLowDateTime;
    ticks.HighPart = filetime.dwHighDateTime;

    ULONGLONG const epoch_ticks = 116444736000000000ULL;

    I64 const seconds = ticks.QuadPart < epoch_ticks
        ? -(I64) ((epoch_ticks - ticks.QuadPart) / _FILE_FILETIME_TICKS_PER_SECOND)
        : (I64) ((ticks.QuadPart - epoch_ticks) / _FILE_FILETIME_TICKS_PER_SECOND);

    trace_log_pop();

    return seconds;
}
#endif // _WIN32

bool file_get_modified(File *const self, I64 *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "out", (void*) out);

    bool success = false;
#ifdef _WIN32
    HANDLE const handle = (HANDLE) _get_osfhandle(fileno(self));
    FILETIME written    = DEFAULT_INITIALIZATION;

    // Only the last-write slot is asked for, matching file_modified_1's ftLastWriteTime.
    if (handle != INVALID_HANDLE_VALUE && GetFileTime(handle, nullptr, nullptr, &written)) {
        *out    = _file_epoch_from_filetime(written);
        success = true;
    }
#else
    struct stat status = DEFAULT_INITIALIZATION;

    if (fstat(fileno(self), &status) == 0) {
        *out    = (I64) status.st_mtime;
        success = true;
    }
#endif // _WIN32

    trace_log_pop();

    return success;
}

USize file_get_size(File *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize file_size = 0;
#ifdef __linux__
    /* fstat by HANDLE, not fseek/ftell: the seek pair moved the stream position
     * (rewinding it to 0), so file_at's bound check silently reset the very
     * position it was about to seek from, and a stat-less error turned ftell's
     * -1 into a huge USize. This also removes the platform caveat the whole-file
     * readers used to document. */
    struct stat file_status = DEFAULT_INITIALIZATION;

    /* The magnitude check runs BEFORE the narrowing cast: on a 32-bit target
     * off_t outranges USize, and a >4 GiB file would otherwise wrap to a small
     * size that sails through FILE_READ_BYTES_MAX. */
    if (fstat(fileno(self), &file_status) == 0 && file_status.st_size > 0 && (U64) file_status.st_size <= (U64) USIZE_MAX) {
        file_size = (USize) file_status.st_size;
    }
#elif defined(_WIN32)
    HANDLE _handle = (HANDLE) _get_osfhandle(fileno(self));

    // Into a LARGE_INTEGER and then assigned, never straight into file_size:
    // GetFileSizeEx always writes 8 bytes, so casting a USize to
    // PLARGE_INTEGER overwrites 4 bytes of stack past it on a 32-bit target.
    LARGE_INTEGER _file_size = DEFAULT_INITIALIZATION;

    if (GetFileSizeEx(_handle, &_file_size)) {
        file_size = (USize) _file_size.QuadPart;
    }
#endif

    trace_log_pop();

    return file_size;
}

bool file_modified_1(char const *const file_name, I64 *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "out", (void*) out);

    bool success = false;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data = DEFAULT_INITIALIZATION;

    if (GetFileAttributesExA((LPCSTR) file_name, GetFileExInfoStandard, &data)) {
        *out    = _file_epoch_from_filetime(data.ftLastWriteTime);
        success = true;
    }
#else
    struct stat status = DEFAULT_INITIALIZATION;

    if (stat(file_name, &status) == 0) {
        *out = (I64) status.st_mtime;
        success = true;
    }
#endif

    trace_log_pop();

    return success;
}

bool file_modified_2(Str const *const file_name, I64 *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "out", (void*) out);

    if (str_get_size(file_name) == 0) {
        trace_log_pop();

        return false;
    }

    char *const path_text = char_new_3(str_get_data(file_name), str_get_size(file_name));

    bool const success = file_modified_1(path_text, out);

    char_delete(path_text);

    trace_log_pop();

    return success;
}

bool file_modified_3(String const *const file_name, I64 *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "out", (void*) out);

    if (string_get_size(file_name) == 0) {
        trace_log_pop();

        return false;
    }

    char *const path_text = char_new_3(string_get_data(file_name), string_get_size(file_name));

    bool const success = file_modified_1(path_text, out);

    char_delete(path_text);

    trace_log_pop();

    return success;
}

bool file_name_reserved_windows(char const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "name", (void*) name);

    /* The superscript-digit COM/LPT forms are not listed: the non-ASCII rule below answers them
     * in both their single-byte ANSI and their UTF-8 spellings. */
    static char const *const reserved[] = {
        "AUX", "CLOCK$", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "CON", "CONIN$", "CONOUT$",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9", "NUL", "PRN"
    };

    /* Win32 strips trailing spaces and dots from a component before it looks the name up, so
     * "nul " and "con." reach the device namespace exactly like "nul" and "con" do. Trim them
     * first, then cut the stem at the first dot - or the first ':', since Win32 reads "CON:" and
     * "CON::$DATA" as the device too (a stream name after a device name is ignored). */
    USize trimmed_size = name_size;

    while (trimmed_size > 0 && (name[trimmed_size - 1] == ' ' || name[trimmed_size - 1] == '.')) {
        trimmed_size -= 1;
    }

    USize stem_size = trimmed_size;

    for (USize i = 0; i < trimmed_size; i += 1) {
        if (name[i] == '.' || name[i] == ':') {
            stem_size = i;

            break;
        }
    }

    /* Win32 trims the stem too before its device check: "CONIN$ .txt" and "AUX .html" open the
     * device exactly like "CONIN$.txt" and "AUX.html" do. */
    while (stem_size > 0 && (name[stem_size - 1] == ' ' || name[stem_size - 1] == '.')) {
        stem_size -= 1;
    }

    bool is_reserved = false;

    for (USize i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i += 1) {
        if (char_compare_iequal_2(name, stem_size, reserved[i], char_length(reserved[i]))) {
            is_reserved = true;

            break;
        }
    }

    /* COM or LPT followed only by bytes >= 0x80: the ANSI APIs read the single cp1252 byte
     * 0xB9/0xB2/0xB3 as the superscript device digit, the wide APIs the UTF-8 pair. */
    if (!is_reserved && stem_size > 3 && (char_compare_iequal_2(name, 3, "COM", 3) || char_compare_iequal_2(name, 3, "LPT", 3))) {
        is_reserved = true;

        for (USize i = 3; i < stem_size; i += 1) {
            if ((U8) name[i] < 0x80) {
                is_reserved = false;

                break;
            }
        }
    }

    trace_log_pop();

    return is_reserved;
}

File* file_open_1(char const *const file_name, char const *const file_mode) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "file_mode", (void*) file_mode);

    File *file = file_open_try_1(file_name, file_mode);

    error_check_null(LOG_METADATA, "file", (void*) file);

    trace_log_pop();

    return file;
}

File* file_open_2(Str const *const file_name, char const *const file_mode) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    File *file = file_open_1(str_get_data(file_name), file_mode);

    trace_log_pop();

    return file;
}

File* file_open_3(String const *const file_name, char const *const file_mode) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    File *file = file_open_1(string_get_data(file_name), file_mode);

    trace_log_pop();

    return file;
}

#ifdef _WIN32
/*
 * Close a handle the nofollow opener is refusing and leave ERROR as the thread's error state -
 * set AFTER the close, which is free to overwrite whatever was there before it.
 */
static File* _file_nofollow_refuse(HANDLE const handle, DWORD const error) {
    trace_log_push(LOG_METADATA);

    CloseHandle(handle);
    SetLastError(error);

    trace_log_pop();

    return nullptr;
}

/*
 * Whether the nofollow opener must refuse PATH on its spelling alone, before any open. For a
 * device the OPEN is the hazard - it can block on a console, connect to a named pipe's server,
 * or hand back a stream that is not the on-disk file the name meant - and for a UNC path it is
 * too: the open connects to the named host (SMB, authenticating with NTLM as this process), or
 * through the loopback (\\localhost\pipe\x, \\127.0.0.1\pipe\x) to a local named pipe. So the
 * handle-type check after it comes too late. Three shapes, all pure bytes:
 *   - two leading separators, '/' and '\' read alike in either place as Win32 reads them: every
 *     UNC path, and with it the device namespaces \\.\ and \\?\ - which reach devices and pipes
 *     by name, \\?\ also skipping the name rewrites every other check relies on - so even a real
 *     file spelled that way is refused.
 *   - the NT \??\ that Win32 passes through verbatim. It starts with ONE separator, so the rule
 *     above does not see it.
 *   - a final component Win32 opens as a device (file_name_reserved_windows). Trailing
 *     separators are skipped first, and any "X:" prefix is dropped from the component, since
 *     "C:nul" opens NUL exactly as "nul" does - which also refuses a one-letter file's
 *     device-named stream ("C:\a\b:con"), a deliberate over-refusal.
 */
static bool _file_path_device_windows(char const *const path, USize const path_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool const unc         = path_size >= _FILE_UNC_PREFIX_SIZE && (path[0] == '/' || path[0] == '\\') && (path[1] == '/' || path[1] == '\\');
    bool const passthrough = path_size >= _FILE_NT_PREFIX_SIZE && char_compare_equal_2(path, _FILE_NT_PREFIX_SIZE, _FILE_NT_PREFIX, _FILE_NT_PREFIX_SIZE);

    if (unc || passthrough) {
        trace_log_pop();

        return true;
    }

    USize end = path_size;

    while (end > 0 && (path[end - 1] == '/' || path[end - 1] == '\\')) {
        end -= 1;
    }

    USize start = end;

    while (start > 0 && path[start - 1] != '/' && path[start - 1] != '\\') {
        start -= 1;
    }

    if (end - start >= _FILE_DRIVE_SPEC_SIZE && path[start + _FILE_DRIVE_SPEC_SIZE - 1] == ':') {
        start += _FILE_DRIVE_SPEC_SIZE;
    }

    bool const reserved = file_name_reserved_windows(path + start, end - start);

    trace_log_pop();

    return reserved;
}
#endif // _WIN32

/*
 * The nofollow open, shared by file_open_nofollow_try_1/2/3: a size rather than a terminator, so
 * the Str form needs no copy. See file_open_nofollow_try_1 (file.h) for the refusal table; the
 * comments here say only why each step has the shape it has.
 */
static File* _file_open_nofollow(char const *const path, USize const path_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

#ifdef _WIN32
    // Before any open: for a device or a UNC path the open IS the hazard, so the post-open
    // FILE_TYPE_DISK check below cannot be the one that refuses it. Same code as that check.
    if (_file_path_device_windows(path, path_size)) {
        SetLastError(ERROR_INVALID_HANDLE);

        trace_log_pop();

        return nullptr;
    }

    WCHAR wide[FILE_PATH_WIDE_MAX] = DEFAULT_INITIALIZATION;

    if (!_file_path_wide(path, path_size, wide)) {
        trace_log_pop();

        return nullptr;
    }

    // FILE_FLAG_OPEN_REPARSE_POINT opens a link ITSELF instead of its target, so the attribute
    // query below sees the link; without FILE_FLAG_BACKUP_SEMANTICS a directory fails to open.
    HANDLE handle = _file_open_wide(wide, GENERIC_READ, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN);

    if (handle == INVALID_HANDLE_VALUE) {
        // CreateFile2's own error state stands - ERROR_FILE_NOT_FOUND stays "not there".
        trace_log_pop();

        return nullptr;
    }

    FILE_ATTRIBUTE_TAG_INFO tag = DEFAULT_INITIALIZATION;

    if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag))) {
        trace_log_pop();

        return _file_nofollow_refuse(handle, GetLastError());
    }

    // CreateFile2 already refuses a directory without BACKUP_SEMANTICS; re-checked here so the
    // refusal does not rest on that flag's absence alone.
    if ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        trace_log_pop();

        return _file_nofollow_refuse(handle, ERROR_ACCESS_DENIED);
    }

    if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        // A name surrogate - a symlink or a junction - is a LINK: refused. The same rule
        // dir_list_entries uses for is_link, so the two never disagree about what a link is.
        if (IsReparseTagNameSurrogate(tag.ReparseTag)) {
            trace_log_pop();

            return _file_nofollow_refuse(handle, ERROR_STOPPED_ON_SYMLINK);
        }

        /* Any other reparse point (a Dropbox / OneDrive placeholder, dedup, WOF) is a FILE whose
         * data a filter driver serves - and opening the reparse point itself bypasses that
         * driver. Re-opened without the flag, which makes the re-open a second path resolution:
         * between the close and the re-open the entry can be swapped for a symlink, and the
         * unflagged re-open would FOLLOW it. So the re-open is pinned to the first handle's
         * identity - volume serial plus 128-bit file ID (FILE_ID_INFO; the 32-bit serial and 64-bit index
         * of BY_HANDLE_FILE_INFORMATION are not unique on ReFS) - and anything else is refused AS a link. A
         * placeholder hydrated by the re-open keeps its file ID, so a real one still opens.
         * Re-opening WITH the flag to re-check the tag cannot replace the pin: the flagged open
         * is exactly the one that bypasses the driver. */
        FILE_ID_INFO first = DEFAULT_INITIALIZATION;

        if (!GetFileInformationByHandleEx(handle, FileIdInfo, &first, sizeof(first))) {
            trace_log_pop();

            return _file_nofollow_refuse(handle, ERROR_STOPPED_ON_SYMLINK);
        }

        CloseHandle(handle);

        handle = _file_open_wide(wide, GENERIC_READ, FILE_FLAG_SEQUENTIAL_SCAN);

        if (handle == INVALID_HANDLE_VALUE) {
            trace_log_pop();

            return nullptr;
        }

        FILE_ID_INFO second = DEFAULT_INITIALIZATION;

        /* The identifier is 16 raw bytes; the size-explicit compare never stops at a NUL. */
        bool const same = GetFileInformationByHandleEx(handle, FileIdInfo, &second, sizeof(second)) != 0                            &&
                          second.VolumeSerialNumber == first.VolumeSerialNumber                                                     &&
                          char_compare_equal_2((char const*) first.FileId.Identifier, sizeof(first.FileId.Identifier),
                                               (char const*) second.FileId.Identifier, sizeof(second.FileId.Identifier));

        if (!same) {
            trace_log_pop();

            return _file_nofollow_refuse(handle, ERROR_STOPPED_ON_SYMLINK);
        }
    }

    // A pipe, a console, a character device the spelling check cannot see (a device name
    // file_name_reserved_windows does not list): not a file, whatever its name says.
    if (GetFileType(handle) != FILE_TYPE_DISK) {
        trace_log_pop();

        return _file_nofollow_refuse(handle, ERROR_INVALID_HANDLE);
    }

    // On success the descriptor OWNS the handle, and the stream the descriptor: one fclose
    // (file_close) releases all three.
    int const descriptor = _open_osfhandle((intptr_t) handle, _O_RDONLY | _O_BINARY);

    if (descriptor == -1) {
        trace_log_pop();

        return _file_nofollow_refuse(handle, ERROR_TOO_MANY_OPEN_FILES);
    }

    File *const file = _fdopen(descriptor, "rb");

    if (memory_empty((void*) file)) {
        // _close closes the HANDLE too - the descriptor owns it now.
        _close(descriptor);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
#else
    char narrow[PATH_MAX] = DEFAULT_INITIALIZATION;

    if (!_file_path_narrow(path, path_size, narrow)) {
        trace_log_pop();

        return nullptr;
    }

    /* O_NOFOLLOW: a final-component symlink fails the open itself (ELOOP). O_NONBLOCK: a FIFO with
     * no writer would otherwise block this open forever; it stays set, since a regular file never
     * answers EAGAIN. O_NOCTTY: a terminal device cannot become the controlling terminal before
     * fstat refuses it. O_CLOEXEC: a child spawned meanwhile does not inherit the descriptor. */
    int const descriptor = open(narrow, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);

    if (descriptor < 0) {
        trace_log_pop();

        return nullptr;
    }

    struct stat info = DEFAULT_INITIALIZATION;

    if (fstat(descriptor, &info) != 0) {
        int const error = errno;

        close(descriptor);
        errno = error;

        trace_log_pop();

        return nullptr;
    }

    if (!S_ISREG(info.st_mode)) {
        close(descriptor);
        errno = S_ISDIR(info.st_mode) ? EISDIR : ENXIO;

        trace_log_pop();

        return nullptr;
    }

    File *const file = fdopen(descriptor, "rb");

    if (memory_empty((void*) file)) {
        int const error = errno;

        close(descriptor);
        errno = error;
    }
#endif // _WIN32

    trace_log_pop();

    return file;
}

File* file_open_nofollow_try_1(char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    // CONTRACT (file.h): nothing runs between the core's return and this one that could
    // overwrite errno / GetLastError.
    File *const file = _file_open_nofollow(path, char_length(path));

    trace_log_pop();

    return file;
}

File* file_open_nofollow_try_2(Str const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    // An empty Str carries a nullptr buffer: refused by value, with no syscall and the OS error
    // state untouched - file_open_try_2's shape.
    if (str_get_size(path) == 0) {
        trace_log_pop();

        return nullptr;
    }

    File *const file = _file_open_nofollow(str_get_data(path), str_get_size(path));

    trace_log_pop();

    return file;
}

File* file_open_nofollow_try_3(String const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    // See file_open_nofollow_try_2.
    if (string_get_size(path) == 0) {
        trace_log_pop();

        return nullptr;
    }

    File *const file = _file_open_nofollow(string_get_data(path), string_get_size(path));

    trace_log_pop();

    return file;
}

File* file_open_try_1(char const *const file_name, char const *const file_mode) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "file_mode", (void*) file_mode);

    // No error_check_null on the result: a path that is not there is an ordinary
    // outcome to report, not a programming error to abort on. This is what makes
    // the difference race-free - testing file_exists_1 first would leave a window
    // in which the file is deleted between the test and the open.
    // CONTRACT (file.h): nothing may run between a failed fopen and the return
    // that could overwrite errno / GetLastError - callers classify the failure
    // from that state.
    File *file = fopen(file_name, file_mode);

    trace_log_pop();

    return file;
}

File* file_open_try_2(Str const *const file_name, char const *const file_mode) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    // file_open_try_1 reports failure by nullptr rather than aborting; an empty Str's nullptr
    // buffer must degrade the same way, not reach file_open_1's own error_check_null on it.
    if (str_get_size(file_name) == 0) {
        trace_log_pop();

        return nullptr;
    }

    File *file = file_open_try_1(str_get_data(file_name), file_mode);

    trace_log_pop();

    return file;
}

File* file_open_try_3(String const *const file_name, char const *const file_mode) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    // See file_open_try_2.
    if (string_get_size(file_name) == 0) {
        trace_log_pop();

        return nullptr;
    }

    File *file = file_open_try_1(string_get_data(file_name), file_mode);

    trace_log_pop();

    return file;
}

File* file_open_wait_1(char const *const file_name, char const *const file_mode, U32 const ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "file_mode", (void*) file_mode);

    File *file = nullptr;
    I32 attempts = 0;

    do {
        file = file_open_try_1(file_name, file_mode);

        if (memory_empty((void*) file)) {
            attempts += 1;

            if (attempts >= FILE_OPEN_WAIT_ATTEMPTS_MAX) {
                break;
            }
            #ifdef __linux__
            // usleep, not sleep: the parameter is documented in milliseconds and
            // sleep takes SECONDS, so every caller waited a thousand times too
            // long - a 30 ms retry became half a minute.
            usleep((useconds_t) ms * 1000);
            #elif defined(_WIN32)
            Sleep(ms);
            #endif
        }
    } while (memory_empty((void*) file));

    trace_log_pop();

    return file;
}

File* file_open_wait_2(Str const *const file_name, char const *const file_mode, U32 const ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    // See file_open_try_2: file_open_wait_1 now routes through file_open_try_1, so it
    // inherits the same reports-failure-not-abort contract on an empty container.
    if (str_get_size(file_name) == 0) {
        trace_log_pop();

        return nullptr;
    }

    File *file = file_open_wait_1(str_get_data(file_name), file_mode, ms);

    trace_log_pop();

    return file;
}

File* file_open_wait_3(String const *const file_name, char const *const file_mode, U32 const ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    // See file_open_try_2.
    if (string_get_size(file_name) == 0) {
        trace_log_pop();

        return nullptr;
    }

    File *file = file_open_wait_1(string_get_data(file_name), file_mode, ms);

    trace_log_pop();

    return file;
}

/*
 * Whether one path component has the shape of an NTFS 8.3 short alias: a stem (the bytes before
 * the first '.') of at most 8 bytes holding '~' followed by an ASCII digit, an extension of at
 * most 3 bytes after that dot, and no space anywhere. Narrower than "contains ~digit": a stem
 * longer than 8 bytes or an extension longer than 3 can never be a short name, so a long name
 * like "my-report~2.pdf" is not refused for looking like one.
 */
static bool _file_component_short_name_shaped(char const *const component, USize const component_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "component", (void*) component);

    USize stem_size = component_size;

    for (USize i = 0; i < component_size; i += 1) {
        if (component[i] == ' ') {
            trace_log_pop();

            return false;
        }

        if (component[i] == '.' && stem_size == component_size) {
            stem_size = i;
        }
    }

    USize const extension_size = stem_size < component_size ? component_size - stem_size - 1 : 0;

    bool shaped = false;

    if (stem_size <= 8 && extension_size <= 3) {
        for (USize i = 0; i + 1 < stem_size; i += 1) {
            if (component[i] == '~' && char_is_number(component[i + 1])) {
                shaped = true;

                break;
            }
        }
    }

    trace_log_pop();

    return shaped;
}

bool file_path_aliased_windows(char const *const path, USize const path_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    bool refused = false;
    USize cursor = 0;

    while (!refused && cursor < path_size) {
        USize const component_start = cursor;

        while (cursor < path_size && path[cursor] != '/' && path[cursor] != '\\') {
            /* ':' can only spell an alternate data stream or a drive on Windows. */
            if (path[cursor] == ':') {
                refused = true;
            }

            cursor += 1;
        }

        USize const component_size = cursor - component_start;

        /* Win32 strips a trailing dot, intermediate components included, so "sub./a.txt" opens
         * sub/a.txt. (A trailing space on an intermediate component is not stripped; refusing
         * both keeps one rule.) */
        if (component_size > 0 && (path[cursor - 1] == '.' || path[cursor - 1] == ' ')) {
            refused = true;
        }

        if (_file_component_short_name_shaped(path + component_start, component_size)) {
            refused = true;
        }

        cursor += 1;
    }

    trace_log_pop();

    return refused;
}

USize file_read_1(File *const self, void *const data, USize const data_size, USize const data_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);
    error_check_non_value_uint(LOG_METADATA, "data_count", data_count);

    USize const file_size = fread(data, data_size, data_count, self);

    trace_log_pop();

    return file_size;
}

void file_read_2(File *const self, Str *const data, USize const data_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Bounded by the Str's own space: the count is caller-supplied and fread
     * honours it literally, so an oversized count wrote past the allocation and
     * str_set_size then recorded a size the buffer never had. */
    USize const usable = str_get_size(data);
    USize const bounded = data_count > usable ? usable : data_count;

    /* Zero the size rather than leaving the PRIOR read's count standing: the
     * caller's idiom is "read, then test the size", so a stale value reports old
     * bytes as freshly read. */
    if (bounded == 0) {
        str_set_size(data, 0);

        trace_log_pop();

        return;
    }

    str_set_size(data, file_read_1(self, str_get_data(data), sizeof(char), bounded));

    trace_log_pop();
}

void file_read_3(File *const self, String *const data) {
    trace_log_push(LOG_METADATA);

    file_read_4(self, data, string_get_capacity(data));

    trace_log_pop();
}

void file_read_4(File *const self, String *const data, USize const data_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    /* Bounded by the String's capacity LESS the terminator slot: the count is
     * caller-supplied and fread honours it literally, so an oversized count wrote
     * past the allocation and string_set_size then recorded size > capacity. The
     * reserved byte keeps string_get_data usable as a C string, which is the
     * invariant every whole-file reader in this module relies on. */
    USize const capacity = string_get_capacity(data);
    USize const usable = capacity > CHAR_END_CHARACTER ? capacity - CHAR_END_CHARACTER : 0;
    USize const bounded = data_count > usable ? usable : data_count;

    /* Zero the size rather than leaving the PRIOR read's count standing (see
     * file_read_2). */
    if (bounded == 0) {
        string_set_size(data, 0);

        trace_log_pop();

        return;
    }

    string_set_size(data, file_read_1(self, string_get_data(data), sizeof(char), bounded));

    trace_log_pop();
}

char* file_read_all_1(File *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    char *const file_buffer = _file_read_to_raw(self);

    trace_log_pop();

    return file_buffer;
}

Str file_read_all_2(File *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    Str const file_buffer = _file_read_to_str(self);

    trace_log_pop();

    return file_buffer;
}

String file_read_all_3(File *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    String const file_buffer = _file_read_to(self);

    trace_log_pop();

    return file_buffer;
}

char* file_read_to_char(char const *const file_name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    File *file = file_open_try_1(file_name, "r");

    char *const file_buffer = _file_read_to_raw(file);

    _file_close_try(&file);

    trace_log_pop();

    return file_buffer;
}

Str file_read_to_str(char const *const file_name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    File *file = file_open_try_1(file_name, "r");

    Str const file_buffer = _file_read_to_str(file);

    _file_close_try(&file);

    trace_log_pop();

    return file_buffer;
}

String file_read_to_string(char const *const file_name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    File *file = file_open_try_1(file_name, "r");

    String const file_buffer = _file_read_to(file);

    _file_close_try(&file);

    trace_log_pop();

    return file_buffer;
}

char* file_read_to_char_wait(char const *const file_name, U32 const ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    File *file = file_open_wait_1(file_name, "r", ms);

    char *const file_buffer = _file_read_to_raw(file);

    _file_close_try(&file);

    trace_log_pop();

    return file_buffer;
}

Str file_read_to_str_wait(char const *const file_name, U32 const ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    File *file = file_open_wait_1(file_name, "r", ms);

    Str const file_buffer = _file_read_to_str(file);

    _file_close_try(&file);

    trace_log_pop();

    return file_buffer;
}

String file_read_to_string_wait(char const *const file_name, U32 const ms) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    File *file = file_open_wait_1(file_name, "r", ms);

    String const file_buffer = _file_read_to(file);

    _file_close_try(&file);

    trace_log_pop();

    return file_buffer;
}

bool file_regular(File *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const regular = _file_is_regular(self);

    trace_log_pop();

    return regular;
}

Result file_remove_1(char const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    // Refused rather than mis-targeted - see _file_path_addressable, whose MEMORY answer passes through.
    Result const addressable = _file_path_addressable(path);

    if (result_is_error(addressable)) {
        trace_log_pop();

        return addressable;
    }

    // unlink/DeleteFileA both refuse a directory on their own (EISDIR / ERROR_ACCESS_DENIED),
    // which is exactly the "regular file only" contract this function promises - no separate
    // guard needed to enforce it.
#ifdef _WIN32
    bool const removed = DeleteFileA((LPCSTR) path) != 0;
#else
    bool const removed = unlink(path) == 0;
#endif

    if (removed) {
        trace_log_pop();

        return RESULT_SUCCESS;
    }

    Result const failure = result_from_os();

    trace_log_pop();

    return result_is_error(failure) ? failure : result_make(RESULT_CATEGORY_IO, 0, 0);
}

Result file_remove_2(Str const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    if (str_get_size(path) == 0) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, FILE_ARGUMENT_EMPTY_PATH, 0);
    }

    char *const path_text = char_new_3(str_get_data(path), str_get_size(path));

    /* The _1 below opens with error_check_null, which ABORTS - inside a family whose header
     * promises it never aborts on an outcome. trash_send_2/_3 were given this exact guard in
     * an earlier round, with a comment naming the reason; the file_* wrappers were written in
     * the same rounds against the same promise and did not get it. */
    if (memory_empty(path_text)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
    }

    Result const result = file_remove_1(path_text);

    char_delete(path_text);

    trace_log_pop();

    return result;
}

Result file_remove_3(String const *const path) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    if (string_get_size(path) == 0) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, FILE_ARGUMENT_EMPTY_PATH, 0);
    }

    char *const path_text = char_new_3(string_get_data(path), string_get_size(path));

    /* The _1 below opens with error_check_null, which ABORTS - inside a family whose header
     * promises it never aborts on an outcome. trash_send_2/_3 were given this exact guard in
     * an earlier round, with a comment naming the reason; the file_* wrappers were written in
     * the same rounds against the same promise and did not get it. */
    if (memory_empty(path_text)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
    }

    Result const result = file_remove_1(path_text);

    char_delete(path_text);

    trace_log_pop();

    return result;
}

Result file_rename_1(char const *const source, char const *const destination) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "source", (void*) source);
    error_check_null(LOG_METADATA, "destination", (void*) destination);

    /* BOTH ends: renaming an addressable source onto an unaddressable destination would
     * replace whatever the code page names, which is the destructive half of this defect. */
    Result const source_addressable = _file_path_addressable(source);

    if (result_is_error(source_addressable)) {
        trace_log_pop();

        return source_addressable;
    }

    Result const destination_addressable = _file_path_addressable(destination);

    if (result_is_error(destination_addressable)) {
        trace_log_pop();

        return destination_addressable;
    }

    // A bare Windows MoveFile refuses an existing destination, unlike POSIX rename() -
    // MOVEFILE_REPLACE_EXISTING is what makes the two platforms agree on always-overwrite.
    // A cross-device attempt fails on both (EXDEV / ERROR_NOT_SAME_DEVICE) rather than
    // silently falling back to a copy - see file.h's contract on this function.
#ifdef _WIN32
    bool const renamed = MoveFileExA((LPCSTR) source, (LPCSTR) destination, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    bool const renamed = rename(source, destination) == 0;
#endif

    if (renamed) {
        trace_log_pop();

        return RESULT_SUCCESS;
    }

    Result const failure = result_from_os();

    trace_log_pop();

    return result_is_error(failure) ? failure : result_make(RESULT_CATEGORY_IO, 0, 0);
}

Result file_rename_2(Str const *const source, Str const *const destination) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "source", (void*) source);
    error_check_null(LOG_METADATA, "destination", (void*) destination);

    if (str_get_size(source) == 0 || str_get_size(destination) == 0) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, FILE_ARGUMENT_EMPTY_PATH, 0);
    }

    char *const source_text      = char_new_3(str_get_data(source), str_get_size(source));
    char *const destination_text = char_new_3(str_get_data(destination), str_get_size(destination));

    /* The _1 below opens with error_check_null, which ABORTS - inside a family whose header
     * promises it never aborts on an outcome. Each delete is guarded because the other
     * allocation may have succeeded, and CFW's free aborts on nullptr. */
    if (memory_empty(source_text) || memory_empty(destination_text)) {
        if (!memory_empty(source_text)) {
            char_delete(source_text);
        }

        if (!memory_empty(destination_text)) {
            char_delete(destination_text);
        }

        trace_log_pop();

        return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
    }

    Result const result = file_rename_1(source_text, destination_text);

    char_delete(source_text);
    char_delete(destination_text);

    trace_log_pop();

    return result;
}

Result file_rename_3(String const *const source, String const *const destination) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "source", (void*) source);
    error_check_null(LOG_METADATA, "destination", (void*) destination);

    if (string_get_size(source) == 0 || string_get_size(destination) == 0) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, FILE_ARGUMENT_EMPTY_PATH, 0);
    }

    char *const source_text      = char_new_3(string_get_data(source), string_get_size(source));
    char *const destination_text = char_new_3(string_get_data(destination), string_get_size(destination));

    /* The _1 below opens with error_check_null, which ABORTS - inside a family whose header
     * promises it never aborts on an outcome. Each delete is guarded because the other
     * allocation may have succeeded, and CFW's free aborts on nullptr. */
    if (memory_empty(source_text) || memory_empty(destination_text)) {
        if (!memory_empty(source_text)) {
            char_delete(source_text);
        }

        if (!memory_empty(destination_text)) {
            char_delete(destination_text);
        }

        trace_log_pop();

        return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
    }

    Result const result = file_rename_1(source_text, destination_text);

    char_delete(source_text);
    char_delete(destination_text);

    trace_log_pop();

    return result;
}

/*
 * A path beside the destination (same directory, by construction - it is just the destination plus a
 * suffix), unique enough for one process's own use: the OS pid rules out collision with
 * another concurrently-replacing process, though not with a second file_replace_1 call
 * racing itself within this same process against the same destination (out of scope - CFW's
 * own callers are single-threaded per file per the module's Thread Safety contract).
 */
static char* _file_replace_temp_path(char const *const destination, USize const attempt) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "destination", (void*) destination);

#ifdef _WIN32
    USize const pid = (USize) GetCurrentProcessId();
#else
    USize const pid = (USize) getpid();
#endif

    char pid_text[_FILE_NUMBER_TEXT_MAX]     = DEFAULT_INITIALIZATION;
    char attempt_text[_FILE_NUMBER_TEXT_MAX] = DEFAULT_INITIALIZATION;

    char_from_numbers_uint_1(pid_text, sizeof(pid_text), pid);
    char_from_numbers_uint_1(attempt_text, sizeof(attempt_text), attempt);

    /* The pid alone was not enough. It rules out a COLLISION WITH ANOTHER PROCESS, which is
     * the case the original comment considered - but not the cross-TIME case that actually
     * bites: a killed run, or a recycled pid, leaves a stale temp behind, the exclusive
     * create then refuses it, and file_replace_1 fails FOREVER for that destination while
     * reporting "destination exists" about a hidden file the caller never named. The attempt
     * counter walks past it, exactly as the trash module's name-claiming loop does. */
    char const *const parts[] = { destination, ".tmp.", pid_text, ".", attempt_text };
    char *const temp_path     = char_join_1(parts, 5, "");

    trace_log_pop();

    return temp_path;
}

/*
 * Carry the destination's existing permissions onto the temp that is about to become it.
 *
 * Without this, file_replace_1 SILENTLY RESET them: the temp is a fresh file created under
 * the umask, and the rename makes that fresh file the destination - so a 0600 secrets file
 * came back 0644 on a successful "replace the contents" call. Nothing in the contract hinted
 * that a success changes destination metadata, and a permission regression is not the kind
 * of surprise a caller finds quickly.
 *
 * Windows is a documented gap rather than a silent one: an explicit ACL on the destination
 * is replaced by fresh inheritance, which needs the security-descriptor APIs to carry over
 * and is out of this function's scope.
 */
static void _file_replace_carry_mode(char const *const destination, char const *const temp_path) {
    trace_log_push(LOG_METADATA);

#ifdef __linux__
    struct stat status = DEFAULT_INITIALIZATION;

    if (stat(destination, &status) != 0) {
        // Nothing to carry: a destination that does not exist yet has no mode to preserve.
        trace_log_pop();

        return;
    }

    /* Reported rather than swallowed. A failed stat is a legitimate "nothing to carry"; a
     * failed chmod AFTER a successful stat is the permission regression this helper exists
     * to prevent, and discarding its result reproduces exactly the silent surprise the
     * function was added to stop. Not fatal - the CONTENT is correct, which is what the
     * caller asked for. */
    if (chmod(temp_path, status.st_mode & 07777) != 0) {
        log_message_try_1(LOG_LEVEL_WARN, "file: replaced the file but could not carry its permissions across (%s)\n", destination);
    }
#else
    (void) destination;
    (void) temp_path;
#endif

    trace_log_pop();
}

Result file_replace_1(char const *const source, char const *const destination) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "source", (void*) source);
    error_check_null(LOG_METADATA, "destination", (void*) destination);

    /* THE EXHAUSTION VERDICT, and the value returned if the loop below never breaks out:
     * every candidate temp name beside the destination was taken. That is EXISTS-shaped and
     * diagnosable, and it is spelled the way this module spells its own refusals - STATE plus
     * the CRT's EEXIST - so file_result_is_exists recognises it. A bare (IO, 0, 0) told the
     * caller nothing, and made the one exhaustion this function can actually hit the one it
     * could not name. */
    Result result = result_make(RESULT_CATEGORY_STATE, (U32) EEXIST, 0);

    /* Bounded rather than one fixed name: attempt 0 is the ordinary case, and the retries
     * exist only to step past a stale temp left by a killed run (see the helper). A caller
     * hitting the bound has thousands of them and a real problem to look at. */
    for (USize attempt = 0; attempt < _FILE_REPLACE_ATTEMPTS_MAX; attempt += 1) {
        char *const temp_path = _file_replace_temp_path(destination, attempt);

        if (memory_empty((void*) temp_path)) {
            trace_log_pop();

            return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
        }

        /* 0600 AT CREATION. The temp sits at a predictable name in the destination's own
         * directory for the whole duration of the copy, so anything broader is a window in
         * which another local user can open a secret and hold the descriptor across the
         * rename. The destination's real mode is carried over afterwards. */
        Result const copied = _file_copy_exclusive(source, temp_path, 0600);

        if (file_result_is_exists(copied)) {
            // Someone else's temp, or our own from a previous life. Try the next name.
            char_delete(temp_path);

            continue;
        }

        if (result_is_error(copied)) {
            /* file_copy_1 removes its own partial now, so there is nothing of ours left at
             * temp_path - and the destination has not been touched at all. */
            char_delete(temp_path);

            trace_log_pop();

            return copied;
        }

        _file_replace_carry_mode(destination, temp_path);

        /* The rename is the one moment the destination actually changes; same-directory by
         * construction, so this is the platform's own atomic rename - never a window where
         * the destination is observably torn or missing. */
        result = file_rename_1(temp_path, destination);

        if (result_is_error(result)) {
            file_remove_1(temp_path);
        }

        char_delete(temp_path);

        trace_log_pop();

        return result;
    }

    trace_log_pop();

    return result;
}

Result file_replace_2(Str const *const source, Str const *const destination) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "source", (void*) source);
    error_check_null(LOG_METADATA, "destination", (void*) destination);

    if (str_get_size(source) == 0 || str_get_size(destination) == 0) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, FILE_ARGUMENT_EMPTY_PATH, 0);
    }

    char *const source_text      = char_new_3(str_get_data(source), str_get_size(source));
    char *const destination_text = char_new_3(str_get_data(destination), str_get_size(destination));

    /* The _1 below opens with error_check_null, which ABORTS - inside a family whose header
     * promises it never aborts on an outcome. Each delete is guarded because the other
     * allocation may have succeeded, and CFW's free aborts on nullptr. */
    if (memory_empty(source_text) || memory_empty(destination_text)) {
        if (!memory_empty(source_text)) {
            char_delete(source_text);
        }

        if (!memory_empty(destination_text)) {
            char_delete(destination_text);
        }

        trace_log_pop();

        return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
    }

    Result const result = file_replace_1(source_text, destination_text);

    char_delete(source_text);
    char_delete(destination_text);

    trace_log_pop();

    return result;
}

Result file_replace_3(String const *const source, String const *const destination) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "source", (void*) source);
    error_check_null(LOG_METADATA, "destination", (void*) destination);

    if (string_get_size(source) == 0 || string_get_size(destination) == 0) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, FILE_ARGUMENT_EMPTY_PATH, 0);
    }

    char *const source_text      = char_new_3(string_get_data(source), string_get_size(source));
    char *const destination_text = char_new_3(string_get_data(destination), string_get_size(destination));

    /* The _1 below opens with error_check_null, which ABORTS - inside a family whose header
     * promises it never aborts on an outcome. Each delete is guarded because the other
     * allocation may have succeeded, and CFW's free aborts on nullptr. */
    if (memory_empty(source_text) || memory_empty(destination_text)) {
        if (!memory_empty(source_text)) {
            char_delete(source_text);
        }

        if (!memory_empty(destination_text)) {
            char_delete(destination_text);
        }

        trace_log_pop();

        return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
    }

    Result const result = file_replace_1(source_text, destination_text);

    char_delete(source_text);
    char_delete(destination_text);

    trace_log_pop();

    return result;
}

/* The per-platform codes the three predicates below classify. Kept here, once, so no
 * consumer has to know them - the whole point of the predicates.
 *
 * MEASURED on this toolchain, and the reason these are not a simple code comparison:
 *   EEXIST = 17    ERROR_NOT_SAME_DEVICE = 17   <- THE SAME NUMBER
 *   ENOENT = 2     ERROR_FILE_NOT_FOUND  = 2    ERROR_PATH_NOT_FOUND = 3
 *   ERROR_FILE_EXISTS = 80   ERROR_ALREADY_EXISTS = 183
 *
 * So on Windows the code alone cannot separate "the destination exists" from "different
 * filesystem" - the natural `if (is_exists) ... else if (is_cross_device)` took the wrong
 * branch for every cross-device rename. The CATEGORY is what disambiguates: the STATE
 * spelling is file_copy_1's own refusal (which carries the C errno), and every other
 * spelling came from the OS - on Windows the copy path files _doserrno, the Win32 code the
 * CRT keeps beside errno, so every OS-shaped code in this family is Win32-shaped there.
 */
#ifdef _WIN32
#define _FILE_CODE_CROSS_DEVICE ERROR_NOT_SAME_DEVICE
#else
#define _FILE_CODE_CROSS_DEVICE EXDEV
#endif

bool file_result_is_cross_device(Result const result) {
    // Never a STATE (this module's own refusal, never an OS verdict - its EEXIST code is
    // numerically identical to ERROR_NOT_SAME_DEVICE on Windows) and never an ARGUMENT (this
    // module's own refusal too, the sibling predicates' guard) - categorically closed rather
    // than resting on the FileArgument base staying clear of EXDEV/ERROR_NOT_SAME_DEVICE.
    return result_is_error(result)
        && result_category(result) != RESULT_CATEGORY_STATE
        && result_category(result) != RESULT_CATEGORY_ARGUMENT
        && result_code(result) == (U16) _FILE_CODE_CROSS_DEVICE;
}

bool file_result_is_exists(Result const result) {
    if (!result_is_error(result)) {
        return false;
    }

    // file_copy_1's own refusal, which reports the CRT's errno rather than an OS code.
    if (result_category(result) == RESULT_CATEGORY_STATE) {
        return result_code(result) == (U16) EEXIST;
    }

    // Never ARGUMENT: that category is this module's own refusal (a bad call, not an OS
    // verdict) and FileArgument's values are not guaranteed clear of every OS code on every
    // platform - the collision that made file_result_is_not_found answer wrongly below.
    if (result_category(result) == RESULT_CATEGORY_ARGUMENT) {
        return false;
    }

#ifdef _WIN32
    // ERROR_ALREADY_EXISTS as well as ERROR_FILE_EXISTS: MoveFileEx and CreateFile return
    // the former where the latter is the one people expect.
    return result_code(result) == (U16) ERROR_FILE_EXISTS
        || result_code(result) == (U16) ERROR_ALREADY_EXISTS;
#else
    return result_code(result) == (U16) EEXIST;
#endif
}

bool file_result_is_not_found(Result const result) {
    // Categorically closed, like file_result_is_exists and file_result_is_cross_device: this
    // module's own ARGUMENT refusals are never an OS not-found answer, regardless of where
    // FileArgument's values happen to sit. (History: with the ORIGINAL 1/2/3 numbering,
    // FILE_ARGUMENT_NOT_REGULAR equalled ENOENT/ERROR_FILE_NOT_FOUND and FILE_ARGUMENT_EMPTY_PATH
    // equalled ERROR_PATH_NOT_FOUND, so this refusal read back as "the path is not there" - the
    // memsec HIGH this guard closed. The enum was also rebased to 0x8001+ as a second layer, but
    // this guard is what makes the predicate correct on its own.)
    if (!result_is_error(result) || result_category(result) == RESULT_CATEGORY_ARGUMENT) {
        return false;
    }

#ifdef _WIN32
    /* ERROR_PATH_NOT_FOUND (3) is the answer when a DIRECTORY COMPONENT is missing - a
     * rename into a non-existent directory, a remove under a missing parent. Omitting it
     * made the predicate answer differently on the two platforms for the same situation,
     * which is worse than having no predicate: a caller stops checking. */
    return result_code(result) == (U16) ERROR_FILE_NOT_FOUND
        || result_code(result) == (U16) ERROR_PATH_NOT_FOUND;
#else
    return result_code(result) == (U16) ENOENT;
#endif
}

bool file_result_is_unaddressable(Result const result) {
    // The one ARGUMENT refusal with a remedy the caller can act on (the trash path still
    // handles such a name); the other two codes in that category are plain call mistakes.
    return result_is_error(result)
        && result_category(result) == RESULT_CATEGORY_ARGUMENT
        && result_code(result) == (U16) FILE_ARGUMENT_UNADDRESSABLE_PATH;
}

Result file_set_modified_1(char const *const path, I64 const seconds) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    // Refused rather than mis-targeted - see _file_path_addressable, whose MEMORY answer passes through.
    Result const addressable = _file_path_addressable(path);

    if (result_is_error(addressable)) {
        trace_log_pop();

        return addressable;
    }

    bool success = false;
#ifdef _WIN32
    // FILE_FLAG_BACKUP_SEMANTICS is what lets this open a DIRECTORY. Without it the
    // Windows branch silently no-opped on directories while the Linux utime branch
    // accepted them - and since the only caller discards the bool, nobody found out.
    HANDLE const handle = CreateFileA((LPCSTR) path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

    if (handle != INVALID_HANDLE_VALUE) {
        // Inverse of file_modified_1's FILETIME -> Unix epoch conversion: same epoch constant,
        // same tick rate, run backwards.
        ULONGLONG const epoch_ticks = 116444736000000000ULL;
        /* Negated on the UNSIGNED side. `-seconds` is undefined for I64_MIN, which has no
         * positive counterpart, and -O3 is entitled to assume it cannot happen. Not
         * attacker-reachable (these values come from file_modified_1), but it costs one
         * expression to be correct rather than lucky. */
        ULONGLONG const magnitude   = seconds < 0 ? ~(ULONGLONG) seconds + 1ULL : (ULONGLONG) seconds;
        ULONGLONG const ticks       = seconds < 0
            ? epoch_ticks - magnitude * _FILE_FILETIME_TICKS_PER_SECOND
            : epoch_ticks + magnitude * _FILE_FILETIME_TICKS_PER_SECOND;

        ULARGE_INTEGER value = DEFAULT_INITIALIZATION;

        value.QuadPart = ticks;

        FILETIME filetime      = DEFAULT_INITIALIZATION;
        filetime.dwLowDateTime  = value.LowPart;
        filetime.dwHighDateTime = value.HighPart;

        // Only the last-write-time argument is non-null: creation/access time are left alone.
        success = SetFileTime(handle, nullptr, nullptr, &filetime) != 0;

        CloseHandle(handle);
    }
#else
    struct utimbuf times = DEFAULT_INITIALIZATION;

    // Both access and modification set to the same value: utime() cannot set one without
    // the other, and the file.h contract only promises the modified time changes - a caller
    // that also cares about atime is out of this function's scope.
    times.actime  = (time_t) seconds;
    times.modtime = (time_t) seconds;

    success = utime(path, &times) == 0;
#endif

    if (success) {
        trace_log_pop();

        return RESULT_SUCCESS;
    }

    /* Classified from the OS, which is the whole reason this returns a Result: a missing
     * path, a read-only filesystem and a permission refusal are three different things a
     * caller may want to say, and a bare false said none of them. */
    Result const failure = result_from_os();

    trace_log_pop();

    return result_is_error(failure) ? failure : result_make(RESULT_CATEGORY_IO, 0, 0);
}

Result file_set_modified_2(Str const *const path, I64 const seconds) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    if (str_get_size(path) == 0) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, FILE_ARGUMENT_EMPTY_PATH, 0);
    }

    char *const path_text = char_new_3(str_get_data(path), str_get_size(path));

    if (memory_empty(path_text)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
    }

    Result const result = file_set_modified_1(path_text, seconds);

    char_delete(path_text);

    trace_log_pop();

    return result;
}

Result file_set_modified_3(String const *const path, I64 const seconds) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "path", (void*) path);

    if (string_get_size(path) == 0) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_ARGUMENT, FILE_ARGUMENT_EMPTY_PATH, 0);
    }

    char *const path_text = char_new_3(string_get_data(path), string_get_size(path));

    if (memory_empty(path_text)) {
        trace_log_pop();

        return result_make(RESULT_CATEGORY_MEMORY, 0, 0);
    }

    Result const result = file_set_modified_1(path_text, seconds);

    char_delete(path_text);

    trace_log_pop();

    return result;
}

bool file_size_1(char const *const file_name, USize *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "out", (void*) out);

    bool success = false;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data = DEFAULT_INITIALIZATION;

    if (GetFileAttributesExA((LPCSTR) file_name, GetFileExInfoStandard, &data)) {
        *out = ((USize) data.nFileSizeHigh << 32) | (USize) data.nFileSizeLow;
        success = true;
    }
#else
    struct stat status = DEFAULT_INITIALIZATION;

    if (stat(file_name, &status) == 0) {
        *out = (USize) status.st_size;
        success = true;
    }
#endif

    trace_log_pop();

    return success;
}

bool file_size_2(Str const *const file_name, USize *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "out", (void*) out);

    if (str_get_size(file_name) == 0) {
        trace_log_pop();

        return false;
    }

    char *const path_text = char_new_3(str_get_data(file_name), str_get_size(file_name));

    bool const success = file_size_1(path_text, out);

    char_delete(path_text);

    trace_log_pop();

    return success;
}

bool file_size_3(String const *const file_name, USize *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "out", (void*) out);

    if (string_get_size(file_name) == 0) {
        trace_log_pop();

        return false;
    }

    char *const path_text = char_new_3(string_get_data(file_name), string_get_size(file_name));

    bool const success = file_size_1(path_text, out);

    char_delete(path_text);

    trace_log_pop();

    return success;
}

USize file_write_1(File *const self, void const *const data, USize const data_size, USize const data_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "data_size", data_size);

    /* Writing an EMPTY buffer is a legal no-op (an empty Str/String forwards a
     * zero count), not caller error. This return sits ABOVE the buffer check on
     * purpose: the canonical empty container carries data == nullptr, so a null
     * check placed first would abort on exactly the case this guard exists for -
     * the fallback would be dead. A null buffer with a NON-zero count is still a
     * programming error and still aborts below. */
    if (data_count == 0) {
        trace_log_pop();

        return 0;
    }

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* fwrite's count is RETURNED, not discarded: a short write (disk full, quota,
     * a closed pipe) used to be indistinguishable from success, which is how an
     * upload endpoint reports a truncated file as saved. */
    USize const written = fwrite(data, data_size, data_count, self);

    trace_log_pop();

    return written;
}

USize file_write_2(File *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const written = file_write_3(self, data, str_get_size(data));

    trace_log_pop();

    return written;
}

USize file_write_3(File *const self, Str const *const data, USize const data_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty container writes nothing and returns 0 (see file_write_1). */
    USize const written = file_write_1(self, str_get_data(data), sizeof(char), data_count);

    trace_log_pop();

    return written;
}

USize file_write_4(File *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    USize const written = file_write_5(self, data, string_get_size(data));

    trace_log_pop();

    return written;
}

USize file_write_5(File *const self, String const *const data, USize const data_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    /* An empty container writes nothing and returns 0 (see file_write_1). */
    USize const written = file_write_1(self, string_get_data(data), sizeof(char), data_count);

    trace_log_pop();

    return written;
}