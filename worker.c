#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sched.h>
#include <pthread.h>
#include <sys/stat.h>
#include "common.h"

#define LOCK_FILE "db.lock"

typedef struct {
    int             proc_id;
    ticket_lock_t  *tl;
    int             lock_fd;     /* fd of the lock file, for fcntl */
    int             db_fd;       /* fd of the database file */
    pthread_mutex_t mtx;         /* level 2: threads in this process */
    int             next_job;    /* next job number to hand out */
    int             total_jobs;
    int             done_in_batch;
    int             holding;     /* 1 if this process holds the DB */
} proc_ctx_t;

/* ---------- Level 1: fair, inter-process lock ---------- */

static int fcntl_lock(int fd, short type)
{
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = type;      /* F_WRLCK or F_UNLCK */
    fl.l_whence = SEEK_SET;
    int rc;
    do { rc = fcntl(fd, F_SETLKW, &fl); } while (rc < 0 && errno == EINTR);
    return rc;
}

/* Take a ticket, wait for our turn (FIFO), then grab the fcntl lock. */
static int db_acquire(proc_ctx_t *c)
{
    unsigned my = atomic_fetch_add(&c->tl->next_ticket, 1);
    while (atomic_load(&c->tl->now_serving) != my)
        sched_yield();
    if (fcntl_lock(c->lock_fd, F_WRLCK) < 0) {
        perror("fcntl lock");
        atomic_fetch_add(&c->tl->now_serving, 1);
        return -1;
    }
    return 0;
}

/* Release the file lock and let the next ticket in. */
static void db_release(proc_ctx_t *c)
{
    if (fcntl_lock(c->lock_fd, F_UNLCK) < 0) perror("fcntl unlock");
    atomic_fetch_add(&c->tl->now_serving, 1);
}

/* ---------- Level 2: thread pool inside the process ---------- */

static void *thread_main(void *arg)
{
    proc_ctx_t *c = arg;

    for (;;) {
        pthread_mutex_lock(&c->mtx);

        if (c->next_job >= c->total_jobs) {          /* all work done */
            pthread_mutex_unlock(&c->mtx);
            break;
        }

        /* First thread of a batch gets DB access for the whole process. */
        if (!c->holding) {
            if (db_acquire(c) < 0) {
                c->next_job = c->total_jobs;          /* abort */
                pthread_mutex_unlock(&c->mtx);
                break;
            }
            c->holding = 1;
            c->done_in_batch = 0;
        }

        int job = c->next_job++;
        char line[128];
        int n = snprintf(line, sizeof(line), "proc=%d job=%d\n",
                         c->proc_id, job);
        ssize_t w = write(c->db_fd, line, (size_t)n);
        if (w != n) perror("write db");

        /* Batch limit reached: give the DB to the next process. */
        if (++c->done_in_batch >= BATCH_SIZE || c->next_job >= c->total_jobs) {
            if (fsync(c->db_fd) < 0) perror("fsync");
            db_release(c);
            c->holding = 0;
        }
        pthread_mutex_unlock(&c->mtx);
    }
    return NULL;
}

int worker_run(int proc_id, ticket_lock_t *tl)
{
    proc_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.proc_id    = proc_id;
    c.tl         = tl;
    c.total_jobs = JOBS_PER_PROC;

    c.lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0644);
    if (c.lock_fd < 0) { perror("open lock"); return 1; }

    /* O_APPEND makes each write land at the end of the file. */
    c.db_fd = open(DB_FILE, O_WRONLY | O_APPEND);
    if (c.db_fd < 0) { perror("open db"); close(c.lock_fd); return 1; }

    if (pthread_mutex_init(&c.mtx, NULL) != 0) {
        fprintf(stderr, "mutex init failed\n");
        return 1;
    }

    pthread_t th[THREADS_PER_PROC];
    int started = 0;
    for (int i = 0; i < THREADS_PER_PROC; i++) {
        int rc = pthread_create(&th[i], NULL, thread_main, &c);
        if (rc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            break;
        }
        started++;
    }
    for (int i = 0; i < started; i++) pthread_join(th[i], NULL);

    pthread_mutex_destroy(&c.mtx);
    close(c.db_fd);
    close(c.lock_fd);
    return (started == THREADS_PER_PROC) ? 0 : 1;
}
