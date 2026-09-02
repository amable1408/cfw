/*
 * arena_pool.c - Implementation of pool arena allocator for the C Libraries Framework
 *
 * See arena_pool.h for API documentation, usage, and error handling notes.
 */

/*
 * Memory Layout for Pool Arena Allocator
 *
 * The pool arena manages a buffer divided into fixed-size blocks, tracked by a free list.
 * Each allocation returns a pointer to a free block, either from the free list or the next
 * available block. Freed blocks are marked in the free list and reused for future allocations.
 *
 * Layout:
 *
 * +----------+----------+----------+----- ... -----+
 * | Block 0  | Block 1  | Block 2  | ...           |
 * +----------+----------+----------+----- ... -----+
 * ^          ^          ^
 * |          |          |
 * data      data+1*size data+2*size ...
 *
 * Two parallel tracking arrays sit between the metadata and the blocks:
 *
 *   - free_list (U8, one per block): 1 = released and reusable, 0 = in use or never handed out.
 *   - run_size (USize, one per block): at a run's FIRST block, how many contiguous blocks that
 *     allocation covers; 0 everywhere else.
 *
 * run_size is what makes a multi-block allocation releasable. Without it free could only mark
 * the one block it was handed, stranding blocks 1..N-1 as used with no pointer left that could
 * ever release them - a permanent in-pool leak. It doubles as the interior-pointer test: a
 * pointer that is block-aligned but lands mid-run reads 0 and is refused.
 *
 * For each allocation:
 *   - Scan the free list for a contiguous run of block_count released blocks; take it if found.
 *   - Otherwise hand out the next block_count blocks past the high-water mark and advance it.
 *   - Either way, record the run length at the starting index.
 *
 * For free:
 *   - Read the run length, mark every block of the run free, zero the whole run, clear the
 *     length, then retract the high-water mark over any now-free trailing blocks.
 *
 * No per-allocation metadata is stored in the data blocks themselves.
 */

/*==============================================================================
 * MARK: - Includes
 *============================================================================*/

#include <arena/arena_pool.h>

/*==============================================================================
 * MARK: - Types
 *============================================================================*/

struct ArenaPool {
    USize capacity;
    void *data;
    USize size;
    U8 *free_list;
    USize block_size;

    /* Run length per starting block; 0 at any index that does not begin an allocation. */
    USize *run_size;
};

/*==============================================================================
 * MARK: - API
 *============================================================================*/

void* arena_pool_alloc(ArenaPool *const self, USize const block_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "block_count", block_count);
    /* Bound against capacity, not `capacity - size`. size is a high-water mark, and try_alloc
     * now satisfies a request from a released interior run even when size has reached capacity;
     * checking the old way would abort here for a request the allocation path fulfils happily.
     * The subtraction form would also wrap, letting a huge block_count through. */
    error_check_out_of_bound_uint(LOG_METADATA,
        "block_count", block_count,
        "self->capacity", self->capacity,
        "block_count > self->capacity", block_count > self->capacity);

    void *const buffer = arena_pool_try_alloc(self, block_count);

    /* allocate treats exhaustion as a programmer error and aborts (the
     * arena.h/allocator.h contract): an in-range block_count the pool cannot
     * currently satisfy is no longer a silent nullptr into unchecked callers. */
    error_check_message(LOG_METADATA, memory_empty(buffer), "arena_pool_alloc: pool exhausted");

    trace_log_pop();

    return buffer;
}

void* arena_pool_alloc_bytes(ArenaPool *const self, USize const byte_count) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_non_value_uint(LOG_METADATA, "byte_count", byte_count);

    /* The facade's byte-denominated adapter: ceil(bytes / block_size) blocks.
     * block_size is the ALIGNED stride the pool actually hands out. */
    USize const block_count = byte_count / self->block_size + (byte_count % self->block_size != 0 ? 1 : 0);
    void *const buffer      = arena_pool_alloc(self, block_count);

    trace_log_pop();

    return buffer;
}

void arena_pool_delete(ArenaPool **const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);
    error_check_null(LOG_METADATA, "*self", (void*) *self);

    memory_delete((void**) &(*self));

    trace_log_pop();
}

bool arena_pool_empty(ArenaPool const *const self) {
    trace_log_push(LOG_METADATA);
    
    error_check_null(LOG_METADATA, "self", (void*) self);
    
    trace_log_pop();

    return self->size == 0;
}

void arena_pool_free(ArenaPool *const self, void *const buffer) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    /* A null buffer is IGNORED, matching free(NULL) and arena_stack_free: the
     * generic release seam forwards empty owners here routinely (a container
     * created and never grown has data == nullptr). */
    if (memory_empty(buffer)) {
        trace_log_pop();

        return;
    }

    /* No error_check on size: releasing the pool's last block drives size to 0, so a second
     * release of that pointer - precisely what the free_list check below is written to absorb -
     * would abort the process before ever reaching the guard. */
    if (self->size == 0) {
        trace_log_pop();

        return;
    }

    U8 *const data = (U8*) self->data;
    U8 *const block = (U8*) buffer;

    if (block < data
        || block >= data + self->block_size * self->capacity
        || (USize) (block - data) % self->block_size != 0) {
        trace_log_pop();

        return;
    }

    USize const offset = (USize) (block - data) / self->block_size;

    if (offset >= self->size || self->free_list[offset] == 1) {
        trace_log_pop();

        return;
    }

    /* Release the WHOLE run. Marking only `offset` stranded blocks 1..N-1 of every multi-block
     * allocation: still flagged used, with no pointer left that could release them. A 0 here
     * means offset does not begin a run - an interior pointer, or a run already released - so
     * it is refused rather than acted on. The upper test is belt-and-braces against a corrupted
     * length walking the loop past the high-water mark. */
    USize const run = self->run_size[offset];

    if (run == 0 || run > self->size - offset) {
        trace_log_pop();

        return;
    }

    memory_set(buffer, self->block_size * run, 0);

    for (USize i = 0; i < run; i += 1) {
        self->free_list[offset + i] = 1;
    }

    self->run_size[offset] = 0;

    while (self->size > 0 && self->free_list[self->size - 1] == 1) {
        self->free_list[self->size - 1] = 0;

        self->size -= 1;
    }

    trace_log_pop();
}

ArenaPool* arena_pool_new(USize const block_size, USize const block_count) {
    trace_log_push(LOG_METADATA);

    /* No error_check on the two sizes: the graceful guard below already rejects 0, and aborting
     * first made those lines dead code AND made this constructor unusable for a caller deriving
     * a pool geometry from configuration or input. A refusal is a nullptr here, as everywhere. */

    /* Blocks are handed out at data + block_size * index, so an unaligned block_size misaligns
     * every block after the first. Rounding it up is what keeps each block usable for any type
     * - the same guarantee memory_alloc gives for the whole arena. */
    USize const block_size_aligned = MEMORY_ALIGN_UP(block_size);

    /* One metadata header, then run_size (USize per block), then free_list (U8 per block), then
     * the blocks. run_size leads the two arrays so it inherits the aligned header offset - a
     * USize array behind a byte array would sit on whatever parity block_count happened to be. */
    USize const metadata    = MEMORY_ALIGN_UP(sizeof(ArenaPool));
    USize const per_block   = block_size_aligned + sizeof(USize) + sizeof(U8);

    if (block_size == 0 || block_count == 0
        || block_size_aligned < block_size
        || block_size_aligned > USIZE_MAX - sizeof(USize) - sizeof(U8)
        || block_count > (USIZE_MAX - metadata - MEMORY_ALIGNMENT) / per_block) {
        trace_log_pop();

        return nullptr;
    }

    /* MEMORY_ALIGN_UP over the tracking arrays is what keeps block 0 aligned: free_list ends on
     * an arbitrary byte boundary, so without this the first block inherits that parity no matter
     * how well block_size is rounded. The guard above reserves MEMORY_ALIGNMENT for this slack. */
    USize const tracking    = metadata + (sizeof(USize) * block_count) + (sizeof(U8) * block_count);
    USize const data_offset = MEMORY_ALIGN_UP(tracking);

    ArenaPool *const arena_pool = (ArenaPool*) memory_try_alloc(data_offset + (block_size_aligned * block_count));

    if (memory_empty(arena_pool)) {
        trace_log_pop();

        return nullptr;
    }

    arena_pool->block_size  = block_size_aligned;
    arena_pool->capacity    = block_count;
    arena_pool->run_size    = (USize*) ((U8*) arena_pool + metadata);
    arena_pool->free_list   = (U8*) arena_pool + metadata + (sizeof(USize) * block_count);
    arena_pool->data        = (void*) ((U8*) arena_pool + data_offset);
    arena_pool->size        = 0;

    trace_log_pop();

    return arena_pool;
}

void* arena_pool_try_alloc(ArenaPool *const self, USize const block_count) {
    trace_log_push(LOG_METADATA);

    /* Deliberately check-free; see arena_linear_try_alloc for why. Bounded by capacity rather
     * than the remaining bump space, because a released interior run can satisfy a request the
     * bump pointer no longer can. */
    if (memory_empty(self) || block_count == 0 || block_count > self->capacity) {
        trace_log_pop();

        return nullptr;
    }

    /* The free list is searched BEFORE the bump pointer is consulted. Gating on
     * `block_count > capacity - size` up front - as this used to - made the pool stop serving
     * the moment size reached capacity, however many interior blocks had been released: size is
     * a high-water mark that only retreats when the TRAILING blocks are free, so a pool cycling
     * allocations of mixed lifetime hard-failed while mostly empty. */
    USize run_start = USIZE_MAX;

    for (USize i = 0; i + block_count <= self->size; i += 1) {
        bool run_free = true;

        for (USize j = 0; j < block_count; j += 1) {
            if (self->free_list[i + j] != 1) {
                run_free = false;

                break;
            }
        }

        if (run_free) {
            run_start = i;

            break;
        }
    }

    if (run_start != USIZE_MAX) {
        for (USize i = 0; i < block_count; i += 1) {
            self->free_list[run_start + i] = 0;
        }

        self->run_size[run_start] = block_count;

        trace_log_pop();

        return (U8*) self->data + self->block_size * run_start;
    }

    if (block_count > self->capacity - self->size) {
        trace_log_pop();

        return nullptr;
    }

    USize const start = self->size;

    self->size += block_count;
    self->run_size[start] = block_count;

    trace_log_pop();

    return (U8*) self->data + self->block_size * start;
}

void* arena_pool_try_alloc_bytes(ArenaPool *const self, USize const byte_count) {
    trace_log_push(LOG_METADATA);

    if (memory_empty(self) || byte_count == 0) {
        trace_log_pop();

        return nullptr;
    }

    /* See arena_pool_alloc_bytes: the facade's byte-denominated adapter. */
    USize const block_count = byte_count / self->block_size + (byte_count % self->block_size != 0 ? 1 : 0);
    void *const buffer      = arena_pool_try_alloc(self, block_count);

    trace_log_pop();

    return buffer;
}
