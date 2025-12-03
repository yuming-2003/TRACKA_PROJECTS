# Project A1: Advanced OS and CPU Feature Exploration — Project Report (ECSE 4320)

> **Author:** Yuming Tseng  
> **Machine:** AMD Ryzen 7 5800X 8 core processor

---

## 1) Introduction
Modern processors expose a rich set of hardware and operating-system mechanisms that jointly determine how real applications behave. While high-level abstractions hide much of this complexity, performance ultimately depends on microarchitectural structures (caches, pipelines, SMT units), kernel scheduling decisions, and I/O subsystem behavior. The goal of this project is to study these mechanisms through controlled microbenchmarks and to observe how small changes in thread placement, access patterns, and request concurrency translate into measurable performance differences.

To achieve this, I implemented a suite of lightweight C benchmarks targeting four OS and CPU features: CPU affinity, SMT interference, cache prefetcher effects, and asynchronous I/O. Each benchmark isolates a single system mechanism while holding others constant, enabling reproducible experiments and clear attribution of observed behavior. All experiments were performed on an AMD Ryzen 7 5800X under Ubuntu/WSL2 using perf to collect cycles, instructions, and cache statistics where supported.

Across the four features, the experiments reveal the interplay between hardware parallelism, OS scheduling policy, memory locality, and I/O queue depth. Small changes—such as pinning threads to cores, varying memory stride, or issuing multiple outstanding I/O requests—produce significant and predictable effects on throughput and utilization. These results highlight the importance of understanding both hardware and OS behavior when analyzing system performance, and they demonstrate how low-level mechanisms shape the efficiency of modern computing systems.
## 2) Experimental Setup/Methodology

All experiments were conducted on an AMD Ryzen 7 5800X (8C/16T) running Ubuntu under WSL2.
Compilation used GCC with `-O2 -pthread` for consistent optimization across all tests. 
Each microbenchmark was run 5 times, and the mean values were reported. 
Linux `perf` was used to collect cycles, instructions, cache/TLB statistics, and scheduler-level
metrics such as context switches and migrations.

To minimize noise, SMT was left on, CPU frequency was controlled by limiting background processes, 
and all tests were executed under an identical system load. The same benchmarking framework was used 
for every feature, ensuring consistency across results.

## 3) Results

### 3.1 Micro-architectural Behavior: Cache Prefetcher Effects
#### Methodology

The `bench_prefetch` benchmark was used to evaluate prefetcher behavior. A 256 MB array was scanned
with two different stride sizes: 64 B (sequential, prefetch-friendly) and 4096 B (page-level stride).
Each experiment was repeated 5 times. perf counters for cycles, instructions, cache-references,
cache-misses, LLC-loads, and LLC-load-misses were collected using:

```bash
perf stat -e cycles,instructions,cache-references,cache-misses,LLC-loads,LLC-load-misses \
    ./bench_prefetch 256 stride repeats
```
#### Program Output Summary
| Metric            | 64-byte Stride (Streaming) | 4096-byte Stride (Sparse) |
|------------------:|---------------------------:|---------------------------:|
| avg_time_s        | 0.012377                   | 0.000554                   |
| bytes_touched     | 33,554,432                 | 524,288                    |
| bandwidth (GiB/s) | 2.525                      | 0.882                      |
| IPC               | 0.82                       | 1.82                       |
| cache-miss rate   | < 1%                       | 6.86%                      |

#### Perf Counter Comparison
| Metric              | 64-byte Stride | 4096-byte Stride | Notes |
|--------------------:|----------------:|------------------:|-------|
| Cycles              | 401,545,042     | 111,725,659       | Sparse loop does fewer iterations |
| Instructions        | 327,353,297     | 203,490,171       | Same reason |
| IPC                 | 0.82            | 1.82              | Higher compute per load in sparse loop |
| Cache References    | 54,618,671      | 5,337,171         | Sparse touches fewer cache lines |
| Cache Misses        | very low        | 365,936           | Prefetcher failure |
| Cache Miss Rate     | < 1%            | 6.86%             | Large stride destroys locality |
| Total Runtime       | 0.012377 s      | 0.000554 s        | Sparse: fewer total loads |
| Bandwidth GiB/s     | 2.525           | 0.882             | Prefetcher success vs failure |

- The stride sweep clearly demonstrates how strongly memory performance depends on spatial locality. With a 64-byte stride, every access landed on the next cache line, allowing the hardware prefetcher to stream data efficiently. This produced high sustained bandwidth (2.525 GiB/s), an extremely low cache-miss rate, and a predictable throughput profile. The CPU spent most cycles executing useful work, reflected in a stable IPC of 0.82, characteristic of a memory-bound but well-prefetched loop.

- In contrast, the 4096-byte stride destroyed locality by jumping ahead one page at a time. Since the pattern was too sparse for the prefetcher to recognize, nearly every access resulted in a cache miss, raising the miss rate to 6.86% and collapsing bandwidth to 0.882 GiB/s. Although the sparse loop retired fewer total loads—raising IPC to 1.82—each load was significantly more expensive due to cache and TLB penalties.

- Overall, this feature highlights that prefetching is only effective when memory accesses exhibit predictable spatial locality. Once locality is lost, the CPU can no longer overlap memory latency, resulting in lower throughput and higher miss penalties. This aligns closely with expected microarchitectural behavior for modern CPUs.


### 3.2 Scheduling & Isolation: CPU Affinity
#### Methodology
A custom multi-threaded benchmark (bench_affinity_smt) was used to perform floating-point intensive work across 4 threads for 5 seconds. Two configurations were tested:

1. No affinity: OS freely schedules and migrates threads

2. Pinned affinity: each thread is bound to cores 0–3

Each run was profiled using:
```bash
/usr/lib/linux-tools-6.8.0-88/perf stat ./bench_affinity_smt 4 5 <affinity_mode> 0 1

```
Averages, throughput, and perf counters were collected to measure microarchitectural effects.
#### Program Output Summary
| Metric                 | No Affinity (mode=0)        | Pinned (mode=1)              |
|-----------------------:|-----------------------------|------------------------------|
| Total iterations       | 3,408,905,452               | 3,401,464,332                |
| Throughput (iters/sec) | 681,726,883.98              | 680,226,310.47               |
| Per-thread variation   | ±0.35%                      | ±0.25%                       |
| Core assignment        | OS-decided (migrating)      | Fixed: cores 0,1,2,3         |

#### Perf Counter Comparison
| Metric                    | No Affinity         | Pinned               | Notes |
|--------------------------:|--------------------:|----------------------:|-------|
| task-clock (ms)           | 20,740              | 20,826               | Slight runtime variation |
| Context switches          | 0                   | 0                    | Good (pure CPU test) |
| CPU migrations            | 0                   | 0                    | WSL2 masks many migrations |
| Cycles                    | 93.094B             | 93.169B              | Nearly identical |
| Instructions              | 88.652B             | 88.458B              | Same compute pattern |
| **IPC**                   | **0.95**            | **0.95**             | Equal efficiency |
| Branch misses             | 0.000%              | 0.000%               | Stable branch predictor |

* Although both configurations delivered nearly identical throughput, pinning threads to dedicated cores produced slightly lower variance in per-thread performance. This reflects improved cache locality—once a thread is bound to a single core, its L1/L2 state remains warm, whereas OS migration risks cold-cache penalties. On this CPU and workload, the compute kernel is bandwidth-light and cache-friendly, so the benefit of affinity is modest.

* Still, the experiment demonstrates a key system behavior:
CPU affinity improves determinism by reducing migration-induced cache disruption, even when average throughput remains the same.

* This aligns with expectations for compute-heavy workloads on modern CPUs.

### 3.3 SMT (Simultaneous Multithreading) interference
#### Methodology
A 2-thread compute-heavy benchmark (bench_affinity_smt) was executed for 5 seconds under two conditions:
1. Both threads pinned to the same physical core
    * Core 0 hosts both logical threads
    * Simulates pure SMT sharing
2. Threads pinned to different physical cores
    * Thread 0 → Core 0
    * Thread 1 → Core 1
    * Ensures no sharing of execution resources
Commands used:

```bash
/usr/lib/linux-tools-6.8.0-88/perf stat \
  ./bench_affinity_smt 2 5 1 0 0      # both threads share core 0

/usr/lib/linux-tools-6.8.0-88/perf stat \
  ./bench_affinity_smt 2 5 1 0 1      # threads on different cores
```
#### Program Output Summary
| Configuration                        | Per-Thread Iters/sec | Total Iters/sec | Speedup |
|--------------------------------------|----------------------:|-----------------:|--------:|
| SMT: Both threads on SAME core       | ~92.5M               | 185.1M          | 1.00×   |
| Two separate physical cores          | ~175.5M              | 351.1M          | 1.90×   |

#### Perf Counter Comparison
| Metric                 | Same Core (SMT)      | Different Cores      | Notes |
|-----------------------:|----------------------:|-----------------------:|-------|
| task-clock (ms)        | 5519                 | 10391                 | CPU usage ~1 vs ~2 cores |
| Cycles                 | 25.05B               | 47.76B                | Nearly 2× more cycles executed |
| Instructions           | 24.09B               | 45.65B                | Matches cycle scaling |
| IPC                    | 0.96                 | 0.96                  | SMT affects throughput, not IPC |
| Branch misses          | 0.00%                | 0.00%                 | Predictable compute loop |
| Page faults            | 64                   | 63                    | Negligible |
* These results illustrate SMT interference clearly. When both threads share a single core, they must compete for front-end decode bandwidth, execution units, and L2 cache ports. This effectively halves the throughput of the compute kernel, reducing total performance to ~185M iters/sec.

* When running on different cores, each thread receives exclusive access to all core execution resources, nearly doubling total throughput to ~351M iters/sec. The almost 2× ratio perfectly matches theoretical expectations for a compute-heavy workload with no memory bottlenecks.

* Importantly, IPC remained constant across both tests, highlighting that SMT does not slow down individual instruction execution—it simply provides fewer cycles per thread due to shared functional units. This aligns with the fundamental design of SMT: opportunistic resource sharing, beneficial for memory-bound workloads but detrimental for compute-bound ones.

### 3.4 Asynchronous vs blocking I/O
#### Methodology
The benchmark bench_async_io was executed in two modes:
1. Blocking I/O (mode=0, num_outstanding=1)
    * Standard read() loop, one request at a time
2. Asynchronous I/O (mode=1, num_outstanding=4)
    * POSIX AIO with 4 concurrent read requests

Both experiments used:
  * File size: 1 GiB
  * Block size: 1 MiB
  * Same input file located on NVMe storage

Commands used:
```bash
/usr/lib/linux-tools-6.8.0-88/perf stat \
  ./bench_async_io ~/a1/bigfile.bin 1048576 0 1

/usr/lib/linux-tools-6.8.0-88/perf stat \
  ./bench_async_io ~/a1/bigfile.bin 1048576 1 4
```
#### Program Output Summary
| Configuration                       | Elapsed Time (s) | Throughput (MiB/s) | Speedup |
|-------------------------------------|------------------:|--------------------:|--------:|
| Blocking I/O (1 request at a time)  | 0.6515           | 1571.73             | 1.00×   |
| Asynchronous I/O (4 outstanding)    | 0.2997           | 3416.31             | 2.17×   |
#### Perf Counter Comparison
| Metric                  | Blocking I/O        | Async I/O (4 queue depth) | Notes |
|------------------------:|---------------------:|---------------------------:|-------|
| task-clock (ms)         | 678                 | 289                       | Faster runtime → less CPU time |
| user time (s)           | 0.000               | 0.004                     | CPU mostly idle during I/O |
| sys time (s)            | 0.679               | 0.282                     | Kernel work for I/O |
| Instructions            | 163,628             | 1,206,307                 | AIO overhead: setup + notifications |
| Branch misses (%)       | 15.7%               | 14.1%                     | WSL2 perf noise |
| Cycles (WSL2)           | Very low            | Very low                  | Perf cycles are not reliable on WSL2 |
* The results clearly show the benefit of increasing I/O concurrency. Blocking mode issues only one request at a time, forcing the CPU to wait on every 1 MiB read. In contrast, the asynchronous version maintains four outstanding requests, allowing the NVMe device and kernel I/O scheduler to pipeline operations. This eliminates idle gaps between requests, cuts runtime in half, and increases throughput from 1571 MiB/s to 3416 MiB/s.

* The CPU remains mostly idle in both cases, which is expected for an I/O-bound workload. The increased syscall and instruction counts in the asynchronous case reflect the overhead of managing AIO structures and completion events, but these costs are negligible compared to the gains from deeper queueing.

* Overall, this experiment demonstrates that I/O concurrency dramatically improves throughput on modern SSDs, validating the importance of asynchronous interfaces for high-performance storage systems.
