// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Charles University

#include <abi.h>
#include <drivers/sv32.h>
#include <lib/string.h>
#include <mm/as.h>
#include <mm/heap.h>
#include <proc/context.h>
#include <proc/scheduler.h>
#include <proc/thread.h>
#include <proc/thread_internal.h>

static thread_t main_thread;
static uint8_t main_stack[THREAD_STACK_SIZE];
static context_t main_ctx;
thread_t* current_thread = NULL;

extern void cpu_switch_context(context_t* from, context_t* to);

static inline void as_acquire(as_t* as) {
    if (as)
        as->refcount++;
}

static inline void as_release(as_t* as) {
    if (!as)
        return;
    if (as->refcount > 0)
        as->refcount--;
    if (as->refcount == 0)
        as_destroy(as);
}

static inline uint32_t satp_make_sv32(uint16_t asid, uintptr_t root_pt_phys) {
    uint32_t ppn = (uint32_t)(root_pt_phys >> 12);
    return (1u << 31) | ((uint32_t)(asid & 0x1FFu) << 22) | (ppn & 0x003FFFFFu);
}

/** Initialize support for threading.
 *
 * Called once at system boot.
 */
void threads_init(void) {
    memset(&main_thread, 0, sizeof(thread_t));
    strncpy(main_thread.name, "main", THREAD_NAME_MAX_LENGTH + 1);
    main_thread.state = THREAD_RUNNING;

    uintptr_t sp = ((uintptr_t)(main_stack + THREAD_STACK_SIZE)) & ~(ABI_STACK_ALIGNMENT - 1);

    main_thread.ctx = &main_ctx;
    main_thread.as = as_get_kernel_as();
    as_acquire(main_thread.as);
    link_init(&main_thread.thread_link);

    context_init(main_thread.ctx, sp);

    current_thread = &main_thread;
}

void thread_trampoline(void) {
    thread_t* t = thread_get_current();
    void* rval = t->entry_func(t->arg);
    t->return_value = rval;
    thread_finish(rval);
}

/** Create a new thread.
 *
 * The thread is automatically placed into the queue of ready threads.
 *
 * This function allocates space for both stack and the thread_t structure
 * (hence the double <code>**</code> in <code>thread_out</code>).
 *
 * This thread will use the same address space as the current one.
 *
 * @param thread_out Where to place the initialized thread_t structure.
 * @param entry Thread entry function.
 * @param data Data for the entry function.
 * @param flags Flags (unused).
 * @param name Thread name (for debugging purposes).
 * @return Error code.
 * @retval EOK Thread was created and started (added to ready queue).
 * @retval ENOMEM Not enough memory to complete the operation.
 * @retval EINVAL Invalid flags (unused).
 */
errno_t thread_create(thread_t** thread_out, thread_entry_func_t entry,
        void* data, unsigned int flags, const char* name) {
    thread_t* t = kmalloc(sizeof(thread_t) + THREAD_STACK_SIZE);
    if (!t)
        return ENOMEM;

    uintptr_t stack_bottom = (uintptr_t)(t + 1);
    uintptr_t stack_top = stack_bottom + THREAD_STACK_SIZE;

    uintptr_t sp = ((uintptr_t)stack_top) & ~(ABI_STACK_ALIGNMENT - 1);

    memset(t, 0, sizeof(*t));
    strncpy(t->name, name, THREAD_NAME_MAX_LENGTH + 1);
    t->entry_func = entry;
    t->arg = data;
    t->state = THREAD_READY;

    t->ctx = kmalloc(sizeof(context_t));
    if (!t->ctx) {
        kfree(t);
        return ENOMEM;
    }

    thread_t* cur = thread_get_current();
    t->as = cur ? cur->as : as_get_kernel_as();
    as_acquire(t->as);

    link_init(&t->thread_link);
    context_init(t->ctx, sp);

    scheduler_add_ready_thread(t);
    *thread_out = t;

    return EOK;
}

/** Create a new thread with new address space.
 *
 * The thread is automatically placed into the queue of ready threads.
 *
 * This function allocates space for both stack and the thread_t structure
 * (hence the double <code>**</code> in <code>thread_out</code>).
 *
 * @param thread_out Where to place the initialized thread_t structure.
 * @param entry Thread entry function.
 * @param data Data for the entry function.
 * @param flags Flags (unused).
 * @param name Thread name (for debugging purposes).
 * @param as_size Address space size, aligned at page size (0 is correct though not very useful).
 * @return Error code.
 * @retval EOK Thread was created and started (added to ready queue).
 * @retval ENOMEM Not enough memory to complete the operation.
 * @retval EINVAL Invalid flags (unused).
 */
errno_t thread_create_new_as(thread_t** thread_out, thread_entry_func_t entry, void* data, unsigned int flags, const char* name, size_t as_size) {
    (void)flags;

    thread_t* t = kmalloc(sizeof(thread_t) + THREAD_STACK_SIZE);
    if (!t)
        return ENOMEM;

    uintptr_t stack_bottom = (uintptr_t)(t + 1);
    uintptr_t stack_top = stack_bottom + THREAD_STACK_SIZE;
    uintptr_t sp = ((uintptr_t)stack_top) & ~(ABI_STACK_ALIGNMENT - 1);

    memset(t, 0, sizeof(*t));
    strncpy(t->name, name, THREAD_NAME_MAX_LENGTH + 1);
    t->entry_func = entry;
    t->arg = data;
    t->state = THREAD_READY;

    t->ctx = kmalloc(sizeof(context_t));
    if (!t->ctx) {
        kfree(t);
        return ENOMEM;
    }

    t->as = as_create(as_size, 0);
    if (!t->as) {
        kfree(t->ctx);
        kfree(t);
        return ENOMEM;
    }

    link_init(&t->thread_link);
    context_init(t->ctx, sp);

    scheduler_add_ready_thread(t);
    *thread_out = t;
    return EOK;
}

/** Return information about currently executing thread.
 *
 * @retval NULL When no thread was started yet.
 */
thread_t* thread_get_current(void) {
    extern thread_t* current_thread;
    return current_thread;
}

/** Yield the processor. */
void thread_yield(void) {
    scheduler_yield();
}

/** Current thread stops execution and is not scheduled until woken up. */
void thread_suspend(void) {
    thread_t* t = thread_get_current();
    t->state = THREAD_BLOCKED;
    scheduler_remove_thread(t);
    scheduler_schedule_next();
}

/** Terminate currently running thread.
 *
 * Thread can (normally) terminate in two ways: by returning from the entry
 * function or by calling this function. The parameter to this function then
 * has the same meaning as the return value from the entry function.
 *
 * Note that this function never returns.
 *
 * @param retval Data to return in thread_join.
 */
void thread_finish(void* retval) {
    thread_t* t = thread_get_current();
    t->state = THREAD_FINISHED;
    t->return_value = retval;

    if (t->as) {
        // Do not keep running on an address space we might destroy.
        // Switch to kernel AS first (safe even if AS survives due to refcount).
        as_t* kas = as_get_kernel_as();
        if (kas && t->as != kas) {
            uint32_t satp = satp_make_sv32(0, as_get_root_page_table(kas));
            sv32_set_satp_tlb_shootdown(satp);
        }

        as_release(t->as);
        t->as = NULL;
    }

    scheduler_remove_thread(t);
    scheduler_schedule_next();
    panic("thread_finish: should never return");
}

/** Tells if a thread finished its execution.
 *
 * Thread finished the execution either when it returned from the entry
 * function, called thread_finish() or was already killed.
 *
 * In other words, when this function returns true, thread_join() should
 * return immediately (it is not required but it is a natural consequence
 * of the above).
 *
 * This function may not be called when thread_join() was already called
 * on the thread in question (because the thread pointer by that time will
 * not be valid).
 *
 * @param thread Thread in question.
 */
bool thread_has_finished(thread_t* thread) {
    assert(thread != NULL);
    return (thread->state == THREAD_FINISHED);
}

/** Wakes-up existing thread.
 *
 * Note that waking-up a running (or ready) thread has no effect (i.e. the
 * function shall not count wake-ups and suspends).
 *
 * Note that waking-up a thread does not mean that it will immediatelly start
 * executing.
 *
 * @param thread Thread to wake-up.
 * @return Error code.
 * @retval EOK Thread was woken-up (or was already ready/running).
 * @retval EEXITED Thread already finished its execution.
 */
errno_t thread_wakeup(thread_t* thread) {
    if (!thread)
        return EINVAL;
    if (thread->state == THREAD_FINISHED)
        return EEXITED;

    if (thread->state == THREAD_BLOCKED) {
        scheduler_add_ready_thread(thread);
        return EOK;
    }

    return EOK;
}

/** Joins another thread (waits for it to terminate).
 *
 * Calling thread_join blocks until the thread we are joining
 * (i.e. the parameter) terminates (either by being killed, finishing its
 * entry function or calling thread_finish). It is not allowed to join the
 * same thread from two places, where possible (see below) this should be
 * detected and signalled via EBUSY return value.
 *
 * After thread_join returns, all pointers to the joined thread (i.e., to the
 * thread_t structure given as parameter) are considered invalid and should
 * not be used (including, but not limited to, calls to thread_finish,
 * thread_join or thread_kill).
 *
 * Note that thread_create is an allocation function (like kmalloc) while
 * thread_join is somewhat a deallocation function (like kfree). As with kfree,
 * it does not make sense to call thread_join twice or use the pointer in any
 * way after return as it is no longer valid.
 *
 * Implementation note: thread_join must release all memory allocated for the
 * joined thread (assuming a successful join). While it is possible to release
 * the thread stack earlier, the rest of the structure must remain valid until
 * a call to thread_join. For example, thread_kill can release the stack of
 * the killed thread because the thread no longer executes but releasing the
 * stack inside thread_finish is not directly possible as the clean-up function
 * still needs to use the stack (it would be possible to delegate the job to
 * a special clean-up thread so that the stack is released as soon as possible
 * but postponing the stack release to thread_join is a typical choice).
 *
 * Note that <code>retval</code> could be <code>NULL</code> if the caller
 * is not interested in the returned value.
 *
 * @param thread Thread to wait for.
 * @param retval Where to place the value returned from thread_finish.
 * @return Error code.
 * @retval EOK Thread was joined.
 * @retval EBUSY Some other thread is already joining this one.
 * @retval EINVAL The thread to join is invalid (including NULL and self).
 * @retval EKILLED Thread was killed.
 */
errno_t thread_join(thread_t* thread, void** retval) {
    if (!thread)
        return EINVAL;
    if (thread == thread_get_current())
        return EINVAL;

    if (thread->join_in_progress)
        return EBUSY;
    thread->join_in_progress = true;

    while (!thread_has_finished(thread)) {
        thread_yield();
    }

    errno_t rc = thread->killed ? EKILLED : EOK;

    if (!thread->killed && retval) {
        *retval = thread->return_value;
    }

    if (thread->as) {
        as_release(thread->as);
        thread->as = NULL;
    }

    if (thread->ctx) {
        kfree(thread->ctx);
        thread->ctx = NULL;
    }
    kfree(thread);

    return rc;
}

/** Switch CPU context to a different thread.
 *
 * Note that this function must work even if there is no current thread
 * (i.e. for the very first context switch in the system).
 *
 * @param thread Thread to switch to.
 */
#include <debug.h>
#include <proc/context.h>
#include <proc/scheduler.h>
#include <proc/thread.h>

extern thread_t* current_thread;
static context_t boot_ctx;

void thread_switch_to(thread_t* next) {
    if (!next)
        return;
    if (current_thread == next)
        return;

    panic_if(next->ctx == NULL, "thread_switch_to: next->ctx is NULL");

    thread_t* prev = current_thread;

    if (prev && prev->state == THREAD_RUNNING)
        prev->state = THREAD_READY;
    next->state = THREAD_RUNNING;

    current_thread = next;

    as_t* next_as = next->as ? next->as : as_get_kernel_as();
    as_t* prev_as = prev ? prev->as : NULL;

    if (next_as && next_as != prev_as) {
        uint32_t satp = satp_make_sv32(0, as_get_root_page_table(next_as));
        sv32_set_satp_tlb_shootdown(satp);
    }

    if (prev == NULL) {
        cpu_switch_context(&boot_ctx, next->ctx);
        return;
    } else {
        panic_if(prev->ctx == NULL, "thread_switch_to: prev->ctx is NULL");
        cpu_switch_context(prev->ctx, next->ctx);
        return;
    }
}

/** Get address space of given thread. */
as_t* thread_get_as(thread_t* thread) {
    return thread ? thread->as : NULL;
}

/** Kills given thread.
 *
 * Note that this function shall work for any existing thread, including
 * currently running one.
 *
 * Joining a killed thread results in EKILLED return value from thread_join.
 *
 * @param thread Thread to kill.
 * @return Error code.
 * @retval EOK Thread was killed.
 * @retval EEXITED Thread already finished its execution.
 */
errno_t thread_kill(thread_t* thread) {
    if (!thread)
        return EINVAL;
    if (thread->state == THREAD_FINISHED)
        return EEXITED;

    thread->killed = true;

    if (thread == thread_get_current()) {
        thread_finish(NULL);
        panic("thread_kill: returned from self kill");
    }

    scheduler_remove_thread(thread);
    thread->state = THREAD_FINISHED;

    if (thread->as) {
        as_release(thread->as);
        thread->as = NULL;
    }

    return EOK;
}
