import pandas as pd
import matplotlib.pyplot as plt

# --------------------------------------------------
# 1) Load your CSV (exactly the content you pasted)
# --------------------------------------------------
df = pd.read_csv("space_accuracy_trials.csv")

# group by filter + n + target_fpr + bpe, compute mean/std of FPR
agg = (
    df.groupby(["filter", "n", "target_fpr", "bpe"])["achieved_fpr"]
      .agg(["mean", "std"])
      .reset_index()
)

# nice colors for consistency
colors = {
    "bloom_blocked": "tab:blue",
    "cuckoo":        "tab:orange",
    "quotient":      "tab:green",
    "xor":           "tab:red",
}

# --------------------------------------------------
# 2) Helper to plot one Space-vs-Accuracy figure
# --------------------------------------------------
def plot_space_vs_accuracy(agg_df, n_value):
    sub = agg_df[agg_df["n"] == n_value]

    plt.figure(figsize=(9, 5))

    for filt in ["bloom_blocked", "cuckoo", "quotient", "xor"]:
        fdf = sub[sub["filter"] == filt].sort_values("bpe")
        if fdf.empty:
            continue

        x = fdf["bpe"].values
        y = fdf["mean"].values
        yerr = fdf["std"].values  # small std → small error bars 😊

        plt.errorbar(
            x,
            y,
            yerr=yerr,
            fmt="o",
            linestyle="none",      # no connecting lines
            capsize=4,
            elinewidth=1,
            markersize=7,
            color=colors[filt],
            label=filt,
        )

    plt.yscale("log")
    plt.xlabel("Bits per entry (BPE)")
    plt.ylabel("Achieved FPR (mean ± std, log scale)")
    plt.title(f"Space vs Accuracy (n = {n_value:,})")
    plt.grid(True, which="both", linestyle="--", alpha=0.4)
    plt.legend()
    plt.tight_layout()
    plt.show()

# --------------------------------------------------
# 3) Make the three plots
# --------------------------------------------------
for n_val in [1_000_000, 5_000_000, 10_000_000]:
    plot_space_vs_accuracy(agg, n_val)
