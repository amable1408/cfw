/*
 * dir.h - Directory operations for the C Libraries Framework
 *
 * Features:
 *   - Cross-platform directory operations (Linux, Windows)
 *   - Idempotent single-level and recursive directory creation
 *   - Existence and emptiness queries that require the path to be a directory
 *   - Directory listing with per-entry metadata (type/size/mtime) gathered in one scan,
 *     "." and ".." excluded
 *   - Guarded recursive copy that merges into the destination
 *   - Current working directory query and change
 *   - Empty-directory removal, guarded recursive removal, and rename
 *
 * Usage Examples:
 *   @code
 *   dir_create_all_1("uploads/42/photos");
 *
 *   if (dir_exists_1("uploads/42")) {
 *       DirEntry *entries = nullptr;
 *       USize     count   = 0;
 *
 *       if (dir_list_entries_1("uploads/42", &entries, &count)) {
 *           for (USize i = 0; i < count; i += 1) {
 *               log_message_1(LOG_LEVEL_INFO, "%s\n", entries[i].name);
 *           }
 *
 *           dir_list_entries_uninit(entries, count);
 *       }
 *   }
 *   @endcode
 *
 * Character Encoding:
 *   - Paths are char* everywhere and are treated as UTF-8. On Windows the module converts at
 *     the OS boundary and calls only the wide entry points, so a path or entry name outside
 *     the active code page round-trips faithfully and can be listed, copied, renamed and
 *     REMOVED - the ANSI APIs could not even see such a name, which made dir_remove_all
 *     report failure and strand the file. Names come back as true UTF-8.
 *   - A name that cannot be decoded to UTF-8 fails the operation rather than being skipped:
 *     silently omitting an entry from a recursive copy or removal would report success on an
 *     incomplete result.
 *
 * Error Handling:
 *   - All API functions null-check their arguments first; see the error macros.
 *   - Functions returning bool indicate success (true) or failure (false).
 *   - GUARANTEED on failure: the OS reason is left readable. After a false return from a
 *     SINGLE-OPERATION function (create, exists, empty, remove, rename, set_current, and the
 *     listing entry points) errno (Linux) / GetLastError (Windows) still holds the failing
 *     value, so a caller can classify it - result_from_os() maps it directly. This is the same
 *     compromise file_open_try documents: bool stays the return type rather than migrating the
 *     surface to Result, but the diagnostic is not thrown away. The module preserves it across
 *     its own internal frees, which are otherwise permitted to overwrite it.
 *   - NOT guaranteed when a _2/_3 overload refuses an EMPTY path: no system call runs, so the
 *     surviving value belongs to whatever the thread did last - possibly a success. That branch
 *     is a caller-argument error rather than an OS failure; test the path yourself if the
 *     distinction matters.
 *   - NOT guaranteed for the RECURSIVE operations (dir_copy_all, dir_remove_all): they aggregate
 *     many failures across a walk, so the surviving last error names whichever entry failed most
 *     recently, not the operation. Treat their false as "the tree is not in the requested state"
 *     and inspect it per entry if you need detail.
 *   - The Str (_2) and String (_3) variants return false for empty paths
 *     instead of converting them.
 *   - dir_remove_all refuses dangerous paths (empty, root, drive root, "." or
 *     ".." segments) and never follows symbolic links or reparse points.
 *   - Both recursive walks REFUSE below 512 levels of nesting rather than descending. Tree depth
 *     is filesystem-driven and an unprivileged process can create it with nested mkdir, so an
 *     unbounded walk means attacker-chosen recursion depth. Measured worst case at that ceiling
 *     is ~370 KB of stack (736 B/frame for copy, 720 B/frame for remove, at -O3): fine on a main
 *     thread, but size a worker thread's stack accordingly before pointing either walk at an
 *     untrusted tree. A refusal returns false like any other failure.
 *   - dir_copy_all refuses a destination that textually equals or lies inside the source, and
 *     refuses "." or ".." segments in EITHER root. The containment test compares paths as
 *     given rather than canonicalizing, so a dot segment would otherwise let one directory be
 *     spelled two ways and defeat the test.
 *
 * Thread Safety:
 *   - Stateless; safe to call from multiple threads on distinct paths.
 *   - Concurrent operations on the same path race at the filesystem level; the
 *     caller must synchronize them.
 *
 * Memory Management:
 *   - dir_get_current returns a framework-allocated buffer; free it with
 *     char_delete.
 *   - dir_list_entries returns a heap-allocated DirEntry array (each name owned);
 *     release it with dir_list_entries_uninit.
 *
 * String Type Support:
 *   - Every path-taking function follows the framework overload pattern:
 *     _1 takes char*, _2 takes Str, _3 takes String. The _2/_3 variants copy
 *     the path to a NUL-terminated buffer and forward to _1.
 *
 * Performance Characteristics:
 *   - dir_create/dir_exists/dir_remove/dir_rename/dir_set_current are single
 *     system calls; dir_create_all issues one mkdir per path segment.
 *   - dir_empty/dir_copy_all/dir_remove_all stream directory entries;
 *     the recursive walks descend per subdirectory and allocate one joined
 *     path per entry.
 *   - dir_list_entries is the ONLY listing primitive: a names-only dir_list family existed
 *     alongside it and was retired, having acquired no caller. Use it for walking a tree -
 *     every field comes from the enumeration itself, so Windows needs no per-entry stat, and
 *     on Linux one fstatat per
 *     entry (plus one per link) is resolved against the open directory descriptor, so no
 *     per-entry path is built and the kernel never re-walks the parent path.
 *     A batched GetFileInformationByHandleEx form was measured against the FindFirstFileW
 *     loop on local NTFS and came out a wash (1.0x at 16k and 75k entries) because Win32
 *     already buffers directory data internally, so it was not kept; if this is ever pointed
 *     at network filesystems, where a round trip genuinely costs, it is worth re-measuring.
 *   - dir_copy_all copies file bodies in 64 KiB chunks (Linux) or via
 *     CopyFileW (Windows).
 *   - On Windows, entry removal inside dir_remove_all retries briefly
 *     (up to 10 x 50 ms) to absorb asynchronous deletes and transient
 *     sync-client locks; the worst case adds 500 ms per stubborn entry.
 *
 * Dependencies:
 *   - <container/string/string.h> for the framework types and the Str/String overloads.
 *     Declared directly: it used to arrive transitively via the retired dir_list family's
 *     al_string.h, so this header depended on a type source it never named.
 *   - Linux: <dirent.h>, <errno.h>, <fcntl.h> (AT_SYMLINK_NOFOLLOW), <sys/stat.h>, <unistd.h>
 *   - Linux REQUIRES -D_GNU_SOURCE. The single-scan listing path uses dirfd + fstatat with
 *     AT_SYMLINK_NOFOLLOW, none of which are declared without it - the build fails outright,
 *     it does not degrade. Every build/linux makefile already defines it, so this is a
 *     documented requirement rather than a live break; it is stated because the dependency
 *     was previously satisfied only by accident of the shared flag set.
 *   - Windows: <platform/windows/windows.h> (wide Win32 entry points throughout)
 *
 * See dir.c for implementation details.
 */

#ifndef DIR_H
#define DIR_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#ifdef __linux__
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <platform/windows/windows.h>
#endif

/* Direct, not transitive. The framework types (USize, I64) and the Str/String overload types
 * used to arrive here only because the retired dir_list family included al_string.h; removing it
 * revealed that this header had never declared its own type dependency. string.h chains str.h,
 * so listing both would violate the no-redundant-chained-include rule. */
#include <container/string/string.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/
/**
 * @brief One directory entry with the metadata gathered during a single scan.
 *
 * Produced by dir_list_entries; released as a block with dir_list_entries_uninit.
 * is_dir reflects the link target (the type test follows a symbolic link), while
 * is_link reports whether the entry itself is a symbolic link or surrogate
 * reparse point — so a link to a directory has both flags set.
 */
typedef struct DirEntry {
    char  *name;    /**< Entry name (no path), heap-owned. */
    bool   is_dir;  /**< Whether the entry (target, for a link) is a directory. */
    bool   is_link; /**< Whether the entry is a symbolic link / surrogate reparse point. */
    USize  size;    /**< File size in bytes (0 for directories or on stat failure). */
    I64    mtime;   /**< Last-modified epoch seconds (0 on stat failure). */
} DirEntry;

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
/**
 * @brief Recursively copy a directory tree into a destination (char*).
 * @param path      Source directory; must exist and must not itself be a
 *                  symbolic link or junction. Symbolic links inside the tree
 *                  are recreated as links on Linux; on Windows, surrogate
 *                  reparse points are not recreated and make the call return
 *                  false. Other special entries are skipped and reported as
 *                  failure.
 * @param path_new  Destination directory; created (with parents) when missing,
 *                  merged into when present — existing files are overwritten.
 *                  Refused when it textually equals or lies inside the source.
 * @return true when the whole tree was copied, false when refused or when any
 *         entry could not be copied.
 */
bool dir_copy_all_1(char const *const path, char const *const path_new);

/**
 * @brief Recursively copy a directory tree into a destination (Str).
 * @param path      Source directory as Str*.
 * @param path_new  Destination directory as Str*.
 * @return true when the whole tree was copied, false otherwise.
 */
bool dir_copy_all_2(Str const *const path, Str const *const path_new);

/**
 * @brief Recursively copy a directory tree into a destination (String).
 * @param path      Source directory as String*.
 * @param path_new  Destination directory as String*.
 * @return true when the whole tree was copied, false otherwise.
 */
bool dir_copy_all_3(String const *const path, String const *const path_new);

/**
 * @brief Create a single-level directory, treating "already exists" as success (char*).
 * @param path  Directory path to create (parents must already exist).
 * @return true when the directory exists after the call, false on failure.
 */
bool dir_create_1(char const *const path);

/**
 * @brief Create a single-level directory (Str).
 * @param path  Directory path as Str*.
 * @return true when the directory exists after the call, false on failure.
 */
bool dir_create_2(Str const *const path);

/**
 * @brief Create a single-level directory (String).
 * @param path  Directory path as String*.
 * @return true when the directory exists after the call, false on failure.
 */
bool dir_create_3(String const *const path);

/**
 * @brief Create a directory and every missing parent, mkdir -p semantics (char*).
 * @param path  Directory path to create; each segment is created in turn and
 *              "already exists" counts as success per segment.
 * @return true when the full path exists after the call, false on failure.
 */
bool dir_create_all_1(char const *const path);

/**
 * @brief Create a directory and every missing parent (Str).
 * @param path  Directory path as Str*.
 * @return true when the full path exists after the call, false on failure.
 */
bool dir_create_all_2(Str const *const path);

/**
 * @brief Create a directory and every missing parent (String).
 * @param path  Directory path as String*.
 * @return true when the full path exists after the call, false on failure.
 */
bool dir_create_all_3(String const *const path);

/**
 * @brief Check if a directory exists and contains no entries (char*).
 * @param path  Directory path to check.
 * @return true if the path is an existing directory with no entries besides
 *         "." and "..", false otherwise (including when it does not exist).
 */
bool dir_empty_1(char const *const path);

/**
 * @brief Check if a directory exists and contains no entries (Str).
 * @param path  Directory path as Str*.
 * @return true if the path is an existing empty directory, false otherwise.
 */
bool dir_empty_2(Str const *const path);

/**
 * @brief Check if a directory exists and contains no entries (String).
 * @param path  Directory path as String*.
 * @return true if the path is an existing empty directory, false otherwise.
 */
bool dir_empty_3(String const *const path);

/**
 * @brief Check if a path exists and is a directory (char*).
 * @param path  Path to check.
 * @return true if the path exists and is a directory, false otherwise.
 */
bool dir_exists_1(char const *const path);

/**
 * @brief Check if a path exists and is a directory (Str).
 * @param path  Path as Str*.
 * @return true if the path exists and is a directory, false otherwise.
 */
bool dir_exists_2(Str const *const path);

/**
 * @brief Check if a path exists and is a directory (String).
 * @param path  Path as String*.
 * @return true if the path exists and is a directory, false otherwise.
 */
bool dir_exists_3(String const *const path);

/**
 * @brief Get the current working directory.
 * @return Framework-allocated copy of the current working directory path, or
 *         nullptr on failure. Free it with char_delete.
 */
char* dir_get_current(void);

/**
 * @brief List a directory with per-entry metadata gathered in one scan (char*).
 *
 * Faster than dir_list + per-entry stat, and the intended primitive for tree walks.
 * On Windows every field is supplied by the enumeration itself and names are decoded from
 * UTF-16 to UTF-8, so an entry outside the active code page survives; on Linux one fstatat
 * per entry supplies the type/size/mtime against the open directory descriptor, plus a
 * follow-up only for the occasional symbolic link. "." and ".." are excluded.
 *
 * @param path         Directory path to list; interpreted as UTF-8 on Windows.
 * @param out_entries  Receives a heap-allocated array of DirEntry on success
 *                     (nullptr when the directory is empty). Release the array
 *                     and every entry name with dir_list_entries_uninit.
 * @param out_count    Receives the number of entries written (0 when empty).
 * @return true when the directory was listed, false when it could not be opened.
 */
bool dir_list_entries_1(char const *const path, DirEntry **const out_entries, USize *const out_count);

/**
 * @brief List a directory with per-entry metadata (Str).
 * @param path         Directory path as Str*.
 * @param out_entries  Receives the entry array; release with dir_list_entries_uninit.
 * @param out_count    Receives the number of entries written.
 * @return true when the directory was listed, false otherwise.
 */
bool dir_list_entries_2(Str const *const path, DirEntry **const out_entries, USize *const out_count);

/**
 * @brief List a directory with per-entry metadata (String).
 * @param path         Directory path as String*.
 * @param out_entries  Receives the entry array; release with dir_list_entries_uninit.
 * @param out_count    Receives the number of entries written.
 * @return true when the directory was listed, false otherwise.
 */
bool dir_list_entries_3(String const *const path, DirEntry **const out_entries, USize *const out_count);

/**
 * @brief Release an entry array produced by dir_list_entries, including every name.
 * @param entries  Array to release; safe when nullptr.
 * @param count    Number of entries in the array.
 */
void dir_list_entries_uninit(DirEntry *const entries, USize const count);

/**
 * @brief Remove an empty directory (char*).
 * @param path  Directory path to remove; fails when the directory is not empty.
 * @return true when the directory was removed, false on failure.
 */
bool dir_remove_1(char const *const path);

/**
 * @brief Remove an empty directory (Str).
 * @param path  Directory path as Str*.
 * @return true when the directory was removed, false on failure.
 */
bool dir_remove_2(Str const *const path);

/**
 * @brief Remove an empty directory (String).
 * @param path  Directory path as String*.
 * @return true when the directory was removed, false on failure.
 */
bool dir_remove_3(String const *const path);

/**
 * @brief Recursively remove a directory and everything inside it (char*).
 * @param path  Directory path to remove. Refused when empty, a filesystem or
 *              drive root, made only of separators, containing a "." or ".."
 *              segment, or itself a symbolic link or junction. Symbolic links
 *              and junctions inside the tree are removed as links, never
 *              followed; non-surrogate reparse points (cloud placeholders) are
 *              traversed like plain entries.
 * @return true when the whole tree was removed, false when refused or when any
 *         entry could not be removed.
 */
bool dir_remove_all_1(char const *const path);

/**
 * @brief Recursively remove a directory and everything inside it (Str).
 * @param path  Directory path as Str*.
 * @return true when the whole tree was removed, false otherwise.
 */
bool dir_remove_all_2(Str const *const path);

/**
 * @brief Recursively remove a directory and everything inside it (String).
 * @param path  Directory path as String*.
 * @return true when the whole tree was removed, false otherwise.
 */
bool dir_remove_all_3(String const *const path);

/**
 * @brief Rename or move a directory (char*).
 * @param path      Existing directory path.
 * @param path_new  New directory path; must not already exist.
 * @return true when the directory was renamed, false on failure.
 */
bool dir_rename_1(char const *const path, char const *const path_new);

/**
 * @brief Rename or move a directory (Str).
 * @param path      Existing directory path as Str*.
 * @param path_new  New directory path as Str*.
 * @return true when the directory was renamed, false on failure.
 */
bool dir_rename_2(Str const *const path, Str const *const path_new);

/**
 * @brief Rename or move a directory (String).
 * @param path      Existing directory path as String*.
 * @param path_new  New directory path as String*.
 * @return true when the directory was renamed, false on failure.
 */
bool dir_rename_3(String const *const path, String const *const path_new);

/**
 * @brief Change the current working directory (char*).
 * @param path  Directory path to change into.
 * @return true when the working directory changed, false on failure.
 */
bool dir_set_current_1(char const *const path);

/**
 * @brief Change the current working directory (Str).
 * @param path  Directory path as Str*.
 * @return true when the working directory changed, false on failure.
 */
bool dir_set_current_2(Str const *const path);

/**
 * @brief Change the current working directory (String).
 * @param path  Directory path as String*.
 * @return true when the working directory changed, false on failure.
 */
bool dir_set_current_3(String const *const path);

#endif // DIR_H