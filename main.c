#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "common.h"

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Fresh database each run. */
    int fd = open(DB_FILE, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) { perror("open db"); return 1; }
    close(fd);

    /* Shared memory for the ticket lock. */
    shm_unlink(SHM_NAME);
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (shm_fd < 0) { perror("shm_open"); return 1; }
    if (ftruncate(shm_fd, sizeof(ticket_lock_t)) < 0) {
        perror("ftruncate"); return 1;
    }
    ticket_lock_t *tl = mmap(NULL, sizeof(*tl), PROT_READ | PROT_WRITE,
                             MAP_SHARED, shm_fd, 0);
    if (tl == MAP_FAILED) { perror("mmap"); return 1; }
    close(shm_fd);
    atomic_init(&tl->next_ticket, 0);
    atomic_init(&tl->now_serving, 0);

    pid_t pids[NUM_PROCESSES];
    for (int i = 0; i < NUM_PROCESSES; i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); break; }
        if (pid == 0) {
            int rc = worker_run(i, tl);
            _exit(rc);
        }
        pids[i] = pid;
    }

    int failed = 0;
    for (int i = 0; i < NUM_PROCESSES; i++) {
        int status;
        while (waitpid(pids[i], &status, 0) < 0 && errno == EINTR) {
            if (g_stop) kill(pids[i], SIGTERM);
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) failed++;
    }

    munmap(tl, sizeof(*tl));
    shm_unlink(SHM_NAME);

    /* Verify: line count must equal total jobs. */
    FILE *f = fopen(DB_FILE, "r");
    long lines = 0;
    if (f) {
        char buf[256];
        while (fgets(buf, sizeof(buf), f)) lines++;
        fclose(f);
    }
    printf("Expected records: %d, found: %ld, failed workers: %d\n",
           NUM_PROCESSES * JOBS_PER_PROC, lines, failed);
    return (failed == 0 &&
            lines == (long)NUM_PROCESSES * JOBS_PER_PROC) ? 0 : 1;
}
