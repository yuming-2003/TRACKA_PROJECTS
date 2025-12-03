CC = gcc
CFLAGS = -O2 -Wall -pthread

all: bench_affinity_smt bench_prefetch bench_async_io

bench_affinity_smt: bench_affinity_smt.c
	$(CC) $(CFLAGS) -o $@ $^

bench_prefetch: bench_prefetch.c
	$(CC) $(CFLAGS) -o $@ $^

bench_async_io: bench_async_io.c
	# -lrt is needed for POSIX AIO on some systems
	$(CC) $(CFLAGS) -lrt -o $@ $^

clean:
	rm -f bench_affinity_smt bench_prefetch bench_async_io
