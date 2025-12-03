// bench_async_io.c
// Compare blocking read() vs POSIX asynchronous I/O.
//
// Usage:
//   ./bench_async_io <file_path> <block_size_bytes> <mode> <num_outstanding>
//
// mode:
//   0 = blocking read()
//   1 = POSIX AIO with up to num_outstanding concurrent reads
//
// Example:
//   ./bench_async_io bigfile.bin 1048576 0 1   # blocking, 1 MiB blocks
//   ./bench_async_io bigfile.bin 1048576 1 4   # async, up to 4 requests in flight

#define _XOPEN_SOURCE 600
#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static inline double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static off_t file_size(int fd) {
    off_t cur = lseek(fd, 0, SEEK_CUR);
    off_t end = lseek(fd, 0, SEEK_END);
    lseek(fd, cur, SEEK_SET);
    return end;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <file_path> <block_size_bytes> <mode> <num_outstanding>\n",
                argv[0]);
        return 1;
    }

    const char *path = argv[1];
    size_t block_size = (size_t)atol(argv[2]);
    int mode = atoi(argv[3]);
    int num_outstanding = atoi(argv[4]);
    if (num_outstanding <= 0) num_outstanding = 1;

    // NOTE: we use plain O_RDONLY for portability; O_DIRECT is optional and not required
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    off_t size = file_size(fd);
    size_t total_blocks = (size + block_size - 1) / block_size;

    printf("# file=%s size=%ld block_size=%zu mode=%d num_outstanding=%d total_blocks=%zu\n",
           path, (long)size, block_size, mode, num_outstanding, total_blocks);

    double start = now_sec();

    if (mode == 0) {
        // Blocking read()
        void *buf;
        if (posix_memalign(&buf, 4096, block_size) != 0) {
            perror("posix_memalign");
            close(fd);
            return 1;
        }
        size_t blocks_read = 0;
        while (blocks_read < total_blocks) {
            ssize_t rd = read(fd, buf, block_size);
            if (rd < 0) {
                perror("read");
                break;
            }
            if (rd == 0) break;
            blocks_read++;
        }
        free(buf);
    } else {
        // POSIX AIO
        struct aiocb *cbs = calloc(num_outstanding, sizeof(struct aiocb));
        void **buffers = calloc(num_outstanding, sizeof(void *));
        if (!cbs || !buffers) {
            fprintf(stderr, "allocation failure\n");
            close(fd);
            return 1;
        }

        for (int i = 0; i < num_outstanding; i++) {
            if (posix_memalign(&buffers[i], 4096, block_size) != 0) {
                perror("posix_memalign");
                close(fd);
                return 1;
            }
        }

        size_t next_block = 0;
        int active = 0;

        while (next_block < total_blocks || active > 0) {
            // If we can, submit new requests
            for (int i = 0; i < num_outstanding && next_block < total_blocks; i++) {
                if (cbs[i].aio_fildes == 0) {
                    memset(&cbs[i], 0, sizeof(struct aiocb));
                    cbs[i].aio_fildes = fd;
                    cbs[i].aio_buf = buffers[i];
                    cbs[i].aio_nbytes = block_size;
                    cbs[i].aio_offset = (off_t)next_block * (off_t)block_size;

                    if (aio_read(&cbs[i]) != 0) {
                        perror("aio_read");
                        cbs[i].aio_fildes = 0;
                        break;
                    }
                    next_block++;
                    active++;
                }
            }

            // Check for completion
            int completed = 0;
            for (int i = 0; i < num_outstanding; i++) {
                if (cbs[i].aio_fildes != 0) {
                    int err = aio_error(&cbs[i]);
                    if (err == 0) {
                        ssize_t ret = aio_return(&cbs[i]);
                        if (ret < 0) {
                            perror("aio_return");
                        }
                        cbs[i].aio_fildes = 0;
                        active--;
                        completed++;
                    } else if (err != EINPROGRESS) {
                        fprintf(stderr, "AIO error: %s\n", strerror(err));
                        cbs[i].aio_fildes = 0;
                        active--;
                        completed++;
                    }
                }
            }

            if (completed == 0 && active > 0) {
                // avoid busy-waiting too hard
                struct timespec ts = {0, 1000000}; // 1 ms
                nanosleep(&ts, NULL);
            }
        }

        for (int i = 0; i < num_outstanding; i++) {
            free(buffers[i]);
        }
        free(cbs);
        free(buffers);
    }

    double end = now_sec();
    double elapsed = end - start;
    double throughput_mb = (double)size / (1024.0 * 1024.0) / elapsed;

    printf("elapsed_s=%.6f throughput_MiBps=%.2f\n", elapsed, throughput_mb);

    close(fd);
    return 0;
}
