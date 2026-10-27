// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Charles University

#include <adt/list.h>
#include <exc.h>
#include <proc/mutex.h>
#include <proc/thread.h>

/** Initializes given mutex.
 *
 * @param mutex Mutex to initialize.
 * @return Error code.
 * @retval EOK Mutex was successfully initialized.
 */
errno_t mutex_init(mutex_t* mutex) {
    assert(mutex != NULL);

    bool saved = interrupts_disable();

    mutex->locked = false;
    mutex->owner = NULL;

    interrupts_restore(saved);
    return EOK;
}

/** Destroys given mutex.
 *
 * The function must panic if the mutex is still locked when being destroyed.
 *
 * @param mutex Mutex to destroy.
 */
void mutex_destroy(mutex_t* mutex) {
    assert(mutex != NULL);

    bool saved = interrupts_disable();

    if (mutex->locked) {
        interrupts_restore(saved);
        panic("mutex is still locked when being destroyed.");
    }

    interrupts_restore(saved);
}

/** Locks the mutex.
 *
 * Note that when this function returns, the mutex must be locked.
 *
 * @param mutex Mutex to be locked.
 */
void mutex_lock(mutex_t* mutex) {
    assert(mutex != NULL);

    thread_t* current = thread_get_current();

    while (true) {
        bool saved = interrupts_disable();

        if (!mutex->locked) {
            mutex->locked = true;
            mutex->owner = current;

            interrupts_restore(saved);
            return;
        }
        interrupts_restore(saved);
        thread_yield();
    }
}

/** Unlocks the mutex.
 *
 * Note that when this function returns, the mutex might be already locked
 * by a different thread.
 *
 * This function shall panic if the mutex is unlocked by a different thread
 * than the one that locked it.
 *
 * @param mutex Mutex to be unlocked.
 */
void mutex_unlock(mutex_t* mutex) {
    assert(mutex != NULL);

    bool saved = interrupts_disable();

    if (!mutex->locked) {
        interrupts_restore(saved);
        panic("unlocking an unlocked mutex");
    }

    thread_t* current = thread_get_current();

    if (mutex->owner != current) {
        interrupts_restore(saved);
        panic("mutex unlocked by non-owner");
    }

    mutex->locked = false;
    mutex->owner = NULL;

    interrupts_restore(saved);
}

/** Try to lock the mutex without waiting.
 *
 * If the mutex is already locked, do nothing and return EBUSY.
 *
 * @param mutex Mutex to be locked.
 * @return Error code.
 * @retval EOK Mutex was successfully locked.
 * @retval EBUSY Mutex is currently locked by a different thread.
 */
errno_t mutex_trylock(mutex_t* mutex) {
    assert(mutex != NULL);

    thread_t* current = thread_get_current();

    bool saved = interrupts_disable();

    if (mutex->locked) {
        interrupts_restore(saved);
        return EBUSY;
    }

    mutex->locked = true;
    mutex->owner = current;

    interrupts_restore(saved);
    return EOK;
}