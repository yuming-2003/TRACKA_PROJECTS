# Project A4: Concurrent Data Structures and Memory Coherence — Project Report (ECSE 4320)

> **Author:** Yuming Tseng  
> **Machine:** AMD Ryzen 7 5800X 8 core processor

---

## 1) Introduction

Modern multicore processors rely heavily on efficient synchronization to support concurrent data structures. Although hash tables are conceptually simple, their performance under contention depends almost entirely on how threads coordinate access to shared buckets. Lock granularity, whether the system uses a single global lock or many fine-grained locks—directly, determines how much parallelism can be extracted from the workload. As the number of threads increases, poor synchronization design can quickly become the dominant bottleneck, overshadowing all other architectural factors.

The goal of this project is to experimentally evaluate how different synchronization strategies affect the throughput of a concurrent hash table. Using two implementations—one coarse-grained (single global mutex) and one fine-grained (striped table with per-bucket locks)—we measure how each design scales under three workloads: lookup-only, insert-only, and a mixed 70% lookup / 30% insert pattern. By varying the thread count from 1 to 8 (Due to a CPU with up to 8 cores) and holding other parameters constant, the experiments isolate lock contention as the primary performance variable.

This feature study complements the broader system-level behaviors explored in earlier assignments by focusing specifically on software-level parallelism and synchronization cost. The results highlight how algorithmic design choices, rather than raw hardware capability, often determine the true performance limits of multithreaded programs.

## 2) Experimental Setup/Methodology
For this Project, I decided to implement a chaining Hash Table, and evaluated two synchronization strategies
1. Coarse-Grained Locking
    * A single pthread_mutex_t protects the entire table.
    * All operations (lookup, insert) serialize through one global lock.
    * Simple to implement but limited by contention under high concurrency.
2. Striped (Fine-Grained) Locking
    * The table uses 1,048,576 buckets, each with its own lock.
    * Threads only lock the specific bucket they access.
    * Minimizes contention under uniform hashing, enabling parallelism.
  
**Both implementations use the same hash function and bucket structure to ensure comparable behavior aside from locking.**

### **Workloads Tested**

Each thread executes a fixed number of operations (`ops_per_thread`) using one of three workload patterns:

1. **Lookup-only**
   - All operations are read-only.
   - Tests scalability when no writes occur.

2. **Insert-only**
   - Threads perform random-key insertions.
   - Tests scalability under write-heavy workloads.

3. **Mixed 70/30 workload**
   - 70% lookups, 30% inserts.
   - Emulates a more realistic read/write access pattern.

Uniform random keys were used to avoid pathological collisions and ensure fair comparison across locking modes.

### **Benchmark Procedure**

For each configuration (locking mode × thread count × workload):

1. Pre-fill the hash table with randomized keys  
   (lookup-only and mixed tests require an initial dataset).
2. Spawn **T threads**, each performing a fixed number of operations.
3. Measure:
   - Total elapsed time
   - Total operations completed
   - Aggregate throughput (ops/sec)
4. Repeat for all combinations:
```bash
mode = {coarse, striped}
threads = {1, 2, 4, 8}
workload = {lookup, insert, mixed}
```
### Experimental Rigor

To ensure that the results reflect the true behavior of each synchronization strategy—and not measurement noise—several controls were applied to the benchmarking environment, implementation, and workload generation. This section documents the full experimental rigor used to guarantee repeatability, fairness, and stability of all measurements. Each configuration (locking mode × thread count × workload) was executed five times, and the median throughput was reported to reduce WSL2 scheduler jitter.

#### Controlling System Noise
To minimize non-deterministic performance variation:
* All tests were run on an idle system, with no downloads, browser tabs, or background jobs.
* CPU frequency scaling effects were minimized by:
  * Limiting thermal load
  * Keeping window focus on terminal (WSL2 boosts scheduling priority)
* Each benchmark was run 5 independent times, and the median result was recorded.
(Median avoids outliers caused by WSL2 context switches.)

#### Isolated concurrency effects 
* Large operation counts (300k–500k per thread) ensured stable timing.
* All experiments were run on an otherwise idle machine.
* Timing used (`clock_gettime(CLOCK_MONOTONIC)`) for high-resolution measurements.


## 3) Results

### 3.1 Lock Granularity: Coarse-Grained vs Striped Hash Table
#### Methodology
To understand how lock granularity affects concurrency, throughput was measured for the two locking strategies under three workloads (lookup-only, insert-only, mixed 70/30) across 1, 2, 4, and 8 threads.

For each experiment:

- The hash table was initialized with 1,048,576 buckets (chaining).
- The same hash function and bucket layout were used for both locking modes.
- Each thread executed a fixed number of operations on uniformly random keys.
- Total elapsed time and total operations were recorded to compute throughput:
  
\[
Throughput = Total Operations / Elapsed Time
\]

This isolates synchronization cost as the main scaling factor.


### 3.2 Lookup-Only Workload
#### Throughput (ops/sec)

| Threads | Coarse-Grained | Striped | Speedup |
|--------:|---------------:|--------:|--------:|
| 1 | 6.76M | 5.74M | 0.85× |
| 2 | 3.09M | 12.08M | 3.9× |
| 4 | 2.99M | 22.75M | 7.6× |
| 8 | 2.34M | 36.26M | 15.4× |
<img width="800" height="600" alt="lookup" src="https://github.com/user-attachments/assets/bc97ac65-b64d-4b54-9984-2385f011132a" />

#### Analysis
The lookup-only experiment shows the clearest contrast in lock granularity:

**Coarse-Grained Locking**

* Throughput drops sharply with more threads (6.7M → 2.3M ops/sec).
With a single global mutex, all lookups serialize. Additional threads increase lock contention without enabling more parallel progress.

**Striped Locking**
* Throughput improves almost linearly (5.7M → 36.2M ops/sec).
Since lookups access independent buckets, collisions are rare and lock contention is minimal.
The system approaches ideal scaling, illustrating how fine-grained synchronization exposes parallelism that coarse-grained locking suppresses.

This result aligns with fundamental OS and concurrency principles that lock granularity directly limits available parallelism.


### 3.3 Insert-Only Workload
#### Throughput (ops/sec)

| Threads | Coarse-Grained | Striped | Speedup |
|--------:|---------------:|--------:|--------:|
| 1 | 3.49M | 2.98M | 0.85× |
| 2 | 1.96M | 6.18M | 3.1× |
| 4 | 1.75M | 11.57M | 6.6× |
| 8 | 1.50M | 21.27M | 14.1× |

<img width="800" height="600" alt="insert" src="https://github.com/user-attachments/assets/1dfa4780-4cd5-43c6-8291-5a66d94c75c1" />

#### Analysis
Insert-heavy workloads exhibit similar trends with slightly lower absolute throughput:

**Coarse-Grained Locking**

* The global lock forces serialization for every modification, causing scaling to degrade from 3.49M → 1.50M ops/sec.
This mirrors the lookup-only behavior but is more pronounced due to longer critical sections.

**Striped Locking**
* Strong scaling persists: 2.9M → 21.2M ops/sec.
Inserts incur write traffic and cause more cache coherence activity, yet performance still grows ~7× from 1 → 8 threads.
This demonstrates that bucket-level locking effectively isolates updates even under write-heavy access patterns.

The results highlight a classic systems insight: increasing write intensity amplifies contention in coarse-grained schemes, but striped locking preserves scalability.


### 3.4 Mixed 70/30 Workload
#### Throughput (ops/sec)

| Threads | Coarse-Grained | Striped | Speedup |
|--------:|---------------:|--------:|--------:|
| 1 | 4.25M | 4.47M | 1.05× |
| 2 | 2.41M | 8.71M | 3.6× |
| 4 | 2.24M | 16.70M | 7.4× |
| 8 | 1.85M | 28.46M | 15.3× |

<img width="800" height="600" alt="mixed" src="https://github.com/user-attachments/assets/edd23f82-617b-497e-b861-20b012710279" />


#### Analysis
The mixed workload naturally falls between the extremes:

**Coarse-Grained Locking**

* Throughput decreases modestly from 4.26M → 1.85M ops/sec.
This reflects the combined effect of serialized lookups and inserts.

**Striped Locking**
* Throughput grows from 4.48M → 28.47M ops/sec, a ~6× improvement.
As with other workloads, collisions are rare due to the large bucket count and uniform hashing, enabling highly parallel execution.

Mixed workload results provide additional confirmation that fine-grained locking is robust across diverse access patterns.



### 3.5 Analysis & Insight
Across all workloads, the patterns are consistent:

- **Coarse-grained locking serializes all operations**, causing throughput to *decrease* as more threads compete for a single mutex.
- **Striped locking enables true parallelism**, since each thread typically locks different buckets.
- Under uniform hashing, contention probability across 1M buckets is extremely low, allowing near-linear scaling.
- **Lookup-only workloads scale the best**, since they only require reading existing chains.
- **Insert-heavy workloads scale less**, as they cause more pointer writes and incur cache-line transfers between cores (false sharing avoidance becomes important here).

### 3.6 Memory Coherence Effects

Lock granularity affects not only contention but also how cache coherence behaves across cores. On a modern AMD system using the MOESI protocol, every lock or write forces ownership of a cache line to move between cores, which directly impacts throughput.

#### Coarse-Grained Locking: Cache-Line Bouncing
The global mutex sits on a single cache line. When multiple threads attempt to acquire it:
* The cache line must migrate between cores (Modified → Invalid → Modified).
* Each migration costs tens–hundreds of cycles.
* With more threads, these invalidations dominate the runtime.

This explains why coarse-grained throughput decreases as threads increase—even lookups must serialize and trigger coherence traffic on the lock’s cache line.
#### Striped Locking: Independent Cache Lines
Striped locking places a separate lock on each bucket, so:
* Threads rarely contend for the same lock.
* Each lock remains in one core’s private cache most of the time.
* Cache-line transfers are rare → minimal coherence overhead.

This is why striped locking shows near-linear scaling across all workloads.

#### Inserts vs. Lookups
Insert operations must modify a bucket, requiring the core to gain exclusive ownership of the bucket’s cache line. This causes:
* More invalidations
* More cache-line transfers

Thus, insert-only workloads scale slightly worse than lookup-only, even under striped locking.

### Conclusion

Overall, this experiment demonstrates how synchronization design fundamentally shapes performance on modern multicore systems. Using coarse-grained locking collapses available parallelism, while fine-grained locking exposes the underlying hardware concurrency and achieves multi-threaded scalability.

