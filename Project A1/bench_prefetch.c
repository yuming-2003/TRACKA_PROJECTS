// bench_prefetch.c
// Memory access benchmark to study cache prefetcher behavior.
//
// Usage:
//   ./bench_prefetch <array_size_mb> <stride_bytes> <repeats>
//
// Example:
//   # streaming (stride=64 bytes ~ one cache line)
//   ./bench_prefetch 256 64 50
//
//   # very sparse stride (e.g., 4096 bytes) to defeat prefetcher
//   ./bench_prefetch 256 4096 50

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

static inline double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <array_size_mb> <stride_bytes> <repeats>\n", argv[0]);
        return 1;
    }

    size_t array_mb = (size_t)atol(argv[1]);
    size_t stride_bytes = (size_t)atol(argv[2]);
    int repeats = atoi(argv[3]);

    size_t array_bytes = array_mb * 1024ULL * 1024ULL;
    size_t elem_count = array_bytes / sizeof(double);
    size_t stride_elems = stride_bytes / sizeof(double);
    if (stride_elems == 0) stride_elems = 1;

    double *arr = aligned_alloc(64, elem_count * sizeof(double));
    if (!arr) {
        perror("aligned_alloc");
        return 1;
    }

    for (size_t i = 0; i < elem_count; i++) {
        arr[i] = (double)i;
    }

    volatile double sum = 0.0;
    double total_time = 0.0;

    for (int r = 0; r < repeats; r++) {
        double start = now_sec();
        for (size_t i = 0; i < elem_count; i += stride_elems) {
            sum += arr[i];
        }
        double end = now_sec();
        total_time += (end - start);
    }

    double avg_time = total_time / repeats;
    double bytes_touched = (double)(elem_count / stride_elems) * sizeof(double);
    double bandwidth = bytes_touched / avg_time / (1024.0 * 1024.0 * 1024.0); // GiB/s

    printf("# array_mb=%zu stride_bytes=%zu repeats=%d\n", array_mb, stride_bytes, repeats);
    printf("avg_time_s=%.6f bytes_touched=%.0f bandwidth_GiBps=%.3f\n",
           avg_time, bytes_touched, bandwidth);

    // prevent optimization
    if (sum == 0.12345) {
        printf("sum=%f\n", sum);
    }

    free(arr);
    return 0;
}
