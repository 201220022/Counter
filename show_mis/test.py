import subprocess
import numpy as np
import csv
import matplotlib.pyplot as plt

graphs = []
with open('../testcases/graphnames.txt', 'r', encoding='utf-8') as f:
    for graph in f:
        graph = graph.strip()
        if graph:
            graphs.append(graph)

def execute(command):
    print(" ".join(command))
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    print(result.stdout)
    if result.stderr:
        print(result.stderr)

def run_mis():
    execute(["make", "clean"])
    execute(["make"])
    for graph in graphs:
        execute(["./mis", "../testcases/bin/" + graph + ".bin", "1"])

def gen_npy():
    for graph in graphs:
        print(graph + ".txt  ==>  " + graph + ".npy")
        start = np.fromfile("counter_distribution/" + graph + "_start.txt", dtype=int, sep="\n")
        np.save("counter_distribution/" + graph + "_start.npy", start)
        end = np.fromfile("counter_distribution/" + graph + "_end.txt", dtype=int, sep="\n")
        np.save("counter_distribution/" + graph + "_end.npy", end)
        gap = start - end
        np.save("counter_distribution/" + graph + "_gap.npy", gap)

def get_csv_row_layer(graph):
    row = []

    lines = []
    with open("round_distribution/" + graph + ".txt", 'r', encoding='utf-8') as f:
        lines = f.readlines()
    row.append(graph)
    row.append(int(lines[0].strip()))
    row.append(int(lines[1].strip()))
    row.append(int(lines[2].strip()))
    row.append("  ")

    row.append(int(lines[3].strip()))
    for i in range(0, int(lines[3].strip())):
        row.append(int(lines[4+i].strip()))

    print(row)
    return row


def get_csv_row_counter(graph, typ):
    row = []

    lines = []
    with open("round_distribution/" + graph + ".txt", 'r', encoding='utf-8') as f:
        lines = f.readlines()
    row.append(graph)
    row.append(int(lines[0].strip()))
    row.append(int(lines[1].strip()))
    row.append(int(lines[2].strip()))
    row.append("  ")

    data = np.load("counter_distribution/" + graph + "_" + typ + ".npy")
    data = np.sort(data)
    row += [int(np.percentile(data, p)) for p in [10,20,30,40,50,60,70,80,90,100]]
    row.append("  ")
    row += [int(np.percentile(data, p)) for p in [91,92,93,94,95,96,97,98,99,100]]

    print(row)
    return row

def gen_csv():
    with open("layer.csv", 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow([
            "graph name", "n", "m", "mis_size", "  ", "n of rounds", "frontier"
        ])
        for graph in graphs:
            writer.writerow(get_csv_row_layer(graph))

    for typ in ["start", "end", "gap"]:
        with open(typ + ".csv", 'w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            writer.writerow([
                "graph name", "n", "m", "mis_size", "  ", 
                "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%", "  ", 
                "91%", "92%", "93%", "94%", "95%", "96%", "97%", "98%", "99%", "100%", "  "
            ])
            for graph in graphs:
                writer.writerow(get_csv_row_counter(graph, typ))

def safe_load(path):
    data = np.load(path)
    data = data[np.isfinite(data)]  # 去掉 NaN 和 Inf
    data = data[data > 0]           # log-log 不支持 <=0 的值
    return np.sort(data)            # 排序仅用于更平滑的展示

def gen_plt():
    rows = len(graphs)
    cols = 2
    fig, axes = plt.subplots(rows, cols, figsize=(10, 5 * rows))  # 大画布

    for i, graph in enumerate(graphs):
        # 三组文件
        datasets = {
            "start": safe_load(f"counter_distribution/{graph}_start.npy"),
            "end":   safe_load(f"counter_distribution/{graph}_end.npy"),
            "gap":   safe_load(f"counter_distribution/{graph}_gap.npy"),
        }

        # ========= 图1：线性直方图 =========
        ax1 = axes[i, 0] if rows > 1 else axes[0]
        for label, values in datasets.items():
            ax1.hist(values, bins=200, alpha=0.5, label=label, histtype='step')
        ax1.set_xlabel("Counter value")
        ax1.set_ylabel("Frequency")
        ax1.set_title(f"{graph} (Linear Scale)")
        ax1.legend()

        # ========= 图2：log–log =========
        ax2 = axes[i, 1] if rows > 1 else axes[1]
        for label, values in datasets.items():
            counts, bins = np.histogram(values, bins=200)
            ax2.loglog(bins[1:], counts, marker='.', linestyle='-', label=label)
        ax2.set_xlabel("Value (log scale)")
        ax2.set_ylabel("Count (log scale)")
        ax2.set_title(f"{graph} (Log–Log)")
        ax2.legend()

    plt.tight_layout()
    plt.savefig("plt/initialized_values_in_counters.png", dpi=300, bbox_inches='tight')
    plt.savefig("plt/initialized_values_in_counters.svg", bbox_inches='tight')
    plt.close()
#run_mis()
#gen_npy()
#gen_csv()
gen_plt()