// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Charles University

#include <adt/list.h>
#include <debug.h>
#include <drivers/timer.h>
#include <exc.h>
#include <proc/scheduler.h>
#include <proc/thread.h>

#ifndef KERNEL_SCHEDULER_QUANTUM
#define KERNEL_SCHEDULER_QUANTUM 2000
#endif

static list_t ready_list;
extern thread_t* current_thread;

/** Initialize support for scheduling.
 *
 * Called once at system boot.
 */
void scheduler_init(void) {
    list_init(&ready_list);
}

/** Marks given thread as ready to be executed.
 *
 * It is expected that this thread would be added at the end
 * of the queue to run in round-robin fashion.
 *
 * @param thread Thread to make runnable.
 */
void scheduler_add_ready_thread(thread_t* thread) {
    assert(thread != NULL);
    if (thread->state == THREAD_FINISHED)
        return;

    bool saved = interrupts_disable();

    list_append(&ready_list, &thread->thread_link);
    thread->state = THREAD_READY;

    interrupts_restore(saved);
}

/** Removes given thread from scheduling.
 *
 * Expected to be called when thread is suspended, terminates execution
 * etc.
 *
 * @param thread Thread to remove from the queue.
 */
void scheduler_remove_thread(thread_t* thread) {
    assert(thread != NULL);

    bool saved = interrupts_disable();
    list_remove(&thread->thread_link);
    interrupts_restore(saved);
}

static inline thread_t* scheduler_pop_next(void) {
    if (list_is_empty(&ready_list))
        return NULL;
    link_t* l = list_pop(&ready_list);
    return list_item(l, thread_t, thread_link);
}

/** Switch to next thread in the queue. */
void scheduler_schedule_next(void) {
    bool saved = interrupts_disable();

    thread_t* next = scheduler_pop_next();
    if (!next) {
        interrupts_restore(saved);
        panic("no threads to schedule");
    }

    timer_interrupt_after(KERNEL_SCHEDULER_QUANTUM);

    thread_switch_to(next);

    interrupts_restore(saved);
}

/** Yield CPU by requeuing current thread and switching to next. */
void scheduler_yield(void) {
    bool saved = interrupts_disable();

    scheduler_timer_interrupt();

    interrupts_restore(saved);
}

/* Preempt current thread on timer interrupt and switch to next. */
void scheduler_timer_interrupt(void) {

    if (list_is_empty(&ready_list)) {
        timer_interrupt_after(KERNEL_SCHEDULER_QUANTUM);
        return;
    }

    thread_t* cur = thread_get_current();

    scheduler_add_ready_thread(cur);

    thread_t* next = scheduler_pop_next();
    timer_interrupt_after(KERNEL_SCHEDULER_QUANTUM);

    thread_switch_to(next);
}
