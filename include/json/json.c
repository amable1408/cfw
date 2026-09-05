#include <json/yyjson.h>

#include <json/json.h>

#define _JSON_WRITE_FLAGS(i) ((i) ? YYJSON_WRITE_PRETTY : YYJSON_WRITE_NOFLAG)
// yyjson_val_mut_copy's worst case: one 24-byte yyjson_mut_val per value copied against
// yyjson's N/2+1 worst-case value count (12xN) plus 1xN headroom for string bytes (Misc 3(c)).
#define _JSON_ARENA_BYTE_FACTOR 13
#define _JSON_ARENA_POOL_BASE 4096

struct Json {
    yyjson_mut_doc      *doc;
    yyjson_mut_val      *val;
    yyjson_alc          alc;
    Json                *owner;
    Json                *parent;
    Json                *next_wrapper;
#ifdef ARENA_IMPLEMENTATION
    Arena               *allocator;
#endif // ARENA_IMPLEMENTATION
    Str                 label;
    USize               index;
    bool                label_ready;
};

static void* _json_heap_malloc(void *const ctx, size_t const size) {
    (void) ctx;

    // yyjson's allocator contract permits a 0-byte request; memory_alloc(0) hits CFW's
    // abort-on-value primitive (this size comes from yyjson/libc bookkeeping, never
    // caller data, so rounding up to 1 is the right tolerance, not a refusal) (item 4).
    return memory_alloc(size == 0 ? (USize) 1 : (USize) size);
}

static void* _json_heap_realloc(void *const ctx, void *const ptr, size_t const old_size, size_t const size) {
    (void) ctx;

    USize const safe_size = size == 0 ? (USize) 1 : (USize) size; // item 4, see _json_heap_malloc

    if (ptr == nullptr) {
        return memory_alloc(safe_size);
    }

    return memory_realloc(ptr, (USize) old_size, safe_size);
}

static void _json_heap_free(void *const ctx, void *const ptr) {
    (void) ctx;

    memory_free(ptr);
}

/* The CFW heap twin of _json_arena_alc below: every yyjson write call in this file
 * passes this so the returned buffer releases through memory_free, not libc free(). */
static yyjson_alc const _JSON_HEAP_ALC = {
    _json_heap_malloc,
    _json_heap_realloc,
    _json_heap_free,
    nullptr,
};

#ifdef ARENA_IMPLEMENTATION
static void* _json_arena_malloc(void *const ctx, size_t const size) {
    // The arena twin of _json_heap_malloc's 0-byte tolerance above: allocator_borrow
    // (arena_linear_alloc) aborts on a 0-byte request, so round up the same way (memsec MED).
    return allocator_borrow(size == 0 ? (USize) 1 : (USize) size, (Arena*) ctx);
}

static void* _json_arena_realloc(void *const ctx, void *const ptr, size_t const old_size, size_t const size) {
    USize const safe_size = size == 0 ? (USize) 1 : (USize) size; // see _json_arena_malloc

    if (ptr == nullptr) {
        return _json_arena_malloc(ctx, size);
    }

    void *const buffer = (void*) allocator_borrow(safe_size, (Arena*) ctx);

    if (buffer == nullptr) {
        return nullptr;
    }

    char_copy_2((char*) buffer, (char*) ptr, (USize) (old_size < size ? old_size : size));

    return buffer;
}

static void _json_arena_free(void *const ctx, void *const ptr) {
    (void) ctx;
    (void) ptr;
}
#endif // ARENA_IMPLEMENTATION

#ifdef ARENA_IMPLEMENTATION
static Json* _json_new(Arena *const allocator)
#else
static Json* _json_new(void)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    Json *const json = allocator != nullptr ? (Json*) allocator_borrow(sizeof(Json), allocator) : (Json*) memory_alloc(sizeof(Json));
#else
    Json *const json = (Json*) memory_alloc(sizeof(Json));
#endif // ARENA_IMPLEMENTATION

    if (json == nullptr) {
        trace_log_pop();

        return nullptr;
    }

#ifdef ARENA_IMPLEMENTATION
    json->allocator = allocator;
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return json;
}

static Json* _json_root(Json const *const self) {
    return self == nullptr ? nullptr : (self->owner == nullptr ? (Json*) self : (Json*) self->owner);
}

#ifdef ARENA_IMPLEMENTATION
static yyjson_alc* _json_alc(Json *const self, USize const hint) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->allocator == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    USize       const           size    = hint < _JSON_ARENA_POOL_BASE ? _JSON_ARENA_POOL_BASE : hint;
    void                *const  pool    = (void*) allocator_borrow(size, self->allocator);
    yyjson_alc  const   *const  alc     = &self->alc;

    if (pool == nullptr || !yyjson_alc_pool_init(&self->alc, pool, (size_t) size)) {
        trace_log_pop();

        return nullptr;
    }

    trace_log_pop();

    return (yyjson_alc*) alc;
}

static yyjson_alc* _json_arena_alc(Json *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->allocator == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    yyjson_alc const *const alc = &self->alc;

    self->alc.malloc    = _json_arena_malloc;
    self->alc.realloc   = _json_arena_realloc;
    self->alc.free      = _json_arena_free;
    self->alc.ctx       = self->allocator;

    trace_log_pop();

    return (yyjson_alc*) alc;
}
#endif // ARENA_IMPLEMENTATION

/* label_is_view: true only when `label`/`label_size` point into yyjson's own persisted
 * storage (a just-created key from _json_add, or an iterator's key node) - the key
 * outlives every wrapper, so a view is safe there. A get-by-name lookup passes the
 * CALLER's own search string instead (may be a transient buffer), so it stays false
 * and gets an owned copy, same as before. */
static Json* _json_wrap(
    Json *const owner, Json const *const parent, yyjson_mut_val const *const val, char const *const label, USize const label_size, USize const index, bool const label_is_view) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "owner", (void*) owner);
    error_check_null(LOG_METADATA, "val", (void*) val);

#ifdef ARENA_IMPLEMENTATION
    Json *const json = _json_new(owner->allocator);
#else
    Json *const json = _json_new();
#endif

    if (json == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    json->doc        = owner->doc;
    json->val        = (yyjson_mut_val*) val;
    json->owner      = owner;
    json->parent     = (Json*) parent;
    json->index      = index;

    if (label != nullptr && label_size > 0) {
        if (label_is_view) {
            json->label = str_init_3((char*) label, label_size);
        } else {
#ifdef ARENA_IMPLEMENTATION
            json->label = owner->allocator != nullptr
                ? str_alloc_init_static(label, label_size, owner->allocator)
                : str_init_static((char*) label, label_size);
#else
            json->label = str_init_static((char*) label, label_size);
#endif
        }

        json->label_ready = true;
    }

    json->next_wrapper = owner->next_wrapper;
    owner->next_wrapper = json;

    trace_log_pop();

    return json;
}

static JsonType _json_type(Json const *const self) {
    if (self == nullptr || self->val == nullptr) {
        return JSON_TYPE_NULL;
    }

    if (yyjson_mut_is_arr(self->val))   { return JSON_TYPE_ARRAY; }
    if (yyjson_mut_is_false(self->val)) { return JSON_TYPE_FALSE; }
    if (yyjson_mut_is_sint(self->val))  { return JSON_TYPE_INTEGER_S; }
    if (yyjson_mut_is_uint(self->val))  { return JSON_TYPE_INTEGER_U; }
    if (yyjson_mut_is_obj(self->val))   { return JSON_TYPE_OBJECT; }
    if (yyjson_mut_is_real(self->val))  { return JSON_TYPE_REAL; }
    if (yyjson_mut_is_str(self->val))   { return JSON_TYPE_STRING; }
    if (yyjson_mut_is_true(self->val))  { return JSON_TYPE_TRUE; }

    return JSON_TYPE_NULL;
}

static bool _json_type_is(Json const *const self, JsonType const type) {
    return _json_type(self) == type;
}

static yyjson_mut_val* _json_value_new(yyjson_mut_doc *const doc, JsonType const type) {
    switch (type) {
        case JSON_TYPE_ARRAY:     { return yyjson_mut_arr(doc); }
        case JSON_TYPE_FALSE:     { return yyjson_mut_false(doc); }
        case JSON_TYPE_INTEGER_S: { return yyjson_mut_sint(doc, 0); }
        case JSON_TYPE_INTEGER_U: { return yyjson_mut_uint(doc, 0); }
        case JSON_TYPE_OBJECT:    { return yyjson_mut_obj(doc); }
        case JSON_TYPE_REAL:      { return yyjson_mut_real(doc, 0); }
        case JSON_TYPE_STRING:    { return yyjson_mut_strncpy(doc, "", 0); }
        case JSON_TYPE_TRUE:      { return yyjson_mut_true(doc); }
        case JSON_TYPE_NULL:
        default:                  { return yyjson_mut_null(doc); }
    }
}

static bool _json_value_matches_raw(yyjson_mut_val const *const val, JsonType const type) {
    if (val == nullptr) {
        return false;
    }

    switch (type) {
        case JSON_TYPE_ARRAY:     { return yyjson_mut_is_arr(val); }
        case JSON_TYPE_FALSE:     { return yyjson_mut_is_false(val); }
        case JSON_TYPE_INTEGER_S: { return yyjson_mut_is_sint(val); }
        case JSON_TYPE_INTEGER_U: { return yyjson_mut_is_uint(val); }
        case JSON_TYPE_NULL:      { return yyjson_mut_is_null(val); }
        case JSON_TYPE_OBJECT:    { return yyjson_mut_is_obj(val); }
        case JSON_TYPE_REAL:      { return yyjson_mut_is_real(val); }
        case JSON_TYPE_STRING:    { return yyjson_mut_is_str(val); }
        case JSON_TYPE_TRUE:      { return yyjson_mut_is_true(val); }
        default:                  { return false; }
    }
}

static Json* _json_container(Json const *const self, JsonType const type) {
    if (self == nullptr) {
        return nullptr;
    }

    if (_json_type_is(self, type)) {
        return (Json*) self;
    }

    if (self->parent != nullptr && _json_type_is(self->parent, type)) {
        return (Json*) self->parent;
    }

    return nullptr;
}

static Json* _json_add(Json *const self, JsonType const json_type, char const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    JsonType   const   container_type  = name == nullptr ? JSON_TYPE_ARRAY : JSON_TYPE_OBJECT;
    Json        *const  parent          = _json_container(self, container_type);
    Json        *const  owner           = _json_root(parent);

    if (owner == nullptr || parent == nullptr || owner->doc == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    yyjson_mut_val *const val = _json_value_new(owner->doc, json_type);

    if (val == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    USize const index = container_type == JSON_TYPE_ARRAY
        ? (USize) yyjson_mut_arr_size(parent->val)
        : (USize) yyjson_mut_obj_size(parent->val);

    if (container_type == JSON_TYPE_ARRAY) {
        if (!yyjson_mut_arr_append(parent->val, val)) {
            trace_log_pop();

            return nullptr;
        }

        Json const *const json = _json_wrap(owner, parent, val, nullptr, 0, index, false);

        trace_log_pop();

        return (Json*) json;
    }

    yyjson_mut_val *const key = yyjson_mut_strncpy(owner->doc, name, (size_t) name_size);

    if (key == nullptr || !yyjson_mut_obj_add(parent->val, key, val)) {
        trace_log_pop();

        return nullptr;
    }

    // key is the doc's own persisted copy of the name (yyjson_mut_strncpy copied it
    // above) - it outlives every wrapper, so the label can safely be a view over it
    // rather than the caller's own (possibly transient) name argument (item 23).
    Json const *const json = _json_wrap(owner, parent, val, yyjson_mut_get_str(key), (USize) yyjson_mut_get_len(key), index, true);

    trace_log_pop();

    return (Json*) json;
}

static Json* _json_get(Json const *const self, char const *const name, USize const name_size, JsonType const type) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    // An empty key is a value, not a contract violation - refuse rather than abort
    // (value-dependent-refusal standard; item 11). self->val == nullptr can't actually
    // happen through this module's own API (every wrapper it hands out carries a val),
    // but costs nothing to check alongside the real value-dependent case (Misc 3(b)).
    if (name_size == 0 || self->val == nullptr || !yyjson_mut_is_obj(self->val)) {
        trace_log_pop();

        return nullptr;
    }

    yyjson_mut_val *const val = yyjson_mut_obj_getn(self->val, name, (size_t) name_size);

    // Type-check the raw value BEFORE allocating a wrapper (item 22): a wrong-type hit
    // now costs only the lookup, never a wrapper allocation followed by a teardown.
    if (!_json_value_matches_raw(val, type)) {
        trace_log_pop();

        return nullptr;
    }

    Json *const owner = _json_root(self);
    Json *const wrapper = _json_wrap(owner, (Json*) self, val, name, name_size, 0, false);

    trace_log_pop();

    return wrapper;
}

static Json* _json_object_get_any(Json const *const self, char const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    if (!yyjson_mut_is_obj(self->val)) {
        trace_log_pop();

        return nullptr;
    }

    yyjson_mut_val *const val = yyjson_mut_obj_getn(self->val, name, (size_t) name_size);

    if (val == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    Json const *const json = _json_wrap(_json_root(self), (Json*) self, val, name, name_size, 0, false);

    trace_log_pop();

    return (Json*) json;
}

static Json* _json_array_at(Json const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    Json const *const parent = _json_type_is(self, JSON_TYPE_ARRAY) ? self : self->parent;

    if (parent == nullptr || !yyjson_mut_is_arr(parent->val)) {
        trace_log_pop();

        return nullptr;
    }

    yyjson_mut_val *const val = yyjson_mut_arr_get(parent->val, (size_t) index);

    if (val == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    Json const *const json = _json_wrap(_json_root(parent), (Json*) parent, val, nullptr, 0, index, false);

    trace_log_pop();

    return (Json*) json;
}

static char* _json_write(Json const *const self, U8 const indentation_size, USize *const size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->val == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    char const *const data = yyjson_mut_val_write_opts(self->val, _JSON_WRITE_FLAGS(indentation_size), &_JSON_HEAP_ALC, (size_t*) size, nullptr);

    trace_log_pop();

    return (char*) data;
}

#ifdef ARENA_IMPLEMENTATION
/* Writes DIRECTLY through an arena-backed yyjson_alc rather than a heap write followed by
 * a copy-into-arena-then-free (item 14/R1 8 trade-off): one allocation instead of two. The
 * yyjson_alc is built from the PARAMETER allocator, never from self->allocator (R3 High 1):
 * yyjson_mut_val_write_opts reads the alc by value during the call and does not retain it
 * afterward, so a local, stack-lived struct is enough - unlike _json_arena_alc (used only
 * for a doc's OWN persistent allocator, which the doc DOES retain across the tree's whole
 * lifetime and so must live in self->alc). This also lets a heap tree (self->allocator ==
 * nullptr) serialize INTO an arena, and a tree in arena A serialize into arena B, instead
 * of silently writing through self's own allocator (or libc malloc for a heap tree) no
 * matter what the caller asked for. Both alloc_get_data_1 (no size needed) and _3/_4 (item
 * 4/Mid 4, view constructors) go through this - size is nullable. */
static char* _json_arena_write(Json const *const self, U8 const indentation_size, Arena *const allocator, USize *const size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    if (self->val == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    yyjson_alc const   alc     = { _json_arena_malloc, _json_arena_realloc, _json_arena_free, allocator };
    char const  *const data    = yyjson_mut_val_write_opts(self->val, _JSON_WRITE_FLAGS(indentation_size), &alc, (size_t*) size, nullptr);

    trace_log_pop();

    return (char*) data;
}
#endif // ARENA_IMPLEMENTATION

static void _json_label_cache(Json *const self, char const *const data, USize const size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    // Build the replacement BEFORE releasing the old label (R3 High 2 / memsec HIGH): a
    // caller may pass a VIEW over the CURRENT label itself - json_get_label always hands
    // back a view (Str l = json_get_label(node); json_set_label_3(node, &l)) - so `data`
    // can alias self->label's own buffer. Freeing self->label first and only then copying
    // from `data` would read already-freed memory in exactly that shape.
#ifdef ARENA_IMPLEMENTATION
    Str const replacement = self->allocator != nullptr
        ? str_alloc_init_static(data, size, self->allocator)
        : str_init_static((char*) data, size);

    if (self->label_ready && self->allocator == nullptr) {
        str_uninit(&self->label);
    }
#else
    Str const replacement = str_init_static((char*) data, size);

    if (self->label_ready) {
        str_uninit(&self->label);
    }
#endif // ARENA_IMPLEMENTATION

    self->label = replacement;
    self->label_ready = true;

    trace_log_pop();
}

#ifdef ARENA_IMPLEMENTATION
static Json* _json_from(char const *const data, USize const data_size, Arena *const allocator, JsonError *const error)
#else
static Json* _json_from(char const *const data, USize const data_size, JsonError *const error)
#endif // ARENA_IMPLEMENTATION
{
    trace_log_push(LOG_METADATA);

    // An empty (size 0) body is a VALUE, not a contract violation - refuse before the
    // null check runs, so a genuinely-null-because-empty buffer (an empty Str/String's
    // data pointer, per string.h) never reaches error_check_null and aborts (item 1).
    // A null pointer with a nonzero size is still a real contract violation and aborts.
    if (data_size == 0) {
        if (error != nullptr) {
            error->position = 0;
            error->message  = "empty input";
        }

        trace_log_pop();

        return nullptr;
    }

    error_check_null(LOG_METADATA, "data", (void*) data);

#ifdef ARENA_IMPLEMENTATION
    // R2 item 8: measured (scratch probe against yyjson_alc_pool_init directly) - yyjson's
    // OWN max-usage figure already parses a 100 KB/18.5k-element body AND a 20k-element
    // body with a 1x multiplier and no margin trouble; the previous 4x was ~4x over-
    // provisioned for this step specifically (json_arena_size folds in the real margin).
    USize           const   hint    = (USize) yyjson_read_max_memory_usage((size_t) data_size, YYJSON_READ_NOFLAG) + _JSON_ARENA_POOL_BASE;
    Json            *const  root    = _json_new(allocator);

    if (root == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    yyjson_alc      *const  alc     = allocator == nullptr ? nullptr : _json_alc(root, hint);

    // A caller-supplied arena that cannot even satisfy the read pool borrow must REFUSE,
    // never fall through to yyjson_read_opts's own default (libc malloc/free) allocator -
    // that would silently succeed off-arena and leak the whole read doc (~13x body bytes)
    // per request (memsec HIGH, R2 High 3). The arena's own borrowed memory up to this
    // point stays unreclaimed until the arena resets, same as every other early-refusal
    // path in this function.
    if (allocator != nullptr && alc == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    yyjson_read_err         err     = DEFAULT_INITIALIZATION;
    yyjson_doc      *const  doc     = yyjson_read_opts((char*) data, (size_t) data_size, YYJSON_READ_NOFLAG, alc, &err);
#else
    Json            *const  root    = _json_new();

    if (root == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    yyjson_read_err         err     = DEFAULT_INITIALIZATION;
    yyjson_doc      *const  doc     = yyjson_read_opts((char*) data, (size_t) data_size, YYJSON_READ_NOFLAG, nullptr, &err);
#endif // ARENA_IMPLEMENTATION

    if (doc == nullptr) {
        if (error != nullptr) {
            error->position = (USize) err.pos;
            error->message  = err.msg;
        }

#ifdef ARENA_IMPLEMENTATION
        if (allocator == nullptr) {
            memory_free(root);
        }
#else
        memory_free(root);
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return nullptr;
    }

#ifdef ARENA_IMPLEMENTATION
    root->doc = yyjson_mut_doc_new(allocator == nullptr ? nullptr : _json_arena_alc(root));
#else
    root->doc = yyjson_mut_doc_new(nullptr);
#endif // ARENA_IMPLEMENTATION
    root->val = yyjson_val_mut_copy(root->doc, yyjson_doc_get_root(doc));

    // yyjson_val_mut_copy returns nullptr on read-pool exhaustion (a mutable copy
    // needs roughly as much room again as the read pool it copies from - json_arena_size
    // sizes for both, but a caller-supplied arena can still be too small). Release the
    // read doc and report failure rather than handing back a root whose val is null,
    // which every getter afterwards would dereference.
    if (root->val == nullptr) {
#ifdef ARENA_IMPLEMENTATION
        if (allocator == nullptr) {
            yyjson_mut_doc_free(root->doc);
            yyjson_doc_free(doc);
            memory_free(root);
        }
#else
        yyjson_mut_doc_free(root->doc);
        yyjson_doc_free(doc);
        memory_free(root);
#endif // ARENA_IMPLEMENTATION

        trace_log_pop();

        return nullptr;
    }

    yyjson_mut_doc_set_root(root->doc, root->val);

#ifdef ARENA_IMPLEMENTATION
    if (allocator == nullptr) {
        yyjson_doc_free(doc);
    }
#else
    yyjson_doc_free(doc);
#endif // ARENA_IMPLEMENTATION

    root->owner = root;

    trace_log_pop();

    return root;
}

#ifdef ARENA_IMPLEMENTATION
Json* json_alloc_from_1(char const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Json const *const json = json_alloc_from_2(data, char_length(data), allocator);

    trace_log_pop();

    return (Json*) json;
}

Json* json_alloc_from_2(char const *const data, USize const data_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    // data_size == 0 is checked before the null check (item 1): an empty Str/String's
    // data pointer is null, and that is a legal empty value, not a contract violation.
    if (data_size == 0) {
        trace_log_pop();

        return nullptr;
    }

    error_check_null(LOG_METADATA, "data", (void*) data);

    Json const *const json = _json_from(data, data_size, allocator, nullptr);

    trace_log_pop();

    return (Json*) json;
}

Json* json_alloc_from_3(Str const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Json const *const json = json_alloc_from_2(str_get_data(data), str_get_size(data), allocator);

    trace_log_pop();

    return (Json*) json;
}

Json* json_alloc_from_4(String const *const data, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Json const *const json = json_alloc_from_2(string_get_data(data), string_get_size(data), allocator);

    trace_log_pop();

    return (Json*) json;
}

Json* json_alloc_from_try_2(char const *const data, USize const data_size, JsonError *const error, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    // error is intentionally NOT null-checked - see json_from_try_2 (Misc 23).
    // Arena twin of json_from_try_2 (R2 High 2): 107 of 123 in-tree parse call sites are
    // the arena form (json_alloc_from_2) against 16 for the heap form, so the error-
    // reporting path needs to reach them too, not only the 16.
    Json const *const json = _json_from(data, data_size, allocator, error);

    trace_log_pop();

    return (Json*) json;
}

char* json_alloc_get_data_1(Json const *const self, U8 const indentation_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    char *const data = _json_arena_write(self, indentation_size, allocator, nullptr);

    trace_log_pop();

    return data;
}

Str json_alloc_get_data_3(Json const *const self, U8 const indentation_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    USize        size    = 0;
    char *const  data    = _json_arena_write(self, indentation_size, allocator, &size);

    if (data == nullptr) {
        Str const empty = str_alloc_init_1(allocator);

        trace_log_pop();

        return empty;
    }

    // A VIEW over the buffer _json_arena_write just wrote directly into the arena (item
    // 4/Mid 4): one arena allocation instead of write-then-copy-then-free. str_alloc_init_3
    // (not str_init_3) so the Str's own `allocator` field carries the arena too, per str.h's
    // ownership contract - str_uninit on the result is still a no-op (owned stays false),
    // same rule as json_get_label (memsec LOW).
    Str const str = str_alloc_init_3(data, size, allocator);

    trace_log_pop();

    return str;
}

String json_alloc_get_data_4(Json const *const self, U8 const indentation_size, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    USize        size    = 0;
    char *const  data    = _json_arena_write(self, indentation_size, allocator, &size);

    if (data == nullptr) {
        String const empty = string_alloc_init_1(allocator);

        trace_log_pop();

        return empty;
    }

    // View over the arena-owned buffer (item 4/Mid 4) - string_alloc_init_4 (not
    // string_init_4) so `allocator` is set too; see json_alloc_get_data_3 (memsec LOW).
    String const string = string_alloc_init_4(data, size, allocator);

    trace_log_pop();

    return string;
}

Json* json_alloc_load_1(char const *const file_name, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    String                  data    = file_read_to_string(file_name);

    // An unreadable, missing or empty document reports as "no JSON" rather than
    // aborting. file_read_to_string now returns an empty String for a missing
    // or zero-byte file, and json_from_* runs error_check_null on the buffer -
    // so without this, fixing the file layer simply moved the abort one frame
    // up the stack.
    if (string_get_size(&data) == 0) {
        string_uninit(&data);

        trace_log_pop();

        return nullptr;
    }

    Json    const   *const  json    = json_alloc_from_4(&data, allocator);

    string_uninit(&data);

    trace_log_pop();

    return (Json*) json;
}

Json* json_alloc_load_3(Str const *const file_name, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Json const *const json = json_alloc_load_1(str_get_data(file_name), allocator);

    trace_log_pop();

    return (Json*) json;
}

Json* json_alloc_load_4(String const *const file_name, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    Json const *const json = json_alloc_load_1(string_get_data(file_name), allocator);

    trace_log_pop();

    return (Json*) json;
}

#endif // ARENA_IMPLEMENTATION

USize json_arena_size(USize const byte_count) {
    trace_log_push(LOG_METADATA);

    // Overflow guard (R3 High 3): both terms below are roughly 13x byte_count: a
    // byte_count large enough to overflow USize during that multiply cannot be sized into
    // a real arena on any real machine, so 0 signals "too large to arena" - documented on
    // the header as the only value this function ever returns for a refusal.
    if (byte_count > (USIZE_MAX - 4 * _JSON_ARENA_POOL_BASE) / (2 * _JSON_ARENA_BYTE_FACTOR)) {
        trace_log_pop();

        return 0;
    }

    // Mirrors _json_from's own sizing: the immutable read pool (hint - yyjson's own max
    // usage figure needs no safety multiplier, measured directly against yyjson_read_opts;
    // R2 item 8), THEN the mutable tree copy made on top of it, THEN headroom for the
    // wrapper handles a caller's getters will allocate (item 19/40 - folds
    // _JSON_ARENA_POOL_BASE, retires JSON_ARENA_SET_SIZE).
    //
    // The copy term is 13xN, not the previous 6x (R3 High 3): yyjson_val_mut_copy
    // allocates one 24-byte yyjson_mut_val per value copied, in ONE chunk sized by the
    // value count, plus a string-pool copy of every string. yyjson's OWN worst-case value
    // count is N/2+1 (a body of nothing but 2-byte scalars, "0,0,0,..."), which alone is
    // 12xN for the value chunk; +1xN of headroom for string bytes gives 13xN. The old 6x
    // was measured off one body shape (a 100 KB run of ascending integers) and under-sized
    // the attacker-cheap all-scalars case, aborting the arena borrow in the checked build.
    USize const read_pool     = (USize) yyjson_read_max_memory_usage((size_t) byte_count, YYJSON_READ_NOFLAG) + _JSON_ARENA_POOL_BASE;
    USize const mutable_copy  = byte_count * _JSON_ARENA_BYTE_FACTOR + _JSON_ARENA_POOL_BASE;
    USize const size          = read_pool + mutable_copy + _JSON_ARENA_POOL_BASE;

    trace_log_pop();

    return size;
}

Json* json_array_add(Json *const self, JsonType const json_type) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    Json const *const json = _json_add(self, json_type, nullptr, 0);

    trace_log_pop();

    return (Json*) json;
}

Json* json_array_at(Json const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    Json const *const json = _json_array_at(self, index);

    trace_log_pop();

    return (Json*) json;
}

Json* json_array_get_next(Json const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // O(1) per step (item 4): yyjson's mutable array is a circular linked list whose
    // container val holds the TAIL in uni.ptr, so tail->next is the first element and
    // an element's own ->next is its successor - wrapping back to the first element
    // marks the end. No walk from the start, unlike the old _json_array_at(index+1).
    if (_json_type_is(self, JSON_TYPE_ARRAY)) {
        if (self->val == nullptr || !yyjson_mut_is_arr(self->val) || yyjson_mut_arr_size(self->val) == 0) {
            trace_log_pop();

            return nullptr;
        }

        yyjson_mut_val *const tail  = (yyjson_mut_val*) self->val->uni.ptr;
        yyjson_mut_val *const first = tail->next;
        Json const *const json = _json_wrap(_json_root(self), self, first, nullptr, 0, 0, false);

        trace_log_pop();

        return (Json*) json;
    }

    if (self->parent != nullptr && _json_type_is(self->parent, JSON_TYPE_ARRAY)) {
        yyjson_mut_val *const tail  = (yyjson_mut_val*) self->parent->val->uni.ptr;
        yyjson_mut_val *const first = tail->next;
        yyjson_mut_val *const next  = self->val->next;

        if (next == first) {
            trace_log_pop();

            return nullptr;
        }

        Json const *const json = _json_wrap(_json_root(self->parent), self->parent, next, nullptr, 0, self->index + 1, false);

        trace_log_pop();

        return (Json*) json;
    }

    trace_log_pop();

    return nullptr;
}

USize json_array_get_size(Json const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // Reports SELF's size, never a parent's (item 26 ruling): calling this on a
    // non-array node is 0, not the enclosing array's count.
    if (self->val == nullptr || !yyjson_mut_is_arr(self->val)) {
        trace_log_pop();

        return 0;
    }

    USize const size = (USize) yyjson_mut_arr_size(self->val);

    trace_log_pop();

    return size;
}

bool json_array_remove(Json *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (self->val == nullptr || !yyjson_mut_is_arr(self->val)) {
        trace_log_pop();

        return false;
    }

    yyjson_mut_val *const removed = yyjson_mut_arr_remove(self->val, (size_t) index);

    trace_log_pop();

    return removed != nullptr;
}

Json* json_at_1(Json const *const self, char const *const search) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "search", (void*) search);

    Json const *const json = json_at_2(self, search, char_length(search));

    trace_log_pop();

    return (Json*) json;
}

Json* json_at_2(Json const *const self, char const *const search, USize const search_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "search", (void*) search);

    if (search_size == 0) {
        trace_log_pop();

        return nullptr;
    }

    Json *current = (Json*) self;
    USize i = 0;

    while (i < search_size && current != nullptr) {
        if (search[i] != '[') {
            i += 1;

            continue;
        }

        i += 1;

        if (i >= search_size) {
            // Trailing unterminated '[' - malformed (item 24).
            trace_log_pop();

            return nullptr;
        }

        if (search[i] == '\'') {
            i += 1;
            USize const begin = i;

            while (i < search_size && search[i] != '\'') {
                i += 1;
            }

            if (i >= search_size) {
                // Unterminated quoted key: the closing ' never arrived.
                trace_log_pop();

                return nullptr;
            }

            USize const key_size = i - begin;

            i += 1; // past the closing '

            if (i >= search_size || search[i] != ']') {
                trace_log_pop();

                return nullptr;
            }

            current = _json_object_get_any(current, search + begin, key_size);

            i += 1; // past the closing ]
        } else if (search[i] >= '0' && search[i] <= '9') {
            USize value = 0;

            while (i < search_size && search[i] >= '0' && search[i] <= '9') {
                USize const digit = (USize) (search[i] - '0');

                if (value > (USIZE_MAX - digit) / 10) {
                    // The index overflows USize - refuse rather than wrap.
                    trace_log_pop();

                    return nullptr;
                }

                value = value * 10 + digit;
                i += 1;
            }

            if (i >= search_size || search[i] != ']') {
                trace_log_pop();

                return nullptr;
            }

            current = _json_array_at(current, value);

            i += 1; // past the closing ]
        } else {
            // Neither a quoted key nor a digit index follows '[' (e.g. [abc]).
            trace_log_pop();

            return nullptr;
        }
    }

    trace_log_pop();

    return current;
}

void json_delete(Json **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (*self == nullptr) {
        trace_log_pop();

        return;
    }

    Json *const root = _json_root(*self);

#ifdef ARENA_IMPLEMENTATION
    if (root->allocator == nullptr) {
#endif // ARENA_IMPLEMENTATION
        Json *wrapper = root->next_wrapper;

        while (wrapper != nullptr) {
            Json *const next = wrapper->next_wrapper;

            if (wrapper->label_ready) {
                str_uninit(&wrapper->label);
            }

            memory_free(wrapper);

            wrapper = next;
        }

        if (root->label_ready) {
            str_uninit(&root->label);
        }

        if (root->doc != nullptr) {
            yyjson_mut_doc_free(root->doc);
        }

        memory_free(root);
#ifdef ARENA_IMPLEMENTATION
    }
#endif // ARENA_IMPLEMENTATION

    *self = nullptr;

    trace_log_pop();
}

Json* json_from_1(char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    Json const *const json = json_from_2(data, char_length(data));

    trace_log_pop();

    return (Json*) json;
}

Json* json_from_2(char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    // data_size == 0 is checked before the null check (item 1) - see json_alloc_from_2.
    if (data_size == 0) {
        trace_log_pop();

        return nullptr;
    }

    error_check_null(LOG_METADATA, "data", (void*) data);

#ifdef ARENA_IMPLEMENTATION
    Json const *const json = _json_from(data, data_size, nullptr, nullptr);
#else
    Json const *const json = _json_from(data, data_size, nullptr);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return (Json*) json;
}

Json* json_from_3(Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    Json const *const json = json_from_2(str_get_data(data), str_get_size(data));

    trace_log_pop();

    return (Json*) json;
}

Json* json_from_4(String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "data", (void*) data);

    Json const *const json = json_from_2(string_get_data(data), string_get_size(data));

    trace_log_pop();

    return (Json*) json;
}

Json* json_from_try_2(char const *const data, USize const data_size, JsonError *const error) {
    trace_log_push(LOG_METADATA);

    // error is intentionally NOT null-checked (Misc 23): _json_from already tolerates a
    // null error throughout (every write is `if (error != nullptr) ...`), so this stays a
    // drop-in for json_from_2 when the caller does not want the failure detail.

    // Delegates entirely to _json_from, which already checks data_size before data
    // (item 1) - duplicating that check here would have to repeat the same ordering
    // to stay correct, so there is one source of truth instead of two.
#ifdef ARENA_IMPLEMENTATION
    Json const *const json = _json_from(data, data_size, nullptr, error);
#else
    Json const *const json = _json_from(data, data_size, error);
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_1(Json const *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_2(self, name, char_length(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_2(Json const *const self, char const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // An EMPTY Str/String (null data, size 0) refuses instead of reaching the null check
    // below - it carries no key to look up (R3 High 3). A REAL non-null "" pointer
    // (name_size == 0 but name != nullptr) is untouched: the any-type getters
    // intentionally look "" up like any other key and find it if the object has one
    // (documented header contract) - only a genuinely absent buffer refuses.
    if (name == nullptr && name_size == 0) {
        trace_log_pop();

        return nullptr;
    }

    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = _json_object_get_any(self, name, name_size);

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_3(Json const *const self, Str const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_2(self, str_get_data(name), str_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_4(Json const *const self, String const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_2(self, string_get_data(name), string_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_array_1(Json const *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_array_2(self, name, char_length(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_array_2(Json const *const self, char const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // Empty key refuses BEFORE the null check (R3 High 3): an EMPTY Str/String's data
    // pointer is null, and that is a legal empty VALUE, not a contract violation - the
    // typed getters already refuse any "" key (real or empty-view), so this only moves
    // WHERE the refusal happens.
    if (name_size == 0) {
        trace_log_pop();

        return nullptr;
    }

    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = _json_get(self, name, name_size, JSON_TYPE_ARRAY);

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_array_3(Json const *const self, Str const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_array_2(self, str_get_data(name), str_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_array_4(Json const *const self, String const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_array_2(self, string_get_data(name), string_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

char* json_get_data_1(Json const *const self, U8 const indentation_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    char const *const data = _json_write(self, indentation_size, nullptr);

    trace_log_pop();

    return (char*) data;
}

Str json_get_data_3(Json const *const self, U8 const indentation_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize        size    = 0;
    char *const  data    = _json_write(self, indentation_size, &size);

    if (data == nullptr) {
        Str const empty = str_init_1();

        trace_log_pop();

        return empty;
    }

    // Adopts the writer's own CFW-heap buffer (item 14) instead of str_init_static's deep
    // copy followed by a free of the original - one allocation, not two.
    Str          str     = str_init_1();
    char *       owned   = data;

    str_move_2(&str, &owned, size);

    trace_log_pop();

    return str;
}

String json_get_data_4(Json const *const self, U8 const indentation_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize        size    = 0;
    char *const  data    = _json_write(self, indentation_size, &size);

    if (data == nullptr) {
        String const empty = string_init_1();

        trace_log_pop();

        return empty;
    }

    // Adopts the writer's own CFW-heap buffer (item 14) instead of string_init_static's
    // deep copy followed by a free of the original - one allocation, not two.
    String       string  = string_init_1();
    char *       owned   = data;

    string_move_2(&string, &owned, size);

    trace_log_pop();

    return string;
}

Str json_get_label(Json const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    Str str = self->label;

    // Always handed back as a VIEW regardless of the node's own internal ownership
    // (item 4): an accidental str_uninit by the caller is then a no-op instead of a
    // double-free at json_delete - the label is released only there, never by the caller.
    str.owned = false;

    trace_log_pop();

    return str;
}

Json* json_get_number_float_1(Json const *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_number_float_2(self, name, char_length(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_number_float_2(Json const *const self, char const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // Empty key refuses before the null check (R3 High 3) - see json_get_array_2.
    if (name_size == 0) {
        trace_log_pop();

        return nullptr;
    }

    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = _json_get(self, name, name_size, JSON_TYPE_REAL);

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_number_float_3(Json const *const self, Str const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_number_float_2(self, str_get_data(name), str_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_number_float_4(Json const *const self, String const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_number_float_2(self, string_get_data(name), string_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_number_int_1(Json const *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_number_int_2(self, name, char_length(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_number_int_2(Json const *const self, char const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // Empty key refuses before the null check (R3 High 3) - see json_get_array_2.
    if (name_size == 0) {
        trace_log_pop();

        return nullptr;
    }

    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = _json_get(self, name, name_size, JSON_TYPE_INTEGER_S);

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_number_int_3(Json const *const self, Str const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_number_int_2(self, str_get_data(name), str_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_number_int_4(Json const *const self, String const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_number_int_2(self, string_get_data(name), string_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_number_uint_1(Json const *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_number_uint_2(self, name, char_length(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_number_uint_2(Json const *const self, char const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // Empty key refuses before the null check (R3 High 3) - see json_get_array_2.
    if (name_size == 0) {
        trace_log_pop();

        return nullptr;
    }

    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = _json_get(self, name, name_size, JSON_TYPE_INTEGER_U);

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_number_uint_3(Json const *const self, Str const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_number_uint_2(self, str_get_data(name), str_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_number_uint_4(Json const *const self, String const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_number_uint_2(self, string_get_data(name), string_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_object_1(Json const *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_object_2(self, name, char_length(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_object_2(Json const *const self, char const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // Empty key refuses before the null check (R3 High 3) - see json_get_array_2.
    if (name_size == 0) {
        trace_log_pop();

        return nullptr;
    }

    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = _json_get(self, name, name_size, JSON_TYPE_OBJECT);

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_object_3(Json const *const self, Str const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_object_2(self, str_get_data(name), str_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_object_4(Json const *const self, String const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_object_2(self, string_get_data(name), string_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

USize json_get_size(Json const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (yyjson_mut_is_arr(self->val)) {
        USize const size = (USize) yyjson_mut_arr_size(self->val);

        trace_log_pop();

        return size;
    }

    if (yyjson_mut_is_obj(self->val)) {
        USize const size = (USize) yyjson_mut_obj_size(self->val);

        trace_log_pop();

        return size;
    }

    if (yyjson_mut_is_str(self->val)) {
        USize const size = (USize) yyjson_mut_get_len(self->val);

        trace_log_pop();

        return size;
    }

    trace_log_pop();

    return 0;
}

Json* json_get_string_1(Json const *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_string_2(self, name, char_length(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_string_2(Json const *const self, char const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // Empty key refuses before the null check (R3 High 3) - see json_get_array_2.
    if (name_size == 0) {
        trace_log_pop();

        return nullptr;
    }

    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = _json_get(self, name, name_size, JSON_TYPE_STRING);

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_string_3(Json const *const self, Str const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_string_2(self, str_get_data(name), str_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_get_string_4(Json const *const self, String const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_get_string_2(self, string_get_data(name), string_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

JsonType json_get_type(Json const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    JsonType const type = _json_type(self);

    trace_log_pop();

    return type;
}

bool json_get_value_bool(Json const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const value = yyjson_mut_get_bool(self->val);

    trace_log_pop();

    return value;
}

FSize json_get_value_number_float(Json const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    FSize const value = (FSize) yyjson_mut_get_real(self->val);

    trace_log_pop();

    return value;
}

ISize json_get_value_number_int(Json const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    ISize const value = (ISize) yyjson_mut_get_sint(self->val);

    trace_log_pop();

    return value;
}

USize json_get_value_number_uint(Json const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const value = (USize) yyjson_mut_get_uint(self->val);

    trace_log_pop();

    return value;
}

char* json_get_value_string_1(Json const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!yyjson_mut_is_str(self->val)) {
        trace_log_pop();

        return nullptr;
    }

    // yyjson_mut_get_str is stable for the tree's lifetime and NUL-terminated for
    // every string value, including an empty one ("" rather than nullptr) - no per-
    // node cache needed (item 3: the cache produced nullptr for "" and a dangling
    // pointer trap on a second call over the same node; both are gone).
    char *const value = (char*) yyjson_mut_get_str(self->val);

    trace_log_pop();

    return value;
}

Str json_get_value_string_3(Json const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!yyjson_mut_is_str(self->val)) {
        Str const empty = str_init_1();

        trace_log_pop();

        return empty;
    }

    USize const size = (USize) yyjson_mut_get_len(self->val);
    Str const str = size == 0 ? str_init_1() : str_init_static(yyjson_mut_get_str(self->val), size);

    trace_log_pop();

    return str;
}

String json_get_value_string_4(Json const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    if (!yyjson_mut_is_str(self->val)) {
        String const empty = string_init_1();

        trace_log_pop();

        return empty;
    }

    USize const size = (USize) yyjson_mut_get_len(self->val);
    String const string = size == 0 ? string_init_1() : string_init_static(yyjson_mut_get_str(self->val), size);

    trace_log_pop();

    return string;
}

bool json_is_bool(Json const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const result = _json_type_is(self, JSON_TYPE_TRUE) || _json_type_is(self, JSON_TYPE_FALSE);

    trace_log_pop();

    return result;
}

Json* json_load_1(char const *const file_name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    String                  data    = file_read_to_string(file_name);

    // An unreadable, missing or empty document reports as "no JSON" rather than
    // aborting. file_read_to_string now returns an empty String for a missing
    // or zero-byte file, and json_from_* runs error_check_null on the buffer -
    // so without this, fixing the file layer simply moved the abort one frame
    // up the stack.
    if (string_get_size(&data) == 0) {
        string_uninit(&data);

        trace_log_pop();

        return nullptr;
    }

    Json    const   *const  json    = json_from_4(&data);

    string_uninit(&data);

    trace_log_pop();

    return (Json*) json;
}

Json* json_load_3(Str const *const file_name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    Json const *const json = json_load_1(str_get_data(file_name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_load_4(String const *const file_name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "file_name", (void*) file_name);

    Json const *const json = json_load_1(string_get_data(file_name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_object_add_1(Json *const self, JsonType const json_type, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_object_add_2(self, json_type, name, char_length(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_object_add_2(Json *const self, JsonType const json_type, char const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // An EMPTY Str/String key (null data, size 0) is a legal "" key to ADD, same as a
    // real char* "" (header: "json_object_add_2..4 can itself add an '' key") -
    // substitute the literal so the null-pointer contract check never fires on this
    // legitimate shape (R3 High 3). Unlike a LOOKUP, add never refuses on size 0.
    char const *const safe_name = name_size == 0 ? "" : name;

    error_check_null(LOG_METADATA, "name", (void*) safe_name);

    Json const *const json = _json_add(self, json_type, safe_name, name_size);

    trace_log_pop();

    return (Json*) json;
}

Json* json_object_add_3(Json *const self, JsonType const json_type, Str const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_object_add_2(self, json_type, str_get_data(name), str_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_object_add_4(Json *const self, JsonType const json_type, String const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json const *const json = json_object_add_2(self, json_type, string_get_data(name), string_get_size(name));

    trace_log_pop();

    return (Json*) json;
}

Json* json_object_add_bool(Json *const self, char const *const name, bool const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json *const node = json_object_add_1(self, JSON_TYPE_TRUE, name);

    // self may not be (or have) an object parent - _json_add already refuses that with
    // nullptr; without this guard the set_* call right below would deref it (memsec HIGH,
    // R2 Mid 7 / task item 1: json_from_1("[1,2]") then json_object_add_bool(root,...)).
    if (node == nullptr) {
        trace_log_pop();

        return nullptr;
    }

    json_set_bool(node, value);

    trace_log_pop();

    return node;
}

Json* json_object_add_null(Json *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    // No node == nullptr guard here (unlike its bool/number/string siblings, R2 Mid 7):
    // there is no json_set_* for JSON_TYPE_NULL to call afterward, so there is nothing to
    // dereference. Deliberate - do not "fix" this by copying the sibling guard (Misc 16).
    Json *const node = json_object_add_1(self, JSON_TYPE_NULL, name);

    trace_log_pop();

    return node;
}

Json* json_object_add_number_float(Json *const self, char const *const name, FSize const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json *const node = json_object_add_1(self, JSON_TYPE_REAL, name);

    if (node == nullptr) { // see json_object_add_bool's guard (memsec HIGH, R2 Mid 7)
        trace_log_pop();

        return nullptr;
    }

    json_set_number_float(node, value);

    trace_log_pop();

    return node;
}

Json* json_object_add_number_float_precision(Json *const self, char const *const name, FSize const value, U8 const precision) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json *const node = json_object_add_1(self, JSON_TYPE_REAL, name);

    if (node == nullptr) { // see json_object_add_bool's guard (memsec HIGH, R2 Mid 7)
        trace_log_pop();

        return nullptr;
    }

    json_set_number_float_precision(node, value, precision);

    trace_log_pop();

    return node;
}

Json* json_object_add_number_int(Json *const self, char const *const name, ISize const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json *const node = json_object_add_1(self, JSON_TYPE_INTEGER_S, name);

    if (node == nullptr) { // see json_object_add_bool's guard (memsec HIGH, R2 Mid 7)
        trace_log_pop();

        return nullptr;
    }

    json_set_number_int(node, value);

    trace_log_pop();

    return node;
}

Json* json_object_add_number_uint(Json *const self, char const *const name, USize const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    Json *const node = json_object_add_1(self, JSON_TYPE_INTEGER_U, name);

    if (node == nullptr) { // see json_object_add_bool's guard (memsec HIGH, R2 Mid 7)
        trace_log_pop();

        return nullptr;
    }

    json_set_number_uint(node, value);

    trace_log_pop();

    return node;
}

Json* json_object_add_string_1(Json *const self, char const *const name, char const *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "value", (void*) value);

    Json *const node = json_object_add_1(self, JSON_TYPE_STRING, name);

    if (node == nullptr) { // see json_object_add_bool's guard (memsec HIGH, R2 Mid 7)
        trace_log_pop();

        return nullptr;
    }

    json_set_string_1(node, value);

    trace_log_pop();

    return node;
}

Json* json_object_add_string_2(Json *const self, char const *const name, char const *const value, USize const value_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    // An EMPTY Str/String value (null data, size 0) is a legal "" string value -
    // substitute the literal so the null-pointer contract check never fires on this
    // legitimate shape (R3 High 3); json_set_string_2 does the same substitution
    // independently, so this only avoids the abort one frame earlier.
    char const *const safe_value = value_size == 0 ? "" : value;

    error_check_null(LOG_METADATA, "value", (void*) safe_value);

    Json *const node = json_object_add_1(self, JSON_TYPE_STRING, name);

    if (node == nullptr) { // see json_object_add_bool's guard (memsec HIGH, R2 Mid 7)
        trace_log_pop();

        return nullptr;
    }

    json_set_string_2(node, safe_value, value_size);

    trace_log_pop();

    return node;
}

Json* json_object_add_string_3(Json *const self, char const *const name, Str const *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "value", (void*) value);

    Json *const node = json_object_add_1(self, JSON_TYPE_STRING, name);

    if (node == nullptr) { // see json_object_add_bool's guard (memsec HIGH, R2 Mid 7)
        trace_log_pop();

        return nullptr;
    }

    json_set_string_3(node, value);

    trace_log_pop();

    return node;
}

Json* json_object_add_string_4(Json *const self, char const *const name, String const *const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "value", (void*) value);

    Json *const node = json_object_add_1(self, JSON_TYPE_STRING, name);

    if (node == nullptr) { // see json_object_add_bool's guard (memsec HIGH, R2 Mid 7)
        trace_log_pop();

        return nullptr;
    }

    json_set_string_4(node, value);

    trace_log_pop();

    return node;
}

Json* json_object_get_next(Json const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // O(1) per step (item 4): the mutable object threads key->val->key->val... on the
    // SAME next pointer, with uni.ptr on the container holding the tail KEY - so a
    // key's own val->next is the next key, and the first key is tail_key->next->next.
    // Wrapping back to the first key marks the end. No re-walk from the start, unlike
    // the old _json_object_at(index+1) (which drove the yyjson iterator from index 0
    // every call).
    if (_json_type_is(self, JSON_TYPE_OBJECT)) {
        if (self->val == nullptr || !yyjson_mut_is_obj(self->val) || yyjson_mut_obj_size(self->val) == 0) {
            trace_log_pop();

            return nullptr;
        }

        yyjson_mut_val *const tail_key  = (yyjson_mut_val*) self->val->uni.ptr;
        yyjson_mut_val *const first_key = tail_key->next->next;
        Json const *const json = _json_wrap(_json_root(self), self, first_key->next, yyjson_mut_get_str(first_key), (USize) yyjson_mut_get_len(first_key), 0, true);

        trace_log_pop();

        return (Json*) json;
    }

    if (self->parent != nullptr && _json_type_is(self->parent, JSON_TYPE_OBJECT)) {
        yyjson_mut_val *const tail_key  = (yyjson_mut_val*) self->parent->val->uni.ptr;
        yyjson_mut_val *const first_key = tail_key->next->next;
        yyjson_mut_val *const next_key  = self->val->next;

        if (next_key == first_key) {
            trace_log_pop();

            return nullptr;
        }

        Json const *const json = _json_wrap(_json_root(self->parent), self->parent, next_key->next, yyjson_mut_get_str(next_key), (USize) yyjson_mut_get_len(next_key), self->index + 1, true);

        trace_log_pop();

        return (Json*) json;
    }

    trace_log_pop();

    return nullptr;
}

bool json_object_remove_1(Json *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    bool const result = json_object_remove_2(self, name, char_length(name));

    trace_log_pop();

    return result;
}

bool json_object_remove_2(Json *const self, char const *const name, USize const name_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // Empty key refuses before the null check (R3 High 3), matching the typed getters'
    // existing "not found" semantics for an empty key.
    if (name_size == 0) {
        trace_log_pop();

        return false;
    }

    error_check_null(LOG_METADATA, "name", (void*) name);

    if (self->val == nullptr || !yyjson_mut_is_obj(self->val)) {
        trace_log_pop();

        return false;
    }

    yyjson_mut_val *const removed = yyjson_mut_obj_remove_keyn(self->val, name, (size_t) name_size);

    trace_log_pop();

    return removed != nullptr;
}

void json_print(Json const *const self, U8 const indentation_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    char *const data = json_get_data_1(self, indentation_size);

    if (data != nullptr) {
        printf("%s\n", data);

        // The buffer comes from CFW's own heap allocator (_JSON_HEAP_ALC), not libc's -
        // release with memory_free, never bare free (R2 Mid 5; matches json.h's rule).
        memory_free(data);
    }

    trace_log_pop();
}

bool json_read_bool(Json const *const self, char const *const name, bool *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "out", (void*) out);

    // Raw-val probe (R2 High 9): json_get_1 is the any-TYPE getter - it wraps a handle
    // on ANY match regardless of type, so a same-key wrong-type miss used to pay for a
    // wrapper allocation anyway. Testing the raw yyjson_mut_val directly means a miss
    // (absent OR wrong type) never allocates a handle at all, and a hit needs none either
    // - only the primitive value is read out.
    if (self->val == nullptr || !yyjson_mut_is_obj(self->val)) {
        trace_log_pop();

        return false;
    }

    yyjson_mut_val *const val = yyjson_mut_obj_getn(self->val, name, (size_t) char_length(name));

    if (val == nullptr || !yyjson_mut_is_bool(val)) {
        trace_log_pop();

        return false;
    }

    *out = yyjson_mut_get_bool(val);

    trace_log_pop();

    return true;
}

bool json_read_int(Json const *const self, char const *const name, ISize *const out) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);
    error_check_null(LOG_METADATA, "out", (void*) out);

    // Raw-val probe (R2 High 9), same shape as json_read_bool: yyjson_mut_is_int covers
    // both the signed and unsigned subtypes in one raw check, and no wrapper handle is
    // ever allocated - not on a miss, not on a wrong-type hit, not even on success.
    if (self->val == nullptr || !yyjson_mut_is_obj(self->val)) {
        trace_log_pop();

        return false;
    }

    yyjson_mut_val *const val = yyjson_mut_obj_getn(self->val, name, (size_t) char_length(name));

    if (val == nullptr || !yyjson_mut_is_int(val)) {
        trace_log_pop();

        return false;
    }

    if (yyjson_mut_is_uint(val)) {
        uint64_t const raw = yyjson_mut_get_uint(val);

        if (raw > (uint64_t) ISIZE_MAX) {
            trace_log_pop();

            return false;
        }

        *out = (ISize) raw;

        trace_log_pop();

        return true;
    }

    *out = (ISize) yyjson_mut_get_sint(val);

    trace_log_pop();

    return true;
}

char* json_read_string(Json const *const self, char const *const name) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "name", (void*) name);

    // Raw-val probe (R3 Mid 5), same shape as json_read_bool/json_read_int: no wrapper
    // handle is ever allocated - read_string is the module's hottest reader, and
    // yyjson_mut_get_str's pointer is already stable for the tree's lifetime, so a handle
    // was never needed to return it. A miss (absent, wrong type, or self not an object)
    // now costs only the lookup.
    if (self->val == nullptr || !yyjson_mut_is_obj(self->val)) {
        trace_log_pop();

        return nullptr;
    }

    yyjson_mut_val *const val = yyjson_mut_obj_getn(self->val, name, (size_t) char_length(name));

    if (val == nullptr || !yyjson_mut_is_str(val)) {
        trace_log_pop();

        return nullptr;
    }

    char *const value = (char*) yyjson_mut_get_str(val);

    trace_log_pop();

    return value;
}

bool json_save_1(Json const *const self, char const *const path, bool const pretty) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "path", (void*) path);

    // Calls _json_write directly for its size out-param (Misc 20) rather than going
    // through json_get_data_1 (no size) and rescanning the buffer with char_length after.
    USize        size    = 0;
    char *const  data    = _json_write(self, pretty ? (U8) 1 : (U8) 0, &size);

    if (data == nullptr) {
        trace_log_pop();

        return false;
    }

    File *file = file_open_try_1(path, "wb");

    if (file == nullptr) {
        memory_free(data);

        trace_log_pop();

        return false;
    }

    USize const written = file_write_1(file, data, 1, size);

    file_close(&file);
    memory_free(data);

    trace_log_pop();

    return written == size;
}

void json_set_bool(Json *const self, bool const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    yyjson_mut_set_bool(self->val, value);

    trace_log_pop();
}

bool json_set_label_1(Json *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const result = json_set_label_2(self, data, char_length(data));

    trace_log_pop();

    return result;
}

bool json_set_label_2(Json *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // Empty new key refuses before the null check (R3 High 3): an EMPTY Str/String's data
    // pointer is null; renaming TO an empty key is already refused by design (item 35 /
    // R2), so this only moves WHERE that refusal happens - before error_check_null.
    if (data_size == 0) {
        trace_log_pop();

        return false;
    }

    error_check_null(LOG_METADATA, "data", (void*) data);

    // Refuse a non-object parent, or a child whose OWN current key is itself empty
    // (label_ready false - rename-from-empty-key is not attempted) BEFORE caching
    // anything: the old code cached the new label even when the rename never ran,
    // leaving get_label and the actual doc key disagreeing (item 35).
    if (self->parent == nullptr || !yyjson_mut_is_obj(self->parent->val) || !self->label_ready) {
        trace_log_pop();

        return false;
    }

    if (!yyjson_mut_obj_rename_keyn(self->doc, self->parent->val, str_get_data(&self->label), str_get_size(&self->label), data, (size_t) data_size)) {
        trace_log_pop();

        return false;
    }

    _json_label_cache(self, data, data_size);

    trace_log_pop();

    return true;
}

bool json_set_label_3(Json *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const result = json_set_label_2(self, str_get_data(data), str_get_size(data));

    trace_log_pop();

    return result;
}

bool json_set_label_4(Json *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const result = json_set_label_2(self, string_get_data(data), string_get_size(data));

    trace_log_pop();

    return result;
}

void json_set_number_float(Json *const self, FSize const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // Precision 0 clears yyjson's FP_TO_FIXED flag entirely (YYJSON_WRITE_FP_TO_FIXED(0)
    // is the zero/no-flag value) - the default is now shortest round-trip, not fixed to 4
    // decimal places (R2 Mid 6): 0.00001 now round-trips instead of printing "0.0000".
    json_set_number_float_precision(self, value, 0);

    trace_log_pop();
}

void json_set_number_float_precision(Json *const self, FSize const value, U8 const precision) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    yyjson_mut_set_real(self->val, (double) value);
    yyjson_mut_set_fp_to_fixed(self->val, precision);

    trace_log_pop();
}

void json_set_number_int(Json *const self, ISize const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    yyjson_mut_set_sint(self->val, (int64_t) value);

    trace_log_pop();
}

void json_set_number_uint(Json *const self, USize const value) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    yyjson_mut_set_uint(self->val, (uint64_t) value);

    trace_log_pop();
}

bool json_set_string_1(Json *const self, char const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const result = json_set_string_2(self, data, char_length(data));

    trace_log_pop();

    return result;
}

bool json_set_string_2(Json *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    // An EMPTY Str/String (null data, size 0) is a legal "" string value - substitute the
    // literal before the null check so the setter never aborts on this shape (R3 High 3).
    // unsafe_yyjson_mut_strncpy would otherwise memcpy 0 bytes FROM a null pointer -
    // a no-op in practice but UB per the C standard regardless of length.
    char const *const safe_data = data_size == 0 ? "" : data;

    error_check_null(LOG_METADATA, "data", (void*) safe_data);

    // unsafe_yyjson_mut_strncpy is yyjson's internal-prefixed API - the only route to a
    // doc-owned char* buffer usable with yyjson_mut_set_strn below. Safe against the
    // vendored, pinned yyjson 0.12.0 copy; re-check this call if the F1 backend swap ever
    // replaces yyjson (Misc 18).
    char *const copy = unsafe_yyjson_mut_strncpy(self->doc, safe_data, (size_t) data_size);

    if (copy == nullptr) {
        trace_log_pop();

        return false;
    }

    yyjson_mut_set_strn(self->val, copy, (size_t) data_size);

    trace_log_pop();

    return true;
}

bool json_set_string_3(Json *const self, Str const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const result = json_set_string_2(self, str_get_data(data), str_get_size(data));

    trace_log_pop();

    return result;
}

bool json_set_string_4(Json *const self, String const *const data) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    bool const result = json_set_string_2(self, string_get_data(data), string_get_size(data));

    trace_log_pop();

    return result;
}

bool json_set_string_static(Json *const self, char const *const data, USize const data_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "data", (void*) data);

    // Copies (item 3): this used to borrow an unterminated buffer, the only setter
    // that did, which made even a same-call read unsafe against a caller that freed
    // its buffer right after. Kept as a distinct entry point; behaves like _2.
    bool const result = json_set_string_2(self, data, data_size);

    trace_log_pop();

    return result;
}