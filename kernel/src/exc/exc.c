// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Charles University

#include <debug.h>
#include <drivers/csr.h>
#include <drivers/machine.h>
#include <exc.h>
#include <proc/scheduler.h>

/** Handles general exception.
 *
 * The function receives a pointer to an exception context, which
 * represents a snapshot of CPU and (some) CSRs at the
 * time of the exception occurring.
 */
void handle_exception_general(exc_context_t* exc_context) {
    unative_t scause = exc_context->scause;

    // Highest bit in scause = interrupt flag (1) or exception (0).
    // Remaining bits = cause code.
    unsigned bits = sizeof(unative_t) * 8;
    unative_t int_bit = (unative_t)1 << (bits - 1);

    bool is_interrupt = (scause & int_bit) != 0;
    unative_t code = scause & ~int_bit;

    // Supervisor timer interrupt has cause code 5.
    if (is_interrupt && code == 5) {
        scheduler_timer_interrupt();
        return;
    }

    if (!is_interrupt && (code == 12 || code == 13 || code == 15)) {
        thread_t* t = thread_get_current();
        if (t) {
            thread_kill(t);
        }
        scheduler_schedule_next();
        return;
    }

    printk("Unhandled exception.");
    machine_enter_debugger();
}
