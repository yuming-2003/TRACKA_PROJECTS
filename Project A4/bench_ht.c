// bench_ht.c
// Project A4: Concurrent Data Structures - Hash Table (Coarse vs Striped)
//
// Build: gcc -O2 -pthread -o bench_ht bench_ht.c
//
// Usage:
//   ./bench_ht <mode> <threads> <ops_per_thread> <workload>
//     mode: 0 = coarse-grained, 1 = striped (fine-grained)
//     threads: 1,2,4,8,...
//     ops_per_thread: e.g., 1000000
//     workload: 0 = lookup-only, 1 = insert-only, 2 = mixed 70/30
//
// Example:
//   ./bench_ht 0 4 1000000 0   # coarse, 4 threads, 1M ops each, lookup-only
//   ./bench_ht 1 8 200000 2   # striped, 8 threads, 200k ops each, mixed 70/30

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#define DIE(msg) do { perror(msg); exit(1); } while (0)

static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

/*** Simple 64-bit hash (splitmix64 style) ***/
static inline uint64_t hash_u64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    x = x ^ (x >> 31);
    return x;
}

/*** Hash table data structures ***/

typedef struct entry {
    uint64_t key;
    uint64_t value;
    struct entry *next;
} entry_t;

typedef struct {
    size_t nbuckets;
    entry_t **buckets;
    pthread_mutex_t lock;      // single global lock
} hash_table_coarse_t;

typedef struct {
    size_t nbuckets;
    entry_t **buckets;
    pthread_mutex_t *bucket_locks; // one lock per bucket
} hash_table_striped_t;

/*** Coarse-grained hash table implementation ***/

hash_table_coarse_t* ht_coarse_create(size_t nbuckets) {
    hash_table_coarse_t *ht = calloc(1, sizeof(*ht));
    if (!ht) DIE("calloc ht_coarse");
    ht->nbuckets = nbuckets;
    ht->buckets = calloc(nbuckets, sizeof(entry_t*));
    if (!ht->buckets) DIE("calloc coarse buckets");
    if (pthread_mutex_init(&ht->lock, NULL) != 0) DIE("mutex_init coarse");
    return ht;
}

void ht_coarse_destroy(hash_table_coarse_t *ht) {
    if (!ht) return;
    for (size_t i = 0; i < ht->nbuckets; i++) {
        entry_t *e = ht->buckets[i];
        while (e) {
            entry_t *n = e->next;
            free(e);
            e = n;
        }
    }
    pthread_mutex_destroy(&ht->lock);
    free(ht->buckets);
    free(ht);
}

void ht_coarse_insert(hash_table_coarse_t *ht, uint64_t key, uint64_t value) {
    uint64_t h = hash_u64(key);
    size_t b = h % ht->nbuckets;
    pthread_mutex_lock(&ht->lock);
    entry_t *e = ht->buckets[b];
    while (e) {
        if (e->key == key) {
            e->value = value;
            pthread_mutex_unlock(&ht->lock);
            return;
        }
        e = e->next;
    }
    e = malloc(sizeof(*e));
    if (!e) DIE("malloc entry");
    e->key = key;
    e->value = value;
    e->next = ht->buckets[b];
    ht->buckets[b] = e;
    pthread_mutex_unlock(&ht->lock);
}

int ht_coarse_find(hash_table_coarse_t *ht, uint64_t key, uint64_t *out_value) {
    uint64_t h = hash_u64(key);
    size_t b = h % ht->nbuckets;
    pthread_mutex_lock(&ht->lock);
    entry_t *e = ht->buckets[b];
    while (e) {
        if (e->key == key) {
            if (out_value) *out_value = e->value;
            pthread_mutex_unlock(&ht->lock);
            return 1;
        }
        e = e->next;
    }
    pthread_mutex_unlock(&ht->lock);
    return 0;
}

int ht_coarse_erase(hash_table_coarse_t *ht, uint64_t key) {
    uint64_t h = hash_u64(key);
    size_t b = h % ht->nbuckets;
    pthread_mutex_lock(&ht->lock);
    entry_t *e = ht->buckets[b];
    entry_t *prev = NULL;
    while (e) {
        if (e->key == key) {
            if (prev) prev->next = e->next;
            else ht->buckets[b] = e->next;
            free(e);
            pthread_mutex_unlock(&ht->lock);
            return 1;
        }
        prev = e;
        e = e->next;
    }
    pthread_mutex_unlock(&ht->lock);
    return 0;
}

/*** Striped (per-bucket) hash table implementation ***/

hash_table_striped_t* ht_striped_create(size_t nbuckets) {
    hash_table_striped_t *ht = calloc(1, sizeof(*ht));
    if (!ht) DIE("calloc ht_striped");
    ht->nbuckets = nbuckets;
    ht->buckets = calloc(nbuckets, sizeof(entry_t*));
    if (!ht->buckets) DIE("calloc striped buckets");
    ht->bucket_locks = malloc(nbuckets * sizeof(pthread_mutex_t));
    if (!ht->bucket_locks) DIE("malloc striped locks");
    for (size_t i = 0; i < nbuckets; i++) {
        if (pthread_mutex_init(&ht->bucket_locks[i], NULL) != 0) DIE("mutex_init striped");
    }
    return ht;
}

void ht_striped_destroy(hash_table_striped_t *ht) {
    if (!ht) return;
    for (size_t i = 0; i < ht->nbuckets; i++) {
        entry_t *e = ht->buckets[i];
        while (e) {
            entry_t *n = e->next;
            free(e);
            e = n;
        }
        pthread_mutex_destroy(&ht->bucket_locks[i]);
    }
    free(ht->bucket_locks);
    free(ht->buckets);
    free(ht);
}

void ht_striped_insert(hash_table_striped_t *ht, uint64_t key, uint64_t value) {
    uint64_t h = hash_u64(key);
    size_t b = h % ht->nbuckets;
    pthread_mutex_lock(&ht->bucket_locks[b]);
    entry_t *e = ht->buckets[b];
    while (e) {
        if (e->key == key) {
            e->value = value;
            pthread_mutex_unlock(&ht->bucket_locks[b]);
            return;
        }
        e = e->next;
    }
    e = malloc(sizeof(*e));
    if (!e) DIE("malloc entry striped");
    e->key = key;
    e->value = value;
    e->next = ht->buckets[b];
    ht->buckets[b] = e;
    pthread_mutex_unlock(&ht->bucket_locks[b]);
}

int ht_striped_find(hash_table_striped_t *ht, uint64_t key, uint64_t *out_value) {
    uint64_t h = hash_u64(key);
    size_t b = h % ht->nbuckets;
    pthread_mutex_lock(&ht->bucket_locks[b]);
    entry_t *e = ht->buckets[b];
    while (e) {
        if (e->key == key) {
            if (out_value) *out_value = e->value;
            pthread_mutex_unlock(&ht->bucket_locks[b]);
            return 1;
        }
        e = e->next;
    }
    pthread_mutex_unlock(&ht->bucket_locks[b]);
    return 0;
}

int ht_striped_erase(hash_table_striped_t *ht, uint64_t key) {
    uint64_t h = hash_u64(key);
    size_t b = h % ht->nbuckets;
    pthread_mutex_lock(&ht->bucket_locks[b]);
    entry_t *e = ht->buckets[b];
    entry_t *prev = NULL;
    while (e) {
        if (e->key == key) {
            if (prev) prev->next = e->next;
            else ht->buckets[b] = e->next;
            free(e);
            pthread_mutex_unlock(&ht->bucket_locks[b]);
            return 1;
        }
        prev = e;
        e = e->next;
    }
    pthread_mutex_unlock(&ht->bucket_locks[b]);
    return 0;
}

/*** Benchmark harness ***/

typedef struct {
    int mode;              // 0 = coarse, 1 = striped
    int workload;          // 0 = lookup-only, 1 = insert-only, 2 = mixed 70/30
    uint64_t ops_per_thread;
    int tid;
    unsigned int seed;
    hash_table_coarse_t *ht_coarse;
    hash_table_striped_t *ht_striped;
    uint64_t *keys;
    size_t nkeys;
} worker_args_t;

static void* worker_fn(void *arg) {
    worker_args_t *wa = (worker_args_t*)arg;

    int mode = wa->mode;
    int workload = wa->workload;
    uint64_t ops = wa->ops_per_thread;
    unsigned int rng = wa->seed;
    uint64_t dummy_sum = 0; // prevent compiler from optimizing finds away

    for (uint64_t i = 0; i < ops; i++) {
        uint64_t idx = rand_r(&rng) % wa->nkeys;
        uint64_t k = wa->keys[idx];

        int op_type;
        if (workload == 0) {
            op_type = 0; // lookup-only
        } else if (workload == 1) {
            op_type = 1; // insert-only
        } else {
            // 70% lookup, 30% insert/erase
            op_type = (rand_r(&rng) % 10 < 7) ? 0 : 1;
        }

        if (mode == 0) {
            // coarse
            if (op_type == 0) {
                uint64_t val;
                if (ht_coarse_find(wa->ht_coarse, k, &val)) {
                    dummy_sum += val;
                }
            } else {
                ht_coarse_insert(wa->ht_coarse, k, i);
            }
        } else {
            // striped
            if (op_type == 0) {
                uint64_t val;
                if (ht_striped_find(wa->ht_striped, k, &val)) {
                    dummy_sum += val;
                }
            } else {
                ht_striped_insert(wa->ht_striped, k, i);
            }
        }
    }

    // minor side-effect to prevent full optimization
    if (dummy_sum == 42) {
        fprintf(stderr, "magic!\n");
    }

    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <mode:0|1> <threads> <ops_per_thread> <workload:0|1|2>\n", argv[0]);
        fprintf(stderr, "  mode: 0 = coarse, 1 = striped\n");
        fprintf(stderr, "  workload: 0 = lookup-only, 1 = insert-only, 2 = mixed 70/30\n");
        return 1;
    }

    int mode = atoi(argv[1]);
    int nthreads = atoi(argv[2]);
    uint64_t ops_per_thread = strtoull(argv[3], NULL, 10);
    int workload = atoi(argv[4]);

    if (mode < 0 || mode > 1 || workload < 0 || workload > 2 || nthreads <= 0) {
        fprintf(stderr, "Invalid arguments.\n");
        return 1;
    }

    const size_t NBuckets = 1 << 20;      // 1,048,576 buckets
    const size_t NKeys = 1000000;         // 1e6 keys

    uint64_t *keys = malloc(NKeys * sizeof(uint64_t));
    if (!keys) DIE("malloc keys");
    for (size_t i = 0; i < NKeys; i++) {
        keys[i] = (uint64_t)i + 1; // simple distinct keys
    }

    hash_table_coarse_t *htc = NULL;
    hash_table_striped_t *hts = NULL;

    if (mode == 0) {
        htc = ht_coarse_create(NBuckets);
    } else {
        hts = ht_striped_create(NBuckets);
    }

    // Pre-populate table with half the keys for lookup/mixed workloads
    size_t prepopulate = (workload == 1) ? 0 : (NKeys / 2);
    for (size_t i = 0; i < prepopulate; i++) {
        uint64_t k = keys[i];
        if (mode == 0) {
            ht_coarse_insert(htc, k, k * 2);
        } else {
            ht_striped_insert(hts, k, k * 2);
        }
    }

    pthread_t *threads = malloc(nthreads * sizeof(pthread_t));
    worker_args_t *args = malloc(nthreads * sizeof(worker_args_t));
    if (!threads || !args) DIE("malloc threads/args");

    uint64_t start_ns = now_ns();

    for (int t = 0; t < nthreads; t++) {
        args[t].mode = mode;
        args[t].workload = workload;
        args[t].ops_per_thread = ops_per_thread;
        args[t].tid = t;
        args[t].seed = (unsigned int)(time(NULL) ^ (t * 1337));
        args[t].ht_coarse = htc;
        args[t].ht_striped = hts;
        args[t].keys = keys;
        args[t].nkeys = NKeys;

        if (pthread_create(&threads[t], NULL, worker_fn, &args[t]) != 0) {
            DIE("pthread_create");
        }
    }

    for (int t = 0; t < nthreads; t++) {
        pthread_join(threads[t], NULL);
    }

    uint64_t end_ns = now_ns();
    double elapsed_s = (end_ns - start_ns) / 1e9;
    double total_ops = (double)ops_per_thread * (double)nthreads;
    double throughput = total_ops / elapsed_s;

    const char *mode_str = (mode == 0) ? "coarse" : "striped";
    const char *workload_str =
        (workload == 0) ? "lookup-only" :
        (workload == 1) ? "insert-only" :
                          "mixed-70/30";

    printf("# mode=%s threads=%d workload=%s\n", mode_str, nthreads, workload_str);
    printf("elapsed_s=%.6f total_ops=%.0f throughput_ops_per_s=%.2f\n",
           elapsed_s, total_ops, throughput);

    free(threads);
    free(args);
    if (mode == 0) ht_coarse_destroy(htc);
    else ht_striped_destroy(hts);
    free(keys);

    return 0;
}
