// bench_affinity_smt.c
// Multi-threaded compute benchmark to study CPU affinity and SMT interference.
//
// Usage:
//   ./bench_affinity_smt <num_threads> <seconds> <affinity_mode> <base_core> <core_stride>
//
// affinity_mode:
//   0 = no explicit affinity (OS is free to schedule)
//   1 = pin each thread i to core = base_core + i * core_stride
//
// Example:
//   # 2 threads, 5 seconds, pinned to cores 0 and 2
//   ./bench_affinity_smt 2 5 1 0 2
//
//   # 2 threads, 5 seconds, both pinned to core 0 (SMT interference scenario)
//   ./bench_affinity_smt 2 5 1 0 0
//
// Metrics printed: per-thread iterations and aggregate iterations/sec.

#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int thread_id;
    int affinity_mode;
    int core_id;
    int duration_sec;
    volatile uint64_t iterations;
} worker_args_t;

static inline double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void *worker(void *arg) {
    worker_args_t *wa = (worker_args_t *)arg;

    if (wa->affinity_mode == 1) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(wa->core_id, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0) {
            perror("sched_setaffinity");
        }
    }

    volatile double x = 1.0;
    double t_start = now_sec();
    double t_end = t_start + wa->duration_sec;
    uint64_t iters = 0;

    while (now_sec() < t_end) {
        // Heavy floating-point loop to burn CPU
        x = x * 1.0000001 + 0.0000001;
        x = x * 0.9999999 - 0.0000001;
        x = x * x * 0.9999998 + 0.0000002;
        iters += 4;
    }

    wa->iterations = iters;
    // prevent compiler from optimizing away
    if (x == 0.0) {
        printf("x = %f\n", x);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr,
                "Usage: %s <num_threads> <seconds> <affinity_mode> <base_core> <core_stride>\n",
                argv[0]);
        return 1;
    }

    int num_threads = atoi(argv[1]);
    int duration_sec = atoi(argv[2]);
    int affinity_mode = atoi(argv[3]);
    int base_core = atoi(argv[4]);
    int core_stride = atoi(argv[5]);

    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
    worker_args_t *args = malloc(sizeof(worker_args_t) * num_threads);

    double start = now_sec();
    for (int i = 0; i < num_threads; i++) {
        args[i].thread_id = i;
        args[i].affinity_mode = affinity_mode;
        args[i].duration_sec = duration_sec;
        args[i].iterations = 0;
        if (affinity_mode == 1) {
            args[i].core_id = base_core + i * core_stride;
        } else {
            args[i].core_id = -1;
        }

        int ret = pthread_create(&threads[i], NULL, worker, &args[i]);
        if (ret != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    double end = now_sec();

    double elapsed = end - start;
    uint64_t total_iters = 0;
    for (int i = 0; i < num_threads; i++) {
        total_iters += args[i].iterations;
    }

    printf("# num_threads=%d duration=%.2fs affinity_mode=%d base_core=%d core_stride=%d\n",
           num_threads, elapsed, affinity_mode, base_core, core_stride);
    for (int i = 0; i < num_threads; i++) {
        printf("thread %d core %d iterations %lu iters/sec %.2f\n",
               i,
               args[i].core_id,
               (unsigned long)args[i].iterations,
               args[i].iterations / elapsed);
    }
    printf("TOTAL iterations %lu iters/sec %.2f\n",
           (unsigned long)total_iters,
           total_iters / elapsed);

    free(threads);
    free(args);
    return 0;
}
