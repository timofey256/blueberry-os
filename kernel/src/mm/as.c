// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Charles University

#include <drivers/sv32.h>
#include <errno.h>
#include <lib/string.h>
#include <mm/as.h>
#include <mm/frame.h>
#include <mm/heap.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define PTE_V (1u << 0)
#define PTE_R (1u << 1)
#define PTE_W (1u << 2)
#define PTE_X (1u << 3)
#define PTE_U (1u << 4)
#define PTE_G (1u << 5)
#define PTE_A (1u << 6)
#define PTE_D (1u << 7)

typedef uint32_t pte_t;

static as_t* g_kernel_as = NULL;

#define KERNEL_MAP_START 0x80000000u
#define KERNEL_MAP_END 0xA0000000u

#define LOADER_MAP_START 0xF0000000u
#define LOADER_MAP_END 0xF0400000u // 4 MiB

static inline uint32_t pte_make_leaf(uintptr_t phys_page_base, uint32_t flags) {
    // phys_page_base must be 4 KiB aligned
    uint32_t ppn = (uint32_t)(phys_page_base >> 12);
    return (ppn << 10) | (flags | PTE_V);
}

static inline uint32_t pte_make_ptr(uintptr_t next_table_phys) {
    // pointer PTE: V=1, R/W/X=0, PPN points to next table
    uint32_t ppn = (uint32_t)(next_table_phys >> 12);
    return (ppn << 10) | PTE_V;
}

static inline uint32_t satp_make_sv32(uint16_t asid, uintptr_t root_pt_phys) {
    // RV32 satp: [31]=MODE, [30:22]=ASID (9 bits), [21:0]=PPN
    uint32_t ppn = (uint32_t)(root_pt_phys >> 12);
    return (1u << 31) | ((uint32_t)(asid & 0x1FFu) << 22) | (ppn & 0x003FFFFFu);
}

static inline bool pte_is_valid(pte_t p) { return (p & PTE_V) != 0; }
static inline bool pte_is_leaf(pte_t p) { return (p & (PTE_R | PTE_W | PTE_X)) != 0; }

static void map_kernel_region(pte_t* root) {
    uint32_t kflags = PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D;

    // existing kernel mapping
    for (uint32_t vpn1 = (KERNEL_MAP_START >> 22); vpn1 < (KERNEL_MAP_END >> 22); ++vpn1) {
        uintptr_t phys_base_4m = ((uintptr_t)vpn1) << 22;
        root[vpn1] = pte_make_leaf(phys_base_4m, kflags);
    }

    // add loader mapping
    for (uint32_t vpn1 = (LOADER_MAP_START >> 22); vpn1 < (LOADER_MAP_END >> 22); ++vpn1) {
        uintptr_t phys_base_4m = ((uintptr_t)vpn1) << 22;
        root[vpn1] = pte_make_leaf(phys_base_4m, kflags);
    }
}

static void* phys_to_ptr(uintptr_t phys) {
    return (void*)phys; // identity mapping
}

static void as_free_page_tables(as_t* as) {
    if (!as) {
        return;
    }

    if (as->root_pt_phys == 0) {
        if (as->l0_pt_phys != 0) {
            (void)frame_free(1, as->l0_pt_phys);
            as->l0_pt_phys = 0;
        }
        return;
    }

    pte_t* root = (pte_t*)phys_to_ptr(as->root_pt_phys);

    for (uint32_t vpn1 = 0; vpn1 < 1024; ++vpn1) {
        pte_t e = root[vpn1];
        if (!pte_is_valid(e)) {
            continue;
        }

        if (!pte_is_leaf(e)) {
            uintptr_t nested_phys = ((uintptr_t)(e >> 10)) << 12;
            (void)frame_free(1, nested_phys);
            root[vpn1] = 0;
        }
    }

    (void)frame_free(1, as->root_pt_phys);
    as->root_pt_phys = 0;
    as->l0_pt_phys = 0;
}

/** Initializes support for address spaces.
 *
 * Called once at system boot.
 */
void as_init(void) {
    g_kernel_as = as_create(0, 0);
    if (!g_kernel_as) {
        while (1) { }
    }

    uint32_t satp = satp_make_sv32(0, g_kernel_as->root_pt_phys);
    sv32_set_satp_tlb_shootdown(satp);
}

as_t* as_get_kernel_as(void) {
    return g_kernel_as;
}

/** Create new address space of given size.
 *
 * The address space must start at address (PAGE_SIZE * PAGE_NULL_COUNT).
 *
 * @param size Address space size (in bytes), aligned to PAGE_SIZE.
 * @param flags Flags (unused).
 * @return New address space.
 * @retval NULL Out of memory.
 */
as_t* as_create(size_t size, unsigned int flags) {
    (void)flags;

    if (size % PAGE_SIZE != 0) {
        return NULL;
    }

    size_t vma_pages = (size == 0) ? 0 : (size / PAGE_SIZE);

    as_t* as = (as_t*)kmalloc(sizeof(as_t));
    if (!as)
        return NULL;
    memset(as, 0, sizeof(*as));
    as->size = size;
    as->refcount = 1;

    if (vma_pages > 0) {
        uintptr_t vma_phys;
        if (frame_alloc(vma_pages, &vma_phys) != EOK) {
            kfree(as);
            return NULL;
        }
        as->vma_phys_start = vma_phys;
        as->vma_pages = vma_pages;
    }

    uintptr_t root_phys;
    if (frame_alloc(1, &root_phys) != EOK) {
        as_destroy(as);
        return NULL;
    }
    as->root_pt_phys = root_phys;
    memset(phys_to_ptr(root_phys), 0, PAGE_SIZE);

    pte_t* root = (pte_t*)phys_to_ptr(root_phys);

    map_kernel_region(root);

    uintptr_t l0_phys0;
    if (frame_alloc(1, &l0_phys0) != EOK) {
        as_destroy(as);
        return NULL;
    }
    as->l0_pt_phys = l0_phys0;
    memset(phys_to_ptr(l0_phys0), 0, PAGE_SIZE);
    root[0] = pte_make_ptr(l0_phys0);

    uint32_t uflags = PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D;

    for (size_t i = 0; i < as->vma_pages; ++i) {
        uintptr_t vaddr = (uintptr_t)(PAGE_NULL_COUNT * PAGE_SIZE) + (uintptr_t)(i * PAGE_SIZE);
        uint32_t vpn1 = (uint32_t)(vaddr >> 22);
        uint32_t vpn0 = (uint32_t)((vaddr >> 12) & 0x3FFu);

        uintptr_t l0_phys;
        pte_t root_entry = root[vpn1];

        if (!pte_is_valid(root_entry)) {
            if (frame_alloc(1, &l0_phys) != EOK) {
                as_destroy(as);
                return NULL;
            }
            memset(phys_to_ptr(l0_phys), 0, PAGE_SIZE);
            root[vpn1] = pte_make_ptr(l0_phys);
        } else {
            if (pte_is_leaf(root_entry)) {
                as_destroy(as);
                return NULL;
            }
            l0_phys = ((uintptr_t)(root_entry >> 10)) << 12;
        }

        pte_t* l0 = (pte_t*)phys_to_ptr(l0_phys);
        uintptr_t phys_page = as->vma_phys_start + (uintptr_t)(i * PAGE_SIZE);
        l0[vpn0] = pte_make_leaf(phys_page, uflags);
    }

    return as;
}

/** Get the size (in bytes) of a given address space. */
size_t as_get_size(as_t* as) {
    return as ? as->size : 0;
}

/** Destroy a given address space, freeing all the memory.
 *
 * It is not expected that a user will call this function directly: an address
 * space should be destroyed when its last thread exits. Therefore one should
 * keep track of owning threads (such API is not defined by the tests and is
 * implementation specific) and when the counter reaches zero, call this
 * function.
 *
 * Note that this function is called explicitly only in unit tests for
 * the as_t structure.
 */
void as_destroy(as_t* as) {
    if (!as)
        return;

    sv32_tlb_shootdown();

    if (as->vma_pages > 0) {
        frame_free(as->vma_pages, as->vma_phys_start);
        as->vma_pages = 0;
        as->vma_phys_start = 0;
    }

    as_free_page_tables(as);
    kfree(as);
}

/** Get physical address of root page table.
 *
 * The function should return complete physical address, i.e. not only PPN.
 *
 * @param as Address space to use.
 * @return Physical address of root page table.
 */
uintptr_t as_get_root_page_table(as_t* as) {
    return as ? as->root_pt_phys : 0;
}
