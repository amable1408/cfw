#include <container/slotmap/slotmap.h>

/*==============================================================================
 * MARK: - Macros and Constants
 *============================================================================*/
#define _SLOTMAP_GENERATION_INITIAL 1
#define _SLOTMAP_GENERATION_SHIFT   SLOTMAP_INDEX_BITS
#define _SLOTMAP_INDEX_MASK         ((SlotMapHandle) ((1u << SLOTMAP_INDEX_BITS) - 1))

/*==============================================================================
 * MARK: - Private Functions
 *============================================================================*/
// Bump a slot's generation on release, skipping 0 on wraparound so a handle
// value of exactly 0 (index 0, generation 0) can never be minted.
static U16 _slotmap_generation_bump(U16 const generation) {
    return generation == U16_MAX ? _SLOTMAP_GENERATION_INITIAL : (U16) (generation + 1);
}

static U16 _slotmap_handle_generation(SlotMapHandle const handle) {
    return (U16) (handle >> _SLOTMAP_GENERATION_SHIFT);
}

static USize _slotmap_handle_index(SlotMapHandle const handle) {
    return (USize) (handle & _SLOTMAP_INDEX_MASK);
}

static SlotMapHandle _slotmap_handle_make(USize const index, U16 const generation) {
    return ((SlotMapHandle) generation << _SLOTMAP_GENERATION_SHIFT) | (SlotMapHandle) index;
}

// Borrow/release through the map's own allocator (arena, or heap when none was
// given) - the single seam every bookkeeping allocation in this module goes
// through, so slotmap_init and slotmap_alloc_init share one implementation.
static void* _slotmap_alloc(SlotMap *const self, USize const byte_count) {
#ifdef ARENA_IMPLEMENTATION
    return allocator_try_borrow(byte_count, self->allocator);
#else
    (void) self;

    return allocator_try_borrow(byte_count);
#endif // ARENA_IMPLEMENTATION
}

static void _slotmap_free(SlotMap *const self, void *const buffer) {
#ifdef ARENA_IMPLEMENTATION
    allocator_release(buffer, self->allocator);
#else
    (void) self;

    allocator_release(buffer);
#endif // ARENA_IMPLEMENTATION
}

// Borrow generations and occupied as ONE block (occupied = generations +
// capacity) instead of two separate allocations - one decline shape, one
// release. allocator_try_borrow gives no zeroing guarantee (unlike the
// aborting heap path this module no longer uses for its own borrows), so
// occupied is zeroed explicitly and every generation seeded to 1 (never 0,
// so handle 0 is never mintable for any slot).
static void _slotmap_build(SlotMap *const self, USize const capacity) {
    USize const stride = sizeof(U16) + sizeof(bool);

    if (capacity > USIZE_MAX / stride) {
        return;
    }

    self->generations = (U16*) _slotmap_alloc(self, capacity * stride);

    if (memory_empty((void*) self->generations)) {
        return;
    }

    self->occupied = (bool*) (self->generations + capacity);

    memory_set((void*) self->occupied, sizeof(bool) * capacity, 0);

    for (USize i = 0; i < capacity; i += 1) {
        self->generations[i] = _SLOTMAP_GENERATION_INITIAL;
    }

    self->capacity = capacity;
}

// Single failure shape for slotmap_new / slotmap_alloc_new: a struct borrow
// decline never reaches here, so this only has to fold in the OTHER decline -
// a built map that landed at capacity 0 (a declined bookkeeping allocation
// inside the init/alloc_init this constructor calls). On that path the struct
// itself is released here and the caller gets the same nullptr a struct-borrow
// decline would have produced, so callers check one shape, not two, and
// nothing is leaked either way.
static SlotMap* _slotmap_new_check(SlotMap *const self) {
    if (self->capacity != 0) {
        return self;
    }

    /* uninit zeroes the whole struct, allocator field included, so the arena that
     * owns the struct is read BEFORE it: releasing through the nulled field would
     * hand an arena-borrowed struct to the heap. */
#ifdef ARENA_IMPLEMENTATION
    Arena *const allocator = self->allocator;
#endif // ARENA_IMPLEMENTATION

    slotmap_uninit(self);

#ifdef ARENA_IMPLEMENTATION
    allocator_release((void*) self, allocator);
#else
    allocator_release((void*) self);
#endif // ARENA_IMPLEMENTATION

    return nullptr;
}

/*==============================================================================
 * MARK: - Public API Implementations
 *============================================================================*/
#ifdef ARENA_IMPLEMENTATION
SlotMap slotmap_alloc_init(USize const capacity, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);
    error_check_out_of_bound_uint(LOG_METADATA, "capacity", capacity, "SLOTMAP_CAPACITY_MAX", SLOTMAP_CAPACITY_MAX, "capacity > SLOTMAP_CAPACITY_MAX", capacity > SLOTMAP_CAPACITY_MAX);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    SlotMap self = DEFAULT_INITIALIZATION;
    self.allocator = allocator;

    _slotmap_build(&self, capacity);

    trace_log_pop();

    return self;
}

SlotMap* slotmap_alloc_new(USize const capacity, Arena *const allocator) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);
    error_check_out_of_bound_uint(LOG_METADATA, "capacity", capacity, "SLOTMAP_CAPACITY_MAX", SLOTMAP_CAPACITY_MAX, "capacity > SLOTMAP_CAPACITY_MAX", capacity > SLOTMAP_CAPACITY_MAX);
    error_check_null(LOG_METADATA, "allocator", (void*) allocator);

    SlotMap *const self = (SlotMap*) allocator_try_borrow(sizeof(SlotMap), allocator);

    if (memory_empty((void*) self)) {
        trace_log_pop();

        return nullptr;
    }

    *self = slotmap_alloc_init(capacity, allocator);

    SlotMap *const result = _slotmap_new_check(self);

    trace_log_pop();

    return result;
}
#endif // ARENA_IMPLEMENTATION

SlotMapHandle slotmap_add(SlotMap *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    SlotMapHandle handle = SLOTMAP_HANDLE_INVALID;

    if (self->count < self->capacity) {
        USize index = self->free_cursor;

        while (index < self->capacity && self->occupied[index]) {
            index += 1;
        }

        /* The scan cannot run off the end while the count is exact (every slot
         * below the cursor is occupied, so a count below capacity leaves a free
         * slot at or above it), but the write below is the only heap write in the
         * module and it must not rest on a counter a copied struct can desync:
         * a run-off answers the invalid handle instead of a one-byte overflow. */
        if (index < self->capacity) {
            self->occupied[index] = true;
            self->count          += 1;
            self->free_cursor     = index + 1;

            handle = _slotmap_handle_make(index, self->generations[index]);
        }
    }

    trace_log_pop();

    return handle;
}

SlotMapHandle slotmap_add_2(SlotMap *const self, USize *const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "index", (void*) index);

    SlotMapHandle const handle = slotmap_add(self);

    *index = slotmap_index(self, handle);

    trace_log_pop();

    return handle;
}

void slotmap_clear(SlotMap *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    for (USize i = 0; i < self->capacity; i += 1) {
        if (self->occupied[i]) {
            self->occupied[i]    = false;
            self->generations[i] = _slotmap_generation_bump(self->generations[i]);
        }
    }

    self->count       = 0;
    self->free_cursor = 0;

    trace_log_pop();
}

void slotmap_delete(SlotMap **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

#ifdef ARENA_IMPLEMENTATION
    Arena *const allocator = (*self)->allocator;
#endif // ARENA_IMPLEMENTATION

    slotmap_uninit(*self);

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

bool slotmap_empty(SlotMap const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->count == 0;
}

USize slotmap_first(SlotMap const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize index = self->capacity;

    if (self->count > 0) {
        index = 0;

        while (index < self->capacity && !self->occupied[index]) {
            index += 1;
        }
    }

    trace_log_pop();

    return index;
}

bool slotmap_full(SlotMap const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->count == self->capacity;
}

USize slotmap_get_capacity(SlotMap const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->capacity;
}

USize slotmap_get_size(SlotMap const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return self->count;
}

SlotMapHandle slotmap_handle_at(SlotMap const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    SlotMapHandle handle = SLOTMAP_HANDLE_INVALID;

    if (index < self->capacity && self->occupied[index]) {
        handle = _slotmap_handle_make(index, self->generations[index]);
    }

    trace_log_pop();

    return handle;
}

USize slotmap_index(SlotMap const *const self, SlotMapHandle const handle) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const index = slotmap_valid(self, handle) ? _slotmap_handle_index(handle) : self->capacity;

    trace_log_pop();

    return index;
}

SlotMap slotmap_init(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);
    error_check_out_of_bound_uint(LOG_METADATA, "capacity", capacity, "SLOTMAP_CAPACITY_MAX", SLOTMAP_CAPACITY_MAX, "capacity > SLOTMAP_CAPACITY_MAX", capacity > SLOTMAP_CAPACITY_MAX);

    SlotMap self = DEFAULT_INITIALIZATION;

    _slotmap_build(&self, capacity);

    trace_log_pop();

    return self;
}

SlotMap* slotmap_new(USize const capacity) {
    trace_log_push(LOG_METADATA);

    error_check_non_value_uint(LOG_METADATA, "capacity", capacity);
    error_check_out_of_bound_uint(LOG_METADATA, "capacity", capacity, "SLOTMAP_CAPACITY_MAX", SLOTMAP_CAPACITY_MAX, "capacity > SLOTMAP_CAPACITY_MAX", capacity > SLOTMAP_CAPACITY_MAX);

#ifdef ARENA_IMPLEMENTATION
    SlotMap *const self = (SlotMap*) allocator_try_borrow(sizeof(SlotMap), nullptr);
#else
    SlotMap *const self = (SlotMap*) allocator_try_borrow(sizeof(SlotMap));
#endif // ARENA_IMPLEMENTATION

    if (memory_empty((void*) self)) {
        trace_log_pop();

        return nullptr;
    }

    *self = slotmap_init(capacity);

    SlotMap *const result = _slotmap_new_check(self);

    trace_log_pop();

    return result;
}

USize slotmap_next(SlotMap const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize next = index >= self->capacity ? self->capacity : index + 1;

    while (next < self->capacity && !self->occupied[next]) {
        next += 1;
    }

    trace_log_pop();

    return next;
}

bool slotmap_occupied(SlotMap const *const self, USize const index) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    trace_log_pop();

    return index < self->capacity && self->occupied[index];
}

bool slotmap_remove(SlotMap *const self, SlotMapHandle const handle) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    bool const removable = slotmap_valid(self, handle);

    if (removable) {
        USize const index = _slotmap_handle_index(handle);

        self->occupied[index]    = false;
        self->generations[index] = _slotmap_generation_bump(self->generations[index]);
        self->count              -= 1;

        if (index < self->free_cursor) {
            self->free_cursor = index;
        }
    }

    trace_log_pop();

    return removable;
}

void slotmap_uninit(SlotMap *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* Unconditional, not gated on MEMORY_NON_DANGLING_POINTER: leaving
     * self->generations non-null would let a second slotmap_uninit call pass
     * this check and release the same (now-freed) block again - this null is
     * what makes uninit idempotent, not just hygiene. */
    if (!memory_empty((void*) self->generations)) {
        _slotmap_free(self, (void*) self->generations);
    }

    self->generations = nullptr;
    self->occupied = nullptr;
    self->capacity = 0;
    self->count = 0;
    self->free_cursor = 0;
#ifdef ARENA_IMPLEMENTATION
    self->allocator = nullptr;
#endif // ARENA_IMPLEMENTATION

    trace_log_pop();
}

bool slotmap_valid(SlotMap const *const self, SlotMapHandle const handle) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    USize const index = _slotmap_handle_index(handle);

    bool const valid = handle != SLOTMAP_HANDLE_INVALID      &&
        index < self->capacity                               &&
        self->occupied[index]                                &&
        self->generations[index] == _slotmap_handle_generation(handle);

    trace_log_pop();

    return valid;
}