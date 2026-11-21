#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <stdatomic.h>

#define NUM_PROCESSES   4
#define THREADS_PER_PROC 8
#define JOBS_PER_PROC   2000
#define BATCH_SIZE      50

#define DB_FILE   "db.dat"
#define SHM_NAME  "/fair_db_shm"

/* Lives in POSIX shared memory, shared by all processes. */
typedef struct {
    atomic_uint next_ticket;   /* next ticket to hand out */
    atomic_uint now_serving;   /* ticket allowed to enter */
} ticket_lock_t;

int  worker_run(int proc_id, ticket_lock_t *tl);

#endif
