// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Charles University

#ifndef _PROC_SCHEDULER_H
#define _PROC_SCHEDULER_H

#include <proc/thread.h>

/* Initialize scheduler internal data structures. Called once during kernel startup. */
void scheduler_init(void);

/* Add a thread to the ready queue. */
void scheduler_add_ready_thread(thread_t* id);

/* Remove a thread from all scheduler structures. */
void scheduler_remove_thread(thread_t* id);

/* Select the next thread to run and switch to it. */
void scheduler_schedule_next(void);

/* Voluntarily yield the CPU from the current thread. */
void scheduler_yield(void);

/* Preempt current thread on timer interrupt and switch to next. */
void scheduler_timer_interrupt(void);

#endif