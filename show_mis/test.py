import subprocess
import numpy as np
import csv

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
        values = np.fromfile("counter_distribution/" + graph + ".txt", dtype=int, sep="\n")
        np.save("counter_distribution/" + graph + ".npy", values)

def get_csv_row(graph):
    row = []

    lines = []
    with open("round_distribution/" + graph + ".txt", 'r', encoding='utf-8') as f:
        lines = f.readlines()
    row.append(graph)
    row.append(int(lines[0].strip()))
    row.append(int(lines[1].strip()))
    row.append(int(lines[2].strip()))
    row.append("  ")

    data = np.load("counter_distribution/" + graph + ".npy")
    data = np.sort(data)
    row += [int(np.percentile(data, p)) for p in [10,20,30,40,50,60,70,80,90,100]]
    row.append("  ")
    row += [int(np.percentile(data, p)) for p in [91,92,93,94,95,96,97,98,99,100]]
    row.append("  ")

    row.append(int(lines[3].strip()))
    for i in range(0, int(lines[3].strip())):
        row.append(int(lines[4+i].strip()))

    print(row)
    return row

def gen_csv():
    csv_file = 'mis_info.csv'
    with open(csv_file, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow([
            "graph name", "n", "m", "mis_size", "  ", 
            "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%", "  ", 
            "91%", "92%", "93%", "94%", "95%", "96%", "97%", "98%", "99%", "100%", "  ", 
            "n of rounds", "frontier"
        ])
        for graph in graphs:
            writer.writerow(get_csv_row(graph))

#run_mis()
#gen_npy()
#gen_csv()