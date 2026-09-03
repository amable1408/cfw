/*
 * hashset.c - Implementation for hashset.h
 *
 * Open-addressing hash table with linear probing over arena/heap bucket arrays.
 * Capacity is a power of two, so probing masks instead of taking a modulo. Keys
 * are stored as borrowed pointers or owned copies (tracked per slot); only owned
 * copies are freed. The table grows and rehashes when load exceeds ~70%. String
 * length, comparison, and copying reuse <char/char.h>.
 */

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <container/hashset/hashset.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

#define _HASHSET_DEFAULT_KEYS 16
#define _HASHSET_FNV_OFFSET 1469598103934665603ull
#define _HASHSET_FNV_PRIME 1099511628211ull
#define _HASHSET_GROWTH_FACTOR 2
#define _HASHSET_LOAD_DENOMINATOR 10
#define _HASHSET_LOAD_NUMERATOR 7
#define _HASHSET_MIN_BUCKETS 16

/*==============================================================================
 * MARK: - Static Functions
 *============================================================================*/

static void* _hashset_alloc(HashSet *const self, USize const byte_count) {
#ifdef ARENA_IMPLEMENTATION
    return allocator_try_borrow(byte_count, self->allocator);
#else
    (void) self;

    return allocator_try_borrow(byte_count);
#endif // ARENA_IMPLEMENTATION
}

static void _hashset_free(HashSet *const self, void *const buffer) {
    if (buffer == nullptr) {
        return;
    }

#ifdef ARENA_IMPLEMENTATION
    allocator_release(buffer, self->allocator);
#else
    (void) self;

    allocator_release(buffer);
#endif // ARENA_IMPLEMENTATION
}

static U64 _hashset_seed(HashSet const *const self) {
    static _Atomic U64 counter = 0;

    U64 const value = atomic_fetch_add_explicit(&counter, 1, memory_order_relaxed) + 1;

    /* Cheap and non-cryptographic on purpose: the set's own (transient) stack
     * address mixed with a process-lifetime counter is enough to defeat an
     * offline, precomputed collision set built against a fixed FNV basis. It
     * is not a keyed MAC and does not defend against an adaptive attacker -
     * see hashset.h's Performance Characteristics. The counter is atomic so
     * concurrent constructions across threads never race the same increment
     * into two sets' seeds. */
    return ((U64) (USize) self) ^ (value * _HASHSET_FNV_PRIME);
}

/* key_size must not span an embedded NUL: a key stored past one would be
 * unfindable by every later probe, which re-derives the stored length with
 * char_length. Such a key is refused as data, not aborted - see add_static_2 /
 * contains_2 / count_2. */
static bool _hashset_key_spans_nul(char const *const key, USize const key_size) {
    return char_find_slice_2(key, key_size, 0, '\0') != nullptr;
}

/* Shared refusal check for the three sized entry points (add_static_2, contains_2,
 * count_2), called first and in this same order by all three so their behavior cannot
 * silently drift apart:
 *   - key_size 0 is an empty key, refused BY POLICY (see the header), not an error.
 *   - key_size == USIZE_MAX is refused as DATA too: _hashset_key_copy computes the copy's
 *     allocation size as key_size + CHAR_END_CHARACTER, which would wrap to 0 and hand back
 *     a buffer far too small for the char_copy_2 that follows it - a heap overflow, not a
 *     graceful decline. Refusing it here keeps that arithmetic from ever running on this
 *     value.
 *   - a key spanning an embedded NUL, per _hashset_key_spans_nul. */
static bool _hashset_sized_key_refused(char const *const key, USize const key_size) {
    if (key_size == 0 || key_size == USIZE_MAX) {
        return true;
    }

    return _hashset_key_spans_nul(key, key_size);
}

static USize _hashset_hash(HashSet const *const self, char const *const key, USize const key_size) {
    U64 hash = _HASHSET_FNV_OFFSET ^ self->seed;

    for (USize i = 0; i < key_size; i += 1) {
        hash ^= (U64) (U8) key[i];
        hash *= _HASHSET_FNV_PRIME;
    }

    /* U64 state throughout, folded once at the end: on a 32-bit USize this is
     * still correct FNV-1a-64 folded down, unlike accumulating in USize
     * directly, which truncates the 64-bit prime to 435 and degrades probe
     * chains on 32-bit Android ABIs. On 64-bit this fold is two instructions. */
    return (USize) (hash ^ (hash >> 32));
}

static USize _hashset_find(HashSet const *const self, char const *const key, USize const key_size) {
    USize const mask = self->capacity - 1;
    USize slot = _hashset_hash(self, key, key_size) & mask;

    for (USize probe = 0; probe < self->capacity; probe += 1) {
        if (self->keys[slot] == nullptr) {
            return self->capacity;
        }

        if (char_compare_equal_2(self->keys[slot], char_length(self->keys[slot]), key, key_size)) {
            return slot;
        }

        slot = (slot + 1) & mask;
    }

    return self->capacity;
}

static char* _hashset_key_copy(HashSet *const self, char const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    char *const copy = (char*) _hashset_alloc(self, key_size + CHAR_END_CHARACTER);

    if (memory_empty(copy)) {
        trace_log_pop();

        return nullptr;
    }

    char_copy_2(copy, key, key_size);
    copy[key_size] = '\0';

    trace_log_pop();

    return copy;
}

static void _hashset_build(HashSet *const self, USize const capacity) {
    trace_log_push(LOG_METADATA);

    self->seed = _hashset_seed(self);

    /* `capacity` counts KEYS, not buckets: target enough buckets that this many
     * keys sit at or under the ~70% load factor before the first add would grow
     * the table. Guard the *10 first - a hint past USIZE_MAX / 10 would wrap. */
    if (capacity > USIZE_MAX / _HASHSET_LOAD_DENOMINATOR) {
        self->capacity = 0;

        trace_log_pop();

        return;
    }

    USize const target = capacity * _HASHSET_LOAD_DENOMINATOR / _HASHSET_LOAD_NUMERATOR + 1;
    USize rounded = _HASHSET_MIN_BUCKETS;

    while (rounded < target) {
        /* Guards the sizeof(char*) * rounded byte multiply below, not just the
         * doubling: char* is this struct's widest bucket array, so bounding by
         * it also bounds the U32 and bool arrays sized off the same `rounded`. */
        if (rounded > USIZE_MAX / (2 * sizeof(char*))) {
            self->capacity = 0;

            trace_log_pop();

            return;
        }

        rounded *= 2;
    }

    self->keys = (char**) _hashset_alloc(self, sizeof(char*) * rounded);
    self->counts = (U32*) _hashset_alloc(self, sizeof(U32) * rounded);
    self->owned = (bool*) _hashset_alloc(self, sizeof(bool) * rounded);
    self->capacity = rounded;
    self->size = 0;

    if (memory_empty(self->keys) || memory_empty(self->counts) || memory_empty(self->owned)) {
        _hashset_free(self, self->keys);
        _hashset_free(self, self->counts);
        _hashset_free(self, self->owned);

        self->keys = nullptr;
        self->counts = nullptr;
        self->owned = nullptr;
        self->capacity = 0;

        trace_log_pop();

        return;
    }

    for (USize i = 0; i < rounded; i += 1) {
        self->keys[i] = nullptr;
        self->counts[i] = 0;
        self->owned[i] = false;
    }

    trace_log_pop();
}

static void _hashset_grow(HashSet *const self) {
    trace_log_push(LOG_METADATA);

    USize const old_capacity = self->capacity;
    char **const old_keys = self->keys;
    U32 *const old_counts = self->counts;
    bool *const old_owned = self->owned;

    /* Mirrors _hashset_build's guard: bounds the doubling AND the sizeof(char*)
     * * new_capacity multiply below, char* being this struct's widest bucket
     * array. A decline keeps the old table untouched. */
    if (old_capacity > USIZE_MAX / (2 * sizeof(char*))) {
        trace_log_pop();

        return;
    }

    USize const new_capacity = old_capacity * _HASHSET_GROWTH_FACTOR;

    char **const keys = (char**) _hashset_alloc(self, sizeof(char*) * new_capacity);
    U32 *const counts = (U32*) _hashset_alloc(self, sizeof(U32) * new_capacity);
    bool *const owned = (bool*) _hashset_alloc(self, sizeof(bool) * new_capacity);

    if (memory_empty(keys) || memory_empty(counts) || memory_empty(owned)) {
        _hashset_free(self, keys);
        _hashset_free(self, counts);
        _hashset_free(self, owned);

        trace_log_pop();

        return;
    }

    for (USize i = 0; i < new_capacity; i += 1) {
        keys[i] = nullptr;
        counts[i] = 0;
        owned[i] = false;
    }

    USize const mask = new_capacity - 1;

    for (USize i = 0; i < old_capacity; i += 1) {
        if (old_keys[i] == nullptr) {
            continue;
        }

        USize slot = _hashset_hash(self, old_keys[i], char_length(old_keys[i])) & mask;

        while (keys[slot] != nullptr) {
            slot = (slot + 1) & mask;
        }

        keys[slot] = old_keys[i];
        counts[slot] = old_counts[i];
        owned[slot] = old_owned[i];
    }

    self->keys = keys;
    self->counts = counts;
    self->owned = owned;
    self->capacity = new_capacity;

    _hashset_free(self, old_keys);
    _hashset_free(self, old_counts);
    _hashset_free(self, old_owned);

    trace_log_pop();
}

static U32 _hashset_add(HashSet *const self, char const *const key, USize const key_size, bool const copy) {
    trace_log_push(LOG_METADATA);

    /* The public entry points already error_check_null this before forwarding
     * here; in an UNCHECKED build that call compiles to nothing, so this is the
     * only guard standing between a null self and a dereference below. */
    if (self == nullptr || self->capacity == 0) {
        trace_log_pop();

        return 0;
    }

    /* An empty key is refused BY POLICY: the set never stores it, so
     * contains/count answer miss/0 without a dedicated empty slot. This is a
     * data choice, not crash avoidance - char_compare_equal_2 does not abort
     * on a zero-size side. */
    if (key_size == 0) {
        trace_log_pop();

        return 0;
    }

    USize const existing = _hashset_find(self, key, key_size);

    if (existing != self->capacity) {
        if (self->counts[existing] < U32_MAX) {
            self->counts[existing] += 1;
        }

        U32 const count = self->counts[existing];

        trace_log_pop();

        return count;
    }

    /* Probed first: a duplicate never reaches here, so a repeated key at the
     * load-factor threshold does not grow the table for nothing. */
    if ((self->size + 1) * _HASHSET_LOAD_DENOMINATOR > self->capacity * _HASHSET_LOAD_NUMERATOR) {
        _hashset_grow(self);
    }

    /* A declined grow leaves size == capacity: refuse the insert with one
     * compare instead of walking a full table looking for a slot that is not
     * there. */
    if (self->size == self->capacity) {
        trace_log_pop();

        return 0;
    }

    USize const mask = self->capacity - 1;
    USize slot = _hashset_hash(self, key, key_size) & mask;

    while (self->keys[slot] != nullptr) {
        slot = (slot + 1) & mask;
    }

    // self->keys is char**: this is the single cast up from the borrowed `char const*` to
    // the store. A borrowed key (copy == false) is never mutated or freed through it - only
    // compared and, on hashset_uninit, handed back read-only via char_length/char_compare -
    // so the const contract is not actually broken, just spent at the point it is stored.
    char *stored = (char*) key;

    if (copy) {
        stored = _hashset_key_copy(self, key, key_size);

        if (memory_empty(stored)) {
            trace_log_pop();

            return 0;
        }
    }

    self->keys[slot] = stored;
    self->counts[slot] = 1;
    self->owned[slot] = copy;
    self->size += 1;

    trace_log_pop();

    return 1;
}

/* Single failure shape for the four allocating constructors below: a struct borrow decline
 * (set == nullptr) never reaches here, so this only has to fold in the OTHER decline - a
 * built set that landed at capacity 0 (a declined bucket allocation inside the init/alloc_init
 * this constructor calls). On that path the struct itself is released here and the caller
 * gets the same nullptr a struct-borrow decline would have produced, so callers check one
 * shape, not two, and nothing is leaked either way. */
static HashSet* _hashset_new_check(HashSet *const set) {
    if (set->capacity != 0) {
        return set;
    }

    hashset_uninit(set);
    _hashset_free(set, (void*) set);

    return nullptr;
}

/*==============================================================================
 * MARK: - API Functions
 *============================================================================*/

U32 hashset_add(HashSet *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    USize const key_size = memory_empty((void*) key) ? 0 : char_length(key);

    U32 const count = _hashset_add(self, key, key_size, false);

    trace_log_pop();

    return count;
}

U32 hashset_add_static(HashSet *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    USize const key_size = memory_empty((void*) key) ? 0 : char_length(key);

    U32 const count = _hashset_add(self, key, key_size, true);

    trace_log_pop();

    return count;
}

U32 hashset_add_static_2(HashSet *const self, char const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    if (_hashset_sized_key_refused(key, key_size)) {
        trace_log_pop();

        return 0;
    }

    U32 const count = _hashset_add(self, key, key_size, true);

    trace_log_pop();

    return count;
}

#ifdef ARENA_IMPLEMENTATION
HashSet hashset_alloc_init_1(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    HashSet const set = hashset_alloc_init_2(_HASHSET_DEFAULT_KEYS, allocator);

    trace_log_pop();

    return set;
}

HashSet hashset_alloc_init_2(USize const capacity, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HashSet set = DEFAULT_INITIALIZATION;
    set.allocator = allocator;

    _hashset_build(&set, capacity);

    trace_log_pop();

    return set;
}

HashSet* hashset_alloc_new_1(Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HashSet *const set = (HashSet*) allocator_try_borrow(sizeof(HashSet), allocator);

    if (memory_empty((void*) set)) {
        trace_log_pop();

        return nullptr;
    }

    *set = hashset_alloc_init_1(allocator);

    HashSet *const result = _hashset_new_check(set);

    trace_log_pop();

    return result;
}

HashSet* hashset_alloc_new_2(USize const capacity, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    HashSet *const set = (HashSet*) allocator_try_borrow(sizeof(HashSet), allocator);

    if (memory_empty((void*) set)) {
        trace_log_pop();

        return nullptr;
    }

    *set = hashset_alloc_init_2(capacity, allocator);

    HashSet *const result = _hashset_new_check(set);

    trace_log_pop();

    return result;
}
#endif // ARENA_IMPLEMENTATION

void hashset_clear(HashSet *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    for (USize i = 0; i < self->capacity; i += 1) {
        if (self->keys[i] != nullptr && self->owned[i]) {
            _hashset_free(self, self->keys[i]);
        }

        self->keys[i] = nullptr;
        self->counts[i] = 0;
        self->owned[i] = false;
    }

    self->size = 0;

    trace_log_pop();
}

bool hashset_contains(HashSet const *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    USize const key_size = memory_empty((void*) key) ? 0 : char_length(key);

    /* Same empty-key policy as hashset_add: refused as a data choice, not a
     * crash to avoid. */
    if (key_size == 0 || self->capacity == 0) {
        trace_log_pop();

        return false;
    }

    bool const found = _hashset_find(self, key, key_size) != self->capacity;

    trace_log_pop();

    return found;
}

bool hashset_contains_2(HashSet const *const self, char const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    if (_hashset_sized_key_refused(key, key_size)) {
        trace_log_pop();

        return false;
    }

    // Same dead-set policy as hashset_contains.
    if (self->capacity == 0) {
        trace_log_pop();

        return false;
    }

    bool const found = _hashset_find(self, key, key_size) != self->capacity;

    trace_log_pop();

    return found;
}

U32 hashset_count(HashSet const *const self, char const *const key) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    USize const key_size = memory_empty((void*) key) ? 0 : char_length(key);

    /* Same empty-key policy as hashset_add: refused as a data choice, not a
     * crash to avoid. */
    if (key_size == 0 || self->capacity == 0) {
        trace_log_pop();

        return 0;
    }

    USize const slot = _hashset_find(self, key, key_size);
    U32 const count = (slot == self->capacity) ? 0 : self->counts[slot];

    trace_log_pop();

    return count;
}

U32 hashset_count_2(HashSet const *const self, char const *const key, USize const key_size) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "key", (void*) key);

    if (_hashset_sized_key_refused(key, key_size)) {
        trace_log_pop();

        return 0;
    }

    // Same dead-set policy as hashset_count.
    if (self->capacity == 0) {
        trace_log_pop();

        return 0;
    }

    USize const slot = _hashset_find(self, key, key_size);
    U32 const count = (slot == self->capacity) ? 0 : self->counts[slot];

    trace_log_pop();

    return count;
}

void hashset_delete(HashSet **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

#ifdef ARENA_IMPLEMENTATION
    Arena *const allocator = (*self)->allocator;
#endif // ARENA_IMPLEMENTATION

    hashset_uninit(*self);

#ifdef ARENA_IMPLEMENTATION
    allocator_release((void*) *self, allocator);
#else
    allocator_release((void*) *self);
#endif // ARENA_IMPLEMENTATION

#ifdef MEMORY_NON_DANGLING_POINTER
    *self = nullptr;
#endif // MEMORY_NON_DANGLING_POINTER

    trace_log_pop();
}

bool hashset_empty(HashSet const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->size == 0;
}

USize hashset_get_capacity(HashSet const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->capacity;
}

HashSet hashset_init_1(void) {
    trace_log_push(LOG_METADATA);

    HashSet const set = hashset_init_2(_HASHSET_DEFAULT_KEYS);

    trace_log_pop();

    return set;
}

HashSet hashset_init_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

    HashSet set = DEFAULT_INITIALIZATION;

    _hashset_build(&set, capacity);

    trace_log_pop();

    return set;
}

HashSet* hashset_new_1(void) {
    trace_log_push(LOG_METADATA);

#ifdef ARENA_IMPLEMENTATION
    HashSet *const set = (HashSet*) allocator_try_borrow(sizeof(HashSet), nullptr);
#else
    HashSet *const set = (HashSet*) allocator_try_borrow(sizeof(HashSet));
#endif // ARENA_IMPLEMENTATION

    if (memory_empty((void*) set)) {
        trace_log_pop();

        return nullptr;
    }

    *set = hashset_init_1();

    HashSet *const result = _hashset_new_check(set);

    trace_log_pop();

    return result;
}

HashSet* hashset_new_2(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);

#ifdef ARENA_IMPLEMENTATION
    HashSet *const set = (HashSet*) allocator_try_borrow(sizeof(HashSet), nullptr);
#else
    HashSet *const set = (HashSet*) allocator_try_borrow(sizeof(HashSet));
#endif // ARENA_IMPLEMENTATION

    if (memory_empty((void*) set)) {
        trace_log_pop();

        return nullptr;
    }

    *set = hashset_init_2(capacity);

    HashSet *const result = _hashset_new_check(set);

    trace_log_pop();

    return result;
}

bool hashset_next(HashSet const *const self, USize *const cursor, char const **const key, U32 *const count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "cursor", (void*) cursor);
    error_check_null(LOG_METADATA, "key", (void*) key);
    error_check_null(LOG_METADATA, "count", (void*) count);

    while (*cursor < self->capacity) {
        USize const slot = *cursor;

        *cursor += 1;

        if (self->keys[slot] != nullptr) {
            *key = self->keys[slot];
            *count = self->counts[slot];

            trace_log_pop();

            return true;
        }
    }

    trace_log_pop();

    return false;
}

USize hashset_size(HashSet const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->size;
}

void hashset_uninit(HashSet *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    hashset_clear(self);

    _hashset_free(self, self->keys);
    _hashset_free(self, self->counts);
    _hashset_free(self, self->owned);

    self->keys = nullptr;
    self->counts = nullptr;
    self->owned = nullptr;
    self->capacity = 0;
    self->size = 0;

    trace_log_pop();
}