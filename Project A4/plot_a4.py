import matplotlib.pyplot as plt

# ==========================
#   RAW DATA (YOUR RESULTS)
# ==========================

threads = [1, 2, 4, 8]

# Lookup-only
coarse_lookup =  [6763059.29, 3093411.02, 2987431.01, 2345938.08]
striped_lookup = [5743098.08, 12078041.25, 22753091.80, 36261629.10]

# Insert-only
coarse_insert =  [3490320.58, 1963350.27, 1756944.76, 1508133.97]
striped_insert = [2976111.17, 6179641.94, 11574638.07, 21266071.49]

# Mixed workload (70/30)
coarse_mixed =  [4256770.92, 2411016.56, 2245413.19, 1849993.12]
striped_mixed = [4478460.78, 8718512.24, 16703913.38, 28469058.92]

# ==============================
#   Helper plotting function
# ==============================
def make_plot(y1, y2, title, filename):
    plt.figure(figsize=(8,6))
    plt.plot(threads, y1, marker='o', linewidth=2, label='Coarse-Grained')
    plt.plot(threads, y2, marker='o', linewidth=2, label='Striped (Fine-Grained)')
    plt.title(title, fontsize=16)
    plt.xlabel("Threads", fontsize=14)
    plt.ylabel("Throughput (ops/sec)", fontsize=14)
    plt.grid(True, linestyle='--', alpha=0.6)
    plt.xticks(threads)
    plt.legend(fontsize=12)
    plt.tight_layout()
    plt.savefig(filename)
    print(f"Saved {filename}")

# ==========================
#   Generate all 3 plots
# ==========================
make_plot(coarse_lookup, coarse_lookup, "", "")

make_plot(
    coarse_lookup,
    striped_lookup,
    "Throughput Scaling — Lookup-Only Workload",
    "lookup.png"
)

make_plot(
    coarse_insert,
    striped_insert,
    "Throughput Scaling — Insert-Only Workload",
    "insert.png"
)

make_plot(
    coarse_mixed,
    striped_mixed,
    "Throughput Scaling — Mixed 70/30 Workload",
    "mixed.png"
)
