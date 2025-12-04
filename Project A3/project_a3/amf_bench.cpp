#include <bits/stdc++.h>
using namespace std;

// ====================== Utility: Random & Hash ======================

struct SplitMix64 {
    uint64_t x;
    SplitMix64(uint64_t seed=0) : x(seed) {}
    uint64_t next() {
        uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
};

inline uint64_t hash64(uint64_t x, uint64_t seed) {
    x ^= seed;
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return x;
}

// Quantile helper
template<typename T>
T quantile(vector<T> v, double q) {
    if (v.empty()) return T{};
    size_t idx = (size_t) floor(q * (v.size() - 1));
    nth_element(v.begin(), v.begin() + idx, v.end());
    return v[idx];
}

// mean/std helpers for error bars
double mean_vec(const vector<double>& v) {
    if (v.empty()) return 0.0;
    long double s = 0.0;
    for (double x : v) s += x;
    return (double)(s / v.size());
}

double stddev_vec(const vector<double>& v) {
    int n = (int)v.size();
    if (n <= 1) return 0.0;
    double m = mean_vec(v);
    long double s = 0.0;
    for (double x : v) {
        long double d = x - m;
        s += d * d;
    }
    return (double) sqrt(s / (n - 1));
}

// ====================== Common Filter Interface ======================

enum class FilterType {
    BLOOM_BLOCKED,
    CUCKOO,
    QUOTIENT,
    XOR_FILTER
};

struct ApproxFilter {
    virtual ~ApproxFilter() {}
    virtual bool insert(uint64_t key) = 0;
    virtual bool contains(uint64_t key) const = 0;
    virtual bool erase(uint64_t key) = 0;
    virtual size_t bytes_used() const = 0;
};

// ====================== Blocked Bloom Filter ======================

struct BlockedBloomFilter : public ApproxFilter {
    size_t m_bits;
    size_t k_hashes;
    size_t block_bits;  // e.g. 512 bits, must be power of 2
    vector<uint64_t> bits;
    uint64_t seed1, seed2;

    BlockedBloomFilter(size_t n, double target_fpr,
                       size_t block_bits_ = 512,
                       uint64_t s1 = 1, uint64_t s2 = 2)
        : block_bits(block_bits_), seed1(s1), seed2(s2)
    {
        double m_real = - (double)n * log(target_fpr) /
                        (log(2.0) * log(2.0));
        m_bits = (size_t)ceil(m_real);

        size_t blocks = (m_bits + block_bits - 1) / block_bits;
        m_bits = blocks * block_bits;

        double bpe = (double)m_bits / (double)n;
        k_hashes = (size_t)max(1.0, round(bpe * log(2.0)));

        size_t words = (m_bits + 63) / 64;
        bits.assign(words, 0);
    }

    inline void set_bit(size_t pos) {
        bits[pos >> 6] |= (1ULL << (pos & 63));
    }
    inline bool get_bit(size_t pos) const {
        return (bits[pos >> 6] >> (pos & 63)) & 1ULL;
    }

    bool insert(uint64_t key) override {
        uint64_t h1 = hash64(key, seed1);
        uint64_t h2 = hash64(key, seed2);
        size_t n_blocks = m_bits / block_bits;
        size_t block = (size_t)(h1 % n_blocks);
        size_t base = block * block_bits;
        size_t mask = block_bits - 1;

        for (size_t i = 0; i < k_hashes; ++i) {
            uint64_t h = h2 + i * 0x9e3779b97f4a7c15ULL;
            size_t offset = (size_t)(h & mask);
            size_t pos = base + offset;
            set_bit(pos);
        }
        return true;
    }

    bool contains(uint64_t key) const override {
        uint64_t h1 = hash64(key, seed1);
        uint64_t h2 = hash64(key, seed2);
        size_t n_blocks = m_bits / block_bits;
        size_t block = (size_t)(h1 % n_blocks);
        size_t base = block * block_bits;
        size_t mask = block_bits - 1;

        for (size_t i = 0; i < k_hashes; ++i) {
            uint64_t h = h2 + i * 0x9e3779b97f4a7c15ULL;
            size_t offset = (size_t)(h & mask);
            size_t pos = base + offset;
            if (!get_bit(pos)) return false;
        }
        return true;
    }

    bool erase(uint64_t) override {
        return false; // no deletes
    }

    size_t bytes_used() const override {
        return bits.size() * sizeof(uint64_t);
    }
};

// ====================== Cuckoo Filter ======================

struct CuckooFilter : public ApproxFilter {
    struct Bucket {
        vector<uint16_t> slots; // 0 means empty
        Bucket(size_t k = 4) : slots(k, 0) {}
    };

    size_t bucket_count;
    size_t bucket_size;
    size_t fp_bits;
    uint16_t fp_mask;
    vector<Bucket> table;
    uint64_t seed_main;
    size_t max_kicks;
    size_t failures;
    size_t stash_size;
    vector<uint16_t> stash;

    // dynamic stats
    size_t insert_calls;
    size_t total_kicks;
    size_t stash_inserts;

    CuckooFilter(size_t n, double target_fpr,
                 size_t bucket_size_ = 4,
                 size_t fp_bits_hint = 8,
                 uint64_t seed = 3,
                 size_t max_kicks_ = 500)
        : bucket_size(bucket_size_),
          seed_main(seed),
          max_kicks(max_kicks_),
          failures(0),
          stash_size(0),
          insert_calls(0),
          total_kicks(0),
          stash_inserts(0)
    {
        int f_from_p = (int)ceil(-log2(target_fpr * bucket_size_));
        int f = (int)fp_bits_hint;
        if (f_from_p > 0) f = max(f, f_from_p);
        f = max(4, min(16, f));
        fp_bits = (size_t)f;
        fp_mask = (uint16_t)((1u << fp_bits) - 1u);

        double lf = 0.9;
        double buckets_f = (double)n / (lf * (double)bucket_size);
        bucket_count = 1;
        while (bucket_count < (size_t)buckets_f) bucket_count <<= 1;
        table.assign(bucket_count, Bucket(bucket_size));
    }

    inline uint16_t fingerprint(uint64_t key) const {
        uint64_t h = hash64(key, seed_main);
        uint16_t fp = (uint16_t)(h & fp_mask);
        if (fp == 0) fp = 1;
        return fp;
    }

    inline size_t index_hash(uint64_t key) const {
        uint64_t h = hash64(key, seed_main ^ 0x12345678abcdefULL);
        return (size_t)(h & (bucket_count - 1));
    }

    inline size_t alt_index(size_t idx, uint16_t fp) const {
        uint64_t h = hash64(fp, seed_main ^ 0xf00df00dULL);
        return (idx ^ (size_t)(h & (bucket_count - 1)));
    }

    bool bucket_insert(Bucket &b, uint16_t fp) {
        for (auto &slot : b.slots) {
            if (slot == 0) {
                slot = fp;
                return true;
            }
        }
        return false;
    }

    bool insert(uint64_t key) override {
        insert_calls++;

        uint16_t fp = fingerprint(key);
        size_t i1 = index_hash(key);
        size_t i2 = alt_index(i1, fp);

        if (bucket_insert(table[i1], fp)) return true;
        if (bucket_insert(table[i2], fp)) return true;

        size_t i = (rand() & 1) ? i1 : i2;
        uint16_t cur_fp = fp;
        for (size_t kick = 0; kick < max_kicks; ++kick) {
            Bucket &b = table[i];
            size_t victim = (size_t)(rand() % bucket_size);
            swap(cur_fp, b.slots[victim]); // evict
            total_kicks++;
            i = alt_index(i, cur_fp);
            if (bucket_insert(table[i], cur_fp)) return true;
        }
        if (stash.size() < 64) {
            stash.push_back(cur_fp);
            stash_size++;
            stash_inserts++;
            return true;
        }

        failures++;
        return false;
    }

    bool contains(uint64_t key) const override {
        uint16_t fp = fingerprint(key);
        size_t i1 = index_hash(key);
        size_t i2 = alt_index(i1, fp);

        for (auto v : table[i1].slots) if (v == fp) return true;
        for (auto v : table[i2].slots) if (v == fp) return true;
        for (auto v : stash) if (v == fp) return true;
        return false;
    }

    bool erase(uint64_t key) override {
        uint16_t fp = fingerprint(key);
        size_t i1 = index_hash(key);
        size_t i2 = alt_index(i1, fp);

        for (auto &v : table[i1].slots) {
            if (v == fp) { v = 0; return true; }
        }
        for (auto &v : table[i2].slots) {
            if (v == fp) { v = 0; return true; }
        }
        for (auto &v : stash) {
            if (v == fp) {
                v = stash.back();
                stash.pop_back();
                stash_size--;
                return true;
            }
        }
        return false;
    }

    size_t bytes_used() const override {
        size_t bytes = 0;
        bytes += bucket_count * bucket_size * sizeof(uint16_t);
        bytes += stash.capacity() * sizeof(uint16_t);
        return bytes;
    }

    size_t capacity() const {
        return bucket_count * bucket_size;
    }

    double failure_rate() const {
        return insert_calls ? (double)failures / (double)insert_calls : 0.0;
    }

    double avg_kicks_per_insert() const {
        return insert_calls ? (double)total_kicks / (double)insert_calls : 0.0;
    }
};

// ====================== Quotient Filter (simple, safe) ======================

struct QuotientFilter : public ApproxFilter {
    struct Slot {
        uint16_t rem;   // remainder (fingerprint)
        uint8_t  state; // 0 = empty, 1 = used, 2 = tombstone
    } __attribute__((packed));

    size_t table_size;   // power of two
    size_t qbits;        // log2(table_size)
    size_t rbits;        // remainder bits
    uint64_t seed;
    vector<Slot> table;

    // stats
    size_t insert_calls;
    uint64_t total_probe_len_insert;

    QuotientFilter(size_t n,
                   double target_fpr,
                   size_t rbits_hint = 8,
                   uint64_t seed_ = 5)
        : seed(seed_), insert_calls(0), total_probe_len_insert(0)
    {
        int r_from_p = (int)ceil(-log2(target_fpr));
        int r = (int)rbits_hint;
        if (r_from_p > 0) r = max(r, r_from_p);
        r = max(4, min(16, r));  // clamp to 4..16 bits
        rbits = (size_t)r;

        double load = 0.8;
        double needed = (double)n / load;
        size_t sz = 1;
        while (sz < (size_t)needed) sz <<= 1;
        table_size = sz;
        qbits = (size_t)round(log2((double)table_size));

        table.assign(table_size, Slot{0, 0});
    }

    inline uint64_t h(uint64_t key) const {
        return hash64(key, seed);
    }

    inline void get_qr(uint64_t hval, size_t &q, uint16_t &r) const {
        uint64_t rmask = (1ULL << rbits) - 1ULL;
        r = (uint16_t)(hval & rmask);
        if (r == 0) r = 1;
        q = (size_t)((hval >> rbits) & (table_size - 1));
    }

    bool insert(uint64_t key) override {
        insert_calls++;

        uint64_t hv = h(key);
        size_t q;
        uint16_t r;
        get_qr(hv, q, r);

        size_t idx = q;
        size_t probes = 0;

        for (size_t i = 0; i < table_size; ++i) {
            probes++;
            Slot &s = table[idx];
            if (s.state == 0 || s.state == 2) {
                s.rem = r;
                s.state = 1;
                total_probe_len_insert += probes;
                return true;
            }
            if (s.state == 1 && s.rem == r) {
                total_probe_len_insert += probes;
                return true;
            }
            idx = (idx + 1) & (table_size - 1);
        }
        total_probe_len_insert += probes;
        return false;
    }

    bool contains(uint64_t key) const override {
        uint64_t hv = h(key);
        size_t q;
        uint16_t r;
        get_qr(hv, q, r);

        size_t idx = q;
        for (size_t i = 0; i < table_size; ++i) {
            const Slot &s = table[idx];
            if (s.state == 0) {
                return false;
            }
            if (s.state == 1 && s.rem == r) {
                return true;
            }
            idx = (idx + 1) & (table_size - 1);
        }
        return false;
    }

    bool erase(uint64_t key) override {
        uint64_t hv = h(key);
        size_t q;
        uint16_t r;
        get_qr(hv, q, r);

        size_t idx = q;
        for (size_t i = 0; i < table_size; ++i) {
            Slot &s = table[idx];
            if (s.state == 0) {
                return false;
            }
            if (s.state == 1 && s.rem == r) {
                s.state = 2; // tombstone
                return true;
            }
            idx = (idx + 1) & (table_size - 1);
        }
        return false;
    }

    size_t bytes_used() const override {
        return table_size * sizeof(Slot);
    }

    size_t capacity() const {
        return table_size;
    }

    double avg_probe_len_insert() const {
        return insert_calls ? (double)total_probe_len_insert /
                              (double)insert_calls : 0.0;
    }

    void compute_cluster_stats(double &avg, size_t &maxlen) const {
        size_t cur = 0;
        uint64_t sum = 0;
        size_t count = 0;
        maxlen = 0;

        for (size_t i = 0; i < table_size; ++i) {
            if (table[i].state == 1) {
                cur++;
            } else {
                if (cur > 0) {
                    sum += cur;
                    count++;
                    if (cur > maxlen) maxlen = cur;
                    cur = 0;
                }
            }
        }
        if (cur > 0) {
            sum += cur;
            count++;
            if (cur > maxlen) maxlen = cur;
        }

        avg = count ? (double)sum / (double)count : 0.0;
    }
};

// ====================== XOR Filter (static) ======================

struct XORFilter : public ApproxFilter {
    size_t size;       // number of slots
    uint8_t fp_bits;
    uint64_t seed;
    vector<uint16_t> fp;

    XORFilter(size_t n, double target_fpr,
              size_t fp_bits_hint = 8,
              uint64_t seed_ = 7)
        : seed(seed_)
    {
        int f_from_p = (int)ceil(-log2(target_fpr));
        int f = (int)fp_bits_hint;
        if (f_from_p > 0) f = max(f, f_from_p);
        f = max(4, min(16, f));
        fp_bits = (uint8_t)f;

        double factor = 1.23;
        size = 1;
        while (size < (size_t)(n * factor)) size <<= 1;
        fp.assign(size, 0);
    }

    struct Edge {
        uint64_t key;
        uint32_t h[3];
        int assigned_index;
    };

    inline uint16_t fingerprint(uint64_t key) const {
        uint64_t hval = hash64(key, seed ^ 0xdeadc0deULL);
        uint16_t f = (uint16_t)(hval & ((1u << fp_bits) - 1u));
        if (f == 0) f = 1;
        return f;
    }

    inline uint32_t pos_hash(uint64_t key, int i) const {
        uint64_t hval = hash64(key, seed + 0x9e3779b97f4a7c15ULL * (i+1));
        return (uint32_t)(hval & (size - 1));
    }

    bool build(const vector<uint64_t> &keys) {
        size_t n = keys.size();
        vector<Edge> edges(n);
        vector<int> deg(size, 0);
        vector<vector<int>> adj(size);

        for (size_t i = 0; i < n; ++i) {
            edges[i].key = keys[i];
            edges[i].assigned_index = -1;
            for (int j = 0; j < 3; ++j) {
                edges[i].h[j] = pos_hash(keys[i], j);
                deg[edges[i].h[j]]++;
            }
        }
        for (size_t i = 0; i < n; ++i) {
            for (int j = 0; j < 3; ++j) {
                adj[edges[i].h[j]].push_back((int)i);
            }
        }

        vector<int> stack;
        stack.reserve(n);
        deque<int> q;
        vector<int> cur_deg = deg;
        vector<char> edge_used(n, 0);

        for (size_t v = 0; v < size; ++v) {
            if (cur_deg[v] == 1) q.push_back((int)v);
        }

        while (!q.empty()) {
            int v = q.front();
            q.pop_front();
            if (cur_deg[v] != 1) continue;

            int chosen_edge = -1;
            for (int ei : adj[v]) {
                if (!edge_used[ei]) { chosen_edge = ei; break; }
            }
            if (chosen_edge == -1) continue;

            edge_used[chosen_edge] = 1;
            stack.push_back(chosen_edge);

            for (int j = 0; j < 3; ++j) {
                int u = (int)edges[chosen_edge].h[j];
                if (cur_deg[u] > 0) {
                    cur_deg[u]--;
                    if (cur_deg[u] == 1) q.push_back(u);
                }
            }
            edges[chosen_edge].assigned_index = v;
        }

        if ((int)stack.size() != (int)n) {
            cerr << "XORFilter build failed: retry with different seed or bigger size\n";
            return false;
        }

        fill(fp.begin(), fp.end(), 0);
        for (int idx = (int)stack.size() - 1; idx >= 0; --idx) {
            int ei = stack[idx];
            Edge &e = edges[ei];
            uint16_t f = fingerprint(e.key);
            uint32_t i0 = e.h[0], i1 = e.h[1], i2 = e.h[2];
            uint32_t v = (uint32_t)e.assigned_index;
            uint16_t val = f;
            val ^= fp[i0];
            val ^= fp[i1];
            val ^= fp[i2];
            val ^= fp[v];
            fp[v] = val;
        }
        return true;
    }

    bool insert(uint64_t) override { return false; } // static
    bool erase(uint64_t) override { return false; }

    bool contains(uint64_t key) const override {
        uint16_t f = fingerprint(key);
        uint32_t i0 = pos_hash(key, 0);
        uint32_t i1 = pos_hash(key, 1);
        uint32_t i2 = pos_hash(key, 2);
        uint16_t v = fp[i0] ^ fp[i1] ^ fp[i2];
        return v == f;
    }

    size_t bytes_used() const override {
        return fp.size() * sizeof(uint16_t);
    }
};

// ====================== Workload Generation ======================

enum class WorkloadType {
    READ_ONLY,
    READ_MOSTLY,
    BALANCED
};

struct Op {
    uint8_t type;          // 0 = query, 1 = insert, 2 = delete
    uint64_t key;
    bool should_be_present;
};

vector<uint64_t> make_keys(size_t n, uint64_t seed) {
    SplitMix64 rng(seed);
    vector<uint64_t> keys(n);
    for (size_t i = 0; i < n; ++i) keys[i] = rng.next();
    return keys;
}

vector<Op> make_workload(size_t n_ops,
                         WorkloadType wt,
                         double negative_share,
                         const vector<uint64_t> &pos_keys,
                         const vector<uint64_t> &neg_keys)
{
    vector<Op> ops;
    ops.reserve(n_ops);
    size_t pos_idx = 0, neg_idx = 0;
    size_t pos_size = pos_keys.size(), neg_size = neg_keys.size();

    double p_query, p_insert;
    if (wt == WorkloadType::READ_ONLY) {
        p_query = 1.0; p_insert = 0.0;
    } else if (wt == WorkloadType::READ_MOSTLY) {
        p_query = 0.95; p_insert = 0.05;
    } else { // BALANCED
        p_query = 0.5; p_insert = 0.5;
    }

    for (size_t i = 0; i < n_ops; ++i) {
        double r = (double)rand() / RAND_MAX;
        Op op{};
        if (r < p_query) {
            op.type = 0;
            double neg_r = (double)rand() / RAND_MAX;
            if (neg_r < negative_share) {
                op.key = neg_keys[neg_idx++ % neg_size];
                op.should_be_present = false;
            } else {
                op.key = pos_keys[pos_idx++ % pos_size];
                op.should_be_present = true;
            }
        } else {
            op.type = 1;
            op.key = pos_keys[pos_idx++ % pos_size];
            op.should_be_present = true;
        }
        ops.push_back(op);
    }
    return ops;
}

// ====================== Benchmark Harness ======================

struct RunResult {
    double seconds;
    double ops_per_sec;
    double p50_ns, p95_ns, p99_ns;
};

RunResult run_workload(ApproxFilter &filter, const vector<Op> &ops,
                       bool dynamic_filter)
{
    using namespace std::chrono;
    vector<double> lat_ns;
    lat_ns.reserve(ops.size());

    auto t0 = high_resolution_clock::now();
    for (const auto &op : ops) {
        auto s = high_resolution_clock::now();
        if (op.type == 0) {
            (void)filter.contains(op.key);
        } else if (op.type == 1 && dynamic_filter) {
            filter.insert(op.key);
        } else if (op.type == 2 && dynamic_filter) {
            filter.erase(op.key);
        } else {
            (void)filter.contains(op.key);
        }
        auto e = high_resolution_clock::now();
        double dt = duration_cast<nanoseconds>(e - s).count();
        lat_ns.push_back(dt);
    }
    auto t1 = high_resolution_clock::now();
    double total_ns = duration_cast<nanoseconds>(t1 - t0).count();
    double seconds = total_ns * 1e-9;
    double ops_per_sec = ops.size() / seconds;

    RunResult rr;
    rr.seconds = seconds;
    rr.ops_per_sec = ops_per_sec;
    rr.p50_ns = quantile(lat_ns, 0.5);
    rr.p95_ns = quantile(lat_ns, 0.95);
    rr.p99_ns = quantile(lat_ns, 0.99);
    return rr;
}

double measure_fpr(ApproxFilter &filter,
                   const vector<uint64_t> &neg_keys)
{
    size_t fp = 0;
    for (auto k : neg_keys) {
        if (filter.contains(k)) fp++;
    }
    return (double)fp / (double)neg_keys.size();
}

double bits_per_entry(const ApproxFilter &filter, size_t n_entries) {
    return (double)filter.bytes_used() * 8.0 / (double)n_entries;
}

// ====================== Experiment Drivers ======================

string filter_type_str(FilterType ft) {
    switch (ft) {
        case FilterType::BLOOM_BLOCKED: return "bloom_blocked";
        case FilterType::CUCKOO: return "cuckoo";
        case FilterType::QUOTIENT: return "quotient";
        case FilterType::XOR_FILTER: return "xor";
    }
    return "unknown";
}

string workload_type_str(WorkloadType wt) {
    switch (wt) {
        case WorkloadType::READ_ONLY:   return "read_only";
        case WorkloadType::READ_MOSTLY: return "read_mostly";
        case WorkloadType::BALANCED:    return "balanced";
    }
    return "unknown";
}

// global trial count for error bars
int g_trials = 5;

// ------------------- Sanity -------------------

void sanity_tests() {
    size_t n = 10000;
    auto pos = make_keys(n, 42);
    auto neg = make_keys(n, 4242);

    {
        cout << "Sanity: Blocked Bloom\n";
        BlockedBloomFilter bloom(n, 0.01);
        for (auto k : pos) bloom.insert(k);
        size_t miss = 0;
        for (auto k : pos) if (!bloom.contains(k)) miss++;
        double fpr = measure_fpr(bloom, neg);
        cout << "  misses=" << miss << " fpr=" << fpr
             << " bpe=" << bits_per_entry(bloom, n) << "\n";
    }
    {
        cout << "Sanity: Cuckoo\n";
        CuckooFilter cf(n, 0.01, 4, 8);
        for (auto k : pos) cf.insert(k);
        size_t miss = 0;
        for (auto k : pos) if (!cf.contains(k)) miss++;
        double fpr = measure_fpr(cf, neg);
        cout << "  misses=" << miss << " fpr=" << fpr
             << " bpe=" << bits_per_entry(cf, n) << "\n";
    }
    {
        cout << "Sanity: Quotient\n";
        QuotientFilter qf(n, 0.01, 8);
        for (auto k : pos) qf.insert(k);
        size_t miss = 0;
        for (auto k : pos) if (!qf.contains(k)) miss++;
        double fpr = measure_fpr(qf, neg);
        cout << "  misses=" << miss << " fpr=" << fpr
             << " bpe=" << bits_per_entry(qf, n) << "\n";
    }
    {
        cout << "Sanity: XOR\n";
        XORFilter xf(n, 0.01, 8);
        if (!xf.build(pos)) {
            cout << "  build failed\n";
        } else {
            size_t miss = 0;
            for (auto k : pos) if (!xf.contains(k)) miss++;
            double fpr = measure_fpr(xf, neg);
            cout << "  misses=" << miss << " fpr=" << fpr
                 << " bpe=" << bits_per_entry(xf, n) << "\n";
        }
    }
}

// ------------------- Simple Sweep (lookup throughput & tails) -------------------

void run_simple_sweep() {
    cout << "filter,n,target_fpr,achieved_fpr,bpe,workload,neg_share,"
            "ops,ops_per_sec_mean,ops_per_sec_std,"
            "p50_ns_mean,p50_ns_std,"
            "p95_ns_mean,p95_ns_std,"
            "p99_ns_mean,p99_ns_std\n";

    vector<size_t> Ns = {1000000};
    vector<double> target_fprs = {0.01};
    vector<double> neg_shares = {0.0, 0.5, 0.9};

    for (size_t n : Ns) {
        auto pos = make_keys(n, 123);
        auto neg = make_keys(n, 456);

        for (double target_fpr : target_fprs) {
            BlockedBloomFilter bloom(n, target_fpr);
            for (auto k : pos) bloom.insert(k);

            CuckooFilter cf(n, target_fpr, 4, 8);
            for (auto k : pos) cf.insert(k);

            QuotientFilter qf(n, target_fpr, 8);
            for (auto k : pos) qf.insert(k);

            XORFilter xf(n, target_fpr, 8);
            bool ok = xf.build(pos);

            vector<pair<FilterType, ApproxFilter*>> filters;
            filters.push_back({FilterType::BLOOM_BLOCKED, &bloom});
            filters.push_back({FilterType::CUCKOO, &cf});
            filters.push_back({FilterType::QUOTIENT, &qf});
            if (ok) filters.push_back({FilterType::XOR_FILTER, &xf});

            for (auto [ft, fptr] : filters) {
                double fpr = measure_fpr(*fptr, neg);
                double bpe = bits_per_entry(*fptr, n);

                for (double neg_share : neg_shares) {
                    auto ops = make_workload(
                        2000000,
                        WorkloadType::READ_ONLY,
                        neg_share,
                        pos, neg
                    );
                    bool dynamic =
                        (ft == FilterType::CUCKOO ||
                         ft == FilterType::QUOTIENT);
                    vector<double> ops_ps, p50s, p95s, p99s;

                    for (int t = 0; t < g_trials; ++t) {
                        RunResult rr = run_workload(*fptr, ops, dynamic);
                        ops_ps.push_back(rr.ops_per_sec);
                        p50s.push_back(rr.p50_ns);
                        p95s.push_back(rr.p95_ns);
                        p99s.push_back(rr.p99_ns);
                    }

                    double ops_mean = mean_vec(ops_ps);
                    double ops_std  = stddev_vec(ops_ps);
                    double p50_mean = mean_vec(p50s);
                    double p50_std  = stddev_vec(p50s);
                    double p95_mean = mean_vec(p95s);
                    double p95_std  = stddev_vec(p95s);
                    double p99_mean = mean_vec(p99s);
                    double p99_std  = stddev_vec(p99s);

                    cout << filter_type_str(ft) << ","
                         << n << ","
                         << target_fpr << ","
                         << fpr << ","
                         << bpe << ","
                         << "read_only" << ","
                         << neg_share << ","
                         << ops.size() << ","
                         << ops_mean << ","
                         << ops_std << ","
                         << p50_mean << ","
                         << p50_std << ","
                         << p95_mean << ","
                         << p95_std << ","
                         << p99_mean << ","
                         << p99_std
                         << "\n";
                }
            }
        }
    }
}

// ------------------- Dynamic Insert/Delete + Load-Factor Sweeps -------------------

void run_dynamic_sweep() {
    cout << "filter,n,target_fpr,load_factor,phase,"
            "ops,ops_per_sec_mean,ops_per_sec_std,"
            "failure_rate,avg_kicks_per_insert,stash_inserts,"
            "avg_probe_len_insert,avg_cluster_len,max_cluster_len\n";

    size_t n = 1000000;
    double target_fpr = 0.01;

    auto keys = make_keys(5000000, 999);

    vector<double> load_factors;
    for (double lf = 0.40; lf <= 0.951; lf += 0.05) {
        load_factors.push_back(lf);
    }

    // ---------------- Cuckoo Filter ----------------
    {
        CuckooFilter base_cf(n, target_fpr, 4, 8);
        size_t capacity = base_cf.capacity();

        for (double lf : load_factors) {
            size_t inserts = (size_t)floor(lf * (double)capacity);

            vector<double> ops_insert, ops_delete;
            double sum_fail = 0.0;
            double sum_kicks = 0.0;
            double sum_stash = 0.0;

            for (int t = 0; t < g_trials; ++t) {
                CuckooFilter cf(n, target_fpr, 4, 8);

                using namespace std::chrono;
                auto t0 = high_resolution_clock::now();
                for (size_t i = 0; i < inserts; ++i) {
                    cf.insert(keys[i]);
                }
                auto t1 = high_resolution_clock::now();
                double ns_insert = duration_cast<nanoseconds>(t1 - t0).count();
                double sec_insert = ns_insert * 1e-9;
                ops_insert.push_back(inserts / sec_insert);

                auto t2 = high_resolution_clock::now();
                for (size_t i = 0; i < inserts; ++i) {
                    cf.erase(keys[i]);
                }
                auto t3 = high_resolution_clock::now();
                double ns_delete = duration_cast<nanoseconds>(t3 - t2).count();
                double sec_delete = ns_delete * 1e-9;
                ops_delete.push_back(inserts / sec_delete);

                sum_fail  += cf.failure_rate();
                sum_kicks += cf.avg_kicks_per_insert();
                sum_stash += cf.stash_inserts;
            }

            double ops_ins_mean = mean_vec(ops_insert);
            double ops_ins_std  = stddev_vec(ops_insert);
            double ops_del_mean = mean_vec(ops_delete);
            double ops_del_std  = stddev_vec(ops_delete);
            double fail_mean    = sum_fail / g_trials;
            double kicks_mean   = sum_kicks / g_trials;
            double stash_mean   = sum_stash / g_trials;

            cout << "cuckoo,"
                 << n << ","
                 << target_fpr << ","
                 << lf << ","
                 << "insert,"
                 << inserts << ","
                 << ops_ins_mean << ","
                 << ops_ins_std << ","
                 << fail_mean << ","
                 << kicks_mean << ","
                 << stash_mean << ","
                 << 0.0 << ","
                 << 0.0 << ","
                 << 0
                 << "\n";

            cout << "cuckoo,"
                 << n << ","
                 << target_fpr << ","
                 << lf << ","
                 << "delete,"
                 << inserts << ","
                 << ops_del_mean << ","
                 << ops_del_std << ","
                 << fail_mean << ","
                 << kicks_mean << ","
                 << stash_mean << ","
                 << 0.0 << ","
                 << 0.0 << ","
                 << 0
                 << "\n";
        }
    }

    // ---------------- Quotient Filter ----------------
    {
        QuotientFilter base_qf(n, target_fpr, 8);
        size_t capacity = base_qf.capacity();

        for (double lf : load_factors) {
            size_t inserts = (size_t)floor(lf * (double)capacity);

            vector<double> ops_insert, ops_delete;
            double sum_probe = 0.0;
            double sum_avg_cluster = 0.0;
            double sum_max_cluster = 0.0;

            for (int t = 0; t < g_trials; ++t) {
                QuotientFilter qf(n, target_fpr, 8);

                using namespace std::chrono;
                auto t0 = high_resolution_clock::now();
                for (size_t i = 0; i < inserts; ++i) {
                    qf.insert(keys[i]);
                }
                auto t1 = high_resolution_clock::now();
                double ns_insert = duration_cast<nanoseconds>(t1 - t0).count();
                double sec_insert = ns_insert * 1e-9;
                ops_insert.push_back(inserts / sec_insert);

                double avg_cluster_len = 0.0;
                size_t max_cluster_len = 0;
                qf.compute_cluster_stats(avg_cluster_len, max_cluster_len);
                sum_probe       += qf.avg_probe_len_insert();
                sum_avg_cluster += avg_cluster_len;
                sum_max_cluster += (double)max_cluster_len;

                auto t2 = high_resolution_clock::now();
                for (size_t i = 0; i < inserts; ++i) {
                    qf.erase(keys[i]);
                }
                auto t3 = high_resolution_clock::now();
                double ns_delete = duration_cast<nanoseconds>(t3 - t2).count();
                double sec_delete = ns_delete * 1e-9;
                ops_delete.push_back(inserts / sec_delete);
            }

            double ops_ins_mean = mean_vec(ops_insert);
            double ops_ins_std  = stddev_vec(ops_insert);
            double ops_del_mean = mean_vec(ops_delete);
            double ops_del_std  = stddev_vec(ops_delete);
            double avg_probe    = sum_probe / g_trials;
            double avg_cluster  = sum_avg_cluster / g_trials;
            double avg_max_cl   = sum_max_cluster / g_trials;

            cout << "quotient,"
                 << n << ","
                 << target_fpr << ","
                 << lf << ","
                 << "insert,"
                 << inserts << ","
                 << ops_ins_mean << ","
                 << ops_ins_std << ","
                 << 0.0 << ","
                 << 0.0 << ","
                 << 0 << ","
                 << avg_probe << ","
                 << avg_cluster << ","
                 << (size_t)avg_max_cl
                 << "\n";

            cout << "quotient,"
                 << n << ","
                 << target_fpr << ","
                 << lf << ","
                 << "delete,"
                 << inserts << ","
                 << ops_del_mean << ","
                 << ops_del_std << ","
                 << 0.0 << ","
                 << 0.0 << ","
                 << 0 << ","
                 << avg_probe << ","
                 << avg_cluster << ","
                 << (size_t)avg_max_cl
                 << "\n";
        }
    }
}

// ------------------- Threaded Throughput Helper -------------------

double run_threaded_throughput(ApproxFilter &filter,
                               WorkloadType wt,
                               double neg_share,
                               const vector<uint64_t> &pos,
                               const vector<uint64_t> &neg,
                               int threads,
                               size_t total_ops,
                               bool dynamic,
                               bool lock_writes)
{
    using namespace std::chrono;
    mutex m;  // for coarse-grain write locking when needed

    auto worker = [&](int tid, size_t start_op, size_t ops_this_thread) {
        SplitMix64 rng(123456789ULL + (uint64_t)tid * 1337ULL);
        double p_query, p_insert;
        if (wt == WorkloadType::READ_ONLY) {
            p_query = 1.0; p_insert = 0.0;
        } else if (wt == WorkloadType::READ_MOSTLY) {
            p_query = 0.95; p_insert = 0.05;
        } else {
            p_query = 0.5; p_insert = 0.5;
        }

        size_t pos_sz = pos.size();
        size_t neg_sz = neg.size();
        size_t local_insert_count = 0;

        for (size_t i = 0; i < ops_this_thread; ++i) {
            double r = (double)rng.next() /
                       (double)numeric_limits<uint64_t>::max();
            if (r < p_query) {
                double rn = (double)rng.next() /
                            (double)numeric_limits<uint64_t>::max();
                uint64_t key;
                if (rn < neg_share) {
                    key = neg[(start_op + i) % neg_sz];
                } else {
                    key = pos[(start_op + i) % pos_sz];
                }
                (void)filter.contains(key);
            } else {
                uint64_t key = pos[(start_op + local_insert_count) % pos_sz];
                local_insert_count++;
                if (dynamic && lock_writes) {
                    lock_guard<mutex> lg(m);
                    filter.insert(key);
                } else if (dynamic) {
                    filter.insert(key);
                } else {
                    (void)filter.contains(key);
                }
            }
        }
    };

    auto t0 = high_resolution_clock::now();
    vector<thread> ts;
    ts.reserve(threads);
    size_t base = 0;
    size_t per = total_ops / (size_t)threads;
    size_t rem = total_ops % (size_t)threads;

    for (int tid = 0; tid < threads; ++tid) {
        size_t ops_this = per + (tid < (int)rem ? 1 : 0);
        ts.emplace_back(worker, tid, base, ops_this);
        base += ops_this;
    }
    for (auto &th : ts) th.join();
    auto t1 = high_resolution_clock::now();

    double elapsed_ns = duration_cast<nanoseconds>(t1 - t0).count();
    double seconds = elapsed_ns * 1e-9;
    return (double)total_ops / seconds;
}

// ------------------- Thread Scaling Experiment -------------------

void run_thread_scaling() {
    cout << "filter,n,target_fpr,workload,neg_share,threads,"
            "ops,ops_per_sec_mean,ops_per_sec_std\n";

    size_t n = 1000000;
    vector<double> target_fprs = {0.01};
    vector<int> thread_counts = {1, 2, 4, 8};
    vector<WorkloadType> workloads = {
        WorkloadType::READ_ONLY,
        WorkloadType::READ_MOSTLY
    };
    double neg_share = 0.5;
    size_t total_ops = 2000000;

    auto pos = make_keys(n, 2025);
    auto neg = make_keys(n, 4049);

    for (double target_fpr : target_fprs) {
        // We'll reconstruct filters once per mode (not per trial) to keep code simple
        BlockedBloomFilter bloom(n, target_fpr);
        for (auto k : pos) bloom.insert(k);

        CuckooFilter cf(n, target_fpr, 4, 8);
        for (auto k : pos) cf.insert(k);

        QuotientFilter qf(n, target_fpr, 8);
        for (auto k : pos) qf.insert(k);

        XORFilter xf(n, target_fpr, 8);
        bool ok = xf.build(pos);

        vector<pair<FilterType, ApproxFilter*>> filters;
        filters.push_back({FilterType::BLOOM_BLOCKED, &bloom});
        filters.push_back({FilterType::CUCKOO, &cf});
        filters.push_back({FilterType::QUOTIENT, &qf});
        if (ok) filters.push_back({FilterType::XOR_FILTER, &xf});

        for (auto [ft, fptr] : filters) {
            for (auto wt : workloads) {
                bool dynamic =
                    (ft == FilterType::CUCKOO ||
                     ft == FilterType::QUOTIENT ||
                     ft == FilterType::BLOOM_BLOCKED);
                bool lock_writes = dynamic && (wt != WorkloadType::READ_ONLY);

                for (int tcount : thread_counts) {
                    vector<double> opsps;
                    for (int trial = 0; trial < g_trials; ++trial) {
                        double ops_per_sec = run_threaded_throughput(
                            *fptr, wt, neg_share,
                            pos, neg,
                            tcount, total_ops,
                            dynamic, lock_writes
                        );
                        opsps.push_back(ops_per_sec);
                    }
                    double ops_mean = mean_vec(opsps);
                    double ops_std  = stddev_vec(opsps);

                    cout << filter_type_str(ft) << ","
                         << n << ","
                         << target_fpr << ","
                         << workload_type_str(wt) << ","
                         << neg_share << ","
                         << tcount << ","
                         << total_ops << ","
                         << ops_mean << ","
                         << ops_std
                         << "\n";
                }
            }
        }
    }
}

// ------------------- Space vs Accuracy -------------------

void run_space_accuracy_sweep() {
    vector<size_t> Ns = {1'000'000, 5'000'000, 10'000'000};
    vector<double> target_fprs = {0.05, 0.01, 0.001};

    cout << "filter,n,target_fpr,achieved_fpr,bpe\n";

    // Deterministic RNG so runs are reproducible
    SplitMix64 rng(123456789ULL);

    for (size_t n : Ns) {
        vector<uint64_t> pos(n), neg(n);
        for (size_t i = 0; i < n; i++) pos[i] = rng.next();
        for (size_t i = 0; i < n; i++) neg[i] = rng.next();

        for (double target_fpr : target_fprs) {
            // Bloom
            {
                BlockedBloomFilter bloom(n, target_fpr);
                for (auto k : pos) bloom.insert(k);
                size_t fp = 0;
                for (auto k : neg) if (bloom.contains(k)) fp++;
                double achieved = (double)fp / (double)n;
                double bpe = bits_per_entry(bloom, n);
                cout << "bloom_blocked," << n << "," << target_fpr << ","
                     << achieved << "," << bpe << "\n";
            }
            // Cuckoo
            {
                CuckooFilter cf(n, target_fpr, 4, 8);
                for (auto k : pos) cf.insert(k);
                size_t fp = 0;
                for (auto k : neg) if (cf.contains(k)) fp++;
                double achieved = (double)fp / (double)n;
                double bpe = bits_per_entry(cf, n);
                cout << "cuckoo," << n << "," << target_fpr << ","
                     << achieved << "," << bpe << "\n";
            }
            // Quotient
            {
                QuotientFilter qf(n, target_fpr, 8);
                for (auto k : pos) qf.insert(k);
                size_t fp = 0;
                for (auto k : neg) if (qf.contains(k)) fp++;
                double achieved = (double)fp / (double)n;
                double bpe = bits_per_entry(qf, n);
                cout << "quotient," << n << "," << target_fpr << ","
                     << achieved << "," << bpe << "\n";
            }
            // XOR
            {
                XORFilter xf(n, target_fpr, 8);
                bool ok = xf.build(pos);
                if (!ok) {
                    double bpe = bits_per_entry(xf, n);
                    cout << "xor," << n << "," << target_fpr << ","
                         << 1.0 << "," << bpe << "\n";
                } else {
                    size_t fp = 0;
                    for (auto k : neg) if (xf.contains(k)) fp++;
                    double achieved = (double)fp / (double)n;
                    double bpe = bits_per_entry(xf, n);
                    cout << "xor," << n << "," << target_fpr << ","
                         << achieved << "," << bpe << "\n";
                }
            }
        }
    }
}




// ------------------- Full Experiments Wrapper -------------------

void run_full_experiments() {
    run_simple_sweep();
    run_dynamic_sweep();
    run_thread_scaling();
    run_space_accuracy_sweep();
}

// ====================== CLI ======================

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    string mode = "sanity";
    g_trials = 5;  // default

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg.rfind("--mode=", 0) == 0) {
            mode = arg.substr(strlen("--mode="));
        } else if (arg.rfind("--trials=", 0) == 0) {
            g_trials = stoi(arg.substr(strlen("--trials=")));
        }
    }

    if (g_trials < 1) g_trials = 1;

    if (mode == "sanity") {
        sanity_tests();
    } else if (mode == "simple_sweep") {
        run_simple_sweep();
    } else if (mode == "dynamic") {
        run_dynamic_sweep();
    } else if (mode == "threaded") {
        run_thread_scaling();
    } else if (mode == "space") {
        run_space_accuracy_sweep();
    } else if (mode == "full") {
        run_full_experiments();
    } else {
        cerr << "Unknown mode: " << mode << "\n";
        cerr << "Usage: " << argv[0]
             << " --mode={sanity|simple_sweep|dynamic|threaded|space|full}"
             << " [--trials=K]\n";
        return 1;
    }

    return 0;
}
