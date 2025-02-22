#include "ze_rw_lock.h"
#include "glib.h"

void ze_init_rw_lock(struct ze_w_priority_lock* lock) {
    g_mutex_init(&lock->lock);
    g_cond_init(&lock->wake_up);
    g_atomic_int_set(&lock->num_readers_active, 0);
    g_atomic_int_set(&lock->num_writers_waiting, 0);
    g_atomic_int_set(&lock->writer_active, 0);
}

void ze_read_lock(struct ze_w_priority_lock* lock) {
    g_mutex_lock(&lock->lock);
    while(g_atomic_int_get(&lock->num_writers_waiting) > 0 || g_atomic_int_get(&lock->writer_active)) {
        g_cond_wait(&lock->wake_up, &lock->lock);
    }
    g_atomic_int_inc(&lock->num_readers_active);
    g_mutex_unlock(&lock->lock);
}

void ze_read_unlock(struct ze_w_priority_lock* lock) {
    g_mutex_lock(&lock->lock);
    // Signal to potential writer threads
    if (g_atomic_int_dec_and_test(&lock->num_readers_active)) {
        g_cond_signal(&lock->wake_up);
    }
    g_mutex_unlock(&lock->lock);
}

void ze_write_lock(struct ze_w_priority_lock* lock) {
    g_mutex_lock(&lock->lock);
    g_atomic_int_inc(&lock->num_writers_waiting);
    while (g_atomic_int_get(&lock->num_readers_active) > 0 || g_atomic_int_get(&lock->writer_active)) {
        g_cond_wait(&lock->wake_up, &lock->lock);
    }
    g_atomic_int_dec_and_test(&lock->num_writers_waiting);
    g_atomic_int_set(&lock->writer_active, 1);
    g_mutex_unlock(&lock->lock);
}

void ze_write_unlock(struct ze_w_priority_lock* lock) {
    g_mutex_lock(&lock->lock);
    g_atomic_int_set(&lock->writer_active, 0);
    g_cond_signal(&lock->wake_up);  
    g_mutex_unlock(&lock->lock);    
}
