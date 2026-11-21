CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -O2 -g -pthread -std=gnu11
LDFLAGS = -pthread -lrt
OBJS    = main.o worker.o

all: fairdb

fairdb: $(OBJS)
	$(CC) $(OBJS) -o fairdb $(LDFLAGS)

%.o: %.c common.h
	$(CC) $(CFLAGS) -c $< -o $@

check: fairdb
	valgrind --leak-check=full --error-exitcode=1 ./fairdb

race: clean
	$(MAKE) CFLAGS="-Wall -g -O1 -pthread -std=gnu11 -fsanitize=thread" \
	        LDFLAGS="-pthread -lrt -fsanitize=thread" fairdb
	./fairdb

clean:
	rm -f fairdb *.o db.dat db.lock

.PHONY: all check race clean
