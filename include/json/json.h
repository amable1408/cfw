/*
 * json.h - JSON parser and writer for the C Libraries Framework
 *
 * A CFW wrapper over yyjson 0.12.0 (vendored under include/json/yyjson.h and
 * yyjson.c, MIT license - see include/json/yyjson.h; the one third-party
 * library that lives under include/ rather than dep/). yyjson owns the
 * parse/serialize engine and the tree storage; this header adds the opaque
 * Json handle, tiered char pointer / sized / Str / String overloads, file loading, arena
 * twins, a bracket path lookup (['key'] and [N]), and CFW's argument checks.
 * yyjson's own API is not part of this header's contract.
 *
 * Features:
 *   - Parse JSON from char, Str, or String data (json_from_*), with a _try
 *     variant (json_from_try_2) that reports the parse failure's byte
 *     position and message instead of discarding it
 *   - Load JSON from a file path (json_load_*) and save back out (json_save_1)
 *   - Query arrays, objects, numbers, strings, booleans, and null - by name
 *     (typed getters), by any type (json_get_1..4), or read the JSON type
 *     directly (json_get_type, json_is_bool)
 *   - Navigate by array index (json_array_at) or a bracket path (json_at_*)
 *   - Add, remove (json_array_remove, json_object_remove_*), and set values
 *   - Serialize JSON with configurable indentation
 *   - Heap and arena allocation variants; json_arena_size sizes an arena for
 *     a document of a given byte count
 *
 * Usage Example:
 *   @code
 *   #include <json/json.h>
 *
 *   Json *root = json_from_1("{\"name\":\"demo\"}");
 *
 *   if (root == nullptr) {
 *       return; // malformed or empty input; the reason is not kept (use
 *   }            // json_from_try_2 for that)
 *
 *   Json   *const  name  = json_get_string_1(root, "name"); // nullptr: absent or wrong type
 *   String         value = name == nullptr ? string_init_1() : json_get_value_string_4(name);
 *
 *   // use value ...
 *
 *   string_uninit(&value);
 *   json_delete(&root); // frees the WHOLE tree; every handle taken from it dies here
 *   @endcode
 *
 * Ownership:
 *   - Every getter, json_at_*, json_array_at, and json_*_get_next call
 *     allocates a new handle onto the same tree and keeps it on an intrusive
 *     list until json_delete; never free a handle yourself.
 *   - json_delete on ANY handle - root or child - frees the whole tree and
 *     nulls only the pointer passed to it. In arena mode the tree lives until
 *     the arena resets and json_delete is a no-op there (it still nulls the
 *     caller's pointer).
 *   - json_get_data_1 writes through CFW's own heap allocator, not libc's:
 *     release the buffer with memory_free, never bare free. Its arena twin
 *     json_alloc_get_data_1 (and _3/_4) write ARENA-owned memory instead -
 *     never call memory_free (or any other release) on that buffer; it lives
 *     with the arena and json_delete's arena teardown reclaims nothing for
 *     it either, same as every other arena-owned allocation this module
 *     hands out.
 *   - json_get_label returns a shallow reference into the node, always
 *     marked as a VIEW on return (regardless of whether the node's own
 *     internal storage happens to be a view into yyjson's own memory or an
 *     owned copy the node caches) - an accidental str_uninit on it is a
 *     no-op, never a double free. It is released only by json_delete.
 *   - json_get_value_string_1 returns yyjson's own NUL-terminated pointer,
 *     stable for the tree's lifetime and identical across repeated calls on
 *     the same node - "" for an empty string value, nullptr only when self
 *     is not a string node. json_get_value_string_3/_4 return independent
 *     owned copies (release with str_uninit/string_uninit); both return the
 *     EMPTY Str/String for an empty string value AND for a non-string node -
 *     use json_get_type or json_is_bool first to tell the two apart.
 *   - json_set_string_static copies its data like every other string setter
 *     (see Tier Suffixes); it is kept as a distinct entry point for source
 *     compatibility only.
 *
 * Error Handling:
 *   - Contract violations - a null self/data/name/... argument - go through
 *     error_check_null, which logs and aborts the process when
 *     ERROR_CHECK_ENABLED is defined. With it undefined the check compiles
 *     out and passing null is undefined behaviour.
 *   - Conditions that depend on a VALUE rather than a broken contract are
 *     refused instead, and this holds in EVERY build, checked or not:
 *     malformed JSON text, an empty (0-byte) input, a missing object key, an
 *     empty key name, a wrong-type access through a typed getter, an
 *     out-of-range array index, and a malformed json_at path segment
 *     ([abc], an unterminated ['x, or a digit index overflowing USize) all
 *     return nullptr/false rather than aborting. json_from_try_2
 *     additionally reports the parse failure's byte position and a constant
 *     message through a caller-owned JsonError.
 *   - An empty ("") key is refused by the TYPED getters (json_get_array_*,
 *     json_get_object_*, json_get_number_*, json_get_string_*), by
 *     json_object_remove_*, and by json_set_label_* (renaming TO an empty key),
 *     but NOT by the any-type getters (json_get_1..4) or by json_at's [''] path
 *     segment - both of those look "" up like any other key and find it if the
 *     object has one (a legal, if unusual, JSON document). An EMPTY Str/String
 *     (a view whose data pointer is itself null) is a special case of this same
 *     "" key on the LOOKUP/removal side: it always refuses, even through
 *     json_get_2..4, since it carries no key to look up - a real non-null ""
 *     pointer still reaches the any-type getters' find-it behavior above.
 *     json_object_add_2..4 can itself add an "" key (real or from an EMPTY
 *     Str/String alike), and every string-VALUE setter (json_set_string_*,
 *     json_object_add_string_*) accepts an EMPTY Str/String as a legal ""
 *     value rather than refusing it.
 *
 * Thread Safety:
 *   - A JSON tree is not internally synchronized. Every getter mutates the
 *     owning tree's wrapper list, so even two threads only READING the same
 *     tree race; a shared tree needs external locking, or one thread at a
 *     time. Separate trees, one per thread, need no coordination.
 *
 * Memory Management:
 *   - Heap constructors must be released with json_delete().
 *   - Arena constructors live for the allocator lifetime; json_delete is
 *     then a no-op that only nulls the caller's pointer.
 *   - Setters that take string data (including json_set_string_static) copy it.
 *   - See Ownership above for handle/label/string-value lifetimes.
 *
 * Performance Characteristics:
 *   - Parse/serialize: O(N) where N is the JSON text size (delegated to yyjson).
 *   - Object key lookup: O(K) where K is the object's key count (yyjson keeps
 *     no index; every lookup is a linear scan).
 *   - json_array_at / json_at index access: O(i) for element i.
 *   - json_array_get_next / json_object_get_next: O(1) per step (a
 *     linked-list hop from the current node), not a re-walk from the start -
 *     measured ~30x faster than the prior O(N)-per-step walk at 10,000
 *     elements.
 *   - Every handle a getter/at/get_next call returns is a heap (or arena)
 *     allocation that lives until json_delete; an unbounded loop over a
 *     long-lived tree grows memory without bound.
 *   - Arena parsing keeps BOTH the immutable read pool and the mutable tree
 *     copy live at once (the read pool needs roughly yyjson's own max-usage
 *     figure for the input; the mutable copy up to 13x the input's byte
 *     count - yyjson's own worst case, one 24-byte yyjson_mut_val per value
 *     copied against yyjson's N/2+1 worst-case value count, plus headroom
 *     for string bytes; an all-scalar body like "0,0,0,..." is the shape
 *     that reaches it). json_arena_size(byte_count) sizes an arena for both
 *     plus roughly 30-35 handles of headroom (sizeof(Json) is 128 bytes -
 *     Str itself is 32 bytes under ARENA_IMPLEMENTATION) for the wrapper
 *     handles a caller's getters allocate - an iteration over K elements
 *     needs roughly K x 128 more bytes on top, but a by-NAME lookup in
 *     arena mode also copies the label into the arena (label_is_view is
 *     false there), so that path costs K x (128 + L + 1) for an L-byte key;
 *     0 means
 *     byte_count itself is too large to size (would overflow USize), never a
 *     valid arena size. A too-small arena degrades to a parse failure
 *     (nullptr) with ERROR_CHECK_ENABLED off - the checked build instead
 *     aborts inside the arena allocator on exhaustion (allocator_borrow's
 *     own contract, not specific to json), matching every other arena
 *     consumer in the framework.
 *
 * Tier Suffixes (per family, where all four apply):
 *   - _1: char pointer (null-terminated).
 *   - _2: char pointer plus explicit size (sized).
 *   - _3: Str.
 *   - _4: String.
 *   A family missing a tier (e.g. json_load leaves _2 unused; json_get_data,
 *   json_alloc_get_data and json_get_value_string leave _2 unused, since they
 *   take no string or buffer input to size) skips that number rather than
 *   reassigning it. Numeric kinds are spelled float, int, and uint, never f,
 *   s, or u; a fixed-precision numeric variant is named with a _precision
 *   suffix instead of a numbered tier (json_set_number_float_precision,
 *   json_object_add_number_float_precision).
 *
 * Dependencies:
 *   - <error/error.h>
 *   - <file/file.h>
 *   - <tracelog/tracelog.h>
 *
 * Third-Party:
 *   - include/json/yyjson.h and yyjson.c bundle yyjson 0.12.0 by YaoYuan,
 *     MIT-licensed (see include/json/yyjson.h) - the parse/serialize engine
 *     this module wraps.
 *
 * See json.c for implementation details.
 */
#ifndef JSON_H
#define JSON_H

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/
#include <error/error.h>
#include <file/file.h>
#include <tracelog/tracelog.h>

/*==============================================================================
 * MARK: - Typedefs and Enums
 *============================================================================*/
/**
 * JSON_TYPE_NULL is returned only for a genuine JSON null literal; a handle with no
 * underlying value must never be passed to a type query (every handle returned by a
 * getter/iterator/add call has one).
 */
typedef enum {
    JSON_TYPE_ARRAY,
    JSON_TYPE_FALSE,
    JSON_TYPE_INTEGER_S,
    JSON_TYPE_INTEGER_U,
    JSON_TYPE_NULL,
    JSON_TYPE_OBJECT,
    JSON_TYPE_REAL,
    JSON_TYPE_STRING,
    JSON_TYPE_TRUE,
} JsonType;

typedef struct Json Json;

/**
 * @brief Parse failure detail for json_from_try_2.
 */
typedef struct JsonError {
    USize        position;  /**< Byte offset into the input where parsing failed. */
    char const  *message;   /**< Constant, statically-owned failure description. */
} JsonError;

/*==============================================================================
 * MARK: - Public API
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
/**
 * @brief Parse null-terminated JSON into arena-owned nodes.
 * @param data JSON text.
 * @param allocator Arena allocator.
 * @return Parsed JSON root.
 */
Json* json_alloc_from_1(char const *const data, Arena *const allocator);

/**
 * @brief Parse sized JSON into arena-owned nodes.
 * @param data JSON text.
 * @param data_size JSON text size.
 * @param allocator Arena allocator.
 * @return Parsed JSON root.
 */
Json* json_alloc_from_2(char const *const data, USize const data_size, Arena *const allocator);

/**
 * @brief Parse JSON from Str into arena-owned nodes.
 * @param data JSON text.
 * @param allocator Arena allocator.
 * @return Parsed JSON root.
 */
Json* json_alloc_from_3(Str const *const data, Arena *const allocator);

/**
 * @brief Parse JSON from String into arena-owned nodes.
 * @param data JSON text.
 * @param allocator Arena allocator.
 * @return Parsed JSON root.
 */
Json* json_alloc_from_4(String const *const data, Arena *const allocator);

/**
 * @brief Parse sized JSON into arena-owned nodes, reporting the parse failure.
 * @param data JSON text.
 * @param data_size JSON text size.
 * @param error Destination for failure detail; set on both a malformed document and an
 *              empty (data_size 0) one, left untouched on success.
 * @param allocator Arena allocator.
 * @return Parsed JSON root, or nullptr on failure (error is set).
 */
Json* json_alloc_from_try_2(char const *const data, USize const data_size, JsonError *const error, Arena *const allocator);

/**
 * @brief Serialize JSON to an arena-owned char buffer.
 * @param self JSON root node.
 * @param indentation_size Pretty-print flag: any non-zero value enables yyjson's
 *        four-space indentation; 0 writes compact JSON.
 * @param allocator Arena allocator.
 * @return Serialized JSON data, ARENA-owned: never release it with memory_free or any
 *         other call - it lives with the arena, and json_delete's arena teardown reclaims
 *         nothing for it either. Nullptr when self has no underlying value (a val-less
 *         handle) or the arena could not satisfy the write (exhaustion).
 */
char* json_alloc_get_data_1(Json const *const self, U8 const indentation_size, Arena *const allocator);

/**
 * @brief Serialize JSON to an arena-owned Str.
 * @param self JSON root node.
 * @param indentation_size Pretty-print flag: any non-zero value enables yyjson's
 *        four-space indentation; 0 writes compact JSON.
 * @param allocator Arena allocator.
 * @return Serialized JSON data, arena-owned - str_uninit on it is a no-op (owned stays
 *         false); it lives with the arena and is released only when the arena resets. The
 *         EMPTY Str when self has no underlying value or the arena could not satisfy the write.
 */
Str json_alloc_get_data_3(Json const *const self, U8 const indentation_size, Arena *const allocator);

/**
 * @brief Serialize JSON to an arena-owned String.
 * @param self JSON root node.
 * @param indentation_size Pretty-print flag: any non-zero value enables yyjson's
 *        four-space indentation; 0 writes compact JSON.
 * @param allocator Arena allocator.
 * @return Serialized JSON data, arena-owned - string_uninit on it is a no-op (owned
 *         stays false); it lives with the arena and is released only when the arena resets.
 *         The EMPTY String when self has no underlying value or the arena could not satisfy
 *         the write.
 */
String json_alloc_get_data_4(Json const *const self, U8 const indentation_size, Arena *const allocator);

/**
 * @brief Load and parse JSON from a char file path using arena memory.
 * @param file_name File path.
 * @param allocator Arena allocator.
 * @return Parsed JSON root.
 */
Json* json_alloc_load_1(char const *const file_name, Arena *const allocator);

/**
 * @brief Load and parse JSON from a Str file path using arena memory.
 * @param file_name File path.
 * @param allocator Arena allocator.
 * @return Parsed JSON root.
 */
Json* json_alloc_load_3(Str const *const file_name, Arena *const allocator);

/**
 * @brief Load and parse JSON from a String file path using arena memory.
 * @param file_name File path.
 * @param allocator Arena allocator.
 * @return Parsed JSON root.
 */
Json* json_alloc_load_4(String const *const file_name, Arena *const allocator);

#endif // ARENA_IMPLEMENTATION
/**
 * @brief Compute a byte count sized to parse a document of byte_count bytes into an
 *        arena: covers the immutable read pool, the mutable tree copy made on top of
 *        it, and headroom for the wrapper handles a caller's getters will allocate.
 * @param byte_count Size of the JSON text to be parsed.
 * @return Recommended arena byte count, or 0 when byte_count is too large to size
 *         without overflowing USize (never a valid arena size otherwise). A too-small
 *         arena reports as a parse failure (nullptr) with ERROR_CHECK disabled; the
 *         checked build aborts inside the arena allocator on exhaustion instead,
 *         matching allocator_borrow's own contract - this is not specific to json,
 *         every arena consumer aborts there.
 */
USize json_arena_size(USize const byte_count);

/**
 * @brief Add a heap-owned child to a JSON array.
 * @param self Array node OR array child node - added to the enclosing array either way
 *        (the same "self or self's parent" rule json_at/json_object_add use).
 * @param json_type Child type.
 * @return Added child node, or nullptr when self is not (and has no) array parent.
 */
Json* json_array_add(Json *const self, JsonType const json_type);

/**
 * @brief Get an array element by index, the public form of the [N] path segment.
 * @param self Array node.
 * @param index Element index.
 * @return Matching element node, or nullptr when out of range.
 */
Json* json_array_at(Json const *const self, USize const index);

/**
 * @brief Return the next array sibling, or the first element when self is the
 *        array node itself (the cursor starts at the container).
 * @param self Array node or array child node.
 * @return Next element, or nullptr past the last one.
 */
Json* json_array_get_next(Json const *const self);

/**
 * @brief Return array child count for self (never a parent's, when self is not itself
 *        an array this is 0).
 * @param self Array node.
 * @return Child count.
 */
USize json_array_get_size(Json const *const self);

/**
 * @brief Remove the array element at index. Any handle still held for the removed
 *        element becomes dangling (its val is gone; do not deref or navigate from it),
 *        and no remaining sibling's cached index is renumbered.
 * @param self Array node.
 * @param index Element index.
 * @return True when an element at index existed and was removed.
 */
bool json_array_remove(Json *const self, USize const index);

/**
 * @brief Find a nested node using a null-terminated path.
 * @param self JSON root node.
 * @param search Search path, e.g. ['items'][0].
 * @return Matching JSON node, self when search is non-empty but has no bracket segment,
 *         or nullptr for an EMPTY search (distinct from the no-bracket-segment case
 *         above), a missing segment, or a malformed one ([abc], an unterminated ['x, a
 *         digit index that overflows USize, or a segment missing its closing bracket).
 */
Json* json_at_1(Json const *const self, char const *const search);

/**
 * @brief Find a nested node using a sized path.
 * @param self JSON root node.
 * @param search Search path.
 * @param search_size Search path size.
 * @return Matching JSON node; see json_at_1 for the self/nullptr edge cases.
 */
Json* json_at_2(Json const *const self, char const *const search, USize const search_size);

/**
 * @brief Free a heap-owned JSON tree and null the pointer.
 * @param self JSON root pointer.
 */
void json_delete(Json **const self);

/**
 * @brief Parse null-terminated JSON into heap-owned nodes.
 * @param data JSON text.
 * @return Parsed JSON root.
 */
Json* json_from_1(char const *const data);

/**
 * @brief Parse sized JSON into heap-owned nodes.
 * @param data JSON text.
 * @param data_size JSON text size.
 * @return Parsed JSON root.
 */
Json* json_from_2(char const *const data, USize const data_size);

/**
 * @brief Parse JSON from Str into heap-owned nodes.
 * @param data JSON text.
 * @return Parsed JSON root.
 */
Json* json_from_3(Str const *const data);

/**
 * @brief Parse JSON from String into heap-owned nodes.
 * @param data JSON text.
 * @return Parsed JSON root.
 */
Json* json_from_4(String const *const data);

/**
 * @brief Parse sized JSON into heap-owned nodes, reporting the parse failure.
 * @param data JSON text.
 * @param data_size JSON text size.
 * @param error Destination for failure detail; set on both a malformed document and an
 *              empty (data_size 0) one, left untouched on success.
 * @return Parsed JSON root, or nullptr on failure (error is set).
 */
Json* json_from_try_2(char const *const data, USize const data_size, JsonError *const error);

/**
 * @brief Get a named child of any type by null-terminated name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching node of any JSON type.
 */
Json* json_get_1(Json const *const self, char const *const name);

/**
 * @brief Get a named child of any type by sized name.
 * @param self Object node.
 * @param name Child name.
 * @param name_size Child name size.
 * @return Matching node of any JSON type.
 */
Json* json_get_2(Json const *const self, char const *const name, USize const name_size);

/**
 * @brief Get a named child of any type by Str name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching node of any JSON type.
 */
Json* json_get_3(Json const *const self, Str const *const name);

/**
 * @brief Get a named child of any type by String name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching node of any JSON type.
 */
Json* json_get_4(Json const *const self, String const *const name);

/**
 * @brief Get a named array child by null-terminated name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching array node.
 */
Json* json_get_array_1(Json const *const self, char const *const name);

/**
 * @brief Get a named array child by sized name.
 * @param self Object node.
 * @param name Child name.
 * @param name_size Child name size.
 * @return Matching array node.
 */
Json* json_get_array_2(Json const *const self, char const *const name, USize const name_size);

/**
 * @brief Get a named array child by Str name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching array node.
 */
Json* json_get_array_3(Json const *const self, Str const *const name);

/**
 * @brief Get a named array child by String name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching array node.
 */
Json* json_get_array_4(Json const *const self, String const *const name);

/**
 * @brief Serialize JSON to a heap-owned char buffer.
 * @param self JSON root node.
 * @param indentation_size Pretty-print flag: any non-zero value enables yyjson's
 *        four-space indentation; 0 writes compact JSON.
 * @return Serialized JSON data, always HEAP-owned (through CFW's own allocator, not
 *         libc's) even when self's tree is arena-backed - this call always allocates a
 *         fresh buffer. Release with memory_free; json_delete never reclaims it (the
 *         arena form is json_alloc_get_data_1, whose buffer must never be freed at all).
 */
char* json_get_data_1(Json const *const self, U8 const indentation_size);

/**
 * @brief Serialize JSON to a heap-owned Str.
 * @param self JSON root node.
 * @param indentation_size Pretty-print flag: any non-zero value enables yyjson's
 *        four-space indentation; 0 writes compact JSON.
 * @return Serialized JSON data.
 */
Str json_get_data_3(Json const *const self, U8 const indentation_size);

/**
 * @brief Serialize JSON to a heap-owned String.
 * @param self JSON root node.
 * @param indentation_size Pretty-print flag: any non-zero value enables yyjson's
 *        four-space indentation; 0 writes compact JSON.
 * @return Serialized JSON data.
 */
String json_get_data_4(Json const *const self, U8 const indentation_size);

/**
 * @brief Return the node label.
 * @param self JSON node.
 * @return Shallow reference into the node's own label storage - a view for a key
 *         reached by add or iteration, an owned copy for a key reached by name
 *         lookup. Either way never call str_uninit on it; it is released only by
 *         json_delete.
 */
Str json_get_label(Json const *const self);

/**
 * @brief Get a named real-number child by null-terminated name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching real-number node.
 */
Json* json_get_number_float_1(Json const *const self, char const *const name);

/**
 * @brief Get a named real-number child by sized name.
 * @param self Object node.
 * @param name Child name.
 * @param name_size Child name size.
 * @return Matching real-number node.
 */
Json* json_get_number_float_2(Json const *const self, char const *const name, USize const name_size);

/**
 * @brief Get a named real-number child by Str name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching real-number node.
 */
Json* json_get_number_float_3(Json const *const self, Str const *const name);

/**
 * @brief Get a named real-number child by String name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching real-number node.
 */
Json* json_get_number_float_4(Json const *const self, String const *const name);

/**
 * @brief Get a named signed-integer child by null-terminated name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching signed-integer node.
 */
Json* json_get_number_int_1(Json const *const self, char const *const name);

/**
 * @brief Get a named signed-integer child by sized name.
 * @param self Object node.
 * @param name Child name.
 * @param name_size Child name size.
 * @return Matching signed-integer node.
 */
Json* json_get_number_int_2(Json const *const self, char const *const name, USize const name_size);

/**
 * @brief Get a named signed-integer child by Str name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching signed-integer node.
 */
Json* json_get_number_int_3(Json const *const self, Str const *const name);

/**
 * @brief Get a named signed-integer child by String name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching signed-integer node.
 */
Json* json_get_number_int_4(Json const *const self, String const *const name);

/**
 * @brief Get a named unsigned-integer child by null-terminated name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching unsigned-integer node.
 */
Json* json_get_number_uint_1(Json const *const self, char const *const name);

/**
 * @brief Get a named unsigned-integer child by sized name.
 * @param self Object node.
 * @param name Child name.
 * @param name_size Child name size.
 * @return Matching unsigned-integer node.
 */
Json* json_get_number_uint_2(Json const *const self, char const *const name, USize const name_size);

/**
 * @brief Get a named unsigned-integer child by Str name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching unsigned-integer node.
 */
Json* json_get_number_uint_3(Json const *const self, Str const *const name);

/**
 * @brief Get a named unsigned-integer child by String name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching unsigned-integer node.
 */
Json* json_get_number_uint_4(Json const *const self, String const *const name);

/**
 * @brief Get a named object child by null-terminated name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching object node.
 */
Json* json_get_object_1(Json const *const self, char const *const name);

/**
 * @brief Get a named object child by sized name.
 * @param self Object node.
 * @param name Child name.
 * @param name_size Child name size.
 * @return Matching object node.
 */
Json* json_get_object_2(Json const *const self, char const *const name, USize const name_size);

/**
 * @brief Get a named object child by Str name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching object node.
 */
Json* json_get_object_3(Json const *const self, Str const *const name);

/**
 * @brief Get a named object child by String name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching object node.
 */
Json* json_get_object_4(Json const *const self, String const *const name);

/**
 * @brief Return child count for arrays and objects, or character length for a string node.
 * @param self JSON node.
 * @return Child/string length count; 0 for any other node type.
 */
USize json_get_size(Json const *const self);

/**
 * @brief Get a named string child by null-terminated name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching string node.
 */
Json* json_get_string_1(Json const *const self, char const *const name);

/**
 * @brief Get a named string child by sized name.
 * @param self Object node.
 * @param name Child name.
 * @param name_size Child name size.
 * @return Matching string node.
 */
Json* json_get_string_2(Json const *const self, char const *const name, USize const name_size);

/**
 * @brief Get a named string child by Str name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching string node.
 */
Json* json_get_string_3(Json const *const self, Str const *const name);

/**
 * @brief Get a named string child by String name.
 * @param self Object node.
 * @param name Child name.
 * @return Matching string node.
 */
Json* json_get_string_4(Json const *const self, String const *const name);

/**
 * @brief Return the node's JSON type.
 * @param self JSON node.
 * @return The node's type. JSON_TYPE_NULL only for a genuine JSON null literal - there is
 *         no json_is_null twin to json_is_bool; use json_get_type(x) == JSON_TYPE_NULL.
 */
JsonType json_get_type(Json const *const self);

/**
 * @brief Return node boolean value.
 * @param self JSON node.
 * @return Boolean value. False when self is not a boolean node.
 */
bool json_get_value_bool(Json const *const self);

/**
 * @brief Return node real-number value.
 * @param self JSON node.
 * @return Real-number value. 0.0 when self is not a real node.
 */
FSize json_get_value_number_float(Json const *const self);

/**
 * @brief Return node signed-integer value.
 * @param self JSON node.
 * @return Signed-integer value, reading BOTH the signed and unsigned integer subtypes
 *         (yyjson's own get_sint covers both - a literal like `1` parses as unsigned and
 *         still reads correctly here); 0 when self is not an integer node at all. An
 *         unsigned value above ISIZE_MAX wraps to a negative ISize rather than saturating
 *         or refusing - use json_read_int instead when that distinction matters.
 */
ISize json_get_value_number_int(Json const *const self);

/**
 * @brief Return node unsigned-integer value.
 * @param self JSON node.
 * @return Unsigned-integer value, reading BOTH the signed and unsigned integer subtypes
 *         (yyjson's own get_uint covers both); 0 when self is not an integer node at all.
 *         A negative signed value wraps to a huge USize rather than saturating or
 *         refusing (e.g. -1 reads as 2^64-1) - use json_read_int instead when that
 *         distinction matters.
 */
USize json_get_value_number_uint(Json const *const self);

/**
 * @brief Return node string value as a raw pointer.
 * @param self JSON node.
 * @return String value pointer owned by the node, stable for the tree's lifetime; "" for
 *         an empty string value, nullptr only when self is not a string node.
 */
char* json_get_value_string_1(Json const *const self);

/**
 * @brief Return node string value as an owned Str.
 * @param self JSON node.
 * @return Owned string value. The EMPTY Str both for an empty string value and for a
 *         non-string node.
 */
Str json_get_value_string_3(Json const *const self);

/**
 * @brief Return node string value as an owned String.
 * @param self JSON node.
 * @return Owned string value. The EMPTY String both for an empty string value and for a
 *         non-string node.
 */
String json_get_value_string_4(Json const *const self);

/**
 * @brief Report whether self is boolean-typed (true or false).
 * @param self JSON node.
 * @return True for a boolean node.
 */
bool json_is_bool(Json const *const self);

/**
 * @brief Load and parse JSON from a char file path.
 * @param file_name File path.
 * @return Parsed JSON root.
 */
Json* json_load_1(char const *const file_name);

/**
 * @brief Load and parse JSON from a Str file path.
 * @param file_name File path.
 * @return Parsed JSON root.
 */
Json* json_load_3(Str const *const file_name);

/**
 * @brief Load and parse JSON from a String file path.
 * @param file_name File path.
 * @return Parsed JSON root.
 */
Json* json_load_4(String const *const file_name);

/**
 * @brief Add a named child using a null-terminated name. Every other one-call
 *        json_object_add_bool/null/number/string wrapper follows the same "self OR
 *        self's parent" rule for its own self param.
 * @param self Object node OR object child node - added to the enclosing object either
 *        way (the same rule json_array_add and json_at use).
 * @param json_type Child type.
 * @param name Child name.
 * @return Added child node, or nullptr when self is not (and has no) object parent.
 */
Json* json_object_add_1(Json *const self, JsonType const json_type, char const *const name);

/**
 * @brief Add a named child using a sized name.
 * @param self Object node.
 * @param json_type Child type.
 * @param name Child name.
 * @param name_size Child name size.
 * @return Added child node.
 */
Json* json_object_add_2(Json *const self, JsonType const json_type, char const *const name, USize const name_size);

/**
 * @brief Add a named child using a Str name.
 * @param self Object node.
 * @param json_type Child type.
 * @param name Child name.
 * @return Added child node.
 */
Json* json_object_add_3(Json *const self, JsonType const json_type, Str const *const name);

/**
 * @brief Add a named child using a String name.
 * @param self Object node.
 * @param json_type Child type.
 * @param name Child name.
 * @return Added child node.
 */
Json* json_object_add_4(Json *const self, JsonType const json_type, String const *const name);

/**
 * @brief Add a named boolean value to an object in one call.
 * @param self Object node.
 * @param name Child name.
 * @param value Boolean value.
 * @return Added child node, or nullptr when self is not (and has no) object parent.
 */
Json* json_object_add_bool(Json *const self, char const *const name, bool const value);

/**
 * @brief Add a named null value to an object in one call.
 * @param self Object node.
 * @param name Child name.
 * @return Added child node.
 */
Json* json_object_add_null(Json *const self, char const *const name);

/**
 * @brief Add a named real (floating-point) value to an object in one call, serialized at
 *        shortest round-trip precision (no fixed decimal count).
 * @param self Object node.
 * @param name Child name.
 * @param value Real value.
 * @return Added child node, or nullptr when self is not (and has no) object parent.
 */
Json* json_object_add_number_float(Json *const self, char const *const name, FSize const value);

/**
 * @brief Add a named real value to an object with a fixed decimal precision.
 * @param self Object node.
 * @param name Child name.
 * @param value Real value.
 * @param precision Number of decimal places.
 * @return Added child node, or nullptr when self is not (and has no) object parent.
 */
Json* json_object_add_number_float_precision(Json *const self, char const *const name, FSize const value, U8 const precision);

/**
 * @brief Add a named signed integer value to an object in one call.
 * @param self Object node.
 * @param name Child name.
 * @param value Signed integer value.
 * @return Added child node, or nullptr when self is not (and has no) object parent.
 */
Json* json_object_add_number_int(Json *const self, char const *const name, ISize const value);

/**
 * @brief Add a named unsigned integer value to an object in one call.
 * @param self Object node.
 * @param name Child name.
 * @param value Unsigned integer value.
 * @return Added child node, or nullptr when self is not (and has no) object parent.
 */
Json* json_object_add_number_uint(Json *const self, char const *const name, USize const value);

/**
 * @brief Add a named string value to an object in one call.
 * @param self Object node.
 * @param name Child name.
 * @param value String value.
 * @return Added child node, or nullptr when self is not (and has no) object parent.
 */
Json* json_object_add_string_1(Json *const self, char const *const name, char const *const value);

/**
 * @brief Add a named sized string value to an object in one call.
 * @param self Object node.
 * @param name Child name.
 * @param value String value.
 * @param value_size String value size.
 * @return Added child node, or nullptr when self is not (and has no) object parent.
 */
Json* json_object_add_string_2(Json *const self, char const *const name, char const *const value, USize const value_size);

/**
 * @brief Add a named Str value to an object in one call.
 * @param self Object node.
 * @param name Child name.
 * @param value Str value.
 * @return Added child node, or nullptr when self is not (and has no) object parent.
 */
Json* json_object_add_string_3(Json *const self, char const *const name, Str const *const value);

/**
 * @brief Add a named String value to an object in one call.
 * @param self Object node.
 * @param name Child name.
 * @param value String value.
 * @return Added child node, or nullptr when self is not (and has no) object parent.
 */
Json* json_object_add_string_4(Json *const self, char const *const name, String const *const value);

/**
 * @brief Return the next object key/value, or the first pair when self is the
 *        object node itself (the cursor starts at the container).
 * @param self Object node or object child node.
 * @return Next child, or nullptr past the last one.
 */
Json* json_object_get_next(Json const *const self);

/**
 * @brief Remove a named key-value pair using a null-terminated name. See json_array_remove
 *        for the dangling-handle/stale-index caveat, which applies identically here.
 * @param self Object node.
 * @param name Child name.
 * @return True when a key named name existed and was removed.
 */
bool json_object_remove_1(Json *const self, char const *const name);

/**
 * @brief Remove a named key-value pair using a sized name.
 * @param self Object node.
 * @param name Child name.
 * @param name_size Child name size.
 * @return True when a key named name existed and was removed.
 */
bool json_object_remove_2(Json *const self, char const *const name, USize const name_size);

/**
 * @brief Print serialized JSON to stdout.
 * @param self JSON root node.
 * @param indentation_size Pretty-print flag: any non-zero value enables yyjson's
 *        four-space indentation; 0 writes compact JSON.
 */
void json_print(Json const *const self, U8 const indentation_size);

/**
 * @brief Read a named boolean value into *out.
 * @param self Object node.
 * @param name Child name (a compile-time key literal).
 * @param out Destination for the boolean value; untouched when not found.
 * @return True when a boolean child named `name` exists (out is then set).
 */
bool json_read_bool(Json const *const self, char const *const name, bool *const out);

/**
 * @brief Read a named signed-integer value into *out.
 * @param self Object node.
 * @param name Child name (a compile-time key literal).
 * @param out Destination for the integer value; untouched when not found.
 * @return True when an integer child named `name` exists and fits ISize (out is
 *         then set); false when absent, non-numeric, or when an unsigned value
 *         exceeds ISize.
 * @note A JSON literal such as `1` parses as an unsigned node, so both the signed
 *       and unsigned interpretations are checked from a single lookup.
 */
bool json_read_int(Json const *const self, char const *const name, ISize *const out);

/**
 * @brief Read a named string value as a raw pointer borrowed from the node.
 * @param self Object node.
 * @param name Child name (a compile-time key literal).
 * @return String value pointer owned by the node, or null when absent or wrong type.
 */
char* json_read_string(Json const *const self, char const *const name);

/**
 * @brief Serialize self and write it to a file path, replacing any existing content.
 * @param self JSON root node.
 * @param path File path.
 * @param pretty Whether to pretty-print the output.
 * @return True when the file was opened and the full serialized document was written.
 */
bool json_save_1(Json const *const self, char const *const path, bool const pretty);

/**
 * @brief Set node boolean value.
 * @param self JSON node.
 * @param value Boolean value.
 */
void json_set_bool(Json *const self, bool const value);

/**
 * @brief Set node label from a null-terminated string.
 * @param self JSON node.
 * @param data Label data.
 * @return True when self is an object child and the rename succeeded.
 */
bool json_set_label_1(Json *const self, char const *const data);

/**
 * @brief Set node label from a sized string.
 * @param self JSON node.
 * @param data Label data.
 * @param data_size Label data size.
 * @return False when data_size is 0, self is not an object child, self's current key
 *         is itself empty (rename-from-empty-key is not supported), or the rename
 *         failed; the cached label is left unchanged on any of these, never desynced
 *         from the actual key.
 */
bool json_set_label_2(Json *const self, char const *const data, USize const data_size);

/**
 * @brief Set node label from Str.
 * @param self JSON node.
 * @param data Label data.
 * @return See json_set_label_2.
 */
bool json_set_label_3(Json *const self, Str const *const data);

/**
 * @brief Set node label from String.
 * @param self JSON node.
 * @param data Label data.
 * @return See json_set_label_2.
 */
bool json_set_label_4(Json *const self, String const *const data);

/**
 * @brief Set node real-number value, serialized at shortest round-trip precision (no
 *        fixed decimal count). Use json_set_number_float_precision for a fixed count.
 * @param self JSON node.
 * @param value Real-number value.
 */
void json_set_number_float(Json *const self, FSize const value);

/**
 * @brief Set node real-number value with explicit precision.
 * @param self JSON node.
 * @param value Real-number value.
 * @param precision Decimal precision.
 */
void json_set_number_float_precision(Json *const self, FSize const value, U8 const precision);

/**
 * @brief Set node signed-integer value.
 * @param self JSON node.
 * @param value Signed-integer value.
 */
void json_set_number_int(Json *const self, ISize const value);

/**
 * @brief Set node unsigned-integer value.
 * @param self JSON node.
 * @param value Unsigned-integer value.
 */
void json_set_number_uint(Json *const self, USize const value);

/**
 * @brief Set node string value from a null-terminated string.
 * @param self JSON node.
 * @param data String data.
 * @return True on success, false when the copy could not be allocated (OOM).
 */
bool json_set_string_1(Json *const self, char const *const data);

/**
 * @brief Set node string value from a sized string.
 * @param self JSON node.
 * @param data String data.
 * @param data_size String data size.
 * @return True on success, false when the copy could not be allocated (OOM).
 */
bool json_set_string_2(Json *const self, char const *const data, USize const data_size);

/**
 * @brief Set node string value from Str.
 * @param self JSON node.
 * @param data String data.
 * @return See json_set_string_2.
 */
bool json_set_string_3(Json *const self, Str const *const data);

/**
 * @brief Set node string value from String.
 * @param self JSON node.
 * @param data String data.
 * @return See json_set_string_2.
 */
bool json_set_string_4(Json *const self, String const *const data);

/**
 * @brief Set node string value, copying data. Kept as a distinct entry point for
 *        source compatibility; behaves identically to json_set_string_2 (it used to
 *        borrow unterminated data - the only setter that did - which made a copy of
 *        an unterminated buffer unsafe to hold onto past the call).
 * @param self JSON node.
 * @param data String data.
 * @param data_size String data size.
 * @return See json_set_string_2.
 */
bool json_set_string_static(Json *const self, char const *const data, USize const data_size);

#endif // JSON_H