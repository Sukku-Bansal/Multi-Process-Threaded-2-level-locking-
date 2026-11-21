# Fair Shared Database: Multi-Process, Multi-Thread (C, Linux)

## Problem
Several processes share one database. Only one process may access it at a
time, and each process may run many threads. Threads and processes must be
synchronized, and no process may starve.

## Build and run
    make
    ./fairdb
    make check    # runs under valgrind
    make race     # builds with ThreadSanitizer

## Design
1. Database: flat file db.dat, one record per line.
2. Inter-process lock: fcntl write lock on db.lock. The kernel releases it
   if a process crashes, so there is no stuck lock.
3. Fairness: a ticket lock in POSIX shared memory (next_ticket,
   now_serving). Processes enter strictly in arrival order.
4. Batching: a process runs at most BATCH_SIZE operations per turn, then
   releases the DB and queues again, so no process can monopolize it.
5. Threads: each process uses a fixed pool of THREADS_PER_PROC threads and a
   mutex. A pool is used instead of thousands of threads to avoid the
   default 8 MB stack cost per thread.
6. Verification: the parent checks that the record count equals the total
   number of jobs.

## Limits and trade-offs
- If a process dies while holding a ticket turn, later tickets wait. A
  production version would add a timeout or use a robust mutex.
- A flat file keeps the focus on concurrency, not SQL. SQLite could replace
  it with the same locking structure.
