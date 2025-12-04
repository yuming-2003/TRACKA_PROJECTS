# plot_throughput_negshare.py
import pandas as pd
import matplotlib.pyplot as plt

CSV_FILE = "simple_sweep.csv"   # <-- change if needed


def plot_throughput(df):
    # Assume single n + target_fpr in this sweep
    n = df["n"].iloc[0]
    target_fpr = df["target_fpr"].iloc[0]

    df_ro = df[df["workload"] == "read_only"].copy()

    name_map = {
        "bloom_blocked": "Blocked Bloom",
        "cuckoo": "Cuckoo",
        "quotient": "Quotient",
        "xor": "XOR"
    }
    df_ro["filter_pretty"] = df_ro["filter"].map(name_map).fillna(df_ro["filter"])

    plt.figure(figsize=(8,6))
    for filt, g in df_ro.groupby("filter_pretty"):
        g = g.sort_values("neg_share")
        plt.errorbar(
            g["neg_share"],
            g["ops_per_sec_mean"] / 1e6,           # convert to Mops/s
            yerr=g["ops_per_sec_std"] / 1e6,
            marker="o",
            capsize=5,
            label=filt,
        )

    plt.xlabel("Negative lookup share")
    plt.ylabel("Throughput (Million ops/s)")
    plt.title(f"Lookup Throughput vs Negative Share (read-only, n={n:,}, target_fpr={target_fpr})")
    plt.grid(True, linestyle="--", alpha=0.4)
    plt.legend()
    plt.tight_layout()
    plt.savefig("throughput_vs_negative_share.png", dpi=200)
    print("Saved throughput_vs_negative_share.png")


def plot_latencies(df):
    df_ro = df[df["workload"] == "read_only"].copy()

    name_map = {
        "bloom_blocked": "Blocked Bloom",
        "cuckoo": "Cuckoo",
        "quotient": "Quotient",
        "xor": "XOR"
    }
    df_ro["filter_pretty"] = df_ro["filter"].map(name_map).fillna(df_ro["filter"])

    for filt, g in df_ro.groupby("filter_pretty"):
        g = g.sort_values("neg_share")

        plt.figure(figsize=(8,6))

        # p50
        plt.errorbar(
            g["neg_share"],
            g["p50_ns_mean"],
            yerr=g["p50_ns_std"],
            marker="o",
            capsize=5,
            label="p50",
        )

        # p95
        plt.errorbar(
            g["neg_share"],
            g["p95_ns_mean"],
            yerr=g["p95_ns_std"],
            marker="o",
            capsize=5,
            label="p95",
        )

        # p99
        plt.errorbar(
            g["neg_share"],
            g["p99_ns_mean"],
            yerr=g["p99_ns_std"],
            marker="o",
            capsize=5,
            label="p99",
        )

        plt.xlabel("Negative lookup share")
        plt.ylabel("Latency (ns)")
        plt.title(f"{filt}: Tail latencies vs negative share (read-only)")
        plt.grid(True, linestyle="--", alpha=0.4)
        plt.legend()
        plt.tight_layout()

        outname = f"latency_vs_negative_share_{filt.replace(' ', '_').lower()}.png"
        plt.savefig(outname, dpi=200)
        print(f"Saved {outname}")


def main():
    df = pd.read_csv(CSV_FILE)
    print("Loaded rows:", len(df))
    plot_throughput(df)
    plot_latencies(df)


if __name__ == "__main__":
    main()
