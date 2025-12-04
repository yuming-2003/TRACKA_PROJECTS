# plot_thread_scaling.py
import pandas as pd
import matplotlib.pyplot as plt

CSV_FILE = "threaded_results.csv"

def main():
    df = pd.read_csv(CSV_FILE)

    name_map = {
        "bloom_blocked": "Blocked Bloom",
        "cuckoo": "Cuckoo",
        "quotient": "Quotient",
        "xor": "XOR"
    }
    df["filter_pretty"] = df["filter"].map(name_map).fillna(df["filter"])

    workloads = ["read_only", "read_mostly"]

    for wt in workloads:
        df_w = df[df["workload"] == wt].copy()
        if df_w.empty:
            print(f"[WARN] No rows for workload={wt}")
            continue

        plt.figure()
        for filt, g in df_w.groupby("filter_pretty"):
            g = g.sort_values("threads")
            plt.errorbar(
                g["threads"],
                g["ops_per_sec_mean"] / 1e6,
                yerr=g["ops_per_sec_std"] / 1e6,
                marker="o",
                capsize=4,
                label=filt,
            )

        plt.xlabel("Threads")
        plt.ylabel("Throughput (Million ops/s)")
        plt.title(f"Thread Scaling ({wt.replace('_',' ')})")
        plt.grid(True, linestyle="--", alpha=0.4)
        plt.legend()
        plt.tight_layout()
        outname = f"thread_scaling_{wt}.png"
        plt.savefig(outname, dpi=200)
        print(f"Saved {outname}")

if __name__ == "__main__":
    main()
