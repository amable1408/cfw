/*
 * file.h - Canonical file I/O interface for reading, writing, and managing files
 *
 * Features:
 *   - Cross-platform support (Linux, Windows)
 *   - Multiple string types (char*, Str, String)
 *   - File existence, size, and last-modified queries
 *   - Blocking file open (file_open_*), a non-aborting file_open_try_* family,
 *     and a retrying file_open_wait_* family
 *   - Read/write helpers for buffers and string types; the READ helpers clamp
 *     their count to the destination's own space (the write helpers honour the
 *     count they are given)
 *   - Whole-file readers: by path (file_read_to_*) and from an open handle
 *     (file_read_all_1/_2/_3), a padding-aligned variant for SIMD scanning
 *     (file_align_read_to_string), and the file_regular directory guard
 *   - Non-aborting file management by path: copy (file_copy_*, refuses an existing
 *     destination), atomic replace (file_replace_*, torn-write-proof via a same-directory
 *     temp file + rename), delete (file_remove_*), rename/move (file_rename_*, always
 *     replaces an existing destination like POSIX rename(), and reports EXDEV /
 *     ERROR_NOT_SAME_DEVICE explicitly rather than silently falling back to a copy), and
 *     file_set_modified_* (pairs with the file_modified_* getter)
 *
 * Usage Examples:
 *   @code
 *   File *file = file_open_try_1("data.txt", "r");
 *
 *   if (file != nullptr) {
 *       char *const contents = file_read_all_1(file);
 *
 *       file_close(&file);
 *       memory_free(contents);
 *   }
 *   @endcode
 *
 * Character Encoding (WINDOWS - AN UNADDRESSABLE PATH IS REFUSED):
 *   - The by-path management family goes through the ANSI Win32 entry points (DeleteFileA,
 *     MoveFileExA, CreateFileA) and the CRT's fopen, so a path is interpreted in the process
 *     code page rather than as UTF-8.
 *   - THE RULE, and it is mandatory for anything added to this family: a path whose UTF-8
 *     reading and code-page reading differ is REFUSED with RESULT_CATEGORY_ARGUMENT, never
 *     acted upon. file_copy_*, file_replace_*, file_remove_*, file_rename_* and
 *     file_set_modified_* all check it. A SIXTH by-path mutator added later must check it too.
 *     The refusal carries FILE_ARGUMENT_UNADDRESSABLE_PATH in its code field: ask
 *     file_result_is_unaddressable rather than comparing categories.
 *   - Why refusing and not "best effort": this was not a failure mode, it was a MIS-TARGET.
 *     The UTF-8 bytes of a non-ASCII name, read in the code page, spell a DIFFERENT name -
 *     and when the mojibake twin of a file exists (the ordinary result of a bad unzip or a
 *     cross-locale copy) the call SUCCEEDED against the twin while the named file survived.
 *     Observed: a delete tool printed "deleted <name>" for a file that was still there, having
 *     destroyed the other one. A refusal is loud and recoverable; that was neither.
 *   - What still works: on a non-UTF-8 code page, exactly the pure-ASCII paths. A non-ASCII
 *     UTF-8 sequence is two or more code units under a single-byte page and one under UTF-8,
 *     and a code-page-encoded byte is invalid UTF-8, so a non-ASCII name is refused in BOTH
 *     encodings (the suite's own fixture, "cafe" with an accent, is refused on cp1252). If the
 *     process code page IS UTF-8 (Windows 10+ can be configured so), the readings agree for
 *     everything and nothing is refused.
 *   - THE REMAINING EXCEPTIONS: file_open_1/2/3 and file_open_try_1/2/3 are NOT guarded - a
 *     write-mode open can still create or truncate the wrong file - and neither are the query
 *     families file_exists_*, file_size_* and file_modified_*, which answer about the code-page
 *     reading. Harmless on their own, since every mutation they lead to inside this module is
 *     refused, but "exists? no -> file_open_1(path, "wb")" is precisely the destructive path.
 *     They are left for the W-API port rather than half-guarded here, because unlike the family
 *     above the openers hand back a live handle whose later writes this module no longer sees.
 *   - trash_send_* (see trash.h) converts to UTF-16 and has none of this, so a path purgo can
 *     send to the recycle bin is not necessarily one file_remove_1 can delete - the trash path
 *     is the one that keeps working for such names. Linux is unaffected: paths are bytes there,
 *     and the check compiles out.
 *
 * Error Handling:
 *   - file_open_1/2/3 abort (through error_check_null) when the path cannot be
 *     opened; file_open_try_1/2/3 return nullptr instead and preserve the OS
 *     error state (errno / GetLastError) for the caller to classify.
 *   - The whole-file readers (file_read_to_*, file_read_all_*) never abort on a
 *     missing, empty, or over-FILE_READ_BYTES_MAX file - they report an empty
 *     result and log a warning instead.
 *   - file_copy_*, file_replace_*, file_remove_*, file_rename_*, file_set_modified_* never
 *     abort on an OUTCOME; they return a Result (see result.h) classifying the failure,
 *     captured via result_from_os() immediately after the failing OS call.
 *   - ONE EXCEPTION, stated plainly because the promise above reads wider than it is: a
 *     failure to allocate an internal WORKING BUFFER aborts, it does not return. The _2/_3
 *     wrappers build a char* from their Str/String argument through char_new_3, which goes
 *     to CFW's allocator, and memory_alloc ends the process on failure under
 *     ERROR_CHECK_ENABLED. RESULT_CATEGORY_MEMORY therefore covers the sites that can
 *     genuinely report it, and is unreachable at the rest. Converting these to a try-form
 *     allocator is tracked work, not a thing this header can promise today.
 *
 * Thread Safety:
 *   - Not thread-safe unless the underlying FILE* is not shared between threads.
 *
 * Memory Management:
 *   - char* results from the whole-file readers are owned by the caller and
 *     freed with memory_free; Str/String results own their buffer and are
 *     released with str_uninit/string_uninit.
 *   - File* handles are owned by the caller and closed with file_close.
 *
 * Performance Characteristics:
 *   - Reads and writes go through buffered stdio (fread/fwrite); no additional
 *     internal buffering.
 *   - file_get_size answers by handle (fstat / GetFileSizeEx) without moving
 *     the stream position.
 *   - file_copy_* and file_replace_* stream through a 64 KiB buffer allocated and freed per
 *     call, so a bulk copy of many small files pays that allocation each time.
 *
 * Dependencies:
 *   - container/string (Str, String), result.h (Result)
 *   - sys/stat.h, unistd.h, utime.h (Linux) or platform/windows/windows.h, io.h (Windows)
 *
 * See file.c for implementation details.
 */
#ifndef FILE_H
#define FILE_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#ifdef __linux__
#include <sys/stat.h>
#include <unistd.h>
#elif defined(_WIN32)
// The shim comes first by contract, ahead of alphabetical order: everything
// else here is an extras-after header (see platform/windows/windows.h).
#include <platform/windows/windows.h>
#include <io.h>
#endif
#include <container/string/string.h>
#include <result.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
/**
 * @note TEXT MODE: the by-path readers open with "r", so on Windows a CRLF pair
 *       is collapsed to LF and a 0x1A byte ends the read. Callers that need the
 *       bytes verbatim (binary payloads, SIMD scanners) must open the handle
 *       themselves with "rb" and use the handle-taking readers.
 */

/**
 * @def FILE_OPEN_WAIT_ATTEMPTS_MAX
 * @brief Number of open attempts the file_open_wait_* family makes before it
 *        gives up and returns nullptr. The wait is BOUNDED, not indefinite:
 *        the total blocking time is at most (FILE_OPEN_WAIT_ATTEMPTS_MAX - 1)
 *        multiplied by the caller's `ms` interval.
 */
#define FILE_OPEN_WAIT_ATTEMPTS_MAX 5

/**
 * @def FILE_READ_BYTES_MAX
 * @brief Largest file the whole-file readers will pull into memory (256 MiB).
 *
 *        These readers promise to report a failure as an empty buffer rather
 *        than to crash, but the allocation behind them aborts the process when
 *        it cannot be satisfied - so a file bigger than free memory would have
 *        broken that promise. A file over this size is refused exactly like an
 *        unreadable one: an empty buffer plus one logged warning. Stream a file
 *        this large instead of reading it whole.
 */
#define FILE_READ_BYTES_MAX ((USize) 256 * 1024 * 1024)

/*==============================================================================
 * MARK: - Typedefs and Enums
 *============================================================================*/
/**
 * @brief File handle type (alias for FILE*).
 */
typedef FILE File;

/**
 * @brief File seek position enumeration.
 */
typedef enum {
    FILE_POSITION_BEGIN,   /**< Seek from beginning of file. */
    FILE_POSITION_CURRENT, /**< Seek from current position. */
    FILE_POSITION_END,     /**< Seek from end of file. */
} File_Position;

/**
 * @brief The code field of a RESULT_CATEGORY_ARGUMENT refusal from the management family.
 *
 * Three refusals used to share ARGUMENT/0/0, and a caller could not tell the one with a
 * remedy - a path this build cannot address, which the trash path still handles - from the
 * two plain call mistakes. The TrashState rule applies: a code the header names is a code
 * the code always produces.
 */
typedef enum FileArgument : U32 {
    /* Based at 0x8000, clear of both OS namespaces (Linux errno and Win32 error codes both
     * live well under it in this tree's usage) - belt-and-suspenders alongside the ARGUMENT
     * category guard every OS-code predicate below carries: a memsec round found the plain
     * 1/2/3 values collided with ENOENT/ERROR_FILE_NOT_FOUND/ERROR_PATH_NOT_FOUND, and
     * file_result_is_not_found answered true for this module's OWN refusal. */
    FILE_ARGUMENT_UNADDRESSABLE_PATH = 0x8001, /**< Windows: the UTF-8 and code-page readings of the path differ (Character Encoding above). */
    FILE_ARGUMENT_NOT_REGULAR        = 0x8002, /**< The source opened but is not a regular file - a directory, on Linux. */
    FILE_ARGUMENT_EMPTY_PATH         = 0x8003  /**< A zero-length Str/String path handed to a _2/_3 wrapper. */
} FileArgument;

/*==============================================================================
 * MARK: - Core File API
 *============================================================================*/
/**
 * @brief Read a whole file by path into a buffer padded for vector reads (String).
 *
 * Same missing/empty contract as the plain readers. The difference is the
 * allocation: the capacity is rounded up to a multiple of @p padding and then
 * given one further whole block, and every byte past the content is zeroed.
 *
 * That is what makes "load one vector past the last byte" safe by construction.
 * A SIMD scanner walks ceil(bytes / width) chunks, so its final load always
 * reads past the end of the document; on an exact-fit allocation that is an
 * out-of-bounds read, harmless only until the buffer ends on a page boundary.
 * The round-up alone is not enough - a file whose size is already a multiple of
 * the padding would get a single spare byte - which is why the extra block is
 * unconditional.
 *
 * @param file_name Path to read.
 * @param padding   Vector width in bytes to align and pad to; must be non-zero.
 * @return The file's contents, reporting the true byte count as the size while
 *         the capacity carries at least @p padding zeroed bytes beyond it, or an
 *         empty String when the file is missing or empty.
 */
String file_align_read_to_string(char const *const file_name, U16 const padding);

/**
 * @brief Seek to a position in the file.
 * @param self      Pointer to file handle.
 * @param index     Offset to seek to. UNSIGNED, so only forward offsets are
 *                  expressible - FILE_POSITION_CURRENT and FILE_POSITION_END
 *                  can therefore seek forward only, and a backward seek needs
 *                  FILE_POSITION_BEGIN with an absolute index.
 * @param position  Reference position (see File_Position).
 * @note The bound check no longer disturbs the stream: file_get_size answers by
 *       handle on both platforms, where the Linux branch used to seek and rewind
 *       (silently resetting the very position this call was about to seek from).
 */
void file_at(File *const self, USize const index, File_Position const position);

/**
 * @brief Get the final path component (basename) of a path, by pointer (char*).
 * @param path Null-terminated path, '/' or '\\' separated. Must not be nullptr.
 * @return Read-only pointer into `path` just past the last separator (the whole string
 *         when there is none). Do not free; valid only while `path` lives.
 */
char* file_basename_1(char const *const path);

/**
 * @brief Close a file and set pointer to nullptr.
 * @param self  Pointer to file handle pointer.
 */
void file_close(File **const self);

/**
 * @brief Copy a file's contents to a new path (char*), refusing an existing destination.
 *
 * NOT atomic and NOT for overwriting: it refuses when the destination already exists, and
 * a caller wanting "replace this file without a torn intermediate state" wants
 * file_replace_1 instead.
 *
 * The refusal is an EXCLUSIVE CREATE, not a check followed by an open, and that matters
 * beyond the obvious race: an existence test built on stat reports a DANGLING SYMLINK as
 * absent, after which a plain create follows the link and writes whatever it points at - a
 * file the caller never named. O_EXCL cannot follow a symlink, so refusal and creation are
 * one step. Test the refusal with file_result_is_exists.
 *
 * A FAILURE REMOVES WHAT IT WROTE. The destination is always this function's own creation,
 * so a partial file left behind would be its litter, and would make the failure permanent -
 * the retry would hit the exists-refusal forever.
 *
 * @param source      Source file. Must exist and be a regular file.
 * @param destination Destination. Must not already exist.
 * @return RESULT_SUCCESS on a COMPLETE copy - a read that fails partway through is reported,
 *         never mistaken for the end of the file - otherwise a Result classifying the
 *         failure: a missing or unreadable source, a source that is not a regular file
 *         (RESULT_CATEGORY_ARGUMENT with FILE_ARGUMENT_NOT_REGULAR on Linux; on Windows fopen
 *         refuses a directory itself, so it arrives as the OS's open failure), an existing
 *         destination (file_result_is_exists), or an I/O failure partway through.
 */
Result file_copy_1(char const *const source, char const *const destination);

/**
 * @brief Copy a file's contents to a new path (Str). See file_copy_1 for the contract.
 * @param source      Source file as Str*.
 * @param destination Destination as Str*.
 * @return As file_copy_1.
 */
Result file_copy_2(Str const *const source, Str const *const destination);

/**
 * @brief Copy a file's contents to a new path (String). See file_copy_1 for the contract.
 * @param source      Source file as String*.
 * @param destination Destination as String*.
 * @return As file_copy_1.
 */
Result file_copy_3(String const *const source, String const *const destination);

/**
 * @brief Check if file is empty.
 * @param self  Pointer to file handle.
 * @return true if file is empty, false otherwise.
 */
bool file_empty(File *const self);

/**
 * @brief Whether the stream's error indicator is set (ferror).
 *
 * The module had no way to ask this, and the gap was not academic: every reader here stops
 * on a zero-length read, and stdio returns zero for END OF FILE and for a READ ERROR alike.
 * Without this, "the read finished" and "the read broke" are the same observation - which is
 * how file_copy_1 came to report a truncated copy as a success.
 *
 * Ask it after a read loop ends, before treating the result as complete.
 *
 * @param self Open file handle. Must not be nullptr.
 * @return true when an error has occurred on the stream.
 */
bool file_error(File *const self);

/**
 * @brief Check if file exists (char*).
 * @param file_name File name as char*.
 * @return true if file exists, false otherwise.
 * @note A DANGLING SYMLINK answers differently per platform: Linux follows it (stat) and says
 *       false; Windows reports the link's own attributes and says true. Unguarded on Windows
 *       for an unaddressable name (see Character Encoding).
 */
bool file_exists_1(char const *const file_name);

/**
 * @brief Check if file exists (Str).
 * @param file_name File name as Str*.
 * @return true if file exists, false otherwise.
 */
bool file_exists_2(Str const *const file_name);

/**
 * @brief Check if file exists (String).
 * @param file_name File name as String*.
 * @return true if file exists, false otherwise.
 */
bool file_exists_3(String const *const file_name);

/**
 * @brief Get a file's extension by pointer — the text after the last '.' of the final
 *        path component (char*). A leading dot (e.g. ".gitignore") is not an extension.
 * @param path Null-terminated path. Must not be nullptr.
 * @return Read-only pointer into `path` just past the basename's last '.', or an empty
 *         string when there is none. Do not free; valid only while `path` lives.
 */
char* file_extension_1(char const *const path);

/**
 * @brief Flush buffered writes to the operating system.
 * @param self  Pointer to file handle.
 * @return True when the flush succeeded. Callers that must know the bytes left
 *         the process (an endpoint answering success) should flush and check
 *         rather than rely on fclose, which reports nothing.
 */
bool file_flush(File *const self);

/**
 * @brief Get file size in bytes.
 *        Answers BY HANDLE on both platforms (fstat / GetFileSizeEx) and never
 *        moves the stream position.
 * @param self  Pointer to file handle.
 * @return File size in bytes, or 0 when the size cannot be determined.
 */
USize file_get_size(File *const self);

/**
 * @brief Get a file's last-modified time as Unix epoch seconds, by path (char*).
 * @param file_name File path as char*.
 * @param out       Receives the epoch seconds on success; untouched on failure.
 * @return true on success; false if the path is missing or unreadable.
 */
bool file_modified_1(char const *const file_name, I64 *const out);

/**
 * @brief Get a file's last-modified time as Unix epoch seconds, by path (Str).
 * @param file_name File path as Str*.
 * @param out       Receives the epoch seconds on success; untouched on failure.
 * @return true on success; false if the path is missing or unreadable.
 */
bool file_modified_2(Str const *const file_name, I64 *const out);

/**
 * @brief Get a file's last-modified time as Unix epoch seconds, by path (String).
 * @param file_name File path as String*.
 * @param out       Receives the epoch seconds on success; untouched on failure.
 * @return true on success; false if the path is missing or unreadable.
 */
bool file_modified_3(String const *const file_name, I64 *const out);

/**
 * @brief Open a file (char*).
 * @param file_name File name as char*.
 * @param file_mode File mode string (e.g., "r", "w").
 * @return Pointer to the opened file. Does NOT return on failure: the result is
 *         run through error_check_null, which under ERROR_CHECK_ENABLED ends the
 *         process. Use file_open_try_1 when a missing or unreadable path is an
 *         outcome to handle rather than a bug - testing this function's result
 *         against null is dead code.
 */
File* file_open_1(char const *const file_name, char const *const file_mode);

/**
 * @brief Open a file (Str).
 * @param file_name File name as Str*.
 * @param file_mode File mode string.
 * @return Pointer to the opened file. Does NOT return on failure: the result is
 *         run through error_check_null, which under ERROR_CHECK_ENABLED ends the
 *         process. Use file_open_try_2 when a missing or unreadable path is an
 *         outcome to handle rather than a bug - testing this function's result
 *         against null is dead code.
 */
File* file_open_2(Str const *const file_name, char const *const file_mode);

/**
 * @brief Open a file (String).
 * @param file_name File name as String*.
 * @param file_mode File mode string.
 * @return Pointer to the opened file. Does NOT return on failure: the result is
 *         run through error_check_null, which under ERROR_CHECK_ENABLED ends the
 *         process. Use file_open_try_3 when a missing or unreadable path is an
 *         outcome to handle rather than a bug - testing this function's result
 *         against null is dead code.
 */
File* file_open_3(String const *const file_name, char const *const file_mode);

/**
 * @brief Open a file, reporting failure instead of aborting (char*).
 *
 * The counterpart to file_open_1 for callers that can handle a path not being
 * there. Checking the returned pointer is the whole point of this function, and
 * unlike a file_exists_1 pre-check it carries no race: the answer comes from the
 * open itself, so nothing can delete the file in between.
 *
 * CONTRACT: on failure the thread-local OS error state (errno / GetLastError)
 * set by the failed open is preserved for the caller to classify (e.g. via
 * result_from_os) - the failure path must never run code that could overwrite
 * it. Callers such as env_load_2 rely on this.
 *
 * @param file_name File name as char*.
 * @param file_mode File mode string (e.g., "r", "w").
 * @return Pointer to the opened file, or nullptr when it could not be opened.
 */
File* file_open_try_1(char const *const file_name, char const *const file_mode);

/**
 * @brief Open a file, reporting failure instead of aborting (Str).
 *
 * Carries file_open_try_1's contract: the OS error state from a failed open is
 * preserved for the caller to classify.
 *
 * @param file_name File name as Str*.
 * @param file_mode File mode string.
 * @return Pointer to the opened file, or nullptr when it could not be opened.
 */
File* file_open_try_2(Str const *const file_name, char const *const file_mode);

/**
 * @brief Open a file, reporting failure instead of aborting (String).
 *
 * Carries file_open_try_1's contract: the OS error state from a failed open is
 * preserved for the caller to classify.
 *
 * @param file_name File name as String*.
 * @param file_mode File mode string.
 * @return Pointer to the opened file, or nullptr when it could not be opened.
 */
File* file_open_try_3(String const *const file_name, char const *const file_mode);

/**
 * @brief Open a file, retrying while it is unavailable (char*).
 *        The retry is BOUNDED at FILE_OPEN_WAIT_ATTEMPTS_MAX attempts, not
 *        indefinite - after the last one it gives up and returns nullptr.
 * @param file_name File name as char*.
 * @param file_mode File mode string.
 * @param ms        Milliseconds to wait between attempts (at most
 *                  FILE_OPEN_WAIT_ATTEMPTS_MAX - 1 waits in total).
 * @return Pointer to opened file, or nullptr when every attempt failed.
 */
File* file_open_wait_1(char const *const file_name, char const *const file_mode, U32 const ms);

/**
 * @brief Open a file, retrying while it is unavailable (Str).
 *        The retry is BOUNDED at FILE_OPEN_WAIT_ATTEMPTS_MAX attempts, not
 *        indefinite - after the last one it gives up and returns nullptr.
 * @param file_name File name as Str*.
 * @param file_mode File mode string.
 * @param ms        Milliseconds to wait between attempts (at most
 *                  FILE_OPEN_WAIT_ATTEMPTS_MAX - 1 waits in total).
 * @return Pointer to opened file, or nullptr when every attempt failed.
 */
File* file_open_wait_2(Str const *const file_name, char const *const file_mode, U32 const ms);

/**
 * @brief Open a file, retrying while it is unavailable (String).
 *        The retry is BOUNDED at FILE_OPEN_WAIT_ATTEMPTS_MAX attempts, not
 *        indefinite - after the last one it gives up and returns nullptr.
 * @param file_name File name as String*.
 * @param file_mode File mode string.
 * @param ms        Milliseconds to wait between attempts (at most
 *                  FILE_OPEN_WAIT_ATTEMPTS_MAX - 1 waits in total).
 * @return Pointer to opened file, or nullptr when every attempt failed.
 */
File* file_open_wait_3(String const *const file_name, char const *const file_mode, U32 const ms);

/**
 * @brief Read from file into buffer.
 * @param self       Pointer to file handle.
 * @param data       Buffer to read into.
 * @param data_size  Size of each element.
 * @param data_count Number of elements to read.
 * @return Number of elements read.
 */
USize file_read_1(File *const self, void *const data, USize const data_size, USize const data_count);

/**
 * @brief Read from file into Str buffer.
 *        BOUNDED by the Str's own size: a data_count larger than the buffer is
 *        clamped rather than written past the allocation.
 * @param self          Pointer to file handle.
 * @param data          Str buffer to read into. Its size on entry is the space
 *                      available; on return it is the count actually read.
 * @param data_count    Number of characters to read (clamped to the Str's size).
 */
void file_read_2(File *const self, Str *const data, USize const data_count);

/**
 * @brief Read from file into String buffer (its capacity LESS the terminator).
 *        Reads capacity - 1 bytes, not capacity: the reserved byte is what keeps
 *        string_get_data usable as a C string. Size the String with
 *        file_get_size(file) + CHAR_END_CHARACTER to read a whole file.
 * @param self   Pointer to file handle.
 * @param data   String buffer to read into. Its size is set to the count read.
 */
void file_read_3(File *const self, String *const data);

/**
 * @brief Read from file into String buffer (up to data_count).
 *        BOUNDED by the String's capacity less one byte reserved for the
 *        terminator, so string_get_data stays usable as a C string.
 * @param self       Pointer to file handle.
 * @param data       String buffer to read into. Its size is set to the count
 *                   actually read.
 * @param data_count Number of characters to read (clamped to capacity - 1).
 */
void file_read_4(File *const self, String *const data, USize const data_count);

/**
 * @brief Read the whole file from an open handle (char*).
 *
 * The receiver-based sibling of file_read_to_char: same missing/empty contract
 * and the same directory guard, but no second open by path - the handle in hand
 * is the one read, so nothing can swap the file between classifying it and
 * reading it (env_load_2 is the canonical caller). Call it on a freshly opened
 * handle: the read starts at the CURRENT position on BOTH platforms, so a
 * non-fresh handle silently yields a partial document (the reported size is the
 * bytes actually read). The position is not preserved. Earlier revisions rewound
 * on Linux only; file_get_size now answers by handle and moves nothing, so the
 * two platforms finally agree.
 *
 * @param self Open file handle.
 * @return An owned, NUL-terminated buffer the caller frees with memory_free -
 *         never null, and empty when the handle is not a regular file or the
 *         file is empty.
 */
char* file_read_all_1(File *const self);

/**
 * @brief Read the whole file from an open handle (Str).
 *
 * As file_read_all_1; see it for the contract.
 *
 * @param self Open file handle.
 * @return The file's contents as a Str owning its buffer, or an empty Str when
 *         the handle is not a regular file or the file is empty.
 */
Str file_read_all_2(File *const self);

/**
 * @brief Read the whole file from an open handle (String).
 *
 * As file_read_all_1; see it for the contract.
 *
 * @param self Open file handle.
 * @return The file's contents, NUL-terminated one byte past the reported size,
 *         or an empty String when the handle is not a regular file or the file
 *         is empty.
 */
String file_read_all_3(File *const self);

/**
 * @brief Read a whole file by path (char*).
 *
 * A MISSING file and an EMPTY file both yield an empty result rather than ending
 * the process. That is the contract callers already assumed: the universal idiom
 * is to read and then test the returned size, and before these guards existed
 * that test was unreachable - a missing path aborted inside file_open_1's
 * error_check_null, and a zero-byte file aborted inside the buffer it was being
 * read into. A sweep found twelve callers written against the assumed contract
 * rather than the real one.
 *
 * file_open_1 keeps its own aborting behaviour: it is the low-level opener and
 * its write-mode callers rely on it. These readers use file_open_try_1 instead.
 *
 * @param file_name Path to read.
 * @return An owned, NUL-terminated buffer the caller frees with memory_free -
 *         never null, and empty when the file is missing or empty.
 */
char* file_read_to_char(char const *const file_name);

/**
 * @brief Read a whole file by path (Str).
 *
 * A MISSING file and an EMPTY file both yield an empty result rather than ending
 * the process. That is the contract callers already assumed: the universal idiom
 * is to read and then test the returned size, and before these guards existed
 * that test was unreachable - a missing path aborted inside file_open_1's
 * error_check_null, and a zero-byte file aborted inside the buffer it was being
 * read into. A sweep found twelve callers written against the assumed contract
 * rather than the real one.
 *
 * @param file_name Path to read.
 * @return The file's contents as a Str owning its buffer, or an empty Str when
 *         the file is missing or empty.
 */
Str file_read_to_str(char const *const file_name);

/**
 * @brief Read a whole file by path (String).
 *
 * A MISSING file and an EMPTY file both yield an empty result rather than ending
 * the process. That is the contract callers already assumed: the universal idiom
 * is to read and then test the returned size, and before these guards existed
 * that test was unreachable - a missing path aborted inside file_open_1's
 * error_check_null, and a zero-byte file aborted inside the buffer it was being
 * read into. A sweep found twelve callers written against the assumed contract
 * rather than the real one.
 *
 * @param file_name Path to read.
 * @return The file's contents, NUL-terminated one byte past the reported size,
 *         or an empty String when the file is missing or empty.
 */
String file_read_to_string(char const *const file_name);

/**
 * @brief Read a whole file by path, retrying briefly while unavailable (char*).
 *
 * A path that NEVER APPEARS and a file that is present but EMPTY both yield an
 * empty result rather than ending the process.
 *
 * @param file_name Path to read.
 * @param ms        Milliseconds to sleep between attempts.
 * @return An owned, NUL-terminated buffer the caller frees with memory_free -
 *         never null, and empty when the file never became readable.
 */
char* file_read_to_char_wait(char const *const file_name, U32 const ms);

/**
 * @brief Read a whole file by path, retrying briefly while unavailable (Str).
 *
 * A path that NEVER APPEARS and a file that is present but EMPTY both yield an
 * empty result rather than ending the process.
 *
 * @param file_name Path to read.
 * @param ms        Milliseconds to sleep between attempts.
 * @return The file's contents as a Str owning its buffer, or an empty Str.
 */
Str file_read_to_str_wait(char const *const file_name, U32 const ms);

/**
 * @brief Read a whole file by path, retrying briefly while unavailable (String).
 *
 * A path that NEVER APPEARS and a file that is present but EMPTY both yield an
 * empty result rather than ending the process.
 *
 * @param file_name Path to read.
 * @param ms        Milliseconds to sleep between attempts.
 * @return The file's contents, NUL-terminated one byte past the reported size,
 *         or an empty String when the file never became readable.
 */
String file_read_to_string_wait(char const *const file_name, U32 const ms);

/**
 * @brief Report whether an open handle is a regular file.
 *
 * Public face of the internal directory guard: glibc's fopen SUCCEEDS on a
 * directory, and the whole-file readers then return empty - this query is how a
 * caller tells "empty file" from "not a file at all". On Windows an open handle
 * is always a regular file (fopen refuses directories), so it returns true.
 *
 * @param self Open file handle.
 * @return true when the handle refers to a regular file.
 */
bool file_regular(File *const self);

/**
 * @brief Delete a file by path (char*).
 * @param path Path to the file. Must be a regular file, not a directory - use
 *             dir_remove_1/dir_remove_all_1 for those.
 * @return RESULT_SUCCESS on success; otherwise a Result classifying the failure
 *         (missing path, a directory, or permission), captured immediately via
 *         result_from_os() after the failing call.
 */
Result file_remove_1(char const *const path);

/**
 * @brief Delete a file by path (Str). See file_remove_1 for the contract.
 * @param path Path as Str*.
 * @return As file_remove_1.
 */
Result file_remove_2(Str const *const path);

/**
 * @brief Delete a file by path (String). See file_remove_1 for the contract.
 * @param path Path as String*.
 * @return As file_remove_1.
 */
Result file_remove_3(String const *const path);

/**
 * @brief Rename or move a file (char*), REPLACING the destination if it already exists.
 *
 * Always-overwrite is a deliberate choice, matching POSIX rename()'s native behaviour on
 * both platforms (Windows goes through MoveFileExA with MOVEFILE_REPLACE_EXISTING to match
 * it, since a bare Windows rename refuses an existing destination). A caller that must NOT
 * overwrite has to check first - this primitive does not re-implement that policy, the same
 * way file_copy_1 refuses rather than silently deciding for the caller.
 *
 * On a CROSS-DEVICE rename this fails with result_code(result) equal to EXDEV (Linux) or
 * ERROR_NOT_SAME_DEVICE (Windows) - never silently falling back to a copy. A caller that
 * wants move-across-filesystems semantics detects exactly this code and does its own
 * copy-then-remove.
 *
 * The source may be a DIRECTORY: rename() and MoveFileExA both move one and this function
 * does not refuse it - only the copy family insists on a regular file.
 *
 * @param source      Existing file to rename/move.
 * @param destination Destination path (may be in the same directory or elsewhere on the same
 *                 filesystem; a different filesystem fails with EXDEV/ERROR_NOT_SAME_DEVICE
 *                 as above).
 * @return RESULT_SUCCESS on success; otherwise a Result classifying the failure.
 */
Result file_rename_1(char const *const source, char const *const destination);

/**
 * @brief Rename or move a file (Str). See file_rename_1 for the contract.
 * @param source      Existing file as Str*.
 * @param destination Destination as Str*.
 * @return As file_rename_1.
 */
Result file_rename_2(Str const *const source, Str const *const destination);

/**
 * @brief Rename or move a file (String). See file_rename_1 for the contract.
 * @param source      Existing file as String*.
 * @param destination Destination as String*.
 * @return As file_rename_1.
 */
Result file_rename_3(String const *const source, String const *const destination);

/**
 * @brief Atomically replace the destination's content with the source's, on the SAME filesystem.
 *
 * Writes a temporary file beside the destination (same directory, so the final rename cannot cross
 * a filesystem boundary), flushes it, then renames it over the destination. A reader can never
 * observe a torn/partial destination: it is either the old content or the fully-written new
 * content, never a truncated write partway through. the source itself is left untouched (this is
 * a copy-and-replace, not a move) - pair with file_remove_1 for move-and-replace, or use
 * file_rename_1 directly when the destination not existing yet is fine.
 *
 * A failure while writing the temporary file leaves the destination untouched and removes
 * the temporary; a failure is only possible during the final rename itself in the narrow
 * window after the temporary is fully written, which the same-filesystem-by-construction
 * design keeps as small as the platform's own rename primitive.
 *
 * "ATOMIC" IS SCOPED TO CONCURRENT READERS, NOT TO A CRASH. Nothing here is fsynced, so a
 * power loss can still leave either version - or, on some filesystems, neither. The
 * guarantee is that no reader ever observes a half-written file, which is what the
 * config-writer pattern this implements actually needs; durability is a different promise
 * and would need its own variant.
 *
 * PERMISSIONS ARE CARRIED OVER on Linux: the temp becomes the destination, so without that
 * step a successful replace would silently reset a 0600 file to the umask default. On
 * Windows an explicit ACL on the destination is NOT carried and is replaced by fresh
 * inheritance - a documented gap, since copying a security descriptor is out of scope here.
 *
 * A destination this call CREATES (one that did not exist) therefore ends up 0600 on Linux -
 * narrower than the 0666-and-umask a plain file_copy_1 would produce. That is deliberate:
 * there is no prior mode to carry, and the temp is written private from its first byte
 * precisely so a secret is never briefly world-readable under a predictable name. Widen it
 * yourself if a new file is meant to be shared.
 *
 * A SYMLINK DESTINATION IS REPLACED BY A REGULAR FILE, not written through. The rename
 * substitutes the temp for whatever the name referred to, so a config that was a symlink
 * into a dotfile store becomes an ordinary file and stops tracking its target. This matches
 * POSIX rename and every other atomic-write helper, and resolving the link first would
 * destroy the same-directory property the atomicity argument rests on - but it is stated
 * here because the copy family documents its own symlink behaviour, and silence next to
 * that reads as "handled".
 *
 * A stale temporary from a killed run does not block the path: the name carries an attempt
 * counter and the call steps past one it did not create.
 *
 * @param source      Source file providing the new content. Must exist and be a regular file.
 * @param destination Destination to replace (created if it does not exist).
 * @return RESULT_SUCCESS on success; otherwise a Result classifying the failure - the destination
 *         is guaranteed untouched (still its old content, or absent if it never existed) on
 *         any failure before the rename step. If every candidate temp name beside the
 *         destination is taken (64 of them, each stepping past a stale temp from a killed run),
 *         the answer is the STATE/EEXIST spelling file_result_is_exists recognises.
 */
Result file_replace_1(char const *const source, char const *const destination);

/**
 * @brief Atomically replace a file's content (Str). See file_replace_1 for the contract.
 * @param source      Source as Str*.
 * @param destination Destination as Str*.
 * @return As file_replace_1.
 */
Result file_replace_2(Str const *const source, Str const *const destination);

/**
 * @brief Atomically replace a file's content (String). See file_replace_1 for the contract.
 * @param source      Source as String*.
 * @param destination Destination as String*.
 * @return As file_replace_1.
 */
Result file_replace_3(String const *const source, String const *const destination);

/**
 * @brief Whether a Result from this family means "the two paths are on different filesystems".
 *
 * These three predicates exist so a caller never writes `#ifdef _WIN32` to classify a
 * failure. The codes genuinely differ per platform (EXDEV is 18, ERROR_NOT_SAME_DEVICE is
 * 17) and the CATEGORY cannot separate them - both classify as SYSTEM - so without this the
 * knowledge leaks into every consumer, which is the opposite of what this framework is for.
 * porto had already grown exactly that hand-rolled block before these existed.
 *
 * @param result A Result returned by file_copy_*, file_replace_*, file_remove_* or
 *               file_rename_*.
 * @return true when the failure was a cross-device attempt - the signal to fall back to a
 *         copy-then-remove rather than to retry the rename.
 */
bool file_result_is_cross_device(Result const result);

/**
 * @brief Whether a Result from this family means "the destination already exists".
 *
 * The answer file_copy_* gives when it refuses rather than overwrite; a caller that wants
 * the overwrite asks file_replace_* instead.
 *
 * @param result A Result returned by this family.
 * @return true for the platform's own already-exists code. Never true for this family's own
 *         RESULT_CATEGORY_ARGUMENT refusals (see file_result_is_unaddressable for those).
 */
bool file_result_is_exists(Result const result);

/**
 * @brief Whether a Result from this family means "the path is not there".
 * @param result A Result returned by this family.
 * @return true for the platform's own not-found code. Never true for this family's own
 *         RESULT_CATEGORY_ARGUMENT refusals (see file_result_is_unaddressable for those) -
 *         FILE_ARGUMENT_NOT_REGULAR / _EMPTY_PATH are NOT the OS's own not-found answer,
 *         even though nothing stopped an early caller from reading them that way.
 */
bool file_result_is_not_found(Result const result);

/**
 * @brief Whether a Result from this family means "this build cannot address that path".
 *
 * The Windows fail-closed refusal from the Character Encoding block, and the one ARGUMENT
 * refusal with a remedy the caller can offer: the trash path (trash_send_*) converts to
 * UTF-16 and still handles such a name.
 *
 * @param result A Result returned by this family.
 * @return true for RESULT_CATEGORY_ARGUMENT with FILE_ARGUMENT_UNADDRESSABLE_PATH; never on
 *         Linux, where every path is addressable.
 */
bool file_result_is_unaddressable(Result const result);

/**
 * @brief Set a file's last-modified time by path, from Unix epoch seconds (char*).
 * Pairs with file_modified_1's getter.
 * Accepts a DIRECTORY as well as a file on both platforms - Windows opens it with
 * FILE_FLAG_BACKUP_SEMANTICS, which is what that flag is for.
 *
 * @param path    Path to an existing file or directory.
 * @param seconds New modification time, Unix epoch seconds.
 * @return RESULT_SUCCESS on success; otherwise a Result classifying the failure. Returns a
 *         Result like the rest of the management family, not a bool like the file_modified_*
 *         getter it pairs with: a caller that cannot
 *         distinguish "the path is gone" from "the filesystem has no sub-second attribute
 *         support" cannot say anything useful about a failed --preserve, and porto's only
 *         caller was discarding the answer precisely because the answer said nothing.
 */
Result file_set_modified_1(char const *const path, I64 const seconds);

/**
 * @brief Set a file's last-modified time by path (Str). See file_set_modified_1.
 * @param path    Path as Str*.
 * @param seconds New modification time, Unix epoch seconds.
 * @return As file_set_modified_1. An empty path is RESULT_CATEGORY_ARGUMENT.
 */
Result file_set_modified_2(Str const *const path, I64 const seconds);

/**
 * @brief Set a file's last-modified time by path (String). See file_set_modified_1.
 * @param path    Path as String*.
 * @param seconds New modification time, Unix epoch seconds.
 * @return As file_set_modified_1. An empty path is RESULT_CATEGORY_ARGUMENT.
 */
Result file_set_modified_3(String const *const path, I64 const seconds);

/**
 * @brief Get a file's size in bytes by path, without opening it (char*).
 * @param file_name File path as char*.
 * @param out       Receives the size on success; untouched on failure.
 * @return true on success; false if the path is missing or unreadable.
 */
bool file_size_1(char const *const file_name, USize *const out);

/**
 * @brief Get a file's size in bytes by path, without opening it (Str).
 * @param file_name File path as Str*.
 * @param out       Receives the size on success; untouched on failure.
 * @return true on success; false if the path is missing or unreadable.
 */
bool file_size_2(Str const *const file_name, USize *const out);

/**
 * @brief Get a file's size in bytes by path, without opening it (String).
 * @param file_name File path as String*.
 * @param out       Receives the size on success; untouched on failure.
 * @return true on success; false if the path is missing or unreadable.
 */
bool file_size_3(String const *const file_name, USize *const out);

/**
 * @brief Write from buffer to file.
 * @param self       Pointer to file handle.
 * @param data       Buffer to write from. Not modified.
 * @param data_size  Size of each element.
 * @param data_count Number of elements to write.
 * @return Number of ELEMENTS written. A value below data_count is a short write
 *         (disk full, quota, closed pipe) - callers that must not report a
 *         truncated file as saved have to compare it. Writing 0 elements is a
 *         legal no-op that returns 0, not an error.
 */
USize file_write_1(File *const self, void const *const data, USize const data_size, USize const data_count);

/**
 * @brief Write from Str buffer to file (entire size).
 * @param self   Pointer to file handle.
 * @param data   Str buffer to write from.
 * @return Number of characters written; below the Str's size on a short write.
 */
USize file_write_2(File *const self, Str const *const data);

/**
 * @brief Write from Str buffer to file (up to data_count).
 * @param self       Pointer to file handle.
 * @param data       Str buffer to write from.
 * @param data_count Number of characters to write.
 * @return Number of characters written; below data_count on a short write.
 * @note NOT bounded: data_count is honoured literally, so a count above the Str's size reads
 *       past its buffer - the read twins clamp to the destination, the write twins do not.
 */
USize file_write_3(File *const self, Str const *const data, USize const data_count);

/**
 * @brief Write from String buffer to file (entire size).
 * @param self   Pointer to file handle.
 * @param data   String buffer to write from.
 * @return Number of characters written; below the String's size on a short write.
 */
USize file_write_4(File *const self, String const *const data);

/**
 * @brief Write from String buffer to file (up to data_count).
 * @param self       Pointer to file handle.
 * @param data       String buffer to write from.
 * @param data_count Number of characters to write.
 * @return Number of characters written; below data_count on a short write.
 * @note NOT bounded: data_count is honoured literally, so a count above the String's size
 *       reads past its buffer - the read twins clamp to the destination, the write twins do not.
 */
USize file_write_5(File *const self, String const *const data, USize const data_count);

#endif // FILE_H