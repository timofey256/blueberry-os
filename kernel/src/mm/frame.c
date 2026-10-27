// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Charles University

#include <adt/align.h>
#include <adt/bitmap.h>
#include <adt/bits.h>
#include <debug.h>
#include <mm/frame.h>
#include <mm/self.h>
#include <types.h>

const uint32_t TEST = 0xABCDEFAB;
const uint32_t PROBING_SIZE = 1 * FRAME_SIZE;
uintptr_t FRAME_ALLOC_BEGIN_ADDRESS, FRAME_ALLOC_END_ADDRESS;
size_t FRAME_COUNT;

static uintptr_t detect_end_of_available_memory(uintptr_t start) {
    uintptr_t last_valid_address = start;
    uintptr_t current_address = start;
    while (1) {
        volatile uint32_t* pointer = (uint32_t*)current_address;
        uint32_t copy = *pointer;
        *pointer = TEST;
        if (*pointer != TEST)
            break;
        *pointer = copy;
        last_valid_address = current_address;
        current_address += PROBING_SIZE;
    }
    return last_valid_address;
}

bitmap_t bitmap;

/**
 * Initializes frame allocator.
 *
 * Called once at system boot.
 */
void frame_init(void) {
    uintptr_t end_of_kernel_image = kernel_get_image_end();
    FRAME_ALLOC_BEGIN_ADDRESS = uintptr_align_up(end_of_kernel_image, FRAME_SIZE);
    FRAME_ALLOC_END_ADDRESS = detect_end_of_available_memory(FRAME_ALLOC_BEGIN_ADDRESS);
    FRAME_COUNT = (FRAME_ALLOC_END_ADDRESS - FRAME_ALLOC_BEGIN_ADDRESS) / FRAME_SIZE;
    size_t bitmap_units = BITMAP_GET_STORAGE_SIZE(FRAME_COUNT); // number of units
    size_t bitmap_size_in_bytes = bitmap_units * sizeof(bitmap_unit_t); // convert units to bytes
    size_t bitmap_frames = size_align_up(bitmap_size_in_bytes, FRAME_SIZE) / FRAME_SIZE;
    bitmap_unit_t* bitmap_storage = (bitmap_unit_t*)FRAME_ALLOC_BEGIN_ADDRESS;
    bitmap_init(&bitmap, FRAME_COUNT, bitmap_storage);
    bitmap_fill_range(&bitmap, 0, bitmap_frames);
}

/**
 * Allocate continuous sequence of physical frames.
 *
 * The allocated frames can be returned by frame_free.
 *
 * Note that frame_free must be given the number of frames being freed.
 * This means that the frame allocator does not need to record the number
 * of frames allocated (only BITMAP_GET_STORAGE_SIZEtheir allocation state: allocated or free).
 *
 * The provided tests will always call this function with count being a power
 * of two (and at least 1). But the implementation may choose to not restrict
 * the caller with this requirement. The restriction is meant to allow
 * implementing a class of frame allocators which require it.
 *
 * @param count How many frames to allocate.
 * @param phys Output-value pointer to the physical address of the
 * first frame in sequence.
 * @return Error code.
 * @retval EOK Frames allocated.
 * @retval ENOMEM Not enough memory.
 */
errno_t frame_alloc(size_t count, uintptr_t* phys) {
    assert(count > 0);
    size_t start;
    if (bitmap_find_range(&bitmap, count, false, &start) != EOK)
        return ENOMEM;
    if (start + count > FRAME_COUNT)
        return ENOMEM;
    bitmap_fill_range(&bitmap, start, count);
    *phys = (uintptr_t)(FRAME_ALLOC_BEGIN_ADDRESS + start * FRAME_SIZE);
    return EOK;
}

/**
 * Free continuous sequence of physical frames.
 *
 * The returned frames were previously allocated by frame_alloc.
 *
 * @param count How many frames to free.
 * @param phys Physical address of the first frame in sequence.
 * @return Error code.
 * @retval EOK Frames freed.
 * @retval ENOENT Invalid frame address.
 * @retval EBUSY Some frames were not allocated (double free).
 */
errno_t frame_free(size_t count, uintptr_t phys) {
    assert(count > 0);
    if ((phys < FRAME_ALLOC_BEGIN_ADDRESS) || (phys + (uintptr_t)(count * (uintptr_t)(FRAME_SIZE)) > FRAME_ALLOC_END_ADDRESS))
        return ENOENT;
    if (phys % FRAME_SIZE != 0)
        return ENOENT;
    size_t start = phys / FRAME_SIZE;
    start -= (FRAME_ALLOC_BEGIN_ADDRESS / FRAME_SIZE);
    if (!bitmap_check_range_is(&bitmap, start, count, true))
        return EBUSY;
    bitmap_clear_range(&bitmap, start, count);
    return EOK;
}