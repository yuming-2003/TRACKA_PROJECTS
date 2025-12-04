# plot_dynamic_loadfactor.py
import pandas as pd
import matplotlib.pyplot as plt

CSV_FILE = "dynamic_results.csv"

def plot_cuckoo(df):
    df_ck = df[df["filter"] == "cuckoo"].copy()
    if df_ck.empty:
        print("[WARN] No cuckoo rows; skipping.")
        return

    # Throughput vs load factor (insert/delete)
    plt.figure()
    for phase in ["insert", "delete"]:
        g = df_ck[df_ck["phase"] == phase].copy()
        if g.empty:
            continue
        g = g.sort_values("load_factor")
        plt.errorbar(
            g["load_factor"],
            g["ops_per_sec_mean"] / 1e6,
            yerr=g["ops_per_sec_std"] / 1e6,
            marker="o",
            capsize=4,
            label=f"{phase} throughput",
        )
    plt.xlabel("Load factor")
    plt.ylabel("Throughput (Million ops/s)")
    plt.title("Cuckoo Filter: Throughput vs Load Factor")
    plt.grid(True, linestyle="--", alpha=0.4)
    plt.legend()
    plt.tight_layout()
    plt.savefig("cuckoo_throughput_vs_loadfactor.png", dpi=200)
    print("Saved cuckoo_throughput_vs_loadfactor.png")

    # Kicks per insert vs load factor (insert phase only)
    g_ins = df_ck[df_ck["phase"] == "insert"].copy()
    if not g_ins.empty:
        g_ins = g_ins.sort_values("load_factor")
        plt.figure()
        plt.plot(
            g_ins["load_factor"],
            g_ins["avg_kicks_per_insert"],
            marker="o",
        )
        plt.xlabel("Load factor")
        plt.ylabel("Avg kicks per insert")
        plt.title("Cuckoo Filter: Kicks vs Load Factor")
        plt.grid(True, linestyle="--", alpha=0.4)
        plt.tight_layout()
        plt.savefig("cuckoo_kicks_vs_loadfactor.png", dpi=200)
        print("Saved cuckoo_kicks_vs_loadfactor.png")


def plot_quotient(df):
    df_qf = df[df["filter"] == "quotient"].copy()
    if df_qf.empty:
        print("[WARN] No quotient rows; skipping.")
        return

    # Throughput vs load factor
    plt.figure()
    for phase in ["insert", "delete"]:
        g = df_qf[df_qf["phase"] == phase].copy()
        if g.empty:
            continue
        g = g.sort_values("load_factor")
        plt.errorbar(
            g["load_factor"],
            g["ops_per_sec_mean"] / 1e6,
            yerr=g["ops_per_sec_std"] / 1e6,
            marker="o",
            capsize=4,
            label=f"{phase} throughput",
        )
    plt.xlabel("Load factor")
    plt.ylabel("Throughput (Million ops/s)")
    plt.title("Quotient Filter: Throughput vs Load Factor")
    plt.grid(True, linestyle="--", alpha=0.4)
    plt.legend()
    plt.tight_layout()
    plt.savefig("quotient_throughput_vs_loadfactor.png", dpi=200)
    print("Saved quotient_throughput_vs_loadfactor.png")

    # Probing & cluster stats (insert only; means already averaged in C++)
    g_ins = df_qf[df_qf["phase"] == "insert"].copy()
    if not g_ins.empty:
        g_ins = g_ins.sort_values("load_factor")

        plt.figure()
        plt.plot(
            g_ins["load_factor"],
            g_ins["avg_probe_len_insert"],
            marker="o",
            label="Avg probe length",
        )
        plt.plot(
            g_ins["load_factor"],
            g_ins["avg_cluster_len"],
            marker="s",
            label="Avg cluster length",
        )
        plt.xlabel("Load factor")
        plt.ylabel("Probes / Cluster length")
        plt.title("Quotient Filter: Probes & Clusters vs Load Factor")
        plt.grid(True, linestyle="--", alpha=0.4)
        plt.legend()
        plt.tight_layout()
        plt.savefig("quotient_probes_clusters_vs_loadfactor.png", dpi=200)
        print("Saved quotient_probes_clusters_vs_loadfactor.png")

        plt.figure()
        plt.plot(
            g_ins["load_factor"],
            g_ins["max_cluster_len"],
            marker="o",
        )
        plt.xlabel("Load factor")
        plt.ylabel("Max cluster length")
        plt.title("Quotient Filter: Max Cluster Length vs Load Factor")
        plt.grid(True, linestyle="--", alpha=0.4)
        plt.tight_layout()
        plt.savefig("quotient_maxcluster_vs_loadfactor.png", dpi=200)
        print("Saved quotient_maxcluster_vs_loadfactor.png")


def main():
    df = pd.read_csv(CSV_FILE)
    print("Loaded dynamic rows:", len(df))
    print("Filters present:", df["filter"].unique())
    plot_cuckoo(df)
    plot_quotient(df)

if __name__ == "__main__":
    main()
