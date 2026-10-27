// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Charles University

#include <adt/align.h>
#include <drivers/machine.h>
#include <exc.h>
#include <lib/print.h>
#include <mm/frame.h>
#include <mm/heap.h>
#include <mm/self.h>

const uint32_t MAGIC_NUMBER = 0xABCDABCD;
const uint32_t MINIMUM_BYTES_OF_INFORMATION = WORD_SIZE;
const uint32_t HEADER_SIZE = sizeof(block_header_t);
const uint32_t HEAP_SIZE_IN_FRAMES = 2;

#define MIN_SPLIT_REMAINDER (HEADER_SIZE + MINIMUM_BYTES_OF_INFORMATION)

block_header_t* beginning_block = NULL;

static inline void heap_panic(const char* message) {
    puts(message);
    machine_halt();
}

static inline void check_header(const block_header_t* block_header) {
    if (block_header == NULL)
        heap_panic("heap: null header");
    if (block_header->magic != MAGIC_NUMBER)
        heap_panic("heap: header magic mismatch (corruption)");
}

static inline uintptr_t block_payload_address(const block_header_t* block_header) {
    return (uintptr_t)(block_header + 1); // automatic c arithmetic moves this by size of one header
}

static inline uintptr_t block_end_address(const block_header_t* block_header) {
    return (uintptr_t)(block_payload_address(block_header) + (uintptr_t)(block_header->payload_size));
}

static void release_last_frames(block_header_t* block_header) {
    if (block_header == NULL || block_header->next != NULL)
        return;
    if (block_header->free == OCCUPIED)
        return;
    uintptr_t start = (uintptr_t)block_header;
    uintptr_t end = block_end_address(block_header);
    start = uintptr_align_up(start, FRAME_SIZE);
    end = uintptr_align_down(end, FRAME_SIZE);
    size_t releasable_frames = (end - start) / FRAME_SIZE;
    if (releasable_frames <= 0)
        return;
    errno_t err = frame_free(releasable_frames, start);
    if (err != EOK) {
        heap_panic("heap: realasing of empty frames error");
    }
    if (block_header->payload_size < (releasable_frames * FRAME_SIZE) + MINIMUM_BYTES_OF_INFORMATION) {
        block_header_t* prev = block_header->previous;
        if (prev != NULL) {
            prev->next = NULL;
        } else {
            beginning_block = NULL;
        }
        block_header = NULL;
    } else {
        block_header->payload_size -= (releasable_frames * FRAME_SIZE);
    }
}

static void split_large_block(block_header_t* block_header, size_t size) {
    uintptr_t new_block_adress = block_payload_address(block_header) + size;
    block_header_t* new_block = (block_header_t*)new_block_adress;

    new_block->magic = MAGIC_NUMBER;
    new_block->free = FREE;
    new_block->payload_size = (block_header->payload_size - size) - HEADER_SIZE;
    new_block->previous = block_header;
    new_block->next = block_header->next;

    if (block_header->next != NULL) {
        block_header_t* child_of_block_header = block_header->next;
        child_of_block_header->previous = new_block;
    }
    block_header->payload_size = size;
    block_header->next = new_block;
}

/** Heap initialization.
 *
 * Called once during system boot.
 */
void heap_init(void) {
    uintptr_t heap_start;
    errno_t err = frame_alloc(HEAP_SIZE_IN_FRAMES, &heap_start);
    if (err != EOK) {
        heap_panic("heap: unable to allocate initial heap memory");
    }
    uintptr_t heap_end = heap_start + HEAP_SIZE_IN_FRAMES * FRAME_SIZE;

    beginning_block = (block_header_t*)heap_start; // the header begins at heap_start -> | header | payload |
    beginning_block->magic = MAGIC_NUMBER;
    beginning_block->payload_size = heap_end - heap_start - HEADER_SIZE; // the memory blocks do not store the header in their size
    beginning_block->free = FREE;
    beginning_block->previous = NULL;
    beginning_block->next = NULL;
}

/** Allocate size bytes on heap.
 *
 * The behavior when size is zero is not defined and none of the provided
 * tests will ever call kmalloc with 0 as the parameter. The implementation may
 * choose to return NULL or a unique pointer for such call (i.e., follow the
 * C standard). Note that causing a kernel panic or not checking for such
 * case at all is also a valid option.
 *
 * @param size Amount of memory to allocate.
 * @return Pointer to allocated memory.
 * @retval NULL Out of memory.
 */
void* kmalloc(size_t size) {
    if (size == 0)
        return NULL;

    bool saved = interrupts_disable();
    void* result = NULL;

    size = size_align_up(size, WORD_SIZE);
    block_header_t* current_block = beginning_block;
    block_header_t* previous_block = NULL;

    while (current_block != NULL) {
        size_t curr_block_size = current_block->payload_size;

        if (current_block->free == FREE && curr_block_size >= size) {
            if (curr_block_size >= (size + MIN_SPLIT_REMAINDER)) {
                split_large_block(current_block, size);
            }
            current_block->free = OCCUPIED;
            result = (void*)block_payload_address(current_block);
            interrupts_restore(saved);
            return result;
        }
        previous_block = current_block;
        current_block = current_block->next;
    }

    // no fit out of the current heap and blocks in heap, thus we require new frame(s)
    size_t total_size_needed = size + HEADER_SIZE;
    size_t frames_for_new_block = size_align_up(total_size_needed, FRAME_SIZE) / FRAME_SIZE;

    uintptr_t new_block_address;
    errno_t err = frame_alloc(frames_for_new_block, &new_block_address);
    if (err != EOK) {
        result = NULL;
        interrupts_restore(saved);
        return result;
    }

    // last block is empty and the frames are consecutive so we make a one larger block instead of a new block
    //  (if no kfree this could result in memory leak in arbitrary size if not implemented)
    if (block_end_address(previous_block) == new_block_address && previous_block != NULL && previous_block->free == FREE) {

        previous_block->payload_size += frames_for_new_block * FRAME_SIZE;

        if (previous_block->payload_size >= size + MIN_SPLIT_REMAINDER) {
            split_large_block(previous_block, size);
        }

        previous_block->free = OCCUPIED;
        result = (void*)block_payload_address(previous_block);
        interrupts_restore(saved);
        return result;
    }

    else {
        // block in new frame, physically not next to the last frame, which is by default the last block
        block_header_t* new_block = (block_header_t*)(new_block_address);
        new_block->magic = MAGIC_NUMBER;
        new_block->next = NULL; // PREVIOUS BLOCK IS THE LAST BLOCK
        new_block->previous = previous_block;
        new_block->free = FREE;
        new_block->payload_size = (frames_for_new_block)*FRAME_SIZE - HEADER_SIZE;

        previous_block->next = new_block;

        if (new_block->payload_size >= size + MIN_SPLIT_REMAINDER) {
            split_large_block(new_block, size);
        }

        new_block->free = OCCUPIED;

        result = (void*)block_payload_address(new_block);
        interrupts_restore(saved);
        return result;
    }
}

/** Free memory previously allocated by kmalloc.
 *
 * The behavior when ptr is NULL is not defined and none of the provided
 * tests will ever call kfree with NULL as the parameter. Note that accepting
 * NULL as a parameter follows the C standard behavior, i.e. everything that
 * malloc can return can be passed to free. In the context of a kernel,
 * aborting the execution on receiving NULL is also an option as that might
 * signal an abnormal situation in a different module.
 *
 * @param ptr Pointer to memory to be freed.
 */
void kfree(void* ptr) {
    if (ptr == NULL) {
        /* tests said they won't pass NULL; accept it or assert */
        heap_panic("heap: kfree(NULL)");
        return;
    }

    bool saved = interrupts_disable();

    uintptr_t block_payload_address = (uintptr_t)ptr;
    uintptr_t block_header_address = block_payload_address - HEADER_SIZE;
    block_header_t* block_header = (block_header_t*)block_header_address;

    check_header(block_header);

    block_header->free = FREE;
    block_header_t* child = block_header->next;
    // we can only coalesce memory blocks if they lie in consecutive frame(s)
    if ((child != NULL && child->free == FREE) && ((uintptr_t)(child) == block_end_address(block_header))) {

        block_header->payload_size += (HEADER_SIZE + child->payload_size);
        block_header->next = child->next;

        if (child->next != NULL) {
            block_header_t* grandchild = child->next;
            grandchild->previous = block_header;
        }
        child = NULL;
    }

    block_header_t* parent = block_header->previous;
    if ((parent != NULL && parent->free == FREE) && (block_header_address == block_end_address(parent))) {

        parent->payload_size += (block_header->payload_size + HEADER_SIZE);
        parent->next = block_header->next;

        if (block_header->next != NULL) {
            block_header_t* helper = block_header->next;
            helper->previous = parent;
        }
        block_header = parent;
    }

    release_last_frames(block_header);
    interrupts_restore(saved);
}