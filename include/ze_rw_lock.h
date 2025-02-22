#pragma once
#include <glib.h>

// This readers-writer lock prefers writers.
// Implemented from https://en.wikipedia.org/wiki/Readers%E2%80%93writer_lock#Using_a_condition_variable_and_a_mutex
// TODO: Measure if this is an improvement over what we have
struct ze_w_priority_lock {
    GCond wake_up;
    GMutex lock;
    gint num_readers_active;
    gint num_writers_waiting;
    gint writer_active;
};

void ze_init_rw_lock(struct ze_w_priority_lock*);
void ze_read_lock(struct ze_w_priority_lock*);
void ze_read_unlock(struct ze_w_priority_lock*);
void ze_write_lock(struct ze_w_priority_lock*);
void ze_write_unlock(struct ze_w_priority_lock*);
