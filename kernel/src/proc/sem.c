// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Charles University

#include <exc.h>
#include <proc/scheduler.h>
#include <proc/sem.h>
#include <proc/thread.h>

/** Initializes given semaphore.
 *
 * @param sem Semaphore to initialize.
 * @param value Initial semaphore value (1 effectively creates mutex).
 * @return Error code.
 * @retval EOK Semaphore was successfully initialized.
 */
errno_t sem_init(sem_t* sem, int value) {
    assert(sem != NULL);

    bool saved = interrupts_disable();

    sem->value = value;

    list_init(&sem->queue);

    interrupts_restore(saved);
    return EOK;
}

/** Destroys given semaphore.
 *
 * The function must panic if there are threads waiting for this semaphore.
 *
 * @param sem Semaphore to destroy.
 */
void sem_destroy(sem_t* sem) {
    assert(sem != NULL);

    bool saved = interrupts_disable();

    if (!list_is_empty(&sem->queue)) {
        interrupts_restore(saved);
        panic("threads waiting on semaphore");
    }

    interrupts_restore(saved);
}
/** Get current value of the semaphore.
 *
 * @param sem Semaphore to query.
 * @return Current semaphore value.
 */
int sem_get_value(sem_t* sem) {
    assert(sem != NULL);

    bool saved = interrupts_disable();
    int value = sem->value;
    interrupts_restore(saved);

    return value;
}

/** Locks (downs) the semaphore.
 *
 * Decrement the value of this semaphore. If the new value would be negative,
 * block and wait for someone to call sem_post() first.
 *
 * @param sem Semaphore to be locked.
 */
void sem_wait(sem_t* sem) {
    assert(sem != NULL);

    bool saved = interrupts_disable();

    sem->value--;

    if (sem->value >= 0) {
        interrupts_restore(saved);
        return;
    }

    thread_t* current = thread_get_current();
    assert(current != NULL);

    current->state = THREAD_BLOCKED;
    list_append(&sem->queue, &current->thread_link);

    scheduler_schedule_next();

    interrupts_restore(saved);
}

/** Unlocks (ups/signals) the semaphore.
 *
 * Increment the value of this semaphore or wake-up one of blocked threads
 * inside sem_wait().
 *
 * @param sem Semaphore to be unlocked.
 */
void sem_post(sem_t* sem) {
    assert(sem != NULL);

    bool saved = interrupts_disable();

    sem->value++;

    if (!list_is_empty(&sem->queue)) {
        link_t* l = list_pop(&sem->queue);
        thread_t* thread = list_item(l, thread_t, thread_link);
        scheduler_add_ready_thread(thread);
    }

    interrupts_restore(saved);
}

/** Try to lock the semaphore without waiting.
 *
 * If the call to sem_wait() would block, do nothing and return EBUSY.
 *
 * @param sem Semaphore to be locked.
 * @return Error code.
 * @retval EOK Semaphore was successfully locked.
 * @retval EBUSY Semaphore has value of 0 and locking would block.
 */
errno_t sem_trywait(sem_t* sem) {
    assert(sem != NULL);

    bool saved = interrupts_disable();

    if (sem->value > 0) {
        sem->value--;
        interrupts_restore(saved);
        return EOK;
    }

    interrupts_restore(saved);
    return EBUSY;
}
