// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Charles University

#ifndef _MM_HEAP_H
#define _MM_HEAP_H

#include <types.h>

typedef struct block_header {
    uint32_t magic;
    size_t payload_size;
    bool free;
    struct block_header* next;
    struct block_header* previous;
} block_header_t;

void heap_init(void);
void* kmalloc(size_t size);
void kfree(void* ptr);

#endif
